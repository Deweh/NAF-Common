#include "UI/Studio/Panels/OptionsBar.h"

#include "UI/Studio/StudioSession.h"
#include "UI/Studio/StudioCamera.h"
#include "imgui/imgui_internal.h"

namespace UI::Studio
{
	OptionsBar::OptionsBar(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	void OptionsBar::Draw()
	{
		ImGuiWindowClass windowClass;
		windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
		ImGui::SetNextWindowClass(&windowClass);
		if (!ImGui::Begin(kOptionsWindow, nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return;
		}

		DrawMainMenuBar();
		DrawSequenceSelector();
		if (const auto sequence = GetActiveSequenceInfo(*m_project)) {
			ImGui::SameLine();
			ImGui::Text("Frame %d / %d  @ %.2f fps", m_project->GetFrame(), sequence->frameCount, sequence->frameRate);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("| %s%s", m_session.name.c_str(), m_project->HasUnsavedChanges() ? " *" : "");
		if (const auto status = m_session.GetStatus()) {
			ImGui::SameLine();
			ImGui::TextDisabled("| %.*s", static_cast<int>(status->size()), status->data());
		}

		if (ImGui::BeginPopupContextWindow("##options_context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
			DrawCreateMenuItems();
			ImGui::EndPopup();
		}
		ImGui::End();
	}

	void OptionsBar::DrawMainMenuBar()
	{
		if (!ImGui::BeginMenuBar()) return;

		if (ImGui::BeginMenu("File")) {
			m_session.ActionMenuItem(Action::kSave);
			if (ImGui::BeginMenu("Export")) {
				m_session.ActionMenuItem(Action::kExportGLB, "GLB...");
				ImGui::EndMenu();
			}
			ImGui::Separator();
			const bool hasBoundActor = m_session.GetDefaultActor() != ProjectEx::kNoObject;
			if (ImGui::MenuItem("Save Rig...", nullptr, false, hasBoundActor)) m_session.RequestPopup({ Popup::kSaveRig });
			if (ImGui::MenuItem("Apply Rig...", nullptr, false, hasBoundActor)) m_session.RequestPopup({ Popup::kApplyRig });
			ImGui::Separator();
			m_session.ActionMenuItem(Action::kClose);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit")) {
			const size_t head = m_project->GetHistoryHead();
			const auto undoLabel = m_project->CanUndo() ? std::format("Undo {}", m_project->GetHistoryDescription(head - 1)) : std::string{ "Undo" };
			const auto redoLabel = m_project->CanRedo() ? std::format("Redo {}", m_project->GetHistoryDescription(head)) : std::string{ "Redo" };
			m_session.ActionMenuItem(Action::kUndo, undoLabel.c_str());
			m_session.ActionMenuItem(Action::kRedo, redoLabel.c_str());
			ImGui::Separator();
			m_session.ActionMenuItem(Action::kInsertKey);
			m_session.ActionMenuItem(Action::kCopyKeys);
			m_session.ActionMenuItem(Action::kPasteKeys);
			m_session.ActionMenuItem(Action::kDeleteSelection);
			m_session.ActionMenuItem(Action::kSelectAll);
			m_session.ActionMenuItem(Action::kRename);
			ImGui::Separator();
			ImGui::MenuItem("Auto Key", nullptr, &m_session.autoKey);
			if (ImGui::BeginMenu("Default Interpolation")) {
				for (const auto interpolation : { ProjectEx::Interpolation::kLinear, ProjectEx::Interpolation::kBezier, ProjectEx::Interpolation::kStep }) {
					if (ImGui::MenuItem(GetInterpolationName(interpolation), nullptr, m_session.defaultInterpolation == interpolation)) m_session.defaultInterpolation = interpolation;
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Create")) {
			DrawCreateMenuItems();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Playback")) {
			m_session.ActionMenuItem(Action::kPlayPause);
			ImGui::Separator();
			m_session.ActionMenuItem(Action::kFirstFrame);
			m_session.ActionMenuItem(Action::kPreviousFrame);
			m_session.ActionMenuItem(Action::kNextFrame);
			m_session.ActionMenuItem(Action::kLastFrame);
			ImGui::Separator();
			m_session.ActionMenuItem(Action::kPreviousKey);
			m_session.ActionMenuItem(Action::kNextKey);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Outliner", nullptr, &m_session.windows.outliner);
			ImGui::MenuItem("Properties", nullptr, &m_session.windows.properties);
			ImGui::MenuItem("Timeline", nullptr, &m_session.windows.timeline);
			ImGui::MenuItem("History", nullptr, &m_session.windows.history);
			ImGui::Separator();
			ImGui::MenuItem("Bones", nullptr, &m_session.viewport.view.bones);
			ImGui::MenuItem("Locators", nullptr, &m_session.viewport.view.locators);
			ImGui::MenuItem("IK Lines", nullptr, &m_session.viewport.view.ikLines);
			ImGui::MenuItem("Scene Center", nullptr, &m_session.viewport.view.sceneCenter);
			ImGui::Separator();
			bool cameraEnabled = m_session.camera.IsEnabled();
			if (ImGui::MenuItem("Studio Camera", nullptr, &cameraEnabled)) m_session.camera.SetEnabled(cameraEnabled);
			ImGui::SetNextItemWidth(120.0f);
			ImGui::SliderFloat("Camera Speed", &m_session.camera.MoveSpeed(), 0.05f, 20.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
			m_session.ActionMenuItem(Action::kFocusSelection);
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void OptionsBar::DrawCreateMenuItems()
	{
		if (ImGui::BeginMenu("Add Actor")) {
			m_session.DrawActorSourceMenu([&](RE::Actor* a_actor) { m_session.AddActor(a_actor); }, Popup::kAddActorByFormId, ProjectEx::kNoObject);
			ImGui::EndMenu();
		}
		m_session.ActionMenuItem(Action::kAddLocator);
		m_session.ActionMenuItem(Action::kCreateIKChain);
		m_session.ActionMenuItem(Action::kCreateSplineIK);
		m_session.ActionMenuItem(Action::kCreateFingerControls);
#ifdef TARGET_GAME_F4
		m_session.ActionMenuItem(Action::kCreateCorrectiveDrivers);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Select an actor that doesn't have them yet.");
#endif
		ImGui::Separator();
		if (ImGui::MenuItem("New Sequence...")) m_session.RequestPopup({ Popup::kNewSequence });
	}

	void OptionsBar::DrawSequenceSelector()
	{
		const auto sequences = m_project->GetSequences();
		const auto activeId = m_project->GetActiveSequenceId();
		const auto active = std::ranges::find(sequences, activeId, &ProjectEx::SequenceInfo::id);
		const std::string activeName = active != sequences.end() ? std::string{ active->name } : std::string{};

		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::BeginCombo("##sequence", activeName.c_str())) {
			for (const auto& sequence : sequences) {
				ImGui::PushID(static_cast<int>(sequence.id));
				if (ImGui::Selectable(std::string{ sequence.name }.c_str(), sequence.id == activeId)) m_project->SetActiveSequence(sequence.id);
				ImGui::PopID();
			}
			ImGui::Separator();
			if (ImGui::Selectable("New Sequence...")) m_session.RequestPopup({ Popup::kNewSequence });
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click for sequence options.");
		if (ImGui::BeginPopupContextItem("##sequence_context")) {
			if (ImGui::MenuItem("New Sequence...")) m_session.RequestPopup({ Popup::kNewSequence });
			if (ImGui::MenuItem("Duplicate")) m_project->DuplicateSequence(activeId);
			if (ImGui::MenuItem("Rename...")) m_session.RequestPopup({ .popup = Popup::kRename, .sequence = activeId });
			if (ImGui::MenuItem("Settings...")) m_session.RequestPopup({ .popup = Popup::kSequenceSettings, .sequence = activeId });
			const bool loopSmoothing = active != sequences.end() && active->loopSmoothing;
			if (ImGui::MenuItem("Cyclic", nullptr, loopSmoothing)) m_project->SetSequenceLoopSmoothing(activeId, !loopSmoothing);
			ImGui::Separator();
			if (ImGui::MenuItem("Remove", nullptr, false, sequences.size() > 1)) m_project->RemoveSequence(activeId);
			ImGui::EndPopup();
		}
	}
}
