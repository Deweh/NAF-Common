#include "UI/StudioMenuEx.h"
#include "Util/Game.h"

#include "UI/Studio/StudioSession.h"
#include "Util/String.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_internal.h"

namespace UI
{
	using namespace Studio;

	StudioMenuEx::StudioMenuEx() = default;
	StudioMenuEx::~StudioMenuEx() = default;

	StudioMenuEx* StudioMenuEx::GetSingleton()
	{
		static StudioMenuEx singleton;
		return &singleton;
	}

	void StudioMenuEx::NewProject(std::string a_name)
	{
		CloseProject();
		auto project = std::make_unique<ProjectEx>();
		// Until the first actor is added, which moves the center onto it.
		if (const auto player = RE::PlayerCharacter::GetSingleton()) project->SetSceneCenter(ProjectEx::GetWorldPlacement(player));
		StartSession(std::move(project), Util::String::GetDataPath() / "Projects" / (a_name + ".nafproject"));
		m_session->SetStatus("New project. Right-click the options bar or press Shift+A to add an actor.");
	}

	std::expected<void, std::string> StudioMenuEx::OpenProject(const std::filesystem::path& a_path)
	{
		CloseProject();
		auto file = Serialization::StudioProjectImport::Read(a_path);
		if (!file) return std::unexpected(std::move(file.error()));

		PendingOpen pending{ .file = std::move(*file), .path = a_path };
		// Suggest each slot's previous actor when it's loaded and still fits. The user confirms before anything binds.
		for (const auto& slot : pending.file.GetActorSlots()) {
			auto& actor = pending.actors.emplace_back();
			const auto hint = slot.bindingHint != 0 ? Util::Game::LookupForm<RE::Actor>(slot.bindingHint) : nullptr;
			const bool taken = std::ranges::any_of(pending.actors, [&](const auto& a_other) { return a_other.get() == hint; });
			if (hint && Util::Game::Get3D(hint) && !taken && ProjectEx::CheckActorCompatibility(slot.skeletonId, slot.gender, hint)) actor.reset(hint);
		}
		m_pendingOpen = std::move(pending);
		// Nothing to bind: open straight away.
		if (m_pendingOpen->actors.empty()) FinishOpen();
		if (m_pendingOpen && !m_pendingOpen->error.empty()) {
			auto error = std::move(m_pendingOpen->error);
			m_pendingOpen.reset();
			return std::unexpected(std::move(error));
		}
		return {};
	}

	void StudioMenuEx::CloseProject()
	{
		if (m_session) m_session->CancelInteraction();
		m_camera.SetEnabled(false);
		m_pendingOpen.reset();
		m_placingScene.reset();
		m_openCancelled = false;
		// Everything per project goes with the session. The export dialog waits for a file still being written.
		m_session.reset();
	}

	bool StudioMenuEx::HasOpenProject() const
	{
		return m_session != nullptr || m_pendingOpen.has_value();
	}

	const std::string& StudioMenuEx::GetProjectName() const
	{
		static const std::string none;
		return m_session ? m_session->name : none;
	}

	StudioMenuEx::ProjectEx* StudioMenuEx::GetProject()
	{
		return m_session ? &m_session->GetProject() : nullptr;
	}

	void StudioMenuEx::StartSession(std::unique_ptr<ProjectEx> a_project, std::filesystem::path a_path)
	{
		m_session = std::make_unique<StudioSession>(std::move(a_project), std::move(a_path), m_camera);
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		m_camera.SetEnabled(true);
	}

	void StudioMenuEx::Draw(Renderer* a_renderer)
	{
		if (m_pendingOpen) {
			DrawBindingDialog();
			if (m_openCancelled) CloseProject();
			return;
		}
		if (!m_session) return;
		ImGuizmo::BeginFrame();
		if (m_placingScene) {
			m_camera.Update();
			DrawScenePlacement(a_renderer);
		} else {
			m_session->ValidateSelection();
			m_camera.Update();
			m_session->HandleKeybinds();

			const ImGuiID dockspace = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
			if (!m_layoutInitialized) BuildDefaultLayout(dockspace);
			m_session->Draw(a_renderer);

			if (m_session->closeRequested) CloseProject();
		}
		// Last, so everything above draws the pose the game is showing this frame. Playback then advances, and it and
		// any edits made while drawing reach the pose buffer before the game's next update reads it.
		if (m_session) m_session->GetProject().Update(ImGui::GetIO().DeltaTime);
	}

	void StudioMenuEx::BuildDefaultLayout(ImGuiID a_dockspace)
	{
		m_layoutInitialized = true;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockBuilderRemoveNode(a_dockspace);
		ImGui::DockBuilderAddNode(a_dockspace, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::DockBuilderSetNodeSize(a_dockspace, viewport->WorkSize);

		ImGuiID center = a_dockspace;
		const ImGuiID top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.07f, nullptr, &center);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
		ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
		const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);

		ImGui::DockBuilderDockWindow(kOptionsWindow, top);
		ImGui::DockBuilderDockWindow(kOutlinerWindow, right);
		ImGui::DockBuilderDockWindow(kPropertiesWindow, rightBottom);
		ImGui::DockBuilderDockWindow(kHistoryWindow, rightBottom);
		ImGui::DockBuilderDockWindow(kTimelineWindow, bottom);
		ImGui::DockBuilderFinish(a_dockspace);
	}
}
