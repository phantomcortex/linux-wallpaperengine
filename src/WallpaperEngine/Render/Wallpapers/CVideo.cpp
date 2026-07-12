#include "CVideo.h"

#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/VideoPlayback/MPV/GLPlayer.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Wallpapers;
using namespace WallpaperEngine::VideoPlayback::MPV;

CVideo::CVideo (
    const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
    const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
) : CWallpaper (wallpaper, context, audioContext, scalingMode, clampMode) {
    // setup framebuffers
    this->setupFramebuffers ();

    const std::filesystem::path videopath
	= this->getVideo ().project.assetLocator->physicalPath (this->getVideo ().filename);

    // create a player with a small framebuffer
    // this will be changed after mpv starts playback and sees the video resolution
    this->m_player = std::make_unique<GLPlayer> (
	this->getContext (), this->CWallpaper::getWallpaperTexture (), videopath, 64, 64,
	this->CWallpaper::getWallpaperFramebuffer ()
    );
    // Seed the backend from the *live* state, not from settings: on a LoadWallpaper swap this
    // object is rebuilt long after the control interface may have changed volume or muted us, and
    // settings.audio still holds the stale command-line values.
    this->updateAudio ();
    // make sure the video has at least one usage marked, this ensures the video plays
    this->m_player->incrementUsageCount ();
}

CVideo::~CVideo () { this->m_player->decrementUsageCount (); }

void CVideo::updateAudio () {
    const auto& context = this->getContext ().getApp ().getContext ();

    // --silent (settings.audio.enabled = false) is a hard, process-lifetime kill switch: it also
    // stops CSound from ever loading, so the control interface cannot meaningfully undo it. Honour
    // it by keeping mpv silent and never consulting the live state again.
    if (!context.settings.audio.enabled) {
	if (this->m_appliedVolume != 0) {
	    this->m_appliedVolume = 0;
	    this->m_player->setVolume (0.0);
	}

	return;
    }

    // Live volume from the control interface, in the engine's 0-128 scale; mpv wants 0-100.
    if (context.state.audio.volume != this->m_appliedVolume) {
	this->m_appliedVolume = context.state.audio.volume;
	this->m_player->setVolume (this->m_appliedVolume * 100.0 / 128.0);
    }

    // Mute when the control interface asks for it, or when automute sees another app playing sound.
    const bool muted = !context.state.audio.enabled
	|| this->getAudioContext ().getDriver ().getAudioDetector ().anythingPlaying ();

    if (muted != this->m_muted) {
	this->m_muted = muted;

	if (this->m_muted) {
	    this->m_player->setMuted ();
	} else {
	    this->m_player->clearMuted ();
	}
    }
}

void CVideo::renderFrame (const glm::ivec4& viewport) {
    this->updateAudio ();

    this->m_player->render ();
}

const Data::Model::Video& CVideo::getVideo () const { return *this->getWallpaperData ().as<Data::Model::Video> (); }

void CVideo::setPause (bool newState) {
    if (newState) {
	this->m_player->setPaused ();
    } else {
	this->m_player->clearPaused ();
    }
}

int CVideo::getWidth () const { return this->m_player ? this->m_player->getWidth () : 16; }

int CVideo::getHeight () const { return this->m_player ? this->m_player->getHeight () : 16; }
