#pragma once

namespace Animation
{
	struct OzzSkeleton;
}

namespace Settings
{
	struct SkeletonDescriptor
	{
		struct BoneData
		{
			std::string name;
			std::string parent;
			bool controlledByDefault = false;
			bool controlledByGame = false;
			ozz::math::Transform restPose;
		};

		std::vector<BoneData> bones;
#ifdef TARGET_GAME_F4
		std::vector<std::string_view> havokOrder;
#endif

		void AddBone(const std::string_view name, const std::string_view parent, const ozz::math::Transform& restPose, int32_t havokIndex = -1, bool controlledByDefault = true, bool controlledByGame = true);
		std::unique_ptr<Animation::OzzSkeleton> BuildRuntime(const std::string_view name);
		std::optional<size_t> GetBoneIndex(const std::string& name) const;
	};
}