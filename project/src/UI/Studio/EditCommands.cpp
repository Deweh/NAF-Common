#include "UI/Studio/StudioSession.h"
#include "Util/Game.h"

#include "UI/Studio/StudioCamera.h"
#include "Animation/Studio/GameBridge.h"
#include "Animation/Studio/StudioMath.h"

namespace UI::Studio
{
	using Animation::Studio::GetTranslation;
	using Animation::Studio::MakeTranslation;

	void StudioSession::InsertKeys()
	{
		std::vector<ProjectEx::PropertyRef> properties;
		for (const auto& element : selection) {
			const auto elementProperties = GetElementProperties(element);
			properties.insert(properties.end(), elementProperties.begin(), elementProperties.end());
		}
		if (m_project->KeyCurrentValue(properties, defaultInterpolation)) SetStatus(std::format("Keyed frame {}.", m_project->GetFrame()));
	}

	void StudioSession::CopySelectedKeys()
	{
		m_keyClipboard.clear();
		if (selectedKeys.empty()) return;
		const int32_t earliest = (std::ranges::min)(selectedKeys | std::views::transform(&ProjectEx::KeyRef::frame));
		for (const auto& ref : selectedKeys) {
			auto key = m_project->GetKey(ref);
			if (!key) continue;
			std::visit([&](auto& a_key) { a_key.frame -= earliest; }, *key);
			m_keyClipboard.push_back({ ref.property, std::move(*key) });
		}
		SetStatus(std::format("Copied {} keyframe(s).", m_keyClipboard.size()));
	}

	void StudioSession::PasteKeys()
	{
		auto keys = m_keyClipboard;
		const int32_t frame = m_project->GetFrame();
		std::set<ProjectEx::KeyRef> pasted;
		for (auto& data : keys) {
			std::visit([&](auto& a_key) {
				a_key.frame += frame;
				pasted.insert({ data.property, a_key.frame });
			}, data.key);
		}
		if (!m_project->SetKeys(keys)) return;
		selectedKeys = std::move(pasted);
		SetStatus(std::format("Pasted {} keyframe(s) at frame {}.", keys.size(), frame));
	}

	void StudioSession::DeleteSelection()
	{
		if (timeline.IsFocused()) {
			const std::vector<ProjectEx::KeyRef> keys{ selectedKeys.begin(), selectedKeys.end() };
			if (m_project->DeleteKeys(keys)) SetStatus(std::format("Deleted {} keyframe(s).", keys.size()));
			selectedKeys.clear();
			return;
		}
		// Bones are part of their actor's skeleton and can't be removed.
		std::vector<ProjectEx::ObjectId> objects;
		for (const auto& element : selection) {
			if (!IsBone(element)) objects.push_back(element.object);
		}
		m_project->BeginEdit("Remove");
		for (const auto id : objects) m_project->RemoveObject(id);
		m_project->EndEdit();
		SetStatus(std::format("Removed {} object(s).", objects.size()));
	}

	void StudioSession::SelectAll()
	{
		if (timeline.IsFocused()) {
			selectedKeys.clear();
			for (const auto& lane : *timeline.BuildLanes()) {
				for (const auto& refs : lane.keys | std::views::values) selectedKeys.insert(refs.begin(), refs.end());
			}
			return;
		}
		// Every bone of the active element's actor, keeping the active element active.
		const auto active = GetActiveElement();
		if (!active || m_project->GetObjectType(active->object) != ProjectEx::ObjectType::kActor) return;
		const auto skeleton = m_project->GetSkeleton(active->object);
		for (int32_t bone = 0; skeleton && bone < skeleton->num_joints(); ++bone) {
			const ProjectEx::ElementRef element{ active->object, static_cast<uint16_t>(bone) };
			if (!IsSelected(element)) selection.insert(selection.begin(), element);
		}
	}

	void StudioSession::StepToKey(int32_t a_direction)
	{
		std::set<int32_t> frames;
		for (const auto& lane : *timeline.BuildLanes()) {
			for (const auto frame : lane.keys | std::views::keys) frames.insert(frame);
		}
		if (frames.empty()) {
			for (const auto& property : m_project->GetTrackedProperties()) {
				for (const auto frame : m_project->GetKeyFrames(property)) frames.insert(frame);
			}
		}
		const int32_t current = m_project->GetFrame();
		if (a_direction > 0) {
			if (const auto next = frames.upper_bound(current); next != frames.end()) m_project->SetFrame(*next);
		} else if (const auto next = frames.lower_bound(current); next != frames.begin()) {
			m_project->SetFrame(*std::prev(next));
		}
	}

	bool StudioSession::CanCreateIKChainFromSelection() const
	{
		size_t bones = 0;
		std::optional<ProjectEx::ObjectId> actor;
		for (const auto& element : selection) {
			if (!IsBone(element)) continue;
			if (actor && *actor != element.object) return false;
			actor = element.object;
			++bones;
		}
		return bones == 3;
	}

	void StudioSession::CreateIKChainFromSelection()
	{
		std::vector<ProjectEx::ElementRef> bones;
		for (const auto& element : selection) {
			if (IsBone(element)) bones.push_back(element);
		}
		if (bones.size() != 3) return;
		std::ranges::sort(bones, {}, [&](const auto& a_bone) { return GetHierarchyDepth(a_bone); });
		const auto result = m_project->AddIKChain(bones[0].object, bones[0].element, bones[1].element, bones[2].element);
		if (!result) {
			SetStatus(result.error());
			return;
		}
		const auto chain = m_project->GetIKChain(*result);
		if (chain) {
			Select({ chain->effector, ProjectEx::kSelf }, SelectMode::kReplace);
			reveal = selection.back();
		}
		SetStatus(chain &&chain->settings.shoulderBone >= 0 ? "IK chain created, with an automatic shoulder. Move its effector to pose the limb." :
		                                                     "IK chain created. Move its effector to pose the limb.");
	}

	bool StudioSession::CanCreateFingerControlsFromSelection() const
	{
		const auto active = GetActiveElement();
		return active && IsBone(*active) && GetSelectedBones(false).size() == 1;
	}

	void StudioSession::CreateFingerControlsFromSelection()
	{
		if (!CanCreateFingerControlsFromSelection()) return;
		const auto bone = *GetActiveElement();
		const auto result = m_project->AddHand(bone.object, bone.element);
		if (!result) {
			SetStatus(result.error());
			return;
		}
		Select({ *result, ProjectEx::kSelf }, SelectMode::kReplace);
		reveal = selection.back();
		const auto hand = m_project->GetHand(*result);
		SetStatus(std::format("Finger controls created for {} finger(s).", hand ? hand->settings.fingers.size() : 0));
	}

	std::optional<std::tuple<ProjectEx::ElementRef, ProjectEx::ElementRef, size_t>> StudioSession::GetSplineIKSelection() const
	{
		auto bones = GetSelectedBones(false);
		if (bones.size() != 2 || bones[0].object != bones[1].object) return std::nullopt;
		const auto skeleton = m_project->GetSkeleton(bones[0].object);
		if (!skeleton) return std::nullopt;
		std::ranges::sort(bones, {}, [&](const auto& a_bone) { return GetHierarchyDepth(a_bone); });
		const auto parents = skeleton->joint_parents();
		size_t length = 1;
		for (int32_t joint = bones[1].element; joint != bones[0].element; joint = parents[joint], ++length) {
			if (joint == ozz::animation::Skeleton::kNoParent) return std::nullopt;
		}
		if (length < 3) return std::nullopt;
		return std::tuple{ bones[0], bones[1], length };
	}

	void StudioSession::CreateSplineIKFromSelection(uint32_t a_controlCount)
	{
		const auto bones = GetSplineIKSelection();
		if (!bones) return;
		const auto& [start, end, length] = *bones;
		const auto result = m_project->AddSplineIK(start.object, start.element, end.element, a_controlCount);
		if (!result) {
			SetStatus(result.error());
			return;
		}
		Select({ *result, ProjectEx::kSelf }, SelectMode::kReplace);
		reveal = selection.back();
		SetStatus("Spline IK created. Move its controls to shape the chain.");
	}

	void StudioSession::ApplyHandPose(ProjectEx::ObjectId a_hand, const std::function<std::pair<float, float>(size_t a_finger)>& a_values, const char* a_description)
	{
		const auto hand = m_project->GetHand(a_hand);
		if (!hand) return;
		using PropertyId = ProjectEx::PropertyId;
		std::vector<ProjectEx::PropertyRef> properties{ { a_hand, ProjectEx::kSelf, PropertyId::kCurl }, { a_hand, ProjectEx::kSelf, PropertyId::kSpread } };
		m_project->BeginEdit(a_description);
		m_project->SetValue(properties[0], 0.0f);
		m_project->SetValue(properties[1], 0.0f);
		for (size_t finger = 0; finger < hand->settings.fingers.size(); ++finger) {
			const auto [curl, spread] = a_values(finger);
			const ProjectEx::PropertyRef curlRef{ a_hand, static_cast<uint16_t>(finger), PropertyId::kCurl };
			const ProjectEx::PropertyRef spreadRef{ a_hand, static_cast<uint16_t>(finger), PropertyId::kSpread };
			m_project->SetValue(curlRef, curl);
			m_project->SetValue(spreadRef, spread);
			properties.push_back(curlRef);
			properties.push_back(spreadRef);
		}
		AutoKey(properties);
		m_project->EndEdit();
	}

	void StudioSession::AddActor(RE::Actor* a_actor)
	{
		const auto result = m_project->AddActor(a_actor);
		if (!result) {
			SetStatus(result.error());
			return;
		}
		Select({ *result, ProjectEx::kSelf }, SelectMode::kReplace);
		reveal = selection.back();

		// Hide the skeleton's bones this actor doesn't have, so only its own bones show.
		std::vector<ProjectEx::ElementRef> missing;
		if (const auto skeleton = m_project->GetSkeleton(*result)) {
			const auto present = Animation::Studio::GetJointsPresentOnActor(a_actor, *skeleton);
			for (size_t i = 0; i < present.size(); ++i) {
				if (!present[i])
					missing.push_back({ *result, static_cast<uint16_t>(i) });
			}
		}
		SetBoneDisplay(missing, false, std::nullopt);

		const auto name = m_project->GetObjectName(*result);
		SetStatus(missing.empty() ? std::format("Added {}.", name) : std::format("Added {}; hid {} bones it doesn't have.", name, missing.size()));
	}

	void StudioSession::BindActor(ProjectEx::ObjectId a_slot, RE::Actor* a_actor)
	{
		const auto result = m_project->BindActor(a_slot, a_actor);
		SetStatus(result ? std::format("Bound {}.", m_project->GetObjectName(a_slot)) : result.error());
	}

	void StudioSession::AddLocator(ProjectEx::ParentRef a_parent, std::optional<ozz::math::Float4x4> a_world)
	{
		const auto result = m_project->AddLocator(a_parent, a_world);
		if (!result) {
			SetStatus(result.error());
			return;
		}
		Select({ *result, ProjectEx::kSelf }, SelectMode::kReplace);
		reveal = selection.back();
	}

	void StudioSession::ResetTransforms(std::span<const ProjectEx::ElementRef> a_elements)
	{
		m_project->BeginEdit("Reset transform");
		for (const auto& element : a_elements) {
			for (const auto& property : GetElementProperties(element)) m_project->ResetValue(property);
		}
		m_project->EndEdit();
	}

	void StudioSession::SetLocatorDefaults(std::span<const ProjectEx::ElementRef> a_elements)
	{
		// SetLocatorDefaultsToCurrent ignores anything that isn't a locator, so a mixed selection is fine.
		m_project->BeginEdit("Set locator default");
		for (const auto& element : a_elements) m_project->SetLocatorDefaultsToCurrent(element.object);
		m_project->EndEdit();
	}

	void StudioSession::AutoKey(std::span<const ProjectEx::PropertyRef> a_properties)
	{
		if (autoKey) m_project->KeyCurrentValue(a_properties, defaultInterpolation);
	}

	std::optional<ozz::math::Float4x4> StudioSession::GetSpawnTransform()
	{
		// At the active element, or in front of the camera.
		if (const auto active = GetActiveElement()) {
			if (const auto world = m_project->GetWorldTransform(*active)) return MakeTranslation(GetTranslation(*world));
		}
		const auto playerCamera = RE::PlayerCamera::GetSingleton();
		if (!playerCamera || !playerCamera->cameraRoot) return std::nullopt;
		const auto& world = playerCamera->cameraRoot->world;
		const RE::NiPoint3 forward{ world.rotate[1][0], world.rotate[1][1], world.rotate[1][2] };
		return MakeTranslation({ world.translate.x + forward.x * kFocusDistance, world.translate.y + forward.y * kFocusDistance, world.translate.z + forward.z * kFocusDistance });
	}

	void StudioSession::ApplyFieldValue(const ProjectEx::PropertyRef& a_property, const ProjectEx::Value& a_value)
	{
		// Folds into the field's interaction while one is open; otherwise this is its own undo step.
		m_project->BeginEdit(std::format("Edit {}", GetPropertyName(a_property.property)));
		m_project->SetValue(a_property, a_value);
		AutoKey(std::span{ &a_property, 1 });
		m_project->EndEdit();
	}

	void StudioSession::DrawActorSourceMenu(const std::function<void(RE::Actor*)>& a_onPick, Popup a_formIdPopup, ProjectEx::ObjectId a_target)
	{
		// Binding a slot: offer the actor it was last bound to, if that form still exists.
		if (a_target != ProjectEx::kNoObject) {
			const auto hint = m_project->GetBindingHint(a_target);
			const auto previous = hint != 0 ? Util::Game::LookupForm<RE::Actor>(hint) : nullptr;
			if (previous && previous != m_project->GetBoundActor(a_target)) {
				if (ImGui::MenuItem(std::format("Previous: {}", GetActorLabel(previous)).c_str())) a_onPick(previous);
			}
		}
		const auto consoleActor = GetConsoleSelectedActor();
		if (ImGui::MenuItem("Console Selection", nullptr, false, consoleActor != nullptr)) a_onPick(consoleActor);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !consoleActor) ImGui::SetTooltip("Select an actor in the console first.");
		if (ImGui::BeginMenu("Nearby")) {
			const auto actors = GetNearbyActors();
			if (actors.empty()) ImGui::TextDisabled("No loaded actors.");
			for (const auto actor : actors) {
				if (ImGui::MenuItem(GetActorLabel(actor).c_str())) a_onPick(actor);
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("By Form ID...")) RequestPopup({ a_formIdPopup, a_target });
	}
}
