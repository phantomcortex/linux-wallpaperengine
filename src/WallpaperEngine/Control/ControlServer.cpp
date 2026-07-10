#include "ControlServer.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Control;

namespace {
constexpr const char* INTERFACE_NAME = "io.github.phantomcortex.WallpaperEngine.Control";
constexpr const char* OBJECT_PATH = "/io/github/phantomcortex/WallpaperEngine";

constexpr const char* INTROSPECTION_XML = R"XML(
<node>
  <interface name='io.github.phantomcortex.WallpaperEngine.Control'>
    <method name='Pause'/>
    <method name='Resume'/>
    <method name='Stop'/>
    <method name='SetVolume'><arg type='u' name='volume' direction='in'/></method>
    <method name='SetMuted'><arg type='b' name='muted' direction='in'/></method>
    <method name='LoadWallpaper'><arg type='s' name='idOrPath' direction='in'/></method>
    <method name='SetProperty'>
      <arg type='s' name='key' direction='in'/>
      <arg type='s' name='value' direction='in'/>
    </method>
    <method name='SetMousePosition'>
      <arg type='d' name='x' direction='in'/>
      <arg type='d' name='y' direction='in'/>
    </method>
    <property name='State' type='s' access='read'/>
    <property name='Wallpaper' type='s' access='read'/>
    <property name='Fps' type='u' access='read'/>
  </interface>
</node>
)XML";

const char* stateToString (PlaybackState state) {
    switch (state) {
        case PlaybackState::Playing: return "playing";
        case PlaybackState::Paused: return "paused";
        case PlaybackState::Stopped: return "stopped";
    }
    return "stopped";
}
} // namespace

ControlServer::ControlServer (int fd, ControlTarget& target) : m_target (target) {
    this->m_context = g_main_context_new ();
    g_main_context_push_thread_default (this->m_context);

    GError* error = nullptr;

    GSocket* socket = g_socket_new_from_fd (fd, &error);
    if (socket == nullptr) {
        g_main_context_pop_thread_default (this->m_context);
        const std::string message = error ? error->message : "unknown error";
        g_clear_error (&error);
        sLog.exception ("Control interface: cannot wrap fd ", fd, ": ", message);
    }

    GSocketConnection* stream = g_socket_connection_factory_create_connection (socket);
    g_object_unref (socket);

    gchar* guid = g_dbus_generate_guid ();
    this->m_connection = g_dbus_connection_new_sync (
        G_IO_STREAM (stream), guid, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER, nullptr, nullptr, &error
    );
    g_free (guid);
    g_object_unref (stream);

    if (this->m_connection == nullptr) {
        g_main_context_pop_thread_default (this->m_context);
        const std::string message = error ? error->message : "unknown error";
        g_clear_error (&error);
        sLog.exception ("Control interface: cannot establish peer connection: ", message);
    }

    // a peer connection stays open until we close it; do not exit the process if the peer leaves
    g_dbus_connection_set_exit_on_close (this->m_connection, FALSE);

    this->m_introspection = g_dbus_node_info_new_for_xml (INTROSPECTION_XML, &error);
    if (this->m_introspection == nullptr) {
        g_main_context_pop_thread_default (this->m_context);
        const std::string message = error ? error->message : "unknown error";
        g_clear_error (&error);
        sLog.exception ("Control interface: invalid introspection XML: ", message);
    }

    static const GDBusInterfaceVTable vtable = { handleMethodCall, handleGetProperty, nullptr, { nullptr } };

    this->m_registrationId = g_dbus_connection_register_object (
        this->m_connection, OBJECT_PATH, this->m_introspection->interfaces[0], &vtable, this, nullptr, &error
    );

    g_main_context_pop_thread_default (this->m_context);

    if (this->m_registrationId == 0) {
        const std::string message = error ? error->message : "unknown error";
        g_clear_error (&error);
        sLog.exception ("Control interface: cannot register object: ", message);
    }

    sLog.out ("Control interface ready on fd ", fd);
}

ControlServer::~ControlServer () {
    if (this->m_connection != nullptr) {
        if (this->m_registrationId != 0) {
            g_dbus_connection_unregister_object (this->m_connection, this->m_registrationId);
        }
        g_dbus_connection_close_sync (this->m_connection, nullptr, nullptr);
        g_object_unref (this->m_connection);
    }
    if (this->m_introspection != nullptr) {
        g_dbus_node_info_unref (this->m_introspection);
    }
    if (this->m_context != nullptr) {
        g_main_context_unref (this->m_context);
    }
}

void ControlServer::iterate () {
    while (g_main_context_iteration (this->m_context, FALSE)) {
        // drain all ready sources without blocking
    }
}

void ControlServer::publishState () {
    const PlaybackState state = this->m_target.state ();
    const std::string wallpaper = this->m_target.wallpaper ();

    const bool stateChanged = state != this->m_lastState;
    const bool wallpaperChanged = wallpaper != this->m_lastWallpaper;

    if (!stateChanged && !wallpaperChanged) {
        return;
    }

    GVariantBuilder changed;
    g_variant_builder_init (&changed, G_VARIANT_TYPE ("a{sv}"));

    if (stateChanged) {
        g_variant_builder_add (&changed, "{sv}", "State", g_variant_new_string (stateToString (state)));
        this->m_lastState = state;
    }
    if (wallpaperChanged) {
        g_variant_builder_add (&changed, "{sv}", "Wallpaper", g_variant_new_string (wallpaper.c_str ()));
        this->m_lastWallpaper = wallpaper;
    }

    GVariantBuilder invalidated;
    g_variant_builder_init (&invalidated, G_VARIANT_TYPE ("as"));

    g_dbus_connection_emit_signal (
        this->m_connection, nullptr, OBJECT_PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new ("(sa{sv}as)", INTERFACE_NAME, &changed, &invalidated), nullptr
    );
}

void ControlServer::handleMethodCall (
    GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* methodName, GVariant* parameters,
    GDBusMethodInvocation* invocation, gpointer userData
) {
    auto* self = static_cast<ControlServer*> (userData);
    ControlTarget& target = self->m_target;
    const std::string method = methodName;

    if (method == "Pause") {
        target.pause ();
    } else if (method == "Resume") {
        target.resume ();
    } else if (method == "Stop") {
        target.stop ();
    } else if (method == "SetVolume") {
        guint32 volume = 0;
        g_variant_get (parameters, "(u)", &volume);
        target.setVolume (volume);
    } else if (method == "SetMuted") {
        gboolean muted = FALSE;
        g_variant_get (parameters, "(b)", &muted);
        target.setMuted (muted != FALSE);
    } else if (method == "LoadWallpaper") {
        const gchar* idOrPath = nullptr;
        g_variant_get (parameters, "(&s)", &idOrPath);
        target.loadWallpaper (idOrPath != nullptr ? idOrPath : "");
    } else if (method == "SetProperty") {
        const gchar* key = nullptr;
        const gchar* value = nullptr;
        g_variant_get (parameters, "(&s&s)", &key, &value);
        target.setProperty (key != nullptr ? key : "", value != nullptr ? value : "");
    } else if (method == "SetMousePosition") {
        gdouble x = 0.0;
        gdouble y = 0.0;
        g_variant_get (parameters, "(dd)", &x, &y);
        target.setMousePosition (x, y);
    } else {
        g_dbus_method_invocation_return_error_literal (
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method"
        );
        return;
    }

    g_dbus_method_invocation_return_value (invocation, nullptr);
}

GVariant* ControlServer::handleGetProperty (
    GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* propertyName, GError** error,
    gpointer userData
) {
    auto* self = static_cast<ControlServer*> (userData);
    const std::string property = propertyName;

    if (property == "State") {
        return g_variant_new_string (stateToString (self->m_target.state ()));
    }
    if (property == "Wallpaper") {
        return g_variant_new_string (self->m_target.wallpaper ().c_str ());
    }
    if (property == "Fps") {
        return g_variant_new_uint32 (self->m_target.fps ());
    }

    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", propertyName);
    return nullptr;
}
