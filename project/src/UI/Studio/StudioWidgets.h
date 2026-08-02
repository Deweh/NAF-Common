#pragma once

#include "Animation/Studio/ProjectEx.h"
#include "UI/DX11Renderer.h"
#include "imgui/imgui.h"

// Constants, colors and small ImGui helpers shared by the Studio's panels and dialogs.
namespace UI::Studio
{
	using ProjectEx = Animation::Studio::ProjectEx;

	// ---- Windows ----

	inline constexpr const char* kOptionsWindow = "Studio##StudioEx";
	inline constexpr const char* kOutlinerWindow = "Outliner##StudioEx";
	inline constexpr const char* kPropertiesWindow = "Properties##StudioEx";
	inline constexpr const char* kTimelineWindow = "Timeline##StudioEx";
	inline constexpr const char* kHistoryWindow = "History##StudioEx";

	inline constexpr double kStatusDuration = 6.0;
	// How far the camera backs off from what it focuses on, and where new locators go when nothing is selected.
	inline constexpr float kFocusDistance = 150.0f;
	// Where a project's scene center starts when it has no actor to start on: this far in front of the camera.
	inline constexpr float kDefaultSceneCenterDistance = 100.0f;
	inline constexpr float kSceneCenterSize = 30.0f;
	inline constexpr size_t kMaxNearbyActors = 32;

	// ---- Colors ----

	inline constexpr ImU32 kColorSelected = IM_COL32(255, 170, 50, 255);
	inline constexpr ImU32 kColorHovered = IM_COL32(110, 220, 255, 255);
	inline constexpr ImU32 kColorBone = IM_COL32(220, 225, 235, 180);
	inline constexpr ImU32 kColorBoneIK = IM_COL32(130, 220, 150, 200);
	inline constexpr ImU32 kColorEffectorReached = IM_COL32(70, 230, 120, 255);
	inline constexpr ImU32 kColorEffectorUnreached = IM_COL32(255, 80, 80, 255);
	inline constexpr ImU32 kColorPole = IM_COL32(100, 160, 255, 255);
	inline constexpr ImU32 kColorLocator = IM_COL32(240, 220, 90, 255);
	inline constexpr ImU32 kColorSplineControl = IM_COL32(255, 150, 60, 255);
	inline constexpr ImU32 kColorSplineCurve = IM_COL32(255, 150, 60, 150);
	inline constexpr ImU32 kColorIKLine = IM_COL32(200, 200, 200, 120);
	inline constexpr ImU32 kColorShoulderGuide = IM_COL32(120, 200, 255, 160);
	inline constexpr ImU32 kColorSceneCenter = IM_COL32(255, 110, 200, 220);
	inline constexpr ImU32 kColorKey = IM_COL32(200, 200, 200, 255);
	inline constexpr ImU32 kColorKeyHere = IM_COL32(240, 200, 60, 255);
	inline constexpr ImU32 kColorEdited = IM_COL32(255, 140, 40, 255);
	inline constexpr ImU32 kColorPlayhead = IM_COL32(90, 160, 255, 255);

	inline constexpr ImVec4 kTextWarning{ 1.0f, 0.75f, 0.3f, 1.0f };
	inline constexpr ImVec4 kTextError{ 1.0f, 0.4f, 0.4f, 1.0f };

	// ---- Widgets ----

	bool InputTextString(const char* a_label, const char* a_hint, std::string& a_value, ImGuiInputTextFlags a_flags = 0);
	void DrawDiamond(ImDrawList* a_drawList, const ImVec2& a_center, float a_radius, ImU32 a_fill, ImU32 a_border);
	// A cross on the ground at the scene center, with its forward arm longer.
	void DrawSceneCenter(DX11Renderer* a_renderer, const ProjectEx::ScenePlacement& a_center);

	// ---- Names ----

	const char* GetInterpolationName(ProjectEx::Interpolation a_interpolation);
	const char* GetPropertyName(ProjectEx::PropertyId a_property);
	// A hand's finger properties carry their finger's name; the hand's own Curl is the whole fist.
	std::string GetPropertyLabel(const ProjectEx& a_project, const ProjectEx::PropertyRef& a_property);
	std::string GetActorLabel(RE::Actor* a_actor);

	// ---- Files ----

	// ImGui text is UTF-8; a std::string converts to a path through the ANSI code page.
	std::filesystem::path Utf8ToPath(std::string_view a_text);
	std::string PathToUtf8(const std::filesystem::path& a_path);
	// Replaces characters Windows doesn't allow in file names.
	std::string SanitizeFileName(std::string a_name);
	// Relative to the game folder when it's inside it, for display.
	std::string GetDisplayPath(const std::filesystem::path& a_path);

	// ---- Game ----

	std::optional<ProjectEx::SequenceInfo> GetActiveSequenceInfo(const ProjectEx& a_project);
	// An "Actor" combo listing every actor; unbound ones can't be picked. Returns whether the choice changed.
	bool DrawBoundActorCombo(const ProjectEx& a_project, ProjectEx::ObjectId& a_actor, float a_width);
	RE::NiPoint3 ToNiPoint(const ozz::math::Float3& a_point);
	// a_distance in front of the camera, facing away from it.
	std::optional<ProjectEx::ScenePlacement> GetPlacementInFrontOfCamera(float a_distance);
	RE::Actor* GetConsoleSelectedActor();
	// The player and up to kMaxNearbyActors loaded high-process actors, nearest first.
	std::vector<RE::Actor*> GetNearbyActors();
}
