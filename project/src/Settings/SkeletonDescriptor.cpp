#include "SkeletonDescriptor.h"
#include "Util/String.h"
#include "Util/Ozz.h"
#include "Animation/Transform.h"
#include "Animation/Ozz.h"

namespace Settings
{
	namespace detail
	{
		int32_t GetJointIndexCI(const ozz::animation::Skeleton* skele, const std::string_view name)
		{
			const auto& idxs = skele->joint_names();
			for (auto iter = idxs.begin(); iter != idxs.end(); iter++) {
				if (Util::String::CaseInsensitiveCompare(*iter, name)) {
					return std::distance(idxs.begin(), iter);
				}
			}

			return -1;
		}
	}

	void SkeletonDescriptor::AddBone(const std::string_view name, const std::string_view parent, const ozz::math::Transform& restPose, int32_t havokIndex, bool controlledByDefault, bool controlledByGame)
	{
		if (Util::String::CaseInsensitiveCompare(name, "Camera")) {
			controlledByDefault = false;
		}

		BoneData* newData = nullptr;
		for (auto& b : bones) {
			if (Util::String::CaseInsensitiveCompare(b.name, name)) {
				newData = std::addressof(b);
				break;
			}
		}

		if (newData == nullptr) {
			newData = std::addressof(bones.emplace_back());
		}
		
		newData->name = name;
		newData->parent = parent;
		newData->restPose = restPose;
		newData->controlledByDefault = controlledByDefault;
		newData->controlledByGame = controlledByGame;
	}

	std::unique_ptr<Animation::OzzSkeleton> SkeletonDescriptor::BuildRuntime(const std::string_view name)
	{
		using namespace ozz::animation::offline;
		if (bones.empty())
			return nullptr;

		struct PointerJoint
		{
			RawSkeleton::Joint joint;
			std::vector<PointerJoint*> children;
		};

		std::vector<std::unique_ptr<PointerJoint>> flatJoints;
		flatJoints.reserve(bones.size());

		//Pass 1: Put all joints into flat vector.
		std::vector<std::string_view> maskedBones;
		std::vector<std::string_view> uncontrolledBones;
		for (auto& b : bones) {
			auto& j = flatJoints.emplace_back(std::make_unique<PointerJoint>());
			j->joint.transform = b.restPose;
			j->joint.name = b.name;

			if (!b.controlledByDefault) {
				maskedBones.push_back(b.name);
			}
			if (!b.controlledByGame) {
				uncontrolledBones.push_back(b.name);
			}
		}

		//Pass 2: Assign children to parents.
		for (size_t i = 0; i < bones.size(); i++) {
			const std::string& parentName = bones[i].parent;
			auto parentIdx = GetBoneIndex(parentName);

			if (!parentIdx.has_value())
				continue;

			flatJoints[parentIdx.value()]->children.push_back(flatJoints[i].get());
		}

		//Pass 3: Fill in real skeleton structure.
		std::function<void(RawSkeleton::Joint & j, PointerJoint * pj)> FillTree = [&FillTree](RawSkeleton::Joint& j, PointerJoint* pj) -> void {
			for (auto& c : pj->children) {
				auto& newJ = j.children.emplace_back(c->joint);
				FillTree(newJ, c);
			}
		};

		RawSkeleton raw;
		for (size_t i = 0; i < bones.size(); i++) {
			//We want to start with roots, nodes that have no parent.
			if (!GetBoneIndex(bones[i].parent).has_value()) {
				FillTree(raw.roots.emplace_back(flatJoints[i]->joint), flatJoints[i].get());
			}
		}

		ozz::animation::offline::SkeletonBuilder builder;
		auto baseData = builder(raw);

		if (!baseData) {
			return nullptr;
		}

		std::unique_ptr<Animation::OzzSkeleton> result = std::make_unique<Animation::OzzSkeleton>();
		result->data = std::move(baseData);
#ifdef TARGET_GAME_F4
		if (havokOrder.size() > 0) {
			result->havokToOzzIdxs.reserve(havokOrder.size());
			result->havokRestPose.reserve(havokOrder.size());
			for (const auto& havokBoneName : havokOrder) {
				int32_t idx = detail::GetJointIndexCI(result->data.get(), havokBoneName);
				ozz::math::Transform rest = ozz::math::Transform::identity();
				if (idx >= 0) {
					rest = ozz::animation::GetJointLocalRestPose(*result->data, idx);
				}
				result->havokToOzzIdxs.push_back(idx);
				result->havokRestPose.push_back(Util::Ozz::ToHkTransform(rest));
			}
		}
#endif

		int32_t numJoints = result->data->num_joints();
		result->defaultBoneMask.reserve(numJoints);
		result->controlledByGameMask.reserve(numJoints);
		result->defaultBoneMask.resize(numJoints, true);
		result->controlledByGameMask.resize(numJoints, true);

		for (auto& mb : maskedBones) {
			int32_t idx = detail::GetJointIndexCI(result->data.get(), mb);
			if (idx >= 0) {
				result->defaultBoneMask[idx] = false;
			}
		}

		for (auto& ucb : uncontrolledBones) {
			int32_t idx = detail::GetJointIndexCI(result->data.get(), ucb);
			if (idx >= 0) {
				result->controlledByGameMask[idx] = false;
			}
		}

		result->name = name;
		return result;
	}

	std::optional<size_t> SkeletonDescriptor::GetBoneIndex(const std::string& name) const
	{
		for (size_t i = 0; i < bones.size(); i++) {
			if (Util::String::CaseInsensitiveCompare(bones[i].name, name))
				return i;
		}
		return std::nullopt;
	}
}