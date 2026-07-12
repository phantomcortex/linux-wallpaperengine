#include "PropertyParser.h"
#include "../Model/Property.h"

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Model;

PropertySharedPtr PropertyParser::parse (const JSON& it, const std::string& name) {
    // type might not be included, in which case means the same as a group
    const auto type = it.optional ("type");

    if (type == "color") {
	return parseColor (it, name);
    }
    if (type == "bool") {
	return parseBoolean (it, name);
    }
    if (type == "slider") {
	return parseSlider (it, name);
    }
    if (type == "combo") {
	return parseCombo (it, name);
    }
    if (type == "text") {
	return parseText (it, name);
    }
    if (type == "scenetexture") {
	return parseSceneTexture (it, name);
    }
    if (type == "file" || type == "directory") {
	return parseFile (it, name);
    }
    if (type == "textinput") {
	return parseTextInput (it, name);
    }
    if (type == "usershortcut") {
	return parseTextInput (it, name);
    }

    if (type.has_value () && type != "group") {
	// show the error and ignore this property
	sLog.error ("Unexpected type for property: ", type);
	sLog.error (it.dump ());
    }

    return nullptr;
}

PropertySharedPtr PropertyParser::parseCombo (const JSON& it, const std::string& name) {
    std::map<std::string, std::string> optionsMap = {};

    const auto options = it.require ("options", "Combo property must have options");

    if (!options.is_array ()) {
	sLog.exception ("Property combo options should be an array");
    }

    for (auto& cur : options) {
	if (!cur.is_object ()) {
	    continue;
	}

	const auto value = cur.require ("value", "Combo option must have a value");

	optionsMap.emplace (
	    value.is_number () ? std::to_string (value.get<int> ()) : value.get<std::string> (),
	    cur.require ("label", "Combo option must have a label")
	);
    }

    // Authors omit "value" (or point it at something not in the options) often enough that either
    // must not cost the wallpaper: fall back to the first authored option instead of throwing.
    std::string selected {};
    const auto value = it.optional ("value");

    if (value.has_value ()) {
	selected = value->is_number () ? std::to_string (value->get<int> ()) : value->get<std::string> ();
    }

    if (selected.empty () || !optionsMap.contains (selected)) {
	std::string fallback {};
	for (const auto& cur : options) {
	    // first entry in *author* order -- optionsMap is sorted by value and loses it
	    if (!cur.is_object ()) {
		continue;
	    }
	    const auto optionValue = cur.require ("value", "Combo option must have a value");
	    fallback = optionValue.is_number () ? std::to_string (optionValue.get<int> ())
						: optionValue.get<std::string> ();
	    break;
	}

	if (fallback.empty ()) {
	    sLog.exception ("Combo property has no usable value or options. Contents: ", it.dump ());
	}

	sLog.error ("Combo property \"", name, "\" has ",
		    selected.empty () ? "no value" : "a value not present in its options",
		    "; defaulting to the first option. Contents: ", it.dump ());
	selected = fallback;
    }

    return std::make_shared<PropertyCombo> (
	PropertyData {
	    .name = name,
	    .text = it.optional<std::string> ("text", ""),
	},
	ComboData { .values = optionsMap },
	selected
    );
}

PropertySharedPtr PropertyParser::parseColor (const JSON& it, const std::string& name) {
    return std::make_shared<PropertyColor> (
	PropertyData {
	    .name = name,
	    .text = it.optional<std::string> ("text", ""),
	},
	it.require ("value", "Property must have a value")
    );
}

PropertySharedPtr PropertyParser::parseBoolean (const JSON& it, const std::string& name) {
    return std::make_shared<PropertyBoolean> (
	PropertyData {
	    .name = name,
	    .text = it.optional<std::string> ("text", ""),
	},
	it.optional ("value", false)
    );
}

PropertySharedPtr PropertyParser::parseSlider (const JSON& it, const std::string& name) {
    return std::make_shared<PropertySlider> (
	PropertyData {
	    .name = name,
	    .text = it.optional<std::string> ("text", ""),
	},
	SliderData {
	    .min = it.optional ("min", 0.0f),
	    .max = it.optional ("max", 0.0f),
	    .step = it.optional ("step", 0.0f),
	},
	it.require ("value", "Property must have a value")
    );
}

PropertySharedPtr PropertyParser::parseText (const JSON& it, const std::string& name) {
    return std::make_shared<PropertyText> (PropertyData {
	.name = name,
	.text = it.optional<std::string> ("text", ""),
    });
}

PropertySharedPtr PropertyParser::parseSceneTexture (const JSON& it, const std::string& name) {
    return std::make_shared<PropertySceneTexture> (
	PropertyData {
	    .name = name,
	    .text = it.optional<std::string> ("text", ""),
	},
	it.require ("value", "Property must have a value")
    );
}

PropertySharedPtr PropertyParser::parseFile (const JSON& it, const std::string& name) {
    return std::make_shared<PropertyFile> (
	PropertyData {
	    .name = name,
	    .text = it.optional<std::string> ("text", ""),
	},
	it.optional<std::string> ("value", "")
    );
}

PropertySharedPtr PropertyParser::parseTextInput (const JSON& it, const std::string& name) {
    return std::make_shared<PropertyTextInput> (
	PropertyData {
	    .name = name,
	    .text = it.optional<std::string> ("text", ""),
	},
	it.require ("value", "Property must have a value").dump ()
    );
}
