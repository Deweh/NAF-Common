#pragma once

#include "Animation/Studio/ProjectEx.h"
#include "Serialization/StudioProjectImport.h"
#include "UI/Renderer.h"
#include "UI/Studio/StudioCamera.h"

namespace UI
{
	namespace Studio
	{
		class StudioSession;
	}

	// NAF Studio's entry point. Runs a project's session through its states: opening a saved project (binding its
	// actor slots, then placing its scene center), then editing, until it closes. The editor itself is the session
	// and its panels, in UI/Studio.
	class StudioMenuEx
	{
	public:
		using ProjectEx = Animation::Studio::ProjectEx;

		static StudioMenuEx* GetSingleton();

		StudioMenuEx(const StudioMenuEx&) = delete;
		StudioMenuEx(StudioMenuEx&&) = delete;
		StudioMenuEx& operator=(const StudioMenuEx&) = delete;
		StudioMenuEx& operator=(StudioMenuEx&&) = delete;

		// Starts a new project, saved as <name>.nafproject in the projects folder.
		void NewProject(std::string a_name);
		// Reads a saved project. It opens once the user has bound an actor to each of its actor slots.
		std::expected<void, std::string> OpenProject(const std::filesystem::path& a_path);
		void CloseProject();
		// Also true while a project being opened waits for its actors to be bound.
		[[nodiscard]] bool HasOpenProject() const;
		[[nodiscard]] const std::string& GetProjectName() const;
		[[nodiscard]] ProjectEx* GetProject();
		void Draw(Renderer* a_renderer);

	private:
		StudioMenuEx();
		~StudioMenuEx();

		// A saved project waiting for its actor slots to be bound before it opens.
		struct PendingOpen
		{
			Serialization::StudioProjectImport::PendingProject file;
			std::filesystem::path path;
			std::vector<RE::NiPointer<RE::Actor>> actors;  // One per slot. Null until chosen.
			std::string error;                             // Why the last attempt to open failed.
		};

		// After a saved project is built, and before editing starts, the user places its scene center in the world.
		struct ScenePlacementStep
		{
			// Where the bound actors stood before the project took them, restored if opening is cancelled.
			std::vector<std::pair<RE::NiPointer<RE::Actor>, ProjectEx::ScenePlacement>> originals;
		};

		void StartSession(std::unique_ptr<ProjectEx> a_project, std::filesystem::path a_path);
		void BuildDefaultLayout(ImGuiID a_dockspace);

		// ---- Opening (OpenFlow.cpp) ----

		void DrawBindingDialog();
		void FinishOpen();
		void DrawScenePlacement(Renderer* a_renderer);

		std::unique_ptr<Studio::StudioSession> m_session;
		std::optional<PendingOpen> m_pendingOpen;
		std::optional<ScenePlacementStep> m_placingScene;
		bool m_openCancelled{ false };  // Cancel in the binding dialog, applied at the end of Draw.
		Studio::StudioCamera m_camera;
		bool m_layoutInitialized{ false };
	};
}
