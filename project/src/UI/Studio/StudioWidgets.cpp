#include "UI/Studio/StudioWidgets.h"
#include "Util/Game.h"

#include "Animation/Studio/StudioMath.h"
#include "Util/String.h"

namespace UI::Studio
{
	namespace
	{
		int ResizeStringCallback(ImGuiInputTextCallbackData* a_data)
		{
			if (a_data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
				auto& string = *static_cast<std::string*>(a_data->UserData);
				string.resize(a_data->BufTextLen);
				a_data->Buf = string.data();
			}
			return 0;
		}
	}

	bool InputTextString(const char* a_label, const char* a_hint, std::string& a_value, ImGuiInputTextFlags a_flags)
	{
		a_flags |= ImGuiInputTextFlags_CallbackResize;
		return ImGui::InputTextWithHint(a_label, a_hint ? a_hint : "", a_value.data(), a_value.capacity() + 1, a_flags, ResizeStringCallback, &a_value);
	}

	const char* GetInterpolationName(ProjectEx::Interpolation a_interpolation)
	{
		switch (a_interpolation) {
		case ProjectEx::Interpolation::kLinear:
			return "Linear";
		case ProjectEx::Interpolation::kBezier:
			return "Bezier";
		case ProjectEx::Interpolation::kStep:
			return "Step";
		}
		return "";
	}

	const char* GetBezierModeName(ProjectEx::BezierMode a_mode)
	{
		switch (a_mode) {
		case ProjectEx::BezierMode::kLocal:
			return "Local";
		case ProjectEx::BezierMode::kContinuous:
			return "Continuous";
		}
		return "";
	}

	const char* GetPropertyName(ProjectEx::PropertyId a_property)
	{
		switch (a_property) {
		case ProjectEx::PropertyId::kTranslation:
			return "Translation";
		case ProjectEx::PropertyId::kRotation:
			return "Rotation";
		case ProjectEx::PropertyId::kScale:
			return "Scale";
		case ProjectEx::PropertyId::kWeight:
			return "Weight";
		case ProjectEx::PropertyId::kSoften:
			return "Soften";
		case ProjectEx::PropertyId::kTwist:
			return "Twist";
		case ProjectEx::PropertyId::kEnabled:
			return "Enabled";
		case ProjectEx::PropertyId::kShoulderWeight:
			return "Shoulder Weight";
		case ProjectEx::PropertyId::kCurl:
			return "Curl";
		case ProjectEx::PropertyId::kSpread:
			return "Spread";
		case ProjectEx::PropertyId::kStretch:
			return "Stretch";
		case ProjectEx::PropertyId::kTangentLength:
			return "Tangent Length";
		case ProjectEx::PropertyId::kOrient:
			return "Orient";
		case ProjectEx::PropertyId::kTwistDistribution:
			return "Twist Distribution";
		}
		return "";
	}

	// A hand's finger properties carry their finger's name; the hand's own Curl is the whole fist.
	std::string GetPropertyLabel(const ProjectEx& a_project, const ProjectEx::PropertyRef& a_property)
	{
		if (a_project.GetObjectType(a_property.object) == ProjectEx::ObjectType::kHand) {
			if (a_property.element == ProjectEx::kSelf) return a_property.property == ProjectEx::PropertyId::kCurl ? "Fist" : GetPropertyName(a_property.property);
			const auto hand = a_project.GetHand(a_property.object);
			const auto skeleton = hand ? a_project.GetSkeleton(hand->actor) : nullptr;
			if (skeleton && a_property.element < hand->settings.fingers.size()) {
				return std::format("{} {}", skeleton->joint_names()[hand->settings.fingers[a_property.element].bones.front()], GetPropertyName(a_property.property));
			}
		}
		return GetPropertyName(a_property.property);
	}
	std::string GetActorLabel(RE::Actor* a_actor)
	{
		const char* name = a_actor->GetDisplayFullName();
		return std::format("{} [{:08X}]", name && name[0] ? name : "Actor", a_actor->GetFormID());
	}

	// ImGui text is UTF-8; a std::string converts to a path through the ANSI code page.
	std::filesystem::path Utf8ToPath(std::string_view a_text)
	{
		return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(a_text.data()), a_text.size()));
	}

	std::string PathToUtf8(const std::filesystem::path& a_path)
	{
		const auto text = a_path.u8string();
		return { reinterpret_cast<const char*>(text.data()), text.size() };
	}

	// Replaces characters Windows doesn't allow in file names.
	std::string SanitizeFileName(std::string a_name)
	{
		for (auto& c : a_name) {
			if (static_cast<unsigned char>(c) < 32 || std::string_view{ "<>:\"/\\|?*" }.find(c) != std::string_view::npos) c = '_';
		}
		return a_name;
	}

	// Lists every actor; unbound ones can't be picked. Returns whether the choice changed.
	bool DrawBoundActorCombo(const ProjectEx& a_project, ProjectEx::ObjectId& a_actor, float a_width)
	{
		const auto label = [&](ProjectEx::ObjectId a_id) {
			return std::format("{}{}", a_project.GetObjectName(a_id), a_project.GetBoundActor(a_id) ? "" : " (unbound)");
		};
		bool changed = false;
		ImGui::SetNextItemWidth(a_width);
		if (ImGui::BeginCombo("Actor", a_actor != ProjectEx::kNoObject ? label(a_actor).c_str() : "None")) {
			for (const auto id : a_project.GetObjects()) {
				if (a_project.GetObjectType(id) != ProjectEx::ObjectType::kActor) continue;
				ImGui::PushID(static_cast<int>(id));
				ImGui::BeginDisabled(!a_project.GetBoundActor(id));
				if (ImGui::Selectable(label(id).c_str(), id == a_actor) && id != a_actor) {
					a_actor = id;
					changed = true;
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	std::optional<ProjectEx::SequenceInfo> GetActiveSequenceInfo(const ProjectEx& a_project)
	{
		for (const auto& sequence : a_project.GetSequences()) {
			if (sequence.id == a_project.GetActiveSequenceId()) return sequence;
		}
		return std::nullopt;
	}
	RE::NiPoint3 ToNiPoint(const ozz::math::Float3& a_point)
	{
		return { a_point.x, a_point.y, a_point.z };
	}

	void DrawDiamond(ImDrawList* a_drawList, const ImVec2& a_center, float a_radius, ImU32 a_fill, ImU32 a_border)
	{
		const ImVec2 top{ a_center.x, a_center.y - a_radius };
		const ImVec2 right{ a_center.x + a_radius, a_center.y };
		const ImVec2 bottom{ a_center.x, a_center.y + a_radius };
		const ImVec2 left{ a_center.x - a_radius, a_center.y };
		if (a_fill) a_drawList->AddQuadFilled(top, right, bottom, left, a_fill);
		if (a_border) a_drawList->AddQuad(top, right, bottom, left, a_border, 1.5f);
	}

	// a_distance in front of the camera, facing away from it.
	std::optional<ProjectEx::ScenePlacement> GetPlacementInFrontOfCamera(float a_distance)
	{
		const auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera || !camera->cameraRoot) return std::nullopt;
		const auto& world = camera->cameraRoot->world;
		const RE::NiPoint3 forward{ world.rotate[1][0], world.rotate[1][1], world.rotate[1][2] };
		return ProjectEx::ScenePlacement{
			{ world.translate.x + forward.x * a_distance, world.translate.y + forward.y * a_distance, world.translate.z + forward.z * a_distance },
			std::atan2(forward.x, forward.y)
		};
	}

	std::string GetDisplayPath(const std::filesystem::path& a_path)
	{
		const auto relative = a_path.lexically_relative(Util::String::GetGamePath());
		return PathToUtf8(!relative.empty() && *relative.begin() != ".." ? relative : a_path);
	}

	void DrawSceneCenter(Renderer* a_renderer, const ProjectEx::ScenePlacement& a_center)
	{
		using Animation::Studio::GetTranslation;
		using Animation::Studio::MakeTranslation;
		const auto center = ProjectEx::ToMatrix(a_center);
		const auto point = [&](float a_x, float a_y) {
			return ToNiPoint(GetTranslation(center * MakeTranslation({ a_x * kSceneCenterSize, a_y * kSceneCenterSize, 0.0f })));
		};
		a_renderer->DrawWorldLine(point(-0.5f, 0.0f), point(0.5f, 0.0f), kColorSceneCenter, 2.0f);
		a_renderer->DrawWorldLine(point(0.0f, -0.5f), point(0.0f, 1.0f), kColorSceneCenter, 2.0f);
		a_renderer->DrawWorldLine(point(-0.15f, 0.85f), point(0.0f, 1.0f), kColorSceneCenter, 2.0f);
		a_renderer->DrawWorldLine(point(0.15f, 0.85f), point(0.0f, 1.0f), kColorSceneCenter, 2.0f);
	}

	RE::Actor* GetConsoleSelectedActor()
	{
		const auto reference = Util::Game::GetConsoleSelectedRef();
		return reference ? reference->As<RE::Actor>() : nullptr;
	}

	std::vector<RE::Actor*> GetNearbyActors()
	{
		std::vector<std::pair<float, RE::Actor*>> found;
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (player) found.emplace_back(0.0f, player);
		if (const auto processLists = RE::ProcessLists::GetSingleton()) {
#if defined TARGET_GAME_F4
			const auto& highActorHandles = processLists->highActorHandles;
#elif defined TARGET_GAME_SF
			const auto& highActorHandles = RE::GetHighActorHandles(processLists);
#endif
			for (const auto& handle : highActorHandles) {
				const auto actor = handle.get();
				if (!actor || actor.get() == player || !Util::Game::Get3D(actor.get())) continue;
				found.emplace_back(player ? actor->GetPosition().GetDistance(player->GetPosition()) : 0.0f, actor.get());
			}
		}
		std::ranges::sort(found, {}, &std::pair<float, RE::Actor*>::first);
		std::vector<RE::Actor*> result;
		for (const auto& [distance, actor] : found | std::views::take(kMaxNearbyActors)) result.push_back(actor);
		return result;
	}
}
