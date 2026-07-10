#pragma once

#include "ControlTarget.h"

#include <gio/gio.h>
#include <string>

namespace WallpaperEngine::Control {
/**
 * Serves the control interface over a private, peer-to-peer D-Bus connection built from a
 * pre-connected socket fd handed to us by the launcher (--control-fd). No bus daemon and no
 * bus name are involved: the connection is authenticated as the server side of the pair.
 *
 * The connection is bound to a private GMainContext that we iterate non-blockingly once per
 * render frame (iterate()), so all method/property callbacks run synchronously on the render
 * thread and need no locking against the rest of the application.
 */
class ControlServer {
public:
    ControlServer (int fd, ControlTarget& target);
    ~ControlServer ();

    ControlServer (const ControlServer&) = delete;
    ControlServer& operator= (const ControlServer&) = delete;

    /** Dispatch any pending method calls without blocking. Call once per frame. */
    void iterate ();

    /** Emit PropertiesChanged for State/Wallpaper when they change since the last call. */
    void publishState ();

private:
    static void handleMethodCall (
        GDBusConnection* connection, const gchar* sender, const gchar* objectPath,
        const gchar* interfaceName, const gchar* methodName, GVariant* parameters,
        GDBusMethodInvocation* invocation, gpointer userData
    );
    static GVariant* handleGetProperty (
        GDBusConnection* connection, const gchar* sender, const gchar* objectPath,
        const gchar* interfaceName, const gchar* propertyName, GError** error, gpointer userData
    );

    ControlTarget& m_target;
    GMainContext* m_context = nullptr;
    GDBusConnection* m_connection = nullptr;
    GDBusNodeInfo* m_introspection = nullptr;
    guint m_registrationId = 0;

    // last values published via PropertiesChanged, for change detection
    PlaybackState m_lastState = PlaybackState::Stopped;
    std::string m_lastWallpaper;
};
} // namespace WallpaperEngine::Control
