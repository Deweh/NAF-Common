#include "UI/Studio/StudioSession.h"

#include <nlohmann/json.hpp>

namespace UI::Studio
{
	using json = nlohmann::json;

	json StudioSession::ElementToJson(const ProjectEx::ElementRef& a_element) const
	{
		json result{ { "object", a_element.object } };
		if (a_element.element == ProjectEx::kSelf) return result;
		const auto skeleton = m_project->GetSkeleton(a_element.object);
		if (!skeleton || a_element.element >= skeleton->num_joints()) return nullptr;
		result["bone"] = skeleton->joint_names()[a_element.element];
		return result;
	}

	std::optional<ProjectEx::ElementRef> StudioSession::ElementFromJson(const json& a_item) const
	{
		ProjectEx::ElementRef result{ a_item.at("object").get<ProjectEx::ObjectId>() };
		if (const auto bone = a_item.find("bone"); bone != a_item.end()) {
			const auto skeleton = m_project->GetSkeleton(result.object);
			if (!skeleton) return std::nullopt;
			const auto boneName = bone->get<std::string>();
			const auto names = skeleton->joint_names();
			for (int32_t i = 0; i < skeleton->num_joints(); ++i) {
				if (boneName == names[i]) result.element = static_cast<uint16_t>(i);
			}
			if (result.element == ProjectEx::kSelf) return std::nullopt;
		}
		return ElementExists(result) ? std::optional{ result } : std::nullopt;
	}

	std::string StudioSession::WriteEditorJson() const
	{
		json editor{
			{ "autoKey", autoKey },
			{ "defaultInterpolation", static_cast<int>(defaultInterpolation) },
			{ "gizmoMode", static_cast<int>(gizmoMode) },
			{ "gizmoSpace", static_cast<int>(gizmoSpace) }
		};
		viewport.WriteSettings(editor);
		timeline.WriteSettings(editor);
		exportDialog.WriteSettings(editor);

		auto& boneDisplayJson = editor["boneDisplay"] = json::array();
		for (const auto& [bone, display] : boneDisplay) {
			auto item = ElementToJson(bone);
			if (item.is_null()) continue;
			item["visible"] = display.visible;
			item["scale"] = display.scale;
			boneDisplayJson.push_back(std::move(item));
		}
		return editor.dump();
	}

	void StudioSession::ReadEditorJson(std::string_view a_json, std::vector<std::string>& a_warnings)
	{
		if (a_json.empty()) return;
		try {
			const auto editor = json::parse(a_json);
			const auto readEnum = [&](const char* a_key, auto a_current, int a_max) {
				return static_cast<decltype(a_current)>(std::clamp(editor.value(a_key, static_cast<int>(a_current)), 0, a_max));
			};

			autoKey = editor.value("autoKey", autoKey);
			defaultInterpolation = readEnum("defaultInterpolation", defaultInterpolation, static_cast<int>(ProjectEx::Interpolation::kStep));
			gizmoMode = readEnum("gizmoMode", gizmoMode, static_cast<int>(GizmoMode::kScale));
			gizmoSpace = readEnum("gizmoSpace", gizmoSpace, static_cast<int>(GizmoSpace::kWorld));
			viewport.ReadSettings(editor);
			timeline.ReadSettings(editor);
			exportDialog.ReadSettings(editor);
			if (const auto boneDisplayJson = editor.find("boneDisplay"); boneDisplayJson != editor.end() && boneDisplayJson->is_array()) {
				for (const auto& item : *boneDisplayJson) {
					const auto bone = ElementFromJson(item);
					if (!bone || !IsBone(*bone)) continue;
					const float scale = item.value("scale", 1.0f);
					const BoneDisplay display{ item.value("visible", true), std::isfinite(scale) ? std::clamp(scale, 0.1f, 4.0f) : 1.0f };
					if (!display.visible || display.scale != 1.0f) boneDisplay[*bone] = display;
				}
			}
		} catch (const std::exception&) {
			a_warnings.push_back("Some editor settings (timeline view, bone display and similar) could not be restored.");
		}
	}
}
