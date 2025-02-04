#include "Settings.h"
#include "simdjson.h"
#include "Util/String.h"
#include "Animation/Transform.h"
#include "Impl.h"

namespace Settings
{
	const std::filesystem::path skeletonsDir = Util::String::GetDataPath() / "Skeletons";
	std::mutex lock;

	std::shared_ptr<Animation::OzzSkeleton>& GetDefaultSkeleton()
	{
		static std::shared_ptr<Animation::OzzSkeleton> defaultSkeleton;
		return defaultSkeleton;
	}

	std::unordered_map<std::string, std::shared_ptr<Animation::OzzSkeleton>>& GetSkeletonMap()
	{
		static std::unordered_map<std::string, std::shared_ptr<Animation::OzzSkeleton>> skeletons;
		return skeletons;
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

	const std::filesystem::path& GetSkeletonsPath()
	{
		return skeletonsDir;
	}

	namespace detail
	{
		std::shared_ptr<Animation::OzzSkeleton> BuildSkeleton(const std::string_view a_name, RE::Actor* a_actor)
		{
			SkeletonDescriptor skele;
			GetImplFunctions().GetSkeletonBonesForActor(skele, a_actor);
			return skele.BuildRuntime(a_name);
		}

		void SpotBuildSkeletonIfNeeded(const std::string_view a_id, RE::Actor* a_actor)
		{
			std::unique_lock l{ lock };
			auto& skeletons = GetSkeletonMap();
			if (auto iter = skeletons.find(a_id.data()); iter != skeletons.end()) {
				return;
			}

			auto result = BuildSkeleton(a_id, a_actor);
			if (result) {
				skeletons.emplace(a_id, result);
			}
		}
	}

	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(const std::string& a_id)
	{
		std::unique_lock l{ lock };
		auto& skeletons = GetSkeletonMap();
		if (auto iter = skeletons.find(a_id); iter != skeletons.end()) {
			return iter->second;
		} else {
			return GetDefaultSkeleton();
		}
	}

	std::shared_ptr<const Animation::OzzSkeleton> GetSkeleton(RE::Actor* a_actor)
	{
		if (!a_actor)
			return GetDefaultSkeleton();

		return GetSkeleton(GetSkeletonIdentifier(a_actor).c_str());
	}

	RE::BSFixedString GetSkeletonIdentifier(RE::Actor* a_actor)
	{
	#if defined TARGET_GAME_F4
		auto& identifier = a_actor->race->behaviorGraphProjectName[0];
	#elif defined TARGET_GAME_SF
		auto& identifier = a_actor->race->formEditorID;
	#endif
		detail::SpotBuildSkeletonIfNeeded(identifier.c_str(), a_actor);
		return identifier;
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
		REL::Relocation<PerformanceMorph*> ptr(REL::ID(872948));
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