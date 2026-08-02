#pragma once

#include "UI/Studio/StudioWidgets.h"

#include <nlohmann/json_fwd.hpp>

namespace UI::Studio
{
	class StudioSession;

	// Export GLB. Bakes the active sequence a slice per frame while the dialog shows its progress, then writes the file
	// on a worker thread.
	class ExportDialog
	{
	public:
		explicit ExportDialog(StudioSession& a_session);
		// Waits for a file still being written: the worker owns everything it uses, but shouldn't outlive the session.
		~ExportDialog() = default;

		// Resets the dialog for the active sequence and the default actor, as it opens.
		void Open();
		void Draw();

		// Its part of the "editor" section: compression, and the sample rate and folder once something was exported.
		void WriteSettings(nlohmann::json& a_editor) const;
		void ReadSettings(const nlohmann::json& a_editor);

	private:
		struct Task
		{
			ProjectEx::BakeJob job;
			std::filesystem::path path;
			uint8_t compression{ 0 };
			std::future<std::expected<void, std::string>> write;  // Valid once baking is done.
		};

		void Start();
		[[nodiscard]] std::string GetDefaultPath(ProjectEx::ObjectId a_actor) const;
		[[nodiscard]] std::filesystem::path ResolvePath() const;

		StudioSession& m_session;
		ProjectEx* m_project;
		std::optional<Task> m_task;
		ProjectEx::ObjectId m_actor{ ProjectEx::kNoObject };
		float m_sampleRate{ 30.0f };
		int32_t m_compression{ 0 };  // 0 is lossless; otherwise the GLTFExport level.
		std::string m_path;          // UTF-8, as typed. Relative paths are under the game folder.
		bool m_pathEdited{ false };  // Otherwise the path follows the chosen actor.
		std::string m_error;
		// Remembered in the "editor" section.
		std::optional<float> m_lastSampleRate;
		std::filesystem::path m_directory;  // Empty means <Data path>/Exports.
	};
}
