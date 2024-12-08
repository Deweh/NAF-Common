#include "Graph.h"
#include "Util/Math.h"
#include "Node.h"
#include "Face/Manager.h"
#include "Util/Timing.h"
#include "Util/String.h"
#include "Util/Ozz.h"
#include "Sequencer.h"
#include "GraphManager.h"
#include "Jobs/IKTwoBoneJob.h"

namespace Animation
{
#ifdef TARGET_GAME_F4
	namespace detail
	{
		void SetCharControllerPosition(RE::bhkCharacterController* a_controller, const RE::NiPoint3A& a_pos)
		{
			using func_t = decltype(&SetCharControllerPosition);
			static REL::Relocation<func_t> func{ REL::ID(1336231) };
			return func(a_controller, a_pos);
		}
	}

	constexpr uint32_t CC_FLAG_NO_SIMULATION{ 0x20000 };
#endif

	Graph::Graph()
	{
		flags.set(FLAGS::kUnloaded3D, FLAGS::kPositionLocked, FLAGS::kLockHeading, FLAGS::kLockAimPitch);
	}

	Graph::~Graph() noexcept
	{
		StopSyncing();
		if (loadedData) {
			Face::Manager::GetSingleton()->OnAnimDataChange(loadedData->faceAnimData, nullptr);
		}
		EnableEyeTracking();
		SetLockPosition(false);
		SendEventExternal("AnimObjUnequip");
	}

	void Graph::OnAnimationReady(const FileID& a_id, std::shared_ptr<IAnimationFile> a_anim)
	{
		std::unique_lock l{ lock };
		if (!loadedData) {
			return;
		}

		if (sequencer && sequencer->OnAnimationReady(a_id, a_anim)) {
			return;
		}

		if (flags.all(FLAGS::kLoadingAnimation)) {
			if (a_id != loadedData->loadingFile) {
				return;
			}

			flags.reset(FLAGS::kLoadingAnimation);
			if (a_anim != nullptr) {
				StartTransition(a_anim->CreateGenerator(), loadedData->transition.queuedDuration);
			}
		}
	}

	void Graph::OnAnimationRequested(const FileID& a_id)
	{
		if (!loadedData) {
			return;
		}

		if (sequencer && sequencer->OnAnimationRequested(a_id)) {
			return;
		}

		if (flags.all(FLAGS::kLoadingAnimation)) {
			FileManager::GetSingleton()->CancelAnimationRequest(loadedData->loadingFile, weak_from_this());
		} else {
			flags.set(FLAGS::kLoadingAnimation);
		}
		
		loadedData->loadingFile = a_id;
	}

	void Graph::QueueEvent(const RE::BSFixedString& a_event, const RE::BSFixedString& a_arg)
	{
		if (!loadedData)
			return;

		loadedData->pendingEvents.emplace_back(a_event, a_arg);
	}

	void Graph::SetSkeleton(std::shared_ptr<const OzzSkeleton> a_descriptor)
	{
		skeleton = a_descriptor;
	}

	void Graph::GetSkeletonNodes(NiSkeletonRootNode* a_rootNode) {
		bool isLoaded = a_rootNode != nullptr;

		transforms.clear();
		EnableEyeTracking();
		SetLoaded(isLoaded);

		if (isLoaded) {
			auto& l = loadedData;
			l->rootNode = a_rootNode;

			if (generator) {
				generator->SetContext({
					.modelSpaceCache = loadedData->lastOutput,
					.prevRootTransform = reinterpret_cast<ozz::math::Float4x4*>(&loadedData->rootNode->previousWorld),
					.rootTransform = reinterpret_cast<ozz::math::Float4x4*>(&loadedData->rootNode->world),
					.restPose = &loadedData->restPose,
					.skeleton = skeleton.get(),
					.physSystem = loadedData->physSystem.get()
				});
			}

			for (auto& name : skeleton->data->joint_names()) {
				RE::NiAVObject* n = a_rootNode->GetObjectByName(name);
				if (!n) {
					transforms.push_back(&l->defaultTransform);
				} else {
					transforms.push_back(reinterpret_cast<ozz::math::Float4x4*>(&n->local));
				}
			}

#ifdef TARGET_GAME_SF
			if (l->eyeTrackData) {
				l->eyeTrackData->eyeTarget = a_rootNode->GetObjectByName("Eye_Target");
				l->eyeTrackData->lEye = a_rootNode->GetObjectByName("L_Eye");
				l->eyeTrackData->rEye = a_rootNode->GetObjectByName("R_Eye");
				flags.set(FLAGS::kRequiresEyeTrackUpdate);
			}
#endif

			flags.set(FLAGS::kRequiresFaceDataUpdate);
		}
	}

	ozz::math::Float4x4 Graph::GetCurrentTransform(size_t nodeIdx)
	{
		if (nodeIdx < transforms.size()) {
			return *transforms[nodeIdx];
		}
		return ozz::math::Float4x4::identity();
	}

	void Graph::Update(float a_deltaTime, bool a_visible, GraphEventProcessor* a_gameGraph)
	{
		PERF_TIMER_START(start);

		if (!loadedData) {
			return;
		}

		if (flags.any(FLAGS::kRequiresEyeTrackUpdate)) {
			DisableEyeTracking();
			flags.reset(FLAGS::kRequiresEyeTrackUpdate);
		}

		if (flags.any(FLAGS::kRequiresFaceDataUpdate)) {
			RE::BSFaceGenAnimationData* oldFaceAnimData = loadedData->faceAnimData;
			UpdateFaceAnimData();
			Face::Manager::GetSingleton()->OnAnimDataChange(oldFaceAnimData, loadedData->faceAnimData);
			SetNoBlink(true);

			if (generator && generator->HasFaceAnimation()) {
				SetFaceMorphsControlled(true, loadedData->transition.queuedDuration);
				generator->SetFaceMorphData(loadedData->faceMorphData.get());
			}
			flags.reset(FLAGS::kRequiresFaceDataUpdate);
		}

		if (!generator) {
			if (a_visible && flags.any(FLAGS::kHasPostGenJob)) {
				requiresBaseTransforms = true;
				UpdatePreGenJobs(a_deltaTime);
				UpdateRestPose();
				PushAnimationOutput(a_deltaTime, loadedData->restPose.get());
			}
			PERF_TIMER_END(start, updateTime);
			PERF_TIMER_COPY_VALUE(updateTime, lastUpdateMs);
			return;
		}

		generator->AdvanceTime(a_deltaTime);
		if (sequencer) {
			sequencer->Update();
		}

		if (syncInst) {
			bool instValid = syncInst->Synchronize(this, [&](Graph* owner, bool a_ownerUpdated) {
				if (sequencer && owner->sequencer && !sequencer->Synchronize(owner->sequencer.get())) {
					return;
				}
				SyncRootToGraph(owner);
				if (owner->generator) {
					generator->Synchronize(owner->generator.get(), a_ownerUpdated ? 0.0f : a_deltaTime * owner->generator->speed);
				}
			});
			if (!instValid) {
				StopSyncing();
			}
		}

		if (a_visible) {
			if (requiresBaseTransforms.load()) {
				UpdateRestPose();
			}
			UpdatePreGenJobs(a_deltaTime);
			auto generatedPose = generator->Generate(loadedData->poseCache, this);

			if (flags.any(FLAGS::kTransitioning)) {
				auto blendPose = loadedData->blendedPose.get();
				AdvanceTransitionTime(a_deltaTime);
				UpdateTransition(ozz::make_span(blendPose), ozz::make_span(generatedPose));
				PushAnimationOutput(a_deltaTime, blendPose);
			} else {
				PushAnimationOutput(a_deltaTime, generatedPose);
			}
		} else {
			if (flags.any(FLAGS::kTransitioning))
				AdvanceTransitionTime(a_deltaTime);

			PushAnimationOutput(a_deltaTime, {});
		}

		PushRootOutput(a_visible);
		ProcessEvents(a_gameGraph);

		PERF_TIMER_END(start, updateTime);
		PERF_TIMER_COPY_VALUE(updateTime, lastUpdateMs);
	}

	bool Graph::AddTwoBoneIKJob(uint8_t a_chainId, const std::span<std::string_view, 3> a_nodeNames, const RE::NiPoint3& a_targetWorld, const RE::NiPoint3& a_poleDirModel, float a_transitionTime)
	{
		uint64_t newGuid = IKTwoBoneJob::ChainIDToGUID(a_chainId);
		for (auto& j : postGenJobs) {
			if (j->GetGUID() == newGuid) {
				return false;
			}
		}

		std::array<int32_t, 3> nodeIdxs;
		if (!Util::GetJointIndexes(skeleton->data.get(), a_nodeNames, nodeIdxs))
			return false;

		if (skeleton->data->joint_parents()[nodeIdxs[2]] == ozz::animation::Skeleton::kNoParent) {
			return false;
		}

		auto d = new IKTwoBoneJob();
		d->target = a_targetWorld;
		d->poleDir = a_poleDirModel;
		d->start_node = nodeIdxs[0];
		d->mid_node = nodeIdxs[1];
		d->end_node = nodeIdxs[2];
		d->chainId = a_chainId;
		d->TransitionIn(a_transitionTime);
		AddPostGenJob(d);
		return true;
	}

	bool Graph::RemoveTwoBoneIKJob(uint8_t a_chainId, float a_transitionTime)
	{
		uint64_t targetGuid = IKTwoBoneJob::ChainIDToGUID(a_chainId);
		for (auto& j : postGenJobs) {
			if (j->GetGUID() == targetGuid) {
				static_cast<IKTwoBoneJob*>(j)->TransitionOut(a_transitionTime, true);
				return true;
			}
		}
		return false;
	}

	void Graph::AddPostGenJob(IPostGenJob* a_job)
	{
		postGenJobs.push_back(a_job);
		flags.set(FLAGS::kHasPostGenJob);
	}

	bool Graph::RemovePostGenJob(uint64_t a_guid)
	{
		for (auto iter = postGenJobs.begin(); iter != postGenJobs.end(); iter++)
		{
			if ((*iter)->GetGUID() == a_guid) {
				(*iter)->Destroy();
				postGenJobs.erase(iter);

				if (postGenJobs.empty()) {
					flags.reset(FLAGS::kHasPostGenJob);
				}
				return true;
			}
		}
		return false;
	}

	void Graph::MakeSyncOwner()
	{
		if (!syncInst) {
			syncInst = std::make_shared<SyncInstance>();
		}
		syncInst->AddMember(this, true);
	}

	void Graph::SyncToGraph(Graph* a_grph)
	{
		StopSyncing();
		syncInst = a_grph->syncInst;
		if (syncInst) {
			syncInst->AddMember(this, false);
		}
	}

	void Graph::StopSyncing()
	{
		if (syncInst) {
			syncInst->RemoveMember(this);
		}
		syncInst.reset();
	}

	void Graph::SyncRootToGraph(Graph* a_grph)
	{
		auto otherCell = a_grph->lastCell;
		if (lastCell == otherCell) {
			rootTransform = a_grph->rootTransform;
#ifdef TARGET_GAME_F4
		} else if (otherCell != nullptr && target->parentCell != otherCell) {
			// If sync owner has moved to a new space, move this ref to the same space.
			// Otherwise, if their new cell is in the same space, just use SetLocation to update the parent cell.
			if (lastCell == nullptr ||
				otherCell->IsInterior() || lastCell->IsInterior() ||
				otherCell->worldSpace != lastCell->worldSpace)
			{
				RE::PLAYER_TARGET_LOC targetLoc;
				targetLoc.world = otherCell->IsInterior() ? nullptr : otherCell->worldSpace;
				targetLoc.interior = otherCell->IsInterior() ? otherCell : nullptr;
				if (auto player = RE::PlayerCharacter::GetSingleton(); target.get() == player) {
					targetLoc.transitionTel = nullptr;
					targetLoc.location = a_grph->rootTransform.translate;
					targetLoc.angle = a_grph->rootTransform.rotate;
					targetLoc.walkThroughDoor = nullptr;
					targetLoc.arrivalFunc = nullptr;
					targetLoc.arrivalFuncData = nullptr;
					targetLoc.fastTravelDist = 0.0f;
					targetLoc.resetWeather = true;
					targetLoc.allowAutoSave = false;
					targetLoc.preventLoadMenu = false;
					targetLoc.skyTransition = false;
					targetLoc.isValid = true;

					player->queuedTargetLoc = targetLoc;
				} else {
					F4SE::GetTaskInterface()->AddTask(
						[interior = targetLoc.interior, world = targetLoc.world, target = target]() {
							target->MoveRefToNewSpace(interior, world);
						});
				}
			} else {
				target->SetLocationOnReference(a_grph->rootTransform.translate);
			}
#endif
		}
	}

	void Graph::SetNoBlink(bool a_noBlink)
	{
		if (!loadedData)
			return;

		Face::Manager::GetSingleton()->SetNoBlink(loadedData->faceAnimData, a_noBlink);
	}

	void Graph::SetFaceMorphsControlled(bool a_controlled, float a_transitionTime)
	{
		auto& l = loadedData;
		if (!l)
			return;

		if (a_controlled && l->faceAnimData) {
			if (l->faceMorphData) {
				l->faceMorphData->lock()->BeginTween(a_transitionTime, l->faceAnimData);
			} else {
				l->faceMorphData = std::make_shared<Face::MorphData>();
				Face::Manager::GetSingleton()->AttachMorphData(l->faceAnimData, l->faceMorphData, a_transitionTime);
			}
		} else if (!a_controlled && l->faceMorphData) {
			Face::Manager::GetSingleton()->DetachMorphData(l->faceAnimData, a_transitionTime);
			l->faceMorphData = nullptr;
		}
	}

	void Graph::DisableEyeTracking()
	{
#ifdef TARGET_GAME_SF
		auto& l = loadedData;
		if (!l)
			return;

		if (!l->eyeTrackData || !l->eyeTrackData->lEye || !l->eyeTrackData->rEye)
			return;

		auto eyeNodes = RE::EyeTracking::GetEyeNodes();
		for (auto& n : eyeNodes.data) {
			if (n.leftEye.get() == l->eyeTrackData->lEye && n.rightEye.get() == l->eyeTrackData->rEye) {
				n.leftEye.reset(l->eyeTrackData->eyeTarget);
				n.rightEye.reset(l->eyeTrackData->eyeTarget);
				break;
			}
		}
#endif
	}

	void Graph::EnableEyeTracking()
	{
#ifdef TARGET_GAME_SF
		auto& l = loadedData;
		if (!l)
			return;

		if (!l->eyeTrackData || !l->eyeTrackData->eyeTarget)
			return;

		auto eyeNodes = RE::EyeTracking::GetEyeNodes();
		for (auto& n : eyeNodes.data) {
			if (n.leftEye.get() == l->eyeTrackData->eyeTarget && n.rightEye.get() == l->eyeTrackData->eyeTarget) {
				n.leftEye.reset(l->eyeTrackData->lEye);
				n.rightEye.reset(l->eyeTrackData->rEye);
				break;
			}
		}
#endif
	}

	void Graph::DetachSequencer(bool a_transitionOut)
	{
		if (sequencer) {
			sequencer.reset();
			flags.reset(FLAGS::kLoadingSequencerAnimation);
			if (a_transitionOut) {
				StartTransition(nullptr, 1.0f);
			}
			xSE::GetTaskInterface()->AddTask([target = target]() {
				GraphManager::GetSingleton()->SendEvent(SequencePhaseChangeEvent{
					.exiting = true,
					.target = target
				});
			});
		}
	}

	void Graph::SetLoaded(bool a_loaded)
	{
		if (a_loaded && !loadedData)
		{
			flags.reset(FLAGS::kUnloaded3D);
			loadedData = std::make_unique<LOADED_DATA>();
			auto& l = loadedData;
			l->blendLayers[0].weight = .0f;
			l->blendLayers[1].weight = .0f;

			if (Util::String::CaseInsensitiveCompare(skeleton->name, "HumanRace")) {
				l->eyeTrackData = std::make_unique<EyeTrackingData>();
			}

			l->lastOutput.reserve(skeleton->data->num_joints());
			l->lastOutput.resize(skeleton->data->num_joints(), ozz::math::Float4x4::identity());
			l->boneMask = skeleton->defaultBoneMask;
			l->poseCache.set_pose_size(skeleton->data->num_soa_joints());
			l->poseCache.reserve(4);
			l->restPose = l->poseCache.acquire_handle();
			l->snapshotPose = l->poseCache.acquire_handle();
			l->blendedPose = l->poseCache.acquire_handle();

			if (unloadedData && !unloadedData->restoreFile.QPath().empty()) {
				loadedData->transition.queuedDuration = 0.2f;
				FileManager::GetSingleton()->RequestAnimation(unloadedData->restoreFile, skeleton->name, weak_from_this());
			}
			unloadedData.reset();
		}
		else if (!a_loaded && !unloadedData)
		{
			if (loadedData) {
				Face::Manager::GetSingleton()->OnAnimDataChange(loadedData->faceAnimData, nullptr);
				loadedData.reset();
			}

			flags.reset(FLAGS::kGeneratedFirstPose);
			flags.set(FLAGS::kUnloaded3D);
			unloadedData = std::make_unique<UNLOADED_DATA>();
			auto& u = unloadedData;

			if (generator) {
				u->restoreFile = FileID(generator->GetSourceFile(), "");
				generator.reset();
				flags.reset(FLAGS::kHasGenerator);
			}

			if (sequencer) {
				sequencer->OnGraphUnloaded();
			}

#ifdef ENABLE_PERFORMANCE_MONITORING
			lastUpdateMs = 0.0f;
#endif
		}
	}

	void Graph::ProcessEvents(GraphEventProcessor* a_gameGraph) const
	{
#ifdef TARGET_GAME_F4
		for (auto& pair : loadedData->pendingEvents)
		{
			static_cast<RE::BSTEventSource<RE::BSAnimationGraphEvent>*>(a_gameGraph)->Notify({
				.holderID = reinterpret_cast<uint64_t>(target.get()),
				.tag = pair.first,
				.payload = pair.second
			});
		}
#endif
		loadedData->pendingEvents.clear();
	}

	void Graph::SendEventExternal(const RE::BSFixedString& a_event, const RE::BSFixedString& a_arg)
	{
#ifdef TARGET_GAME_F4
		xSE::GetTaskInterface()->AddTask([hndl = target->GetHandle(), a_event = a_event, a_arg = a_arg]() {
			RE::NiPointer<RE::TESObjectREFR> target = hndl.get();
			if (!target)
				return;

			RE::BSTSmartPointer<RE::BSAnimationGraphManager> mngr;
			if (!target->GetAnimationGraphManagerImpl(mngr) || !mngr)
				return;

			RE::BSAnimationGraphEvent evnt{ .holderID = reinterpret_cast<uint64_t>(target.get()), .tag = a_event, .payload = a_arg };
			for (auto& g : mngr->graph) {
				static_cast<RE::BSTEventSource<RE::BSAnimationGraphEvent>*>(g.get())->Notify(evnt);
			}
		});
#endif
	}

	void Graph::SetLockPosition(bool a_lock)
	{
		if (!a_lock) {
#ifdef TARGET_GAME_F4
			if (flags.any(FLAGS::kPositionLocked)) {
				auto actor = target->As<RE::Actor>();
				if (!actor)
					return;

				auto currentProcess = actor->currentProcess;
				if (!currentProcess)
					return;

				auto middleHigh = currentProcess->middleHigh;
				if (!middleHigh)
					return;

				auto& charController = middleHigh->charController;
				if (!charController)
					return;

				charController->flags &= ~CC_FLAG_NO_SIMULATION;
			}
#endif
			flags.reset(FLAGS::kPositionLocked);
		} else {
			flags.set(FLAGS::kPositionLocked);
		}
	}

	size_t Graph::GetSizeBytes() const
	{
		size_t result = sizeof(Graph) + std::span(transforms).size_bytes() + std::span(postGenJobs).size_bytes();

		if (sequencer)
			result += sizeof(Sequencer);

		if (generator)
			result += generator->GetSizeBytes();

		if (loadedData) {
			auto l = loadedData.get();
			size_t maskSize = l->boneMask.size();
			result += sizeof(LOADED_DATA) + (l->poseCache.transforms_capacity() * sizeof(ozz::math::SoaTransform)) + std::span(l->pendingEvents).size_bytes()
				+ std::span(l->lastOutput).size_bytes() + (maskSize > 0 ? maskSize / 8 : 0);

			if (l->eyeTrackData) {
				result += sizeof(EyeTrackingData);
			}
		}

		if (unloadedData) {
			result += sizeof(UNLOADED_DATA);
		}

		return result;
	}

	bool Graph::GetRequiresDetach() const
	{
		return flags.none(
			FLAGS::kPersistent,
			FLAGS::kHasPostGenJob,
			FLAGS::kHasGenerator,
			FLAGS::kTransitioning,
			FLAGS::kLoadingAnimation,
			FLAGS::kLoadingSequencerAnimation);
	}

	bool Graph::GetRequiresBaseTransforms() const
	{
		return requiresBaseTransforms.load();
	}

	void Graph::UpdateFaceAnimData()
	{
		auto l = loadedData.get();
		if (!l) {
			return;
		}

#if defined TARGET_GAME_F4
		auto a = target->As<RE::Actor>();
		if (!a) {
			l->faceAnimData = nullptr;
			return;
		}

		auto proc = a->currentProcess;
		if (!proc) {
			l->faceAnimData = nullptr;
			return;
		}

		auto middleHigh = proc->middleHigh;
		if (!middleHigh) {
			l->faceAnimData = nullptr;
			return;
		}

		l->faceAnimData = middleHigh->faceAnimationData;
#elif defined TARGET_GAME_SF
		auto m = l->rootNode->bgsModelNode;
		if (!m) {
			l->faceAnimData = nullptr;
			return;
		}

		if (m->facegenNodes.size < 1) {
			l->faceAnimData = nullptr;
			return;
		}

		auto fn = m->facegenNodes.data[0];
		if (!fn) {
			l->faceAnimData = nullptr;
			return;
		}

		l->faceAnimData = fn->faceGenAnimData;
#endif
	}

	void Graph::AdvanceTransitionTime(float a_deltaTime)
	{
		auto& transition = loadedData->transition;

		transition.localTime += a_deltaTime;
		if (transition.localTime >= transition.duration) {
			if (transition.onEnd != nullptr) {
				transition.onEnd();
			}

			flags.reset(FLAGS::kTransitioning);

			if (transition.duration < 0.01f) {
				transition.duration = 0.01f;
			}

			transition.localTime = transition.duration;
		}
	}

	void Graph::UpdateTransition(const ozz::span<ozz::math::SoaTransform>& a_output, const ozz::span<ozz::math::SoaTransform>& a_generatedPose)
	{
		auto& transition = loadedData->transition;
		auto& blendLayers = loadedData->blendLayers;
		blendLayers[0].transform = a_generatedPose;
		blendLayers[1].transform = loadedData->snapshotPose.get_ozz();

		ozz::animation::BlendingJob blendJob;
		blendJob.rest_pose = loadedData->restPose.get_ozz();
		blendJob.layers = ozz::make_span(blendLayers);
		blendJob.output = a_output;
		blendJob.threshold = 1.0f;

		float normalizedTime = transition.ease(transition.localTime / transition.duration);
		if (transition.startLayer >= 0) {
			blendLayers[transition.startLayer].weight = 1.0f - normalizedTime;
		}
		if (transition.endLayer >= 0) {
			blendLayers[transition.endLayer].weight = normalizedTime;
		}

		blendJob.Run();
	}

	void Graph::UpdatePreGenJobs(float a_deltaTime)
	{
		Physics::ModelSpaceSystem* physSystem = loadedData->physSystem.get();
		if (!physSystem)
			return;

		NiSkeletonRootNode* rootNode = loadedData->rootNode;
		physSystem->Update(a_deltaTime, *reinterpret_cast<ozz::math::Float4x4*>(&rootNode->world), *reinterpret_cast<ozz::math::Float4x4*>(&rootNode->previousWorld));
	}

	void Graph::UpdatePostGenJobs(float a_deltaTime, const std::span<ozz::math::SoaTransform>& a_output)
	{
		if (postGenJobs.empty())
			return;

		ozz::animation::LocalToModelJob l2mJob;
		l2mJob.skeleton = skeleton->data.get();
		l2mJob.input = ozz::make_span(a_output);
		l2mJob.output = ozz::make_span(loadedData->lastOutput);
		l2mJob.Run();

		ozz::math::SimdInt4 invertible;
		IPostGenJob::Context ctxt{
			.localTransforms = a_output.data(),
			.localCount = a_output.size(),
			.modelSpaceMatrices = loadedData->lastOutput.data(),
			.modelSpaceCount = loadedData->lastOutput.size(),
			.rootMatrix = *reinterpret_cast<ozz::math::Float4x4*>(&loadedData->rootNode->world),
			.prevRootMatrix = *reinterpret_cast<ozz::math::Float4x4*>(&loadedData->rootNode->previousWorld),
			.invertedRootMatrix = ozz::math::Invert(ctxt.rootMatrix, &invertible),
			.skeleton = skeleton->data.get(),
			.deltaTime = a_deltaTime
		};

		for (auto iter = postGenJobs.begin(); iter != postGenJobs.end();) {
			if (!(*iter)->Run(ctxt)) [[unlikely]] {
				(*iter)->Destroy();
				iter = postGenJobs.erase(iter);

				if (postGenJobs.empty()) {
					flags.reset(FLAGS::kHasPostGenJob);
				}
			} else {
				++iter;
			}
		}
	}

	void Graph::StartTransition(std::unique_ptr<Generator> a_dest, float a_transitionTime)
	{
		if (!loadedData)
			return;

		auto& transition = loadedData->transition;
		auto& blendLayers = loadedData->blendLayers;
		bool needsRestPose = a_dest && a_dest->RequiresRestPose();

		const auto SetData = [&](TRANSITION_TYPE t) {
			switch (t) {
			case kGameToGraph:
				requiresBaseTransforms = true;
				transition.startLayer = -1;
				transition.endLayer = 0;
				transition.onEnd = [this, needsRestPose = needsRestPose]() {
					requiresBaseTransforms = needsRestPose;
				};
				break;
			case kGraphToGame:
				requiresBaseTransforms = true;
				transition.startLayer = 0;
				transition.endLayer = -1;
				transition.onEnd = [&]() {
					flags.reset(FLAGS::kHasGenerator);
					generator.reset();
				};
				break;
			case kGeneratorToGenerator:
				QueueEvent("AnimObjUnequip", "");
				requiresBaseTransforms = needsRestPose;
				blendLayers[0].weight = 0.0f;
				blendLayers[1].weight = 1.0f;
				transition.startLayer = 1;
				transition.endLayer = 0;
				transition.onEnd = nullptr;
				break;
			case kGraphSnapshotToGame:
				requiresBaseTransforms = true;
				blendLayers[0].weight = 0.0f;
				blendLayers[1].weight = 1.0f;
				transition.startLayer = 1;
				transition.endLayer = -1;
				transition.onEnd = [&]() {
					flags.reset(FLAGS::kHasGenerator);
					generator.reset();
				};
				break;
			}
		};

		transition.localTime = 0.0f;
		transition.duration = a_transitionTime;

		if (flags.all(FLAGS::kTransitioning)) {
			SnapshotPose();
			if (a_dest != nullptr) {
				SetData(TRANSITION_TYPE::kGeneratorToGenerator);
			} else {
				SetData(TRANSITION_TYPE::kGraphSnapshotToGame);
			}
		} else if (flags.all(FLAGS::kHasGenerator)) {
			if (a_dest != nullptr) {
				SnapshotPose();
				SetData(TRANSITION_TYPE::kGeneratorToGenerator);
			} else {
				SetData(TRANSITION_TYPE::kGraphToGame);
			}
		} else {
			if (a_dest != nullptr) {
				SetData(TRANSITION_TYPE::kGameToGraph);
			} else {
				return;
			}
		}

		if (generator != nullptr) {
			generator->OnDetaching();
		}

		flags.set(FLAGS::kTransitioning);
		if (a_dest != nullptr) {
			generator = std::move(a_dest);

			if (generator->RequiresPhysicsSystem()) {
				if (!loadedData->physSystem) {
					loadedData->physSystem = std::make_unique<Physics::ModelSpaceSystem>();
				}
			} else {
				loadedData->physSystem.reset();
			}

			generator->SetContext({
				.modelSpaceCache = loadedData->lastOutput,
				.prevRootTransform = reinterpret_cast<ozz::math::Float4x4*>(&loadedData->rootNode->previousWorld),
				.rootTransform = reinterpret_cast<ozz::math::Float4x4*>(&loadedData->rootNode->world),
				.restPose = &loadedData->restPose,
				.skeleton = skeleton.get(),
				.physSystem = loadedData->physSystem.get()
			});

			if (generator->HasFaceAnimation()) {
				loadedData->transition.queuedDuration = a_transitionTime;
				SetFaceMorphsControlled(true, a_transitionTime);
				generator->SetFaceMorphData(loadedData->faceMorphData.get());
			} else {
				SetFaceMorphsControlled(false, a_transitionTime);
			}

			flags.set(FLAGS::kHasGenerator);
		} else {
			SetFaceMorphsControlled(false, a_transitionTime);
		}
	}

	void Graph::PushAnimationOutput(float a_deltaTime, const std::span<ozz::math::SoaTransform>& a_output)
	{
		if (loadedData->lastOutput.size() < 2)
			return;

		if (!a_output.empty()) {
			UpdatePostGenJobs(a_deltaTime, a_output);
			Util::Ozz::UnpackSoaTransforms(a_output, loadedData->lastOutput, skeleton->data.get());
		}

		flags.set(FLAGS::kGeneratedFirstPose);
		const auto& source = loadedData->lastOutput;
		const auto& dest = transforms;
		const auto& mask = loadedData->boneMask;

		size_t end = transforms.size();
		for (size_t i = 0; i < end; i++) {
			if (mask[i]) {
				*dest[i] = source[i];
			}
		}

#ifdef TARGET_GAME_SF
		auto r = loadedData->rootNode;
		if (!r)
			return;
		auto m = r->bgsModelNode;
		if (!m)
			return;
		auto u = m->unk10;
		if (!u)
			return;
		u->needsUpdate = true;
#endif
	}

	void Graph::PushRootOutput(bool a_visible)
	{
		if (flags.none(FLAGS::kPositionLocked)) {
			ResetRootTransform();
			return;
		}

		auto currentCell = target->parentCell;
		if (lastCell != currentCell) {
#if defined TARGET_GAME_F4
			if (currentCell == nullptr || lastCell == nullptr ||
				currentCell->IsInterior() || lastCell->IsInterior() ||
				currentCell->worldSpace != lastCell->worldSpace)
			{
				ResetRootTransform();
			} else {
				lastCell = target->parentCell;
			}
#elif defined TARGET_GAME_SF
			ResetRootTransform();
#endif
		}

		if (flags.any(FLAGS::kLockLookPitch)) {
			target->data.angle.x = 0.0f;
			target->data.angle.y = 0.0f;
		}
		if (flags.any(FLAGS::kLockHeading)) {
			target->data.angle.z = rootTransform.rotate.z;
		}

#if defined TARGET_GAME_F4
		RE::NiPoint3A paddedTranslation = { rootTransform.translate.x, rootTransform.translate.y, rootTransform.translate.z };
		target->data.location = paddedTranslation;

		auto actor = target->As<RE::Actor>();
		if (!actor)
			return;

		auto currentProcess = actor->currentProcess;
		if (!currentProcess)
			return;

		auto middleHigh = currentProcess->middleHigh;
		if (!middleHigh)
			return;

		auto& charController = middleHigh->charController;
		if (!charController)
			return;

		if (flags.any(FLAGS::kLockAimPitch)) {
			charController->pitchAngle = 0.0f;
		}
		charController->flags |= CC_FLAG_NO_SIMULATION;
		detail::SetCharControllerPosition(charController.get(), paddedTranslation);
#elif defined TARGET_GAME_SF
		target->data.location = rootTransform.translate;

		static RE::TransformsManager* transformManager = RE::TransformsManager::GetSingleton();
		if (a_visible) {
			transformManager->RequestPositionUpdate(target.get(), rootTransform.translate);
		} else {
			//This doesn't keep the actor perfectly in place, but it's good enough for when the camera is looking away.
			SFSE::GetTaskInterface()->AddTask([rootXYZ = rootTransform, target = target] {
				auto actor = target->As<RE::Actor>();
				if (!actor)
					return;

				auto process = actor->currentProcess;
				if (!process)
					return;

				auto middleHigh = process->middleHigh;
				if (!middleHigh)
					return;

				auto charProxy = middleHigh->charProxy;
				if (!charProxy)
					return;

				charProxy->SetPosition(RE::NiPoint3A(rootXYZ.translate));
			});
		}
#endif
	}

	void Graph::UpdateRestPose()
	{
		const auto output = loadedData->restPose.get();
		const int end = skeleton->data->num_joints();

		for (size_t i = 0, k = 0; i < end; i += 4, k++) {
			ozz::math::SimdFloat4 translations[4];
			ozz::math::SimdFloat4 rotations[4];
			ozz::math::SimdFloat4 scales[4];

			size_t remaining = std::min(4ui64, end - i);
			for (int j = 0; j < remaining; j++) {
				ozz::math::ToAffine(*transforms[i + j], &translations[j], &rotations[j], &scales[j]);
			}

			auto& out = output[k];
			ozz::math::Transpose4x3(translations, &out.translation.x);
			ozz::math::Transpose4x4(rotations, &out.rotation.x);
			ozz::math::Transpose4x3(scales, &out.scale.x);
		}
	}

	void Graph::SnapshotPose()
	{
		if (!loadedData)
			return;

		std::span<ozz::math::SoaTransform> snapshotPose = loadedData->snapshotPose.get();
		if (flags.any(FLAGS::kGeneratedFirstPose)) {
			Util::Ozz::PackSoaTransforms(loadedData->lastOutput, snapshotPose, skeleton->data.get());
		} else {
			UpdateRestPose();
			std::span<ozz::math::SoaTransform> restPose = loadedData->restPose.get();
			std::copy(restPose.begin(), restPose.end(), snapshotPose.begin());
		}
	}

	void Graph::ResetRootTransform()
	{
		lastCell = target->parentCell;
		rootTransform.rotate = target->data.angle;
		rootTransform.translate = target->data.location;
	}
}