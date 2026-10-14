#pragma once

#include "UI/Studio/Actions.h"
#include "UI/Studio/Dialogs/ExportDialog.h"
#include "UI/Studio/Dialogs/Popups.h"
#include "UI/Studio/Dialogs/RigDialogs.h"
#include "UI/Studio/Panels/HistoryPanel.h"
#include "UI/Studio/Panels/OptionsBar.h"
#include "UI/Studio/Panels/Outliner.h"
#include "UI/Studio/Panels/Properties.h"
#include "UI/Studio/Panels/Timeline.h"
#include "UI/Studio/Panels/Viewport.h"
#include "UI/Studio/StudioWidgets.h"

#include <nlohmann/json_fwd.hpp>

namespace UI::Studio
{
	class StudioCamera;

	enum class GizmoMode : uint8_t
	{
		kTranslate,
		kRotate,
		kScale
	};

	enum class GizmoSpace : uint8_t
	{
		kLocal,
		kWorld
	};

	enum class SelectMode : uint8_t
	{
		kReplace,
		kToggle,
		kAdd
	};

	// How a bone's diamond is shown in the viewport. View state only; it doesn't affect the animation.
	struct BoneDisplay
	{
		bool visible{ true };
		float scale{ 1.0f };  // Multiplies the diamond's width, which is otherwise derived from the bone's length.
	};

	struct WindowVisibility
	{
		bool outliner{ true };
		bool properties{ true };
		bool timeline{ true };
		bool history{ false };
	};

	// ---- Interactions ----
	// At most one runs at a time. Each owns a project edit for its duration; Escape cancels it.

	struct GizmoTarget
	{
		ProjectEx::ElementRef element;
		ozz::math::Float4x4 startWorld;
	};

	struct GizmoInteraction
	{
		std::vector<GizmoTarget> targets;  // Parents before children.
		ozz::math::Float4x4 pivotStart;
		ozz::math::Float4x4 pivot;  // Manipulated by the gizmo. Targets follow pivot * inverse(pivotStart).
	};

	struct KeyDragInteraction
	{
		std::vector<ProjectEx::KeyRef> original;  // Reapplied with the current delta after cancelling each frame.
		float startMouseX{ 0.0f };
		int32_t delta{ 0 };
	};

	struct ScrubInteraction
	{
	};

	struct BoxSelectInteraction
	{
		ImVec2 start;
		bool additive{ false };
	};

	// A Properties window field being dragged or typed into.
	struct FieldEditInteraction
	{
		ImGuiID id{ 0 };
	};

	using Interaction = std::variant<std::monostate, GizmoInteraction, KeyDragInteraction, ScrubInteraction, BoxSelectInteraction, FieldEditInteraction>;

	// One open project in the editor: the project, everything its panels share, and the panels. Closing the project
	// destroys the session, so per-project UI state can't outlive it.
	//
	// Split by topic: StudioSession.cpp (frame, interactions, saving), Selection.cpp, EditCommands.cpp, Actions.cpp
	// and EditorSettings.cpp.
	class StudioSession
	{
	public:
		StudioSession(std::unique_ptr<ProjectEx> a_project, std::filesystem::path a_path, StudioCamera& a_camera);
		~StudioSession();

		StudioSession(const StudioSession&) = delete;
		StudioSession& operator=(const StudioSession&) = delete;

		[[nodiscard]] ProjectEx& GetProject() { return *m_project; }

		// ---- Frame ----

		// Drops selected elements and keys that no longer exist, since undo can remove them.
		void ValidateSelection();
		void HandleKeybinds();
		// The editing UI: every window and popup. The caller sets up the dockspace first.
		void Draw(Renderer* a_renderer);
		void SetStatus(std::string a_status);
		[[nodiscard]] std::optional<std::string_view> GetStatus() const;  // While it's recent enough to show.
		bool SaveProject();
		void RequestPopup(PopupRequest a_request) { popups.Request(a_request); }

		// ---- Actions ----

		[[nodiscard]] bool CanExecute(Action a_action) const;
		void ExecuteAction(Action a_action);
		void ActionMenuItem(Action a_action, const char* a_label = nullptr);

		// ---- Interactions ----

		void BeginInteraction(Interaction a_interaction, std::string a_description);
		void EndInteraction();
		void CancelInteraction();
		[[nodiscard]] bool IsInteracting() const;

		// ---- Selection & elements ----

		static SelectMode GetClickSelectMode();
		void Select(ProjectEx::ElementRef a_element, SelectMode a_mode);
		[[nodiscard]] bool IsSelected(ProjectEx::ElementRef a_element) const;
		[[nodiscard]] std::optional<ProjectEx::ElementRef> GetActiveElement() const;
		[[nodiscard]] bool IsBone(ProjectEx::ElementRef a_element) const;
		[[nodiscard]] bool ElementExists(ProjectEx::ElementRef a_element) const;
		[[nodiscard]] std::string GetElementLabel(ProjectEx::ElementRef a_element) const;
		[[nodiscard]] std::vector<ProjectEx::PropertyRef> GetElementProperties(ProjectEx::ElementRef a_element) const;
		// Orders gizmo targets and new chains parents first: an actor, its bones by depth, then locators.
		[[nodiscard]] int32_t GetHierarchyDepth(ProjectEx::ElementRef a_element) const;
		[[nodiscard]] std::vector<ProjectEx::ElementRef> GetSelectedBones(bool a_includeDescendants) const;
		// The active element's actor (a modifier's owner) when it's bound, otherwise the first bound actor.
		[[nodiscard]] ProjectEx::ObjectId GetDefaultActor() const;

		// ---- Bone display ----

		[[nodiscard]] BoneDisplay GetBoneDisplay(ProjectEx::ElementRef a_bone) const;
		void SetBoneDisplay(std::span<const ProjectEx::ElementRef> a_bones, std::optional<bool> a_visible, std::optional<float> a_scale);
		void ResetBoneDisplay(ProjectEx::ObjectId a_actor);

		// ---- Editing operations ----

		void InsertKeys();
		void CopySelectedKeys();
		void PasteKeys();
		void DeleteSelection();
		void SelectAll();
		void StepToKey(int32_t a_direction);
		void CreateIKChainFromSelection();
		[[nodiscard]] bool CanCreateIKChainFromSelection() const;
		void CreateFingerControlsFromSelection();
		[[nodiscard]] bool CanCreateFingerControlsFromSelection() const;
		// a_controlCount is clamped to the path's length.
		void CreateSplineIKFromSelection(uint32_t a_controlCount);
		// The selection's two bones, start first, when they make a valid spline path. Its length in bones is the
		// third value.
		[[nodiscard]] std::optional<std::tuple<ProjectEx::ElementRef, ProjectEx::ElementRef, size_t>> GetSplineIKSelection() const;
		void ApplyHandPose(ProjectEx::ObjectId a_hand, const std::function<std::pair<float, float>(size_t a_finger)>& a_values, const char* a_description);
		void AddActor(RE::Actor* a_actor);
		void BindActor(ProjectEx::ObjectId a_slot, RE::Actor* a_actor);
		void AddLocator(ProjectEx::ParentRef a_parent, std::optional<ozz::math::Float4x4> a_world);
		void ResetTransforms(std::span<const ProjectEx::ElementRef> a_elements);
		void SetLocatorDefaults(std::span<const ProjectEx::ElementRef> a_elements);
		// Keys a_properties when Auto Key is on. Keying is a UI preference, so edits call this themselves.
		void AutoKey(std::span<const ProjectEx::PropertyRef> a_properties);
		// Sets a value and auto-keys it, folded into an open field edit or as its own undo step.
		void ApplyFieldValue(const ProjectEx::PropertyRef& a_property, const ProjectEx::Value& a_value);
		// At the active element, or in front of the camera.
		[[nodiscard]] std::optional<ozz::math::Float4x4> GetSpawnTransform();
		// The previous binding, the console selection, nearby actors, or a form ID popup. Shared by every actor menu.
		void DrawActorSourceMenu(const std::function<void(RE::Actor*)>& a_onPick, Popup a_formIdPopup, ProjectEx::ObjectId a_target);

		// ---- Editor settings ----
		// The project file's "editor" section: UI preferences that belong to the project. Each panel reads and writes
		// its own keys.

		[[nodiscard]] std::string WriteEditorJson() const;
		void ReadEditorJson(std::string_view a_json, std::vector<std::string>& a_warnings);
		// Elements by bone name, like the rest of the project file. Null when the bone can't be named.
		[[nodiscard]] nlohmann::json ElementToJson(const ProjectEx::ElementRef& a_element) const;
		// Throws on malformed JSON, like the rest of ReadEditorJson.
		[[nodiscard]] std::optional<ProjectEx::ElementRef> ElementFromJson(const nlohmann::json& a_item) const;

		// ---- Shared state ----

		std::string name;
		std::filesystem::path path;
		StudioCamera& camera;
		bool closeRequested{ false };  // Closing is deferred to the end of Draw, after everything using the project.

		// Selection. Validated against the project each frame, since undo can remove selected things.
		std::vector<ProjectEx::ElementRef> selection;  // back() is the active element.
		std::set<ProjectEx::KeyRef> selectedKeys;
		std::optional<ProjectEx::ElementRef> reveal;  // Expanded and scrolled to in the outliner on its next draw.

		Interaction interaction;
		bool gizmoCancelled{ false };  // Ignore the gizmo until the mouse is released after a cancel.

		// Editing preferences.
		GizmoMode gizmoMode{ GizmoMode::kRotate };
		GizmoSpace gizmoSpace{ GizmoSpace::kLocal };  // Forced to world while more than one element is selected.
		bool autoKey{ false };
		ProjectEx::Interpolation defaultInterpolation{ ProjectEx::Interpolation::kBezier };

		std::map<ProjectEx::ElementRef, BoneDisplay> boneDisplay;  // Only bones that differ from the default.
		WindowVisibility windows;

	private:
		std::unique_ptr<ProjectEx> m_project;
		std::vector<ProjectEx::KeyData> m_keyClipboard;  // Frames relative to the earliest copied key.
		std::string m_status;
		double m_statusTime{ 0.0 };

	public:
		// Panels and dialogs, after the state they use.
		OptionsBar optionsBar{ *this };
		Outliner outliner{ *this };
		Properties properties{ *this };
		Timeline timeline{ *this };
		HistoryPanel history{ *this };
		Viewport viewport{ *this };
		Popups popups{ *this };
		ExportDialog exportDialog{ *this };
		RigDialogs rigDialogs{ *this };
	};
}
