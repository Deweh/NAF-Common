#pragma once

namespace Animation
{
	class IAnimationFile;
	class IBasicAnimation;
	class AnimID;
	class OzzSkeleton;
}

namespace Settings
{
	class SkeletonDescriptor;

	struct ImplFunctions
	{
		void (*GetSkeletonBonesForActor)(SkeletonDescriptor&, RE::Actor*);
		std::unique_ptr<Animation::IBasicAnimation> (*LoadOtherAnimationFile)(const Animation::AnimID&, const Animation::OzzSkeleton*);
	};

	ImplFunctions& GetImplFunctions();
}