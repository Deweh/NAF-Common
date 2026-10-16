#include "UI/Studio/StudioSession.h"

namespace UI::Studio
{
	SelectMode StudioSession::GetClickSelectMode()
	{
		const auto& io = ImGui::GetIO();
		return io.KeyCtrl ? SelectMode::kToggle : io.KeyShift ? SelectMode::kAdd : SelectMode::kReplace;
	}

	void StudioSession::Select(ProjectEx::ElementRef a_element, SelectMode a_mode)
	{
		const auto existing = std::ranges::find(selection, a_element);
		switch (a_mode) {
		case SelectMode::kReplace:
			selection = { a_element };
			break;
		case SelectMode::kAdd:
			// Re-adding moves the element to the back, making it active.
			if (existing != selection.end()) selection.erase(existing);
			selection.push_back(a_element);
			break;
		case SelectMode::kToggle:
			if (existing != selection.end()) selection.erase(existing);
			else selection.push_back(a_element);
			break;
		}
	}

	bool StudioSession::IsSelected(ProjectEx::ElementRef a_element) const
	{
		return std::ranges::contains(selection, a_element);
	}

	std::optional<ProjectEx::ElementRef> StudioSession::GetActiveElement() const
	{
		return selection.empty() ? std::nullopt : std::optional{ selection.back() };
	}

	bool StudioSession::IsBone(ProjectEx::ElementRef a_element) const
	{
		return a_element.element != ProjectEx::kSelf && m_project->GetObjectType(a_element.object) == ProjectEx::ObjectType::kActor;
	}

	bool StudioSession::ElementExists(ProjectEx::ElementRef a_element) const
	{
		const auto type = m_project->GetObjectType(a_element.object);
		if (!type) return false;
		if (a_element.element == ProjectEx::kSelf) return true;
		const auto skeleton = *type == ProjectEx::ObjectType::kActor ? m_project->GetSkeleton(a_element.object) : nullptr;
		return skeleton && a_element.element < skeleton->num_joints();
	}

	std::string StudioSession::GetElementLabel(ProjectEx::ElementRef a_element) const
	{
		if (IsBone(a_element)) {
			const auto skeleton = m_project->GetSkeleton(a_element.object);
			return skeleton ? skeleton->joint_names()[a_element.element] : "Bone";
		}
		std::string label{ m_project->GetObjectName(a_element.object) };
		if (m_project->GetObjectType(a_element.object) == ProjectEx::ObjectType::kActor && !m_project->GetBoundActor(a_element.object)) label += " (unbound)";
		return label;
	}

	std::vector<ProjectEx::PropertyRef> StudioSession::GetElementProperties(ProjectEx::ElementRef a_element) const
	{
		using PropertyId = ProjectEx::PropertyId;
		const auto type = m_project->GetObjectType(a_element.object);
		if (!type) return {};
		const auto transform = [](ProjectEx::ObjectId a_object, uint16_t a_element) {
			return std::vector<ProjectEx::PropertyRef>{
				{ a_object, a_element, PropertyId::kTranslation },
				{ a_object, a_element, PropertyId::kRotation },
				{ a_object, a_element, PropertyId::kScale }
			};
		};
		switch (*type) {
		case ProjectEx::ObjectType::kActor:
			{
				if (a_element.element != ProjectEx::kSelf) return transform(a_element.object, a_element.element);
				// The whole actor: every bone.
				std::vector<ProjectEx::PropertyRef> result;
				const auto skeleton = m_project->GetSkeleton(a_element.object);
				for (int32_t bone = 0; skeleton && bone < skeleton->num_joints(); ++bone) {
					const auto boneProperties = transform(a_element.object, static_cast<uint16_t>(bone));
					result.insert(result.end(), boneProperties.begin(), boneProperties.end());
				}
				return result;
			}
		case ProjectEx::ObjectType::kLocator:
			return transform(a_element.object, ProjectEx::kSelf);
		case ProjectEx::ObjectType::kIKChain:
			return {
				{ a_element.object, ProjectEx::kSelf, PropertyId::kEnabled },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kWeight },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kSoften },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kTwist },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kShoulderWeight },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kTwistDistribution }
			};
		case ProjectEx::ObjectType::kHand:
			{
				std::vector<ProjectEx::PropertyRef> result{
					{ a_element.object, ProjectEx::kSelf, PropertyId::kEnabled },
					{ a_element.object, ProjectEx::kSelf, PropertyId::kWeight },
					{ a_element.object, ProjectEx::kSelf, PropertyId::kCurl },
					{ a_element.object, ProjectEx::kSelf, PropertyId::kSpread }
				};
				const auto hand = m_project->GetHand(a_element.object);
				for (size_t finger = 0; hand && finger < hand->settings.fingers.size(); ++finger) {
					result.push_back({ a_element.object, static_cast<uint16_t>(finger), PropertyId::kCurl });
					result.push_back({ a_element.object, static_cast<uint16_t>(finger), PropertyId::kSpread });
				}
				return result;
			}
		case ProjectEx::ObjectType::kCorrectiveDriver:
			{
				std::vector<ProjectEx::PropertyRef> result{
					{ a_element.object, ProjectEx::kSelf, PropertyId::kEnabled },
					{ a_element.object, ProjectEx::kSelf, PropertyId::kWeight }
				};
				const auto drivers = m_project->GetCorrectiveDriver(a_element.object);
				for (size_t driver = 0; drivers && driver < drivers->settings.drivers.size(); ++driver) {
					result.push_back({ a_element.object, static_cast<uint16_t>(driver), PropertyId::kWeight });
				}
				return result;
			}
		case ProjectEx::ObjectType::kSplineIK:
			return {
				{ a_element.object, ProjectEx::kSelf, PropertyId::kEnabled },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kWeight },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kTwist },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kStretch },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kTangentLength },
				{ a_element.object, ProjectEx::kSelf, PropertyId::kOrient }
			};
		}
		return {};
	}

	int32_t StudioSession::GetHierarchyDepth(ProjectEx::ElementRef a_element) const
	{
		// An actor precedes its bones, which hang from its root: a root bone edited before its actor's placement
		// would be moved twice. Bones always precede locators, since a locator can hang from a bone but never the reverse.
		constexpr int32_t kLocatorDepthBase = 1 << 16;
		if (a_element.element == ProjectEx::kSelf && m_project->GetObjectType(a_element.object) == ProjectEx::ObjectType::kActor) return -1;
		if (IsBone(a_element)) {
			const auto skeleton = m_project->GetSkeleton(a_element.object);
			const auto parents = skeleton->joint_parents();
			int32_t depth = 0;
			for (int32_t joint = parents[a_element.element]; joint != ozz::animation::Skeleton::kNoParent; joint = parents[joint]) ++depth;
			return depth;
		}
		if (m_project->GetObjectType(a_element.object) != ProjectEx::ObjectType::kLocator) return 0;
		int32_t depth = kLocatorDepthBase;
		for (auto parent = m_project->GetLocatorParent(a_element.object); parent && parent->object != ProjectEx::kNoObject; parent = m_project->GetLocatorParent(parent->object)) ++depth;
		return depth;
	}

	ProjectEx::ObjectId StudioSession::GetDefaultActor() const
	{
		const auto owningActor = [&](ProjectEx::ObjectId a_id) -> ProjectEx::ObjectId {
			const auto type = m_project->GetObjectType(a_id);
			if (!type) return ProjectEx::kNoObject;
			switch (*type) {
			case ProjectEx::ObjectType::kActor:
				return a_id;
			case ProjectEx::ObjectType::kIKChain:
				return m_project->GetIKChain(a_id)->actor;
			case ProjectEx::ObjectType::kHand:
				return m_project->GetHand(a_id)->actor;
			case ProjectEx::ObjectType::kSplineIK:
				return m_project->GetSplineIK(a_id)->actor;
			case ProjectEx::ObjectType::kCorrectiveDriver:
				return m_project->GetCorrectiveDriver(a_id)->actor;
			default:
				return ProjectEx::kNoObject;
			}
		};
		if (const auto active = GetActiveElement()) {
			if (const auto actor = owningActor(active->object); m_project->GetBoundActor(actor)) return actor;
		}
		for (const auto id : m_project->GetObjects()) {
			if (m_project->GetBoundActor(id)) return id;
		}
		return ProjectEx::kNoObject;
	}

	std::vector<ProjectEx::ElementRef> StudioSession::GetSelectedBones(bool a_includeDescendants) const
	{
		std::vector<ProjectEx::ElementRef> bones;
		for (const auto& element : selection) {
			if (IsBone(element)) bones.push_back(element);
		}
		if (!a_includeDescendants) return bones;

		std::set<ProjectEx::ElementRef> result{ bones.begin(), bones.end() };
		for (const auto& bone : bones) {
			const auto parents = m_project->GetSkeleton(bone.object)->joint_parents();
			for (int32_t joint = 0; joint < static_cast<int32_t>(parents.size()); ++joint) {
				for (int32_t ancestor = parents[joint]; ancestor != ozz::animation::Skeleton::kNoParent; ancestor = parents[ancestor]) {
					if (ancestor != bone.element) continue;
					result.insert({ bone.object, static_cast<uint16_t>(joint) });
					break;
				}
			}
		}
		return { result.begin(), result.end() };
	}

	// ---- Bone display ----

	BoneDisplay StudioSession::GetBoneDisplay(ProjectEx::ElementRef a_bone) const
	{
		const auto display = boneDisplay.find(a_bone);
		return display != boneDisplay.end() ? display->second : BoneDisplay{};
	}

	void StudioSession::SetBoneDisplay(std::span<const ProjectEx::ElementRef> a_bones, std::optional<bool> a_visible, std::optional<float> a_scale)
	{
		for (const auto& bone : a_bones) {
			auto display = GetBoneDisplay(bone);
			if (a_visible) display.visible = *a_visible;
			if (a_scale) display.scale = *a_scale;
			if (display.visible && display.scale == 1.0f) boneDisplay.erase(bone);
			else boneDisplay[bone] = display;
		}
	}

	void StudioSession::ResetBoneDisplay(ProjectEx::ObjectId a_actor)
	{
		std::erase_if(boneDisplay, [&](const auto& a_entry) { return a_entry.first.object == a_actor; });
	}
}
