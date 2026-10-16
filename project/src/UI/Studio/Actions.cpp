#include "UI/Studio/StudioSession.h"

#include "Animation/Studio/StudioMath.h"
#include "UI/Studio/StudioCamera.h"
#include "imgui/imgui_internal.h"

namespace UI::Studio
{
	namespace
	{
		constexpr ImGuiKeyChord Chord(ImGuiKey a_key, int a_mods = 0)
		{
			return static_cast<ImGuiKeyChord>(a_key) | a_mods;
		}

		constexpr int kCtrl = ImGuiMod_Ctrl;
		constexpr int kShift = ImGuiMod_Shift;
	}

	bool IsChordPressed(ImGuiKeyChord a_chord, bool a_repeat)
	{
		if (a_chord == 0) return false;
		const auto key = static_cast<ImGuiKey>(a_chord & ~ImGuiMod_Mask_);
		return ImGui::GetIO().KeyMods == (a_chord & ImGuiMod_Mask_) && ImGui::IsKeyPressed(key, a_repeat);
	}

	const char* GetActionLabel(Action a_action)
	{
		static constexpr std::array<const char*, static_cast<size_t>(Action::kCount)> labels{
			"Save",
			"Export GLB...",
			"Close Project",
			"Undo",
			"Redo",
			"Play / Pause",
			"Next Frame",
			"Previous Frame",
			"Next Keyframe",
			"Previous Keyframe",
			"First Frame",
			"Last Frame",
			"Insert Keyframe",
			"Delete",
			"Copy Keyframes",
			"Paste Keyframes",
			"Select All",
			"Rename",
			"Translate",
			"Rotate",
			"Scale",
			"Toggle Local/World",
			"Create IK Chain",
			"Create Finger Controls",
			"Create Spline IK...",
			"Add Corrective Drivers",
			"Add Actor",
			"Add Locator",
			"Focus Selection"
		};
		return labels[static_cast<size_t>(a_action)];
	}

	ImGuiKeyChord GetActionChord(Action a_action)
	{
		// Fixed defaults. Every keybind lookup goes through here, so making them configurable only means
		// replacing this table with a runtime one.
		static constexpr std::array<ImGuiKeyChord, static_cast<size_t>(Action::kCount)> keybinds{
			Chord(ImGuiKey_S, kCtrl),          // kSave
			0,                                 // kExportGLB
			0,                                 // kClose
			Chord(ImGuiKey_Z, kCtrl),          // kUndo
			Chord(ImGuiKey_Y, kCtrl),          // kRedo
			Chord(ImGuiKey_Space),             // kPlayPause
			Chord(ImGuiKey_RightArrow),        // kNextFrame
			Chord(ImGuiKey_LeftArrow),         // kPreviousFrame
			Chord(ImGuiKey_UpArrow),           // kNextKey
			Chord(ImGuiKey_DownArrow),         // kPreviousKey
			Chord(ImGuiKey_Home),              // kFirstFrame
			Chord(ImGuiKey_End),               // kLastFrame
			Chord(ImGuiKey_I),                 // kInsertKey
			Chord(ImGuiKey_Delete),            // kDeleteSelection
			Chord(ImGuiKey_C, kCtrl),          // kCopyKeys
			Chord(ImGuiKey_V, kCtrl),          // kPasteKeys
			Chord(ImGuiKey_A, kCtrl),          // kSelectAll
			Chord(ImGuiKey_F2),                // kRename
			Chord(ImGuiKey_T),                 // kGizmoTranslate
			Chord(ImGuiKey_R),                 // kGizmoRotate
			Chord(ImGuiKey_Y),                 // kGizmoScale
			Chord(ImGuiKey_X),                 // kToggleGizmoSpace
			Chord(ImGuiKey_I, kShift),         // kCreateIKChain
			0,                                 // kCreateFingerControls
			0,                                 // kCreateSplineIK
			0,                                 // kCreateCorrectiveDrivers
			Chord(ImGuiKey_A, kShift),         // kAddActor
			Chord(ImGuiKey_L, kShift),         // kAddLocator
			Chord(ImGuiKey_F)                  // kFocusSelection
		};
		return keybinds[static_cast<size_t>(a_action)];
	}

	bool IsActionRepeatable(Action a_action)
	{
		switch (a_action) {
		case Action::kNextFrame:
		case Action::kPreviousFrame:
		case Action::kNextKey:
		case Action::kPreviousKey:
			return true;
		default:
			return false;
		}
	}

	std::string GetShortcutText(Action a_action)
	{
		const auto chord = GetActionChord(a_action);
		if (chord == 0) return {};
		char buffer[64];
		ImGui::GetKeyChordName(chord, buffer, sizeof(buffer));
		return buffer;
	}

	bool StudioSession::CanExecute(Action a_action) const
	{
		if (!m_project || IsInteracting()) return false;
		const auto active = GetActiveElement();
		switch (a_action) {
		case Action::kExportGLB:
			return std::ranges::any_of(m_project->GetObjects(), [&](ProjectEx::ObjectId a_id) { return m_project->GetBoundActor(a_id) != nullptr; });
		case Action::kUndo:
			return m_project->CanUndo();
		case Action::kRedo:
			return m_project->CanRedo();
		case Action::kInsertKey:
			return !selection.empty();
		case Action::kDeleteSelection:
			return timeline.IsFocused() ? !selectedKeys.empty() : std::ranges::any_of(selection, [&](const auto& a_element) { return !IsBone(a_element); });
		case Action::kCopyKeys:
			return !selectedKeys.empty();
		case Action::kPasteKeys:
			return !m_keyClipboard.empty();
		case Action::kRename:
			return active && !IsBone(*active);
		case Action::kCreateIKChain:
			return CanCreateIKChainFromSelection();
		case Action::kCreateFingerControls:
			return CanCreateFingerControlsFromSelection();
		case Action::kCreateSplineIK:
			return GetSplineIKSelection().has_value();
		case Action::kCreateCorrectiveDrivers:
			return CanCreateCorrectiveDriversFromSelection();
		case Action::kFocusSelection:
			return active.has_value();
		default:
			return true;
		}
	}

	void StudioSession::ExecuteAction(Action a_action)
	{
		if (!CanExecute(a_action)) return;
		const auto active = GetActiveElement();
		switch (a_action) {
		case Action::kSave:
			SaveProject();
			break;
		case Action::kExportGLB:
			RequestPopup({ Popup::kExportGLB });
			break;
		case Action::kClose:
			if (m_project->HasUnsavedChanges()) RequestPopup({ Popup::kConfirmClose });
			else closeRequested = true;
			break;
		case Action::kUndo:
			{
				const std::string description{ m_project->GetHistoryDescription(m_project->GetHistoryHead() - 1) };
				if (m_project->Undo()) SetStatus("Undid " + description + ".");
				break;
			}
		case Action::kRedo:
			{
				const std::string description{ m_project->GetHistoryDescription(m_project->GetHistoryHead()) };
				if (m_project->Redo()) SetStatus("Redid " + description + ".");
				break;
			}
		case Action::kPlayPause:
			if (m_project->IsPlaying()) m_project->Pause();
			else m_project->Play(timeline.GetLoopRange());
			break;
		case Action::kNextFrame:
			m_project->SetFrame(m_project->GetFrame() + 1);
			break;
		case Action::kPreviousFrame:
			m_project->SetFrame(m_project->GetFrame() - 1);
			break;
		case Action::kNextKey:
			StepToKey(1);
			break;
		case Action::kPreviousKey:
			StepToKey(-1);
			break;
		case Action::kFirstFrame:
			m_project->SetFrame(timeline.GetLoopRange() ? timeline.GetLoopRange()->first : 0);
			break;
		case Action::kLastFrame:
			if (const auto sequence = GetActiveSequenceInfo(*m_project)) m_project->SetFrame(timeline.GetLoopRange() ? timeline.GetLoopRange()->second : sequence->frameCount);
			break;
		case Action::kInsertKey:
			InsertKeys();
			break;
		case Action::kDeleteSelection:
			DeleteSelection();
			break;
		case Action::kCopyKeys:
			CopySelectedKeys();
			break;
		case Action::kPasteKeys:
			PasteKeys();
			break;
		case Action::kSelectAll:
			SelectAll();
			break;
		case Action::kRename:
			RequestPopup({ Popup::kRename, active->object });
			break;
		case Action::kGizmoTranslate:
			gizmoMode = GizmoMode::kTranslate;
			break;
		case Action::kGizmoRotate:
			gizmoMode = GizmoMode::kRotate;
			break;
		case Action::kGizmoScale:
			gizmoMode = GizmoMode::kScale;
			break;
		case Action::kToggleGizmoSpace:
			gizmoSpace = gizmoSpace == GizmoSpace::kLocal ? GizmoSpace::kWorld : GizmoSpace::kLocal;
			break;
		case Action::kCreateIKChain:
			CreateIKChainFromSelection();
			break;
		case Action::kCreateFingerControls:
			CreateFingerControlsFromSelection();
			break;
		case Action::kCreateSplineIK:
			RequestPopup({ Popup::kCreateSplineIK });
			break;
		case Action::kCreateCorrectiveDrivers:
			CreateCorrectiveDriversFromSelection();
			break;
		case Action::kAddActor:
			RequestPopup({ Popup::kAddActorMenu });
			break;
		case Action::kAddLocator:
			AddLocator({}, GetSpawnTransform());
			break;
		case Action::kFocusSelection:
			if (const auto world = m_project->GetWorldTransform(*active)) camera.Focus(Animation::Studio::GetTranslation(*world));
			break;
		case Action::kCount:
			break;
		}
	}

	void StudioSession::ActionMenuItem(Action a_action, const char* a_label)
	{
		const auto shortcut = GetShortcutText(a_action);
		if (ImGui::MenuItem(a_label ? a_label : GetActionLabel(a_action), shortcut.empty() ? nullptr : shortcut.c_str(), false, CanExecute(a_action))) {
			ExecuteAction(a_action);
		}
	}
}
