#pragma once

#include "Serialization/StudioProjectImport.h"
#include "UI/Studio/StudioWidgets.h"

namespace UI::Studio
{
	class StudioSession;

	// Save Rig and Apply Rig. Rigs live in <Data path>/Rigs/<name>.nafrig.
	class RigDialogs
	{
	public:
		explicit RigDialogs(StudioSession& a_session);

		// As a dialog opens: a_actor if it's bound, otherwise the default actor.
		void OpenSave(ProjectEx::ObjectId a_actor);
		void OpenApply(ProjectEx::ObjectId a_actor);
		void Draw();

		// A rig's "editor" section: the actor's bone display, by bone name.
		[[nodiscard]] std::string WriteEditorJson(ProjectEx::ObjectId a_actor) const;
		void ReadEditorJson(std::string_view a_json, ProjectEx::ObjectId a_actor, std::vector<std::string>& a_warnings);

	private:
		// A file in the rigs folder, read when the Apply Rig dialog opens.
		struct Entry
		{
			std::string name;
			std::filesystem::path path;
			std::expected<Serialization::StudioProjectImport::PendingRig, std::string> rig;
		};

		void DrawSave();
		void DrawApply();
		void Scan();
		void Save();
		void Apply();
		[[nodiscard]] std::filesystem::path GetSavePath() const;  // For the name typed in the Save Rig dialog.

		StudioSession& m_session;
		ProjectEx* m_project;
		ProjectEx::ObjectId m_actor{ ProjectEx::kNoObject };
		std::string m_name;
		std::vector<Entry> m_entries;
		int32_t m_index{ -1 };
		ProjectEx::RigOptions m_options;
		bool m_applyBoneDisplay{ true };
		bool m_keyPose{ false };
		std::string m_error;
	};
}
