#pragma once

#include "imgui/imgui.h"

namespace UI::Studio
{
	// Everything a menu item, context menu entry or keybind can do. All of them run through StudioSession::ExecuteAction.
	enum class Action : uint8_t
	{
		kSave,
		kExportGLB,
		kClose,
		kUndo,
		kRedo,
		kPlayPause,
		kNextFrame,
		kPreviousFrame,
		kNextKey,
		kPreviousKey,
		kFirstFrame,
		kLastFrame,
		kInsertKey,
		kDeleteSelection,
		kCopyKeys,
		kPasteKeys,
		kSelectAll,
		kRename,
		kGizmoTranslate,
		kGizmoRotate,
		kGizmoScale,
		kToggleGizmoSpace,
		kCreateIKChain,
		kCreateFingerControls,
		kCreateSplineIK,
		kAddActor,
		kAddLocator,
		kFocusSelection,

		kCount
	};

	const char* GetActionLabel(Action a_action);
	// Fixed defaults. Every keybind lookup goes through here, so making them configurable only means replacing its
	// table with a runtime one.
	ImGuiKeyChord GetActionChord(Action a_action);
	bool IsActionRepeatable(Action a_action);
	std::string GetShortcutText(Action a_action);
	// Whether a_chord was pressed this frame with exactly its modifiers.
	bool IsChordPressed(ImGuiKeyChord a_chord, bool a_repeat);
}
