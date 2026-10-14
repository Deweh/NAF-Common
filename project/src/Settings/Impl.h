#pragma once

namespace Animation
{
	class IAnimationFile;
	class IBasicAnimation;
	struct AnimID;
	struct OzzSkeleton;
}

namespace Settings
{
	class SkeletonDescriptor;

	struct ImplFunctions
	{
		// Fills in the skeleton of a race: the game's animation skeleton (bones added as game-controlled), then the race's
		// skeleton model nodes it lacks (added as not controlled). Returns false if the race has no usable skeleton. The
		// actor is one of the race, or null; Starfield doesn't need it, Fallout 4 reads its Havok skeleton from it.
		bool (*BuildSkeleton)(SkeletonDescriptor&, RE::TESRace*, RE::Actor*);
		std::unique_ptr<Animation::IBasicAnimation> (*LoadOtherAnimationFile)(const Animation::AnimID&, const Animation::OzzSkeleton*);
		RE::TESObjectREFR* (*GetConsoleSelectedRef)() = nullptr;
	};

	ImplFunctions& GetImplFunctions();
}