#include "UI/Studio/Dialogs/Popups.h"

#include "UI/Studio/StudioSession.h"

namespace UI::Studio
{
	Popups::Popups(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	const char* Popups::GetName(Popup a_popup)
	{
		static constexpr std::array<const char*, 14> names{
			"", "##add_actor_menu", "Add Actor by Form ID", "Bind Actor by Form ID", "Rename", "New Sequence", "Sequence Settings", "Close Project", "Project Opened", "Create Spline IK",
			"Export GLB", "Save Rig", "Apply Rig", "Rig Applied"
		};
		const auto index = static_cast<size_t>(a_popup);
		return index < names.size() ? names[index] : "";
	}

	void Popups::ShowWarnings(Popup a_popup, std::vector<std::string> a_warnings)
	{
		m_warnings = std::move(a_warnings);
		Request({ a_popup });
	}

	void Popups::Open(const PopupRequest& a_request)
	{
		const auto sequence = GetActiveSequenceInfo(*m_project);
		switch (a_request.popup) {
		case Popup::kAddActorByFormId:
		case Popup::kBindActor:
			m_formIdInput = 0;
			break;
		case Popup::kRename:
			if (a_request.sequence != ProjectEx::kNoSequence) {
				for (const auto& info : m_project->GetSequences()) {
					if (info.id == a_request.sequence) m_textInput = info.name;
				}
			} else {
				m_textInput = m_project->GetObjectName(a_request.object);
			}
			break;
		case Popup::kNewSequence:
			m_textInput = std::format("Sequence {}", m_project->GetSequences().size() + 1);
			m_frameRateInput = sequence ? sequence->frameRate : 30.0f;
			m_frameCountInput = sequence ? sequence->frameCount : 30;
			break;
		case Popup::kSequenceSettings:
			for (const auto& info : m_project->GetSequences()) {
				if (info.id != a_request.sequence) continue;
				m_textInput = info.name;
				m_frameRateInput = info.frameRate;
				m_frameCountInput = info.frameCount;
				m_loopSmoothingInput = info.loopSmoothing;
			}
			m_rescaleKeysInput = true;
			break;
		case Popup::kCreateSplineIK:
			m_controlCountInput = 3;
			break;
		case Popup::kExportGLB:
			m_session.exportDialog.Open();
			break;
		case Popup::kSaveRig:
			m_session.rigDialogs.OpenSave(a_request.object);
			break;
		case Popup::kApplyRig:
			m_session.rigDialogs.OpenApply(a_request.object);
			break;
		default:
			break;
		}
	}

	void Popups::Draw()
	{
		const auto name = [](Popup a_popup) { return GetName(a_popup); };

		if (m_pending.popup != Popup::kNone) {
			m_open = std::exchange(m_pending, {});
			Open(m_open);
			ImGui::OpenPopup(name(m_open.popup));
		}

		if (ImGui::BeginPopup(name(Popup::kAddActorMenu))) {
			ImGui::TextDisabled("Add Actor");
			ImGui::Separator();
			m_session.DrawActorSourceMenu([&](RE::Actor* a_actor) { m_session.AddActor(a_actor); }, Popup::kAddActorByFormId, ProjectEx::kNoObject);
			ImGui::EndPopup();
		}

		const auto modal = [&](Popup a_popup) { return ImGui::BeginPopupModal(name(a_popup), nullptr, ImGuiWindowFlags_AlwaysAutoResize); };
		const auto buttons = [&](const char* a_confirm, bool a_enabled) {
			ImGui::BeginDisabled(!a_enabled);
			const bool confirmed = ImGui::Button(a_confirm, { 120.0f, 0.0f });
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", { 120.0f, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
			return confirmed;
		};

		for (const auto popup : { Popup::kAddActorByFormId, Popup::kBindActor }) {
			if (!modal(popup)) continue;
			if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
			const bool entered = ImGui::InputScalar("Form ID", ImGuiDataType_U32, &m_formIdInput, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
			const auto actor = RE::TESForm::GetFormByID<RE::Actor>(m_formIdInput);
			const char* actorName = actor ? actor->GetDisplayFullName() : nullptr;
			ImGui::TextDisabled("%s", actor ? (actorName && actorName[0] ? actorName : "Actor") : "No actor with that form ID.");
			if ((buttons(popup == Popup::kBindActor ? "Bind" : "Add", actor != nullptr) || entered) && actor) {
				if (popup == Popup::kBindActor) m_session.BindActor(m_open.object, actor);
				else m_session.AddActor(actor);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (modal(Popup::kRename)) {
			if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
			const bool entered = InputTextString("Name", nullptr, m_textInput, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			if ((buttons("Rename", !m_textInput.empty()) || entered) && !m_textInput.empty()) {
				if (m_open.sequence != ProjectEx::kNoSequence) m_project->RenameSequence(m_open.sequence, m_textInput);
				else m_project->RenameObject(m_open.object, m_textInput);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (modal(Popup::kNewSequence)) {
			InputTextString("Name", nullptr, m_textInput);
			ImGui::DragFloat("Frame Rate", &m_frameRateInput, 0.1f, 1.0f, 240.0f, "%.2f fps");
			ImGui::InputInt("Length (frames)", &m_frameCountInput);
			m_frameCountInput = (std::max)(1, m_frameCountInput);
			if (buttons("Create", !m_textInput.empty())) {
				m_project->BeginEdit("New sequence");
				if (const auto id = m_project->AddSequence(m_textInput, m_frameRateInput, m_frameCountInput); id != ProjectEx::kNoSequence) m_project->SetActiveSequence(id);
				m_project->EndEdit();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (modal(Popup::kCreateSplineIK)) {
			// Popups block the rest of the UI, so the selection is the one the dialog was opened for.
			const auto selection = m_session.GetSplineIKSelection();
			bool canCreate = false;
			if (selection) {
				const auto& [start, end, length] = *selection;
				ImGui::TextDisabled("%s > %s (%zu bones)", m_session.GetElementLabel(start).c_str(), m_session.GetElementLabel(end).c_str(), length);
				// At most one control per distinct point: bones on top of each other share one.
				const int32_t maximum = static_cast<int32_t>(m_project->GetSplineIKControlLimit(start.object, start.element, end.element));
				if (maximum >= 2) {
					canCreate = true;
					m_controlCountInput = std::clamp(m_controlCountInput, 2, maximum);
					ImGui::SliderInt("Controls", &m_controlCountInput, 2, maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Placed on bones spread evenly along the chain, always including both ends.\nCan't be changed once the spline is created.");
				} else {
					ImGui::TextDisabled("The bones are too close together to form a curve.");
				}
			} else {
				ImGui::TextDisabled("Select a start and an end bone, with at least one bone between them.");
			}
			if (buttons("Create", canCreate) && selection) {
				m_session.CreateSplineIKFromSelection(static_cast<uint32_t>(m_controlCountInput));
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		m_session.exportDialog.Draw();
		m_session.rigDialogs.Draw();

		if (modal(Popup::kSequenceSettings)) {
			InputTextString("Name", nullptr, m_textInput);
			ImGui::InputInt("Length (frames)", &m_frameCountInput);
			m_frameCountInput = (std::max)(1, m_frameCountInput);
			ImGui::DragFloat("Frame Rate", &m_frameRateInput, 0.1f, 1.0f, 240.0f, "%.2f fps");
			ImGui::Checkbox("Keep timing when changing frame rate", &m_rescaleKeysInput);
			ImGui::Checkbox("Cyclic", &m_loopSmoothingInput);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Loop every curve in this sequence seamlessly: after its last key, a curve\ncontinues into its first, so keys don't need to sit on frame 0 or the last frame.");
			if (buttons("Apply", !m_textInput.empty())) {
				// Length first, so rescaling for a new frame rate scales the new length.
				m_project->BeginEdit("Sequence settings");
				m_project->RenameSequence(m_open.sequence, m_textInput);
				m_project->SetSequenceFrameCount(m_open.sequence, m_frameCountInput);
				m_project->SetSequenceFrameRate(m_open.sequence, m_frameRateInput, m_rescaleKeysInput);
				m_project->SetSequenceLoopSmoothing(m_open.sequence, m_loopSmoothingInput);
				m_project->EndEdit();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (modal(Popup::kConfirmClose)) {
			ImGui::TextUnformatted("This project has unsaved changes. Save them before closing?");
			if (ImGui::Button("Save", { 120.0f, 0.0f })) {
				if (m_session.SaveProject()) m_session.closeRequested = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (buttons("Don't Save", true)) {
				m_session.closeRequested = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		for (const auto popup : { Popup::kLoadWarnings, Popup::kRigWarnings }) {
			if (!modal(popup)) continue;
			ImGui::TextUnformatted(popup == Popup::kLoadWarnings ? "The project opened, with these changes:" : "The rig was applied, with these changes:");
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
			for (const auto& warning : m_warnings) ImGui::BulletText("%s", warning.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			if (ImGui::Button("OK", { 120.0f, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				m_warnings.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}
