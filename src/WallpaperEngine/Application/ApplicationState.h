#pragma once

#include "ApplicationContext.h"

namespace WallpaperEngine::Application {
/**
 * Represents current application state
 */
class ApplicationState {
public:
    struct {
	bool keepRunning;
	/** Explicit user/control-interface pause, independent of the fullscreen-driven pause */
	bool paused;
    } general {};

    struct {
	bool enabled;
	int volume;
    } audio {};

    struct {
	bool enabled;
    } mouse {};
};
} // namespace WallpaperEngine::Application