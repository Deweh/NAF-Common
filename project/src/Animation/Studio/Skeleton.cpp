#include "Skeleton.h"

namespace Animation::Studio
{
	bool IsDescendantOrSelf(ozz::span<const int16_t> a_parents, int32_t a_bone, int32_t a_ancestor)
	{
		for (int32_t joint = a_bone; joint >= 0 && joint < static_cast<int32_t>(a_parents.size()); joint = a_parents[joint]) {
			if (joint == a_ancestor) return true;
		}
		return false;
	}

	std::vector<int32_t> GetBonePath(const ozz::animation::Skeleton& a_skeleton, int32_t a_start, int32_t a_end)
	{
		const int32_t jointCount = a_skeleton.num_joints();
		if (a_start < 0 || a_start >= jointCount || a_end < 0 || a_end >= jointCount) return {};
		const auto parents = a_skeleton.joint_parents();
		std::vector<int32_t> path;
		for (int32_t joint = a_end; joint >= 0 && joint < jointCount; joint = parents[joint]) {
			path.push_back(joint);
			if (joint == a_start) {
				std::ranges::reverse(path);
				return path;
			}
		}
		return {};
	}

	std::vector<std::vector<int32_t>> GetChildren(const ozz::animation::Skeleton& a_skeleton)
	{
		const int32_t jointCount = a_skeleton.num_joints();
		const auto parents = a_skeleton.joint_parents();
		std::vector<std::vector<int32_t>> children(jointCount);
		for (int32_t joint = 0; joint < jointCount; ++joint) {
			if (parents[joint] != ozz::animation::Skeleton::kNoParent) children[parents[joint]].push_back(joint);
		}
		return children;
	}

	std::optional<std::vector<ozz::math::Float4x4>> GetRestModel(const ozz::animation::Skeleton& a_skeleton)
	{
		std::vector<ozz::math::Float4x4> rest(a_skeleton.num_joints());
		ozz::animation::LocalToModelJob job;
		job.skeleton = &a_skeleton;
		job.input = a_skeleton.joint_rest_poses();
		job.output = ozz::make_span(rest);
		if (!job.Run()) return std::nullopt;
		return rest;
	}
}
