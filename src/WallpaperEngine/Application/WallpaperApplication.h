#pragma once

#include <chrono>
#include <random>
#include <thread>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Assets/AssetLocator.h"

#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/Render/Drivers/Detectors/FullScreenDetector.h"
#include "WallpaperEngine/Render/Drivers/GLFWOpenGLDriver.h"
#include "WallpaperEngine/Render/Drivers/Output/GLFWWindowOutput.h"
#include "WallpaperEngine/Render/RenderContext.h"

#include "WallpaperEngine/Audio/Drivers/SDLAudioDriver.h"

#include "WallpaperEngine/Input/InputContext.h"
#ifdef ENABLE_WEB
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"
#else
namespace WallpaperEngine::WebBrowser { class WebBrowserContext; }
#endif

#include "WallpaperEngine/Control/ControlTarget.h"
#ifdef ENABLE_CONTROL_DBUS
#include "WallpaperEngine/Control/ControlServer.h"
#endif

#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Media/MediaSource.h"

#include <set>

namespace WallpaperEngine::Application {

using namespace WallpaperEngine::Assets;
using namespace WallpaperEngine::Data::Model;
/**
 * Small wrapper class over the actual wallpaper's main application skeleton
 */
class WallpaperApplication : public WallpaperEngine::Control::ControlTarget {
public:
    explicit WallpaperApplication (ApplicationContext& context);

    /**
     * Prepares the application for rendering.
     */
    void setup ();
    /**
     * Renders a frame of the application.
     */
    void render ();
    /**
     * Cleans up all the resources used by the application.
     */
    static void cleanup ();
    /**
     * Shows the application until it's closed
     */
    void show ();
    /**
     * Handles a OS signal sent to this PID
     *
     * @param signal
     */
    void signal (int signal);
    /**
     * @return Maps screens to loaded backgrounds
     */
    [[nodiscard]] const std::map<std::string, ProjectUniquePtr>& getBackgrounds () const;
    /**
     * @return The current application context
     */
    [[nodiscard]] ApplicationContext& getContext () const;
    /**
     * Renders a frame
     */
    void update (Render::Drivers::Output::OutputViewport* viewport);
    /**
     * Gets the output
     */
    [[nodiscard]] const WallpaperEngine::Render::Drivers::Output::Output& getOutput () const;
    /**
     * Sets the destination framebuffer for rendering. If not called, the default framebuffer will be used.
     */
    void setDestinationFramebuffer (GLuint framebuffer);

    /**
     * Gets the currently set destination framebuffer for rendering. If not set, returns 0 (the default framebuffer).
     */
    [[nodiscard]] GLuint getDestinationFramebuffer () const;

    // ControlTarget: runtime actions driven by the control interface (see Control/ControlTarget.h)
    void pause () override;
    void resume () override;
    void stop () override;
    void setVolume (uint32_t volume) override;
    void setMuted (bool muted) override;
    void loadWallpaper (const std::string& idOrPath) override;
    void setProperty (const std::string& key, const std::string& value) override;
    void setMousePosition (double x, double y) override;
    [[nodiscard]] WallpaperEngine::Control::PlaybackState state () const override;
    [[nodiscard]] std::string wallpaper () const override;
    [[nodiscard]] uint32_t fps () const override;

private:
    /**
     * Sets up an asset locator for the given background
     *
     * @param bg
     */
    AssetLocatorUniquePtr setupAssetLocator (const std::string& bg) const;
    /**
     * Initializes subsystems required for application operation
     */
    void initializeSubsystems ();

    /**
     * Loads projects based off the settings
     */
    void loadBackgrounds ();
    /**
     * Loads the given project
     *
     * @param bg
     * @return
     */
    [[nodiscard]] ProjectUniquePtr loadBackground (const std::string& bg);
    /**
     * Prepares all background's values and updates their properties if required
     */
    void setupProperties ();
    /**
     * Updates the properties for the given background based on the current context
     *
     * @param project
     */
    void setupPropertiesForProject (const Project& project);
    /**
     * Prepares CEF browser to be used
     */
    void setupBrowser ();
    /**
     * Prepares desktop environment-related things (like render, window, fullscreen detector, etc)
     */
    void setupOutput ();
    /**
     * Prepares all audio-related things (like detector, output, etc)
     */
    void setupAudio ();
    /**
     * Prepares the render-context of all the backgrounds so they can be displayed on the screen
     */
    void prepareOutputs ();
    /**
     * Prepares output debugging for all opengl errors
     */
    void setupOpenGLDebugging ();
    /**
     * Takes an screenshot of the background and saves it to the specified path
     *
     * @param filename
     */
    void takeScreenshot (const std::filesystem::path& filename) const;

    struct ActivePlaylist {
	ApplicationContext::PlaylistDefinition definition;
	std::vector<std::size_t> order;
	std::size_t orderIndex = 0;
	std::chrono::steady_clock::time_point nextSwitch;
	std::chrono::steady_clock::time_point lastUpdate;
	std::set<std::size_t> failedIndices;
    };

    void initializePlaylists ();
    void updatePlaylists ();
    void advancePlaylist (
	const std::string& screen, ActivePlaylist& playlist, const std::chrono::steady_clock::time_point& now
    );
    bool selectNextCandidate (ActivePlaylist& playlist, std::size_t& outOrderIndex);
    bool preflightWallpaper (const std::string& path);
    std::vector<std::size_t> buildPlaylistOrder (const ApplicationContext::PlaylistDefinition& definition);
    void ensureBrowserForProject (const Project& project);
    bool makeAnyViewportCurrent () const;

    /** The application context that contains the current app settings */
    ApplicationContext& m_context;
    /** Maps screens to backgrounds */
    std::map<std::string, ProjectUniquePtr> m_backgrounds {};
    std::map<std::string, ActivePlaylist> m_activePlaylists {};

    std::unique_ptr<WallpaperEngine::Audio::Drivers::Detectors::AudioPlayingDetector> m_audioDetector = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::AudioContext> m_audioContext = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::Drivers::SDLAudioDriver> m_audioDriver = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::Drivers::Recorders::PlaybackRecorder> m_audioRecorder = nullptr;
    std::unique_ptr<WallpaperEngine::Render::RenderContext> m_renderContext = nullptr;
    std::unique_ptr<WallpaperEngine::Render::Drivers::VideoDriver> m_videoDriver = nullptr;
    std::unique_ptr<WallpaperEngine::Render::Drivers::Detectors::FullScreenDetector> m_fullScreenDetector = nullptr;
#ifdef ENABLE_WEB
    std::unique_ptr<WallpaperEngine::WebBrowser::WebBrowserContext> m_browserContext = nullptr;
#endif
    /** @return the web browser context, or nullptr when built without web (CEF) support */
    [[nodiscard]] WallpaperEngine::WebBrowser::WebBrowserContext* browserContext () const {
#ifdef ENABLE_WEB
	return this->m_browserContext.get ();
#else
	return nullptr;
#endif
    }
    std::unique_ptr<WallpaperEngine::Media::MediaSource> m_mediaSource = nullptr;
#ifdef ENABLE_CONTROL_DBUS
    std::unique_ptr<WallpaperEngine::Control::ControlServer> m_controlServer = nullptr;
#endif
    /** Control-interface audio state; combined into the live state.audio.volume the callback reads */
    int m_desiredVolume = 0;
    bool m_muted = false;
    /** Identifier of the wallpaper currently loaded on this renderer's output */
    std::string m_currentWallpaper {};
    /** Rolling FPS measurement exposed over the control interface */
    uint32_t m_measuredFps = 0;
    uint32_t m_fpsFrameRef = 0;
    std::chrono::steady_clock::time_point m_fpsTimeRef {};
    std::mt19937 m_playlistRng { std::random_device {}() };
    bool m_isPaused = false;
    bool m_screenShotTaken = false;
    uint32_t m_nextFrameScreenshot = 0;
    std::chrono::steady_clock::time_point m_pauseStart {};
    GLuint m_destinationFramebuffer = 0;
    /** Writer for the pending screenshot. Owned so it can be joined before the process exits. */
    mutable std::thread m_screenshotWriter;
};
} // namespace WallpaperEngine::Application
