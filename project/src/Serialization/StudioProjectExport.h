#pragma once

#include "Animation/Studio/ProjectEx.h"

#include <nlohmann/json_fwd.hpp>

namespace Serialization
{
	class StudioProjectExport
	{
	public:
		StudioProjectExport() = delete;

		// Saves everything except history, playback state and actor bindings (a binding hint is kept per slot).
		// a_editorJson is a JSON object owned by the UI, stored as is; empty for none. The previous file is kept
		// next to the new one as ".bak".
		static std::expected<void, std::string> Save(const std::filesystem::path& a_path, const Animation::Studio::ProjectEx& a_project, std::string_view a_editorJson = {});
		// Saves a_actor's rig: its pose, its modifiers and their locators, and its bone mask. a_editorJson is stored like
		// the project's.
		static std::expected<void, std::string> SaveRig(const std::filesystem::path& a_path, Animation::Studio::ProjectEx& a_project, Animation::Studio::ProjectEx::ObjectId a_actor,
			std::string_view a_editorJson = {});

	private:
		// Any object but an actor, in the project schema. a_live writes live values in place of defaults.
		static nlohmann::ordered_json WriteObject(const Animation::Studio::ProjectEx& a_project, Animation::Studio::ProjectEx::ObjectId a_id, bool a_live);
	};
}
