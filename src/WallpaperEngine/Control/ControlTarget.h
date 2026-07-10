#pragma once

#include <cstdint>
#include <string>

namespace WallpaperEngine::Control {
/** Coarse playback state reported over the control interface */
enum class PlaybackState { Playing, Paused, Stopped };

/**
 * Abstract set of runtime actions the control interface can drive. Implemented by
 * WallpaperApplication; ControlServer holds a reference to it and never touches app internals
 * directly. This keeps the D-Bus/GLib code decoupled and independently testable.
 */
class ControlTarget {
public:
    virtual ~ControlTarget () = default;

    virtual void pause () = 0;
    virtual void resume () = 0;
    virtual void stop () = 0;
    virtual void setVolume (uint32_t volume) = 0;
    virtual void setMuted (bool muted) = 0;
    virtual void loadWallpaper (const std::string& idOrPath) = 0;
    virtual void setProperty (const std::string& key, const std::string& value) = 0;
    /** Pointer position, normalized [0,1] with a top-left origin */
    virtual void setMousePosition (double x, double y) = 0;

    [[nodiscard]] virtual PlaybackState state () const = 0;
    [[nodiscard]] virtual std::string wallpaper () const = 0;
    [[nodiscard]] virtual uint32_t fps () const = 0;
};
} // namespace WallpaperEngine::Control
