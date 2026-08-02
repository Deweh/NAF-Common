#pragma once

// Bone hierarchy queries on ozz skeletons. Pure.
namespace Animation::Studio
{
	[[nodiscard]] bool IsDescendantOrSelf(ozz::span<const int16_t> a_parents, int32_t a_bone, int32_t a_ancestor);
	// The bones from a_start down to a_end, or empty if a_start isn't an ancestor of a_end.
	[[nodiscard]] std::vector<int32_t> GetBonePath(const ozz::animation::Skeleton& a_skeleton, int32_t a_start, int32_t a_end);
	// Each joint's children.
	[[nodiscard]] std::vector<std::vector<int32_t>> GetChildren(const ozz::animation::Skeleton& a_skeleton);
	// The rest pose in model space. Nullopt if the job fails.
	[[nodiscard]] std::optional<std::vector<ozz::math::Float4x4>> GetRestModel(const ozz::animation::Skeleton& a_skeleton);
}
