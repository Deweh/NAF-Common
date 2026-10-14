#include "UI/Studio/StudioSession.h"

#include "Serialization/StudioProjectExport.h"
#include "UI/Studio/StudioCamera.h"
#include "imgui/imgui_internal.h"

namespace UI::Studio
{
	StudioSession::StudioSession(std::unique_ptr<ProjectEx> a_project, std::filesystem::path a_path, StudioCamera& a_camera) :
		name(a_path.stem().string()),
		path(std::move(a_path)),
		camera(a_camera),
		m_project(std::move(a_project))
	{
		m_project->MarkSaved();
	}

	// Out of line, so the panels' destructors (the export dialog waits for its writer) run with every type complete.
	StudioSession::~StudioSession() = default;

	void StudioSession::Draw(Renderer* a_renderer)
	{
		optionsBar.Draw();
		outliner.Draw();
		properties.Draw();
		timeline.Draw();
		history.Draw();
		viewport.Draw(a_renderer);
		popups.Draw();

		// A field that stops being drawn while active never reports its deactivation, which would leave its edit open
		// and the UI locked. That happens when typing into a field and then selecting an element of another type in
		// the Outliner, which draws before the Properties.
		if (const auto edit = std::get_if<FieldEditInteraction>(&interaction); edit && ImGui::GetActiveID() != edit->id) EndInteraction();
	}

	std::optional<std::string_view> StudioSession::GetStatus() const
	{
		if (m_status.empty() || ImGui::GetTime() - m_statusTime >= kStatusDuration) return std::nullopt;
		return m_status;
	}

	bool StudioSession::SaveProject()
	{
		// Playback samples between frames; save the frame it stops on.
		m_project->Pause();
		const auto result = Serialization::StudioProjectExport::Save(path, *m_project, WriteEditorJson());
		if (!result) {
			SetStatus(result.error());
			return false;
		}
		m_project->MarkSaved();
		SetStatus(std::format("Saved {}.", name));
		return true;
	}

	void StudioSession::HandleKeybinds()
	{
		if (IsInteracting()) {
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) CancelInteraction();
			return;
		}
		if (ImGui::GetIO().WantTextInput || camera.IsMouseLooking() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
		for (size_t i = 0; i < static_cast<size_t>(Action::kCount); ++i) {
			const auto action = static_cast<Action>(i);
			if (IsChordPressed(GetActionChord(action), IsActionRepeatable(action))) {
				ExecuteAction(action);
				return;
			}
		}
	}

	void StudioSession::ValidateSelection()
	{
		std::erase_if(selection, [&](const auto& a_element) { return !ElementExists(a_element); });
		std::erase_if(selectedKeys, [&](const auto& a_key) { return !m_project->GetKey(a_key); });
		if (reveal && !ElementExists(*reveal)) reveal.reset();
	}

	void StudioSession::SetStatus(std::string a_status)
	{
		m_status = std::move(a_status);
		m_statusTime = ImGui::GetTime();
	}

	void StudioSession::BeginInteraction(Interaction a_interaction, std::string a_description)
	{
		EndInteraction();
		m_project->BeginEdit(std::move(a_description));
		interaction = std::move(a_interaction);
	}

	void StudioSession::EndInteraction()
	{
		if (!IsInteracting()) return;
		interaction = std::monostate{};
		m_project->EndEdit();
	}

	void StudioSession::CancelInteraction()
	{
		if (!IsInteracting()) return;
		if (const auto drag = std::get_if<KeyDragInteraction>(&interaction)) selectedKeys = { drag->original.begin(), drag->original.end() };
		if (std::holds_alternative<GizmoInteraction>(interaction)) gizmoCancelled = true;
		if (std::holds_alternative<FieldEditInteraction>(interaction)) ImGui::ClearActiveID();
		m_project->CancelEdit();
		interaction = std::monostate{};
		m_project->EndEdit();
		SetStatus("Cancelled.");
	}

	bool StudioSession::IsInteracting() const
	{
		return !std::holds_alternative<std::monostate>(interaction);
	}
}
