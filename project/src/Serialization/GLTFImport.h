#pragma once
#include "Animation/Generator.h"
#include "Settings/SkeletonDescriptor.h"
#include "Animation/Ozz.h"

namespace Serialization
{
	class GLTFImport
	{
	public:
		enum ErrorCode : uint16_t
		{
			kSuccess = 0,
			kNoSkeleton = 1,
			kFailedToLoad = 2,
			kFailedToMakeClip = 3,
			kInvalidAnimationIdentifier = 4,
			kUnspecifiedError = 5
		};

		struct AnimationIdentifer
		{
			enum Type : uint8_t
			{
				kIndex = 0,
				kName = 1
			};

			Type type = kIndex;
			std::string name = "";
			size_t index = 0;
		};

		struct AnimationResult
		{
			ozz::unique_ptr<ozz::animation::Animation> anim = nullptr;
			ErrorCode error = kUnspecifiedError;
		};

		struct AnimationInfo
		{
			RE::Actor* targetActor = nullptr;
			std::string fileName = "";
			AnimationIdentifer id = {};
			AnimationResult result;
		};

		struct AssetData
		{
			fastgltf::Asset asset;
			std::map<size_t, std::vector<std::string>> morphTargets;
			std::map<size_t, std::string> originalNames;
			// Animation index -> its "unmasked_bones" and "masked_bones" extras.
			std::map<size_t, std::vector<std::string>> unmaskedBones;
			std::map<size_t, std::vector<std::string>> maskedBones;
		};

		static void LoadAnimation(AnimationInfo& info);
		// The rest pose of the rig the file was authored with, read from its skins' inverse bind matrices, as local transforms in skeleton joint order.
		// Joints the file has no node for take the skeleton's rest pose. Returns nothing if no skeleton joint is in a skin.
		static std::optional<std::vector<ozz::math::Transform>> CreateRestPoseFromSkins(const AssetData* assetData, const ozz::animation::Skeleton& skeleton);
		static bool RetargetAnimation(ozz::animation::offline::RawAnimation* anim, const std::vector<ozz::math::Transform>& sourceRestPose, const std::vector<ozz::math::Transform>& targetRestPose, const ozz::animation::Skeleton& skeleton);
		static std::unique_ptr<Animation::RawOzzAnimation> CreateRawAnimation(const AssetData* assetData, const fastgltf::Animation* anim, const Animation::OzzSkeleton* skeleton);
		static std::unique_ptr<Animation::OzzAnimation> CreateRuntimeAnimation(const AssetData* assetData, const fastgltf::Animation* anim, const Animation::OzzSkeleton* skeleton);
		static std::unique_ptr<AssetData> LoadGLTF(const std::filesystem::path& fileName);
	};
}