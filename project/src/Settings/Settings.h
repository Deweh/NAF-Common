#pragma once
#include "SkeletonDescriptor.h"
#include "Animation/Ozz.h"

namespace Settings
{
	void Init();
	const std::filesystem::path& GetSkeletonsPath();
	std::unordered_map<std::string, std::shared_ptr<Animation::OzzSkeleton>>& GetSkeletonMap();
	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(const std::string& a_behPath);
	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(RE::Actor* a_actor);
	RE::BSFixedString GetSkeletonIdentifier(RE::Actor* a_actor);
	bool IsDefaultSkeleton(std::shared_ptr<const Animation::OzzSkeleton> a_skeleton);
	const std::map<std::string, size_t>& GetFaceMorphIndexMap();
	const std::vector<std::string>& GetFaceMorphs();
}