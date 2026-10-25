#include "ControlServer.h"
#include "WallpaperEngine/Logging/Log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <utility>

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

// Structured logging for the control path (objectives Q5). Each record is one line:
//   [wallpaperengine] {"ts":"...","level":"info","src":"engine","event":"...",...}
// the same tag and envelope the GNOME Shell extension writes, so one grep over the journal
// interleaves both sides. Values are pre-rendered JSON (jstr/jnum) so callers pick the type.
using Field = std::pair<const char*, std::string>;

// Per-call tracing (every method, with arguments and timing) and the periodic heartbeat are off
// unless the launcher sets WPE_VERBOSE; lifecycle events and every failure are always logged.
bool verbose () {
    static const bool enabled = [] {
    const char* value = std::getenv ("WPE_VERBOSE");
    return value != nullptr && *value != '\0' && std::string (value) != "0";
    }();
    return enabled;
}

std::string timestamp () {
    const auto now = std::chrono::system_clock::now ();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch ()).count ();
    const std::time_t seconds = static_cast<std::time_t> (ms / 1000);
    std::tm utc {};
    gmtime_r (&seconds, &utc);
    char buffer[80];
    std::snprintf (
        buffer, sizeof (buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900, utc.tm_mon + 1,
        utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int> (ms % 1000)
    );
    return buffer;
}

std::string jstr (const std::string& value) {
    std::string out = "\"";
    for (const unsigned char c : value) {
    switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
        if (c < 0x20) {
            char escape[8];
            std::snprintf (escape, sizeof (escape), "\\u%04x", c);
            out += escape;
        } else {
            out += static_cast<char> (c);
        }
    }
    }
    return out + "\"";
}

std::string jnum (long long value) { return std::to_string (value); }

void logEvent (const char* level, const char* event, std::initializer_list<Field> fields) {
    std::string line = "[wallpaperengine] {\"ts\":" + jstr (timestamp ()) + ",\"level\":" + jstr (level)
    + ",\"src\":\"engine\",\"event\":" + jstr (event);
    for (const auto& [key, value] : fields) {
    line += ",\"";
    line += key;
    line += "\":" + value;
    }
    line += "}";

    // warnings and errors go to the error streams so they stay separable from the chatter
    if (std::string (level) == "info" || std::string (level) == "debug") {
    sLog.out (line);
    } else {
    sLog.error (line);
    }
}

long long elapsedMs (std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - since)
    .count ();
}

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

    logEvent ("info", "control-ready", { { "fd", jnum (fd) }, { "verbose", verbose () ? "true" : "false" } });
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

    // The render loop is what calls us, so this doubles as a liveness signal: a heartbeat that
    // stops appearing means the loop stopped, which is the first thing to know when a wallpaper
    // freezes. Every 10s, verbose only.
    if (verbose () && std::chrono::steady_clock::now () - this->m_lastHeartbeat >= std::chrono::seconds (10)) {
        this->m_lastHeartbeat = std::chrono::steady_clock::now ();
        logEvent ("debug", "heartbeat", {
            { "state", jstr (stateToString (state)) },
            { "wallpaper", jstr (wallpaper) },
            { "fps", jnum (this->m_target.fps ()) },
        });
    }

    if (!stateChanged && !wallpaperChanged) {
        return;
    }

    logEvent ("info", "state-changed", {
        { "state", jstr (stateToString (state)) },
        { "wallpaper", jstr (wallpaper) },
    });

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

    const auto started = std::chrono::steady_clock::now ();
    gchar* printed = parameters != nullptr ? g_variant_print (parameters, FALSE) : nullptr;
    const std::string args = printed != nullptr ? printed : "()";
    g_free (printed);

    // A scene swap is the one call that can take seconds and the one that fails in the field, so it
    // is always logged; the rest (SetMousePosition can arrive every frame) only when verbose.
    const bool isLoad = method == "LoadWallpaper";
    if (verbose ()) {
        logEvent ("debug", "control-call", { { "method", jstr (method) }, { "args", jstr (args) } });
    } else if (isLoad) {
        logEvent ("info", "load-begin", { { "args", jstr (args) } });
    }

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
        if (!target.loadWallpaper (idOrPath != nullptr ? idOrPath : "")) {
            logEvent ("warn", "load-failed", {
                { "wallpaper", jstr (idOrPath != nullptr ? idOrPath : "") },
                { "ms", jnum (elapsedMs (started)) },
                { "note", jstr ("previous wallpaper still playing") },
            });
            // The previous wallpaper keeps playing; tell the caller so it can report the failure
            // instead of believing the swap happened.
            g_dbus_method_invocation_return_error (
                invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "cannot load wallpaper '%s'; the previous wallpaper is still playing",
                idOrPath != nullptr ? idOrPath : ""
            );
            return;
        }
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
        logEvent ("warn", "control-unknown-method", { { "method", jstr (method) } });
        g_dbus_method_invocation_return_error_literal (
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method"
        );
        return;
    }

    if (isLoad) {
        logEvent ("info", "load-done", { { "args", jstr (args) }, { "ms", jnum (elapsedMs (started)) } });
    } else if (verbose ()) {
        logEvent ("debug", "control-reply", { { "method", jstr (method) }, { "ms", jnum (elapsedMs (started)) } });
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
