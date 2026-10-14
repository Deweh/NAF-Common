#pragma once
#include "SkeletonDescriptor.h"
#include "Animation/Ozz.h"

namespace Settings
{
	void Init();
	// Skeletons are identified by the EditorID of the race they're built from, and built the first time they're needed.
	// Returns the default skeleton if the race has none.
	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(const std::string& a_raceEditorId);
	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(RE::Actor* a_actor);
	RE::BSFixedString GetSkeletonIdentifier(RE::Actor* a_actor);
	// Whether content saved for one skeleton ID fits another: the same ID, or skeletons that animate the same game bones
	// (see OzzSkeleton::gameSkeletonHash). On Fallout 4, a saved ID can also be the race's behavior graph project, which
	// identified skeletons before races did.
	bool AreSkeletonsCompatible(const std::string& a_savedId, const std::string& a_currentId);
	bool IsDefaultSkeleton(std::shared_ptr<const Animation::OzzSkeleton> a_skeleton);
	const std::map<std::string, size_t>& GetFaceMorphIndexMap();
	const std::vector<std::string>& GetFaceMorphs();
}