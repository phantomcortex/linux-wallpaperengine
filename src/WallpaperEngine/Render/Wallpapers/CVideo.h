#pragma once

#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/VideoPlayback/MPV/GLPlayer.h"

namespace WallpaperEngine::Render::Wallpapers {
using namespace WallpaperEngine::VideoPlayback::MPV;

class CVideo final : public CWallpaper {
public:
    CVideo (
	const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
	const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
    );

    ~CVideo () override;

    const Data::Model::Video& getVideo () const;

    [[nodiscard]] int getWidth () const override;
    [[nodiscard]] int getHeight () const override;

    void setPause (bool newState) override;

protected:
    void renderFrame (const glm::ivec4& viewport) override;

    friend class CWallpaper;

private:
    /**
     * Reconciles the mpv backend with the live audio state (control interface volume/mute and the
     * automute detector). Called every frame; only touches mpv when something actually changed.
     */
    void updateAudio ();

    GLPlayerUniquePtr m_player;

    bool m_muted = false;
    /** Last volume pushed to mpv, in the engine's 0-128 scale; -1 until the first push */
    int m_appliedVolume = -1;
};
} // namespace WallpaperEngine::Render::Wallpapers
