#include "Settings.h"
#include "Util/String.h"
#include "Animation/Transform.h"
#include "Impl.h"

namespace Settings
{
	std::mutex lock;

	std::shared_ptr<Animation::OzzSkeleton>& GetDefaultSkeleton()
	{
		static std::shared_ptr<Animation::OzzSkeleton> defaultSkeleton;
		return defaultSkeleton;
	}

	void InitDefaultSkeleton()
	{
		SkeletonDescriptor skele;
		skele.AddBone("Root", "", ozz::math::Transform::identity());
		skele.AddBone("COM", "Root", ozz::math::Transform::identity());
		GetDefaultSkeleton() = skele.BuildRuntime("DEFAULT");
	}

	void Init()
	{
		InitDefaultSkeleton();
	}

	namespace detail
	{
		// Skeletons are identified by the EditorID of the race they're built from.
		using SkeletonMap = std::map<std::string, std::shared_ptr<Animation::OzzSkeleton>, Util::String::CaseInsensitiveLess>;

		SkeletonMap& GetSkeletonMap()
		{
			static SkeletonMap skeletons;
			return skeletons;
		}

		// Races whose skeleton couldn't be built without an actor, so lookups by ID don't retry every time.
		std::set<std::string, Util::String::CaseInsensitiveLess>& GetUnbuildableRaces()
		{
			static std::set<std::string, Util::String::CaseInsensitiveLess> races;
			return races;
		}

		// The race whose animation skeleton an actor uses.
		RE::TESRace* GetSkeletonRace(RE::Actor* a_actor)
		{
#if defined TARGET_GAME_F4
			return a_actor->race;
#elif defined TARGET_GAME_SF
			return RE::GetAnimationRace(a_actor);
#endif
		}

		RE::TESRace* LookupRace(const std::string& a_editorId)
		{
			if (a_editorId.empty())
				return nullptr;
#if defined TARGET_GAME_F4
			return RE::TESForm::GetFormByEditorID<RE::TESRace>(RE::BSFixedString(a_editorId));
#elif defined TARGET_GAME_SF
			return RE::TESForm::LookupByEditorID<RE::TESRace>(RE::BSFixedString(a_editorId));
#endif
		}

		std::string GetRaceId(RE::TESRace* a_race)
		{
			const char* id = a_race ? a_race->formEditorID.c_str() : nullptr;
			return id ? id : "";
		}

		// Returns the race's skeleton, building it if it hasn't been yet. Null if it can't be built.
		std::shared_ptr<Animation::OzzSkeleton> FindOrBuildSkeleton(RE::TESRace* a_race, RE::Actor* a_actor)
		{
			const std::string id = GetRaceId(a_race);
			if (id.empty())
				return nullptr;

			std::unique_lock l{ lock };
			auto& skeletons = GetSkeletonMap();
			if (auto iter = skeletons.find(id); iter != skeletons.end())
				return iter->second;
			if (!a_actor && GetUnbuildableRaces().contains(id))
				return nullptr;

			SkeletonDescriptor skele;
			std::shared_ptr<Animation::OzzSkeleton> result;
			if (GetImplFunctions().BuildSkeleton(skele, a_race, a_actor))
				result = skele.BuildRuntime(id);

			if (!result) {
				if (!a_actor)
					GetUnbuildableRaces().insert(id);
				return nullptr;
			}

			skeletons.emplace(id, result);
			return result;
		}
	}

	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(const std::string& a_id)
	{
		if (auto result = detail::FindOrBuildSkeleton(detail::LookupRace(a_id), nullptr))
			return result;
		return GetDefaultSkeleton();
	}

	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(RE::Actor* a_actor)
	{
		if (!a_actor)
			return GetDefaultSkeleton();

		if (auto result = detail::FindOrBuildSkeleton(detail::GetSkeletonRace(a_actor), a_actor))
			return result;
		return GetDefaultSkeleton();
	}

	RE::BSFixedString GetSkeletonIdentifier(RE::Actor* a_actor)
	{
		if (!a_actor)
			return {};

		const auto race = detail::GetSkeletonRace(a_actor);
		detail::FindOrBuildSkeleton(race, a_actor);
		return RE::BSFixedString(detail::GetRaceId(race));
	}

	bool AreSkeletonsCompatible(const std::string& a_savedId, const std::string& a_currentId)
	{
		if (Util::String::CaseInsensitiveCompare(a_savedId, a_currentId))
			return true;

#if defined TARGET_GAME_F4
		// Before skeletons were identified by race, Fallout 4 ones were identified by the race's behavior graph project.
		if (const auto race = detail::LookupRace(a_currentId); race && Util::String::CaseInsensitiveCompare(a_savedId, race->behaviorGraphProjectName[0].c_str()))
			return true;
#endif

		const auto saved = GetSkeleton(a_savedId);
		const auto current = GetSkeleton(a_currentId);
		if (IsDefaultSkeleton(saved) || IsDefaultSkeleton(current))
			return false;
		return saved->gameSkeletonHash.has_value() && saved->gameSkeletonHash == current->gameSkeletonHash;
	}

	bool IsDefaultSkeleton(std::shared_ptr<const Animation::OzzSkeleton> a_skeleton)
	{
		return a_skeleton.get() == GetDefaultSkeleton().get();
	}

#ifdef TARGET_GAME_SF
	struct PerformanceMorph
	{
		RE::BSFixedString name;
		bool someFlag;
		uint8_t pad09;
		uint16_t pad0A;
		uint32_t pad0C;
	};

	PerformanceMorph* GetMorphNames()
	{
		// 1.16: 105 entries, filled by a static initializer from "browLowererL" to "BSX".
		REL::Relocation<PerformanceMorph*> ptr(REL::ID(930506));
		return ptr.get();
	}
#endif

	const std::map<std::string, size_t>& GetFaceMorphIndexMap()
	{
		static std::map<std::string, size_t> idxMap;
		if (idxMap.empty()) {
			auto& names = GetFaceMorphs();
			for (size_t i = 0; i < names.size(); i++) {
				idxMap[names[i]] = i;
			}
		}
		return idxMap;
	}

	const std::vector<std::string>& GetFaceMorphs()
	{
		static std::vector<std::string> data;
		if (data.empty()) {
#if defined TARGET_GAME_SF
			auto names = GetMorphNames();
			for (size_t i = 0; i < FACE_MORPHS_SIZE; i++) {
				data.push_back(names[i].name.c_str());
			}
#elif defined TARGET_GAME_F4
			data = {
				"BrowSqueeze",
				"JawFwd",
				"JawOpen",
				"LBrowOutUp",
				"LCheekUp",
				"LFrown",
				"LJaw",
				"LLipCornerIn",
				"LLipCornerOut",
				"LLwrLidDn",
				"LLwrLidUp",
				"LLwrLipDn",
				"LLwrLipUp",
				"LMidBrowDn",
				"LMidBrowUp",
				"LNoseUp",
				"LOutBrowDn",
				"LSmile",
				"LUprLidDn",
				"LUprLidUp",
				"LUprLipDn",
				"LUprLipUp",
				"LwrLipFunnel",
				"LwrLipRollIn",
				"LwrLipRollOut",
				"Pucker",
				"RBrowOutUp",
				"RCheekUp",
				"RFrown",
				"RJaw",
				"RLipCornerIn",
				"RLipCornerOut",
				"RLwrLidDn",
				"RLwrLidUp",
				"RLwrLipDn",
				"RLwrLipUp",
				"RMidBrowDn",
				"RMidBrowUp",
				"RNoseUp",
				"ROutBrowDn",
				"RSmile",
				"RUprLidDn",
				"RUprLidUp",
				"RUprLipDn",
				"RUprLipUp",
				"StickyLips",
				"UprLipFunnel",
				"UprLipRollIn",
				"UprLipRollOut",
				"Tongue",
				"LookUp",
				"LookDown",
				"LookLeft",
				"LookRight"
			};
#endif
		}
		return data;
	}
}