#pragma once
#include "Generator.h"
#include "Settings/SkeletonDescriptor.h"
#include "Easing.h"
#include "Node.h"
#include "Ozz.h"
#include "FileManager.h"
#include "SyncInstance.h"
#include "Face/Manager.h"
#include "PoseCache.h"
#include "Sequencer.h"
#include "IAnimEventHandler.h"
#include "Jobs/IPostGenJob.h"
#include "Physics/ModelSpaceSystem.h"

namespace Animation
{
	class Sequencer;

	class Graph :
		public FileRequesterBase,
		public IAnimEventHandler
	{
	public:
		struct TransitionData
		{
			std::function<void()> onEnd = nullptr;
			int startLayer = 0;
			int endLayer = 0;
			float localTime = 0.0f;
			float duration = 0.0f;
			float queuedDuration = 0.0f;
			CubicInOutEase<float> ease;
		};

		struct EyeTrackingData
		{
			RE::NiNode* lEye = nullptr;
			RE::NiNode* rEye = nullptr;
			RE::NiNode* eyeTarget = nullptr;
		};

		enum FLAGS : uint16_t
		{
			kNoFlags = 0,
			kPersistent = 1u << 0,
			kUnloaded3D = 1u << 1,

			kHasPostGenJob = 1u << 2,
			kTransitioning = 1u << 3,
			kHasGenerator = 1u << 4,

			kLoadingAnimation = 1u << 5,
			kLoadingSequencerAnimation = 1u << 7,

			kPositionLocked = 1u << 8,
			kLockAimPitch = 1u << 9,
			kLockLookPitch = 1u << 10,
			kLockHeading = 1u << 11,

			kIsPlayer = 1u << 12,
			kRequiresEyeTrackUpdate = 1u << 13,
			kRequiresFaceDataUpdate = 1u << 14,
			kGeneratedFirstPose = 1u << 15
		};

		enum TRANSITION_TYPE : uint8_t
		{
			kNoTransition = 0,
			kGameToGraph = 1,
			kGraphToGame = 2,
			kGraphSnapshotToGame = 3,
			kGeneratorToGenerator = 4
		};

		struct LOADED_DATA
		{
			TransitionData transition;
			std::array<ozz::animation::BlendingJob::Layer, 2> blendLayers;
			ozz::math::Float4x4 defaultTransform;
			FileID loadingFile;
			PoseCache poseCache;
			std::vector<std::pair<RE::BSFixedString, RE::BSFixedString>> pendingEvents;
			std::vector<ozz::math::Float4x4> lastOutput;
			std::vector<bool> boneMask;
			PoseCache::Handle restPose;
			PoseCache::Handle snapshotPose;
			PoseCache::Handle blendedPose;
			std::shared_ptr<Face::MorphData> faceMorphData = nullptr;
#ifdef TARGET_GAME_SF
			std::unique_ptr<EyeTrackingData> eyeTrackData = nullptr;
#endif
			std::unique_ptr<Physics::ModelSpaceSystem> physSystem = nullptr;
			NiSkeletonRootNode* rootNode = nullptr;
			RE::BSFaceGenAnimationData* faceAnimData = nullptr;
		};

		struct UNLOADED_DATA
		{
			FileID restoreFile;
		};

		std::mutex lock;
		xSE::stl::enumeration<FLAGS, uint16_t> flags = kNoFlags;
		RE::NiPointer<RE::TESObjectREFR> target;
		std::shared_ptr<const OzzSkeleton> skeleton;
		XYZTransform rootTransform;
		std::vector<ozz::math::Float4x4*> transforms;
		std::vector<IPostGenJob*> postGenJobs;
		std::shared_ptr<SyncInstance> syncInst = nullptr;
		std::unique_ptr<Sequencer> sequencer = nullptr;
		std::unique_ptr<Generator> generator = nullptr;
		std::unique_ptr<LOADED_DATA> loadedData = nullptr;
		std::unique_ptr<UNLOADED_DATA> unloadedData = nullptr;
		RE::TESObjectCELL* lastCell = nullptr;
#ifdef ENABLE_PERFORMANCE_MONITORING
		float lastUpdateMs = 0.0f;
		float baseUpdateMs = 0.0f;
#endif

		std::atomic<bool> requiresBaseTransforms = true;

		Graph();
		virtual ~Graph() noexcept;

		virtual void OnAnimationReady(const FileID& a_id, std::shared_ptr<IAnimationFile> a_anim) override;
		virtual void OnAnimationRequested(const FileID& a_id) override;
		virtual void QueueEvent(const RE::BSFixedString& a_event, const RE::BSFixedString& a_arg) override;

		void SetSkeleton(std::shared_ptr<const OzzSkeleton> a_descriptor);
		void GetSkeletonNodes(NiSkeletonRootNode* a_rootNode);
		ozz::math::Float4x4 GetCurrentTransform(size_t nodeIdx);
		void Update(float a_deltaTime, bool a_visible, GraphEventProcessor* a_gameGraph);
		bool AddTwoBoneIKJob(uint8_t a_chainId, const std::span<std::string_view, 3> a_nodeNames, const RE::NiPoint3& a_targetWorld, const RE::NiPoint3& a_poleDirModel = { 0.0f, 1.0f, 0.0f }, float a_transitionTime = 1.0f);
		bool RemoveTwoBoneIKJob(uint8_t a_chainId, float a_transitionTime = 1.0f);
		void AddPostGenJob(IPostGenJob* a_job);
		bool RemovePostGenJob(uint64_t a_guid);
		void StartTransition(std::unique_ptr<Generator> a_dest, float a_transitionTime);
		void AdvanceTransitionTime(float a_deltaTime);
		void UpdateTransition(const ozz::span<ozz::math::SoaTransform>& a_output, const ozz::span<ozz::math::SoaTransform>& a_generatedPose);
		void UpdatePreGenJobs(float a_deltaTime);
		void UpdatePostGenJobs(float a_deltaTime, const std::span<ozz::math::SoaTransform>& a_output);
		void PushAnimationOutput(float a_deltaTime, const std::span<ozz::math::SoaTransform>& a_output);
		void PushRootOutput(bool a_visible);
		void UpdateRestPose();
		void SnapshotPose();
		void ResetRootTransform();
		void MakeSyncOwner();
		void SyncToGraph(Graph* a_grph);
		void StopSyncing();
		void SyncRootToGraph(Graph* a_grph);
		void UpdateFaceAnimData();
		void SetNoBlink(bool a_noBlink);
		void SetFaceMorphsControlled(bool a_controlled, float a_transitionTime);
		void DisableEyeTracking();
		void EnableEyeTracking();
		void DetachSequencer(bool a_transitionOut = true);
		void SetLoaded(bool a_loaded);
		void ProcessEvents(GraphEventProcessor* a_gameGraph) const;
		void SendEventExternal(const RE::BSFixedString& a_event, const RE::BSFixedString& a_arg = "");
		void SetLockPosition(bool a_lock);
		size_t GetSizeBytes() const;
		bool GetRequiresDetach() const;
		bool GetRequiresBaseTransforms() const;
	};
}