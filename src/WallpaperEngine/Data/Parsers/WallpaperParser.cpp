#include "WallpaperParser.h"

#include "ObjectParser.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Data::Parsers;

WallpaperUniquePtr WallpaperParser::parse (const JSON& file, Project& project) {
    switch (project.type) {
	case Project::Type_Scene:
	    return parseScene (file, project);
	case Project::Type_Video:
	    return parseVideo (file, project);
	case Project::Type_Web:
	    return parseWeb (file, project);
	default:
	    sLog.exception ("Unexpected project type value found... This is likely a bug");
    }
}

SceneUniquePtr WallpaperParser::parseScene (const JSON& file, Project& project) {
    const auto scene = JSON::parse (project.assetLocator->readString (file));
    const auto camera = scene.require ("camera", "Scenes must have a camera section");
    const auto general = scene.require ("general", "Scenes must have a general section");
    const auto projection
	= general.require ("orthogonalprojection", "General section must have orthogonal projection info");
    const auto objects = scene.require ("objects", "Scenes must have an objects section");

    // "orthogonalprojection": null marks a perspective-camera scene. The renderer only implements
    // an orthogonal camera (see the CONVERSION TODO in CScene), so treat it like an auto
    // projection derived from the object extents: a flattened render beats aborting the process
    // over a required width the scene never declares.
    const bool isOrthogonal = projection.is_object ();
    const bool isAutoProjection = !isOrthogonal || projection.optional ("auto", false);
    if (!isOrthogonal) {
	sLog.error ("Scene declares a perspective camera (orthogonalprojection: ", projection.dump (),
		    "); rendering with an auto orthogonal projection instead");
    }
    const auto& properties = project.properties;

    // TODO: FIND IF THESE DEFAULTS ARE SENSIBLE OR NOT AND PERFORM PROPER VALIDATION WHEN CAMERA PREVIEW AND CAMERA
    // PARALLAX ARE PRESENT

    return std::make_unique <Scene> (
        WallpaperData {
            .filename = "",
            .project = project
        }, SceneData {
            .colors = {
                .ambient  = general.user ("ambientcolor", properties, glm::vec3 (0.0f)),
                .skylight = general.user ("skylightcolor", properties, glm::vec3 (0.0f)),
                .clear = general.user ("clearcolor", properties, glm::vec3 (1.0f)),
            },
            .camera = {
                .fade = general.user ("camerafade", properties, false),
                .preview = general.optional ("camerapreview", false),
                .bloom = {
                    .enabled = general.user ("bloom", properties, false),
                    .strength = general.user ("bloomstrength", properties, 0.0f),
                    .threshold = general.user ("bloomthreshold", properties, 0.0f),
                },
                .parallax = {
                    .enabled = general.user ("cameraparallax", properties, false),
                    .amount = general.user ("cameraparallaxamount", properties, 1.0f),
                    .delay = general.user ("cameraparallaxdelay", properties, 0.0f),
                    .mouseInfluence = general.user ("cameraparallaxmouseinfluence", properties, 1.0f),
                },
                .shake = {
                    .enabled = general.user ("camerashake", properties, false),
                    .amplitude = general.user ("camerashakeamplitude", properties, 0.0f),
                    .roughness = general.user ("camerashakeroughness", properties, 0.0f),
                    .speed = general.user ("camerashakespeed", properties, 0.0f),
                },
                .configuration = {
                    .center = camera.require <glm::vec3> ("center", "Camera must have a center position"),
                    .eye = camera.require <glm::vec3> ("eye", "Camera must have an eye position"),
                    .up = camera.require <glm::vec3> ("up", "Camera must have an up position"),
                },
                .projection = {
                    .width  = isAutoProjection ? 0 : projection.require <int> ("width",  "Projection must have a width"),
                    .height = isAutoProjection ? 0 : projection.require <int> ("height", "Projection must have a height"),
                    .isAuto = isAutoProjection,
                    .nearz = camera.user ("nearz", properties, 0.0f),
                    .farz = camera.user ("farz", properties, 1000.0f),
                    .fov = camera.user ("fov", properties, 50.0f)
                }
            },
            .objects = parseObjects (objects, project),
        }
    );
}

VideoUniquePtr WallpaperParser::parseVideo (const JSON& file, Project& project) {
    return std::make_unique<Video> (WallpaperData { .filename = file, .project = project });
}

WebUniquePtr WallpaperParser::parseWeb (const JSON& file, Project& project) {
    return std::make_unique<Web> (WallpaperData {
	.filename = file,
	.project = project,
    });
}

ObjectList WallpaperParser::parseObjects (const JSON& objects, const Project& project) {
    ObjectList result = {};

    for (const auto& cur : objects) {
	// One object the parser chokes on must not cost the whole wallpaper. Scenes carry objects
	// the engine does not model yet, and authors write fields in shapes it does not expect; a
	// scene missing one object still renders, whereas letting the exception out aborts the
	// process and leaves the desktop black.
	try {
	    result.emplace_back (ObjectParser::parse (cur, project));
	} catch (const std::exception& ex) {
	    sLog.error ("Skipping object that failed to parse: ", ex.what (), ". Contents: ", cur.dump ());
	}
    }

    return result;
}