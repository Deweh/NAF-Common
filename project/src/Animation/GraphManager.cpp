#include "GraphManager.h"
#include "Settings/Settings.h"
#include "Serialization/GLTFImport.h"
#include "Util/String.h"
#include "Graph.h"
#include "Generator.h"
#include "FileManager.h"
#include "Util/Trampoline.h"
#include "Util/Timing.h"

namespace Animation
{
	void GraphManager::CreateSaveData(SaveData& a_data)
	{
		const auto SerializeGraph = [&](Graph* g) -> SaveData::RefData {
			SaveData::RefData refData;
			refData.formId = g->GetTargetFormID();
			refData.syncOwner = g->GetSyncOwnerFormID();
			refData.animFile = g->GetCurrentAnimationFile();

			if (auto gen = g->generator.get(); gen) {
				refData.localTime = gen->localTime;
				refData.speedMult = gen->speed;
				if (ProceduralGenerator* pGen = dynamic_cast<ProceduralGenerator*>(gen); pGen) {
					pGen->ForEachVariable([&](std::string_view name, float& value) {
						refData.blendVars.emplace_back(SaveData::BlendGraphVariable{ .name = std::string(name), .value = value });
					});
				}
			} else {
				refData.localTime = 0.0f;
				refData.speedMult = 0.0f;
			}
			return refData;
		};

		std::unique_lock l{ stateLock };
		for (auto& g : state->loadedGraphs) {
			a_data.refs.push_back(std::move(SerializeGraph(g.second.get())));
		}
		for (auto& g : state->unloadedGraphs) {
			a_data.refs.push_back(std::move(SerializeGraph(g.second.get())));
		}
	}

	void GraphManager::LoadSaveData(const SaveData& a_data)
	{
		const auto GetActor = [](uint32_t a_id) {
#if defined TARGET_GAME_SF
			return RE::TESForm::LookupByID<RE::Actor>(a_id);
#elif defined TARGET_GAME_F4
			return RE::TESForm::GetFormByID<RE::Actor>(a_id);
#endif
		};

		std::unique_lock l{ stateLock };
		for (const auto& ref : a_data.refs)
		{
			RE::Actor* curActor = GetActor(ref.formId);
			if (!curActor)
				continue;

			std::shared_ptr<Graph> g = GetGraphLockless(curActor, true);
			g->MountAnimationFile(FileID(ref.animFile, ""), 0.0f, false);

			if (auto gen = g->generator.get(); gen) {
				gen->localTime = ref.localTime;
				gen->speed = ref.speedMult;

				if (ProceduralGenerator* pGen = dynamic_cast<ProceduralGenerator*>(gen); pGen) {
					for (const auto& pVar : ref.blendVars) {
						pGen->SetVariable(pVar.name, pVar.value);
					}
				}
			}

			if (ref.syncOwner != 0 && ref.syncOwner != ref.formId) {
				RE::Actor* syncActor = GetActor(ref.syncOwner);
				if (!syncActor)
					continue;

				std::shared_ptr<Graph> syncGraph = GetGraphLockless(syncActor, true);
				syncGraph->MakeSyncOwner();
				g->SyncToGraph(syncGraph.get());
			}
		}
	}

	GraphManager* GraphManager::GetSingleton()
	{
		static GraphManager* singleton{ new GraphManager() };
		return singleton;
	}

	bool GraphManager::LoadAndStartAnimation(RE::Actor* a_actor, const std::string_view a_filePath, const std::string_view a_animId, float a_transitionTime)
	{
		return VisitGraph(a_actor, [&](Graph* g) {
			g->MountAnimationFile(FileID(a_filePath, a_animId), a_transitionTime);
			return true;
		}, true);
	}

	void GraphManager::StartSequence(RE::Actor* a_actor, std::vector<Sequencer::PhaseData>&& a_phaseData)
	{
		auto seq = std::make_unique<Sequencer>(std::move(a_phaseData));
		VisitGraph(a_actor, [&](Graph* g) {
			g->sequencer = std::move(seq);
			g->sequencer->OnAttachedToGraph(g);
			return true;
		}, true);
	}

	bool GraphManager::AdvanceSequence(RE::Actor* a_actor, bool a_smooth)
	{
		return VisitGraph(a_actor, [&](Graph* g) {
			if (!g->sequencer)
				return false;

			g->sequencer->flags.set(Sequencer::FLAG::kForceAdvance);
			if (a_smooth)
				g->sequencer->flags.set(Sequencer::FLAG::kSmoothAdvance);
			return true;
		});
	}

	bool GraphManager::SetSequencePhase(RE::Actor* a_actor, size_t a_idx)
	{
		return VisitGraph(a_actor, [&](Graph* g) {
			if (!g->sequencer)
				return false;

			g->sequencer->SetPhase(a_idx);
			return true;
		});
	}

	size_t GraphManager::GetSequencePhase(RE::Actor* a_actor)
	{
		size_t result = UINT64_MAX;
		VisitGraph(a_actor, [&](Graph* g) {
			if (!g->sequencer)
				return false;

			result = g->sequencer->GetPhase();
			return true;
		});
		return result;
	}

	bool GraphManager::SetAnimationSpeed(RE::Actor* a_actor, float a_speed)
	{
		return VisitGraph(a_actor, [&](Graph* g) {
			if (!g->generator)
				return false;

			g->generator->speed = a_speed;
			return true;
		});
	}

	float GraphManager::GetAnimationSpeed(RE::Actor* a_actor)
	{
		float result = 0.0f;
		VisitGraph(a_actor, [&](Graph* g) {
			if (!g->generator)
				return false;

			result = g->generator->speed;
			return true;
		});
		return result;
	}

	void GraphManager::SyncGraphs(const std::vector<RE::Actor*>& a_actors)
	{
		if (a_actors.size() < 2)
			return;

		std::unique_lock sl{ stateLock };
		auto owner = GetGraphLockless(*a_actors.begin(), true);
		std::unique_lock ol{ owner->lock };
		owner->MakeSyncOwner();
		ol.unlock();
		for (size_t i = 1; i < a_actors.size(); i++) {
			auto g = GetGraphLockless(a_actors[i], true);
			std::unique_lock l{ g->lock };
			g->SyncToGraph(owner.get());
		}
	}

	void GraphManager::StopSyncing(RE::Actor* a_actor)
	{
		VisitGraph(a_actor, [&](Graph* g) {
			g->StopSyncing();
			return true;
		});
	}

	bool GraphManager::SetProceduralVariable(RE::Actor* a_actor, const std::string_view a_name, float a_value)
	{
		return VisitGraph(a_actor, [&](Graph* g) {
			auto gen = g->generator.get();
			if (!gen)
				return false;

			if (gen->GetType() != GenType::kProcedural)
				return false;

			return static_cast<ProceduralGenerator*>(gen)->SetVariable(a_name, a_value);
		});
	}

	float GraphManager::GetProceduralVariable(RE::Actor* a_actor, const std::string_view a_name)
	{
		float result = 0.0f;

		VisitGraph(a_actor, [&](Graph* g) {
			auto gen = g->generator.get();
			if (!gen)
				return false;

			if (gen->GetType() != GenType::kProcedural)
				return false;

			result = static_cast<ProceduralGenerator*>(gen)->GetVariable(a_name);
			return true;
		});

		return result;
	}

	void GraphManager::SetGraphControlsPosition(RE::Actor* a_actor, bool a_controls)
	{
		VisitGraph(a_actor, [&](Graph* g) {
			g->SetLockPosition(a_controls);
			return true;
		});
	}

	bool GraphManager::AttachGenerator(RE::Actor* a_actor, std::unique_ptr<Generator> a_gen, float a_transitionTime)
	{
		return VisitGraph(a_actor, [&](Graph* g) {
			g->DetachSequencer(false);
			g->StartTransition(std::move(a_gen), a_transitionTime);
			return true;
		}, true);
	}

	bool GraphManager::DetachGenerator(RE::Actor* a_actor, float a_transitionTime)
	{
		bool detachRequired = false;
		bool result = VisitGraph(a_actor, [&](Graph* g) {
			g->flags.reset(Graph::FLAGS::kLoadingAnimation);
			g->DetachSequencer(false);

			if (g->flags.none(Graph::FLAGS::kUnloaded3D)) {
				g->StartTransition(nullptr, a_transitionTime);
			} else {
				detachRequired = true;
			}
			return true;
		});

		if (detachRequired) {
			DetachGraph(a_actor);
		}

		return result;
	}

	void GraphManager::Reset()
	{
		std::unique_lock l{ stateLock };
		state = std::make_unique<PersistentState>();
		playerIsManaged = false;
	}

	std::shared_ptr<Graph> GraphManager::GetGraph(RE::Actor* a_actor, bool a_create)
	{
		std::shared_lock ls{ stateLock, std::defer_lock };
		std::unique_lock lu{ stateLock, std::defer_lock };
		if (a_create) {
			lu.lock();
		} else {
			ls.lock();
		}

		return GetGraphLockless(a_actor, a_create);
	}

	void GraphManager::SetGraphLoaded(RE::IAnimationGraphManagerHolder* a_graph, bool a_loaded)
	{
		std::unique_lock l{ stateLock };
		if (a_loaded) {
			auto iter = state->unloadedGraphs.find(a_graph);
			if (iter == state->unloadedGraphs.end())
				return;

			auto ele = state->unloadedGraphs.extract(iter);
			state->loadedGraphs.insert(std::move(ele));
		} else {
			auto iter = state->loadedGraphs.find(a_graph);
			if (iter == state->loadedGraphs.end())
				return;

			auto ele = state->loadedGraphs.extract(iter);
			state->unloadedGraphs.insert(std::move(ele));
		}
	}

	void GraphManager::ProcessActor3DChange(RE::Actor* a_actor, RE::NiAVObject* a_3d)
	{
		if (auto g = GetGraph(a_actor, false); g) {
			std::unique_lock l{ g->lock };
#if defined TARGET_GAME_F4
			g->On3DChange(static_cast<RE::NiNode*>(a_3d));
#elif defined TARGET_GAME_SF
			g->On3DChange(static_cast<RE::BGSFadeNode*>(a_3d));
#endif
			bool isLoaded = g->flags.none(Graph::FLAGS::kUnloaded3D);
			l.unlock();
			SetGraphLoaded(a_actor, isLoaded);
		}
	}

	static std::shared_ptr<Graph> CreateGraph(RE::Actor* a_actor)
	{
		std::shared_ptr<Graph> g = std::make_shared<Graph>();
		g->target.reset(a_actor);
		if (a_actor == RE::PlayerCharacter::GetSingleton()) {
			g->flags.set(Graph::FLAGS::kIsPlayer);
		}
		g->SetSkeleton(Settings::GetSkeleton(a_actor));
		g->ResetRootTransform();
		RE::NiAVObject* actor3d = nullptr;
#if defined TARGET_GAME_F4
		g->On3DChange(static_cast<RE::NiNode*>(a_actor->Get3D()));
#elif defined TARGET_GAME_SF
		auto loadedData = a_actor->loadedData.lock_read();
		if (*loadedData != nullptr) {
			g->On3DChange(static_cast<RE::BGSFadeNode*>(loadedData->data3D.get()));
		}
#endif
		return g;
	}

	std::shared_ptr<Graph> GraphManager::GetGraphLockless(RE::Actor* a_actor, bool a_create)
	{
		if (auto iter = state->loadedGraphs.find(a_actor); iter != state->loadedGraphs.end())
			return iter->second;

		if (auto iter = state->unloadedGraphs.find(a_actor); iter != state->unloadedGraphs.end())
			return iter->second;

		if (!a_create)
			return nullptr;

		std::shared_ptr<Graph> g = CreateGraph(a_actor);

		if (a_actor == RE::PlayerCharacter::GetSingleton()) {
			playerIsManaged = true;
		}
		state->managedRefs.insert(a_actor->formID);

		if (g->flags.none(Graph::FLAGS::kUnloaded3D)) {
			state->loadedGraphs[a_actor] = g;
		} else {
			state->unloadedGraphs[a_actor] = g;
		}
		return g;
	}

	bool GraphManager::DetachGraph(RE::TESObjectREFR* a_graphHolder)
	{
		if (!a_graphHolder)
			return false;

		if (a_graphHolder == RE::PlayerCharacter::GetSingleton()) {
			playerIsManaged = false;
		}

		std::unique_lock l{ stateLock };
		state->managedRefs.erase(a_graphHolder->formID);

		if (auto iter = state->loadedGraphs.find(a_graphHolder); iter != state->loadedGraphs.end()) {
			state->loadedGraphs.erase(iter);
			return true;
		}
		if (auto iter = state->unloadedGraphs.find(a_graphHolder); iter != state->unloadedGraphs.end()) {
			state->unloadedGraphs.erase(iter);
			return true;
		}
		return false;
	}

	void GraphManager::GetAllGraphs(std::vector<std::pair<RE::TESObjectREFR*, std::weak_ptr<Graph>>>& a_refsOut)
	{
		std::shared_lock ls{ stateLock };
		a_refsOut.clear();
		for (auto& iter : state->loadedGraphs) {
			a_refsOut.emplace_back(static_cast<RE::TESObjectREFR*>(iter.first), iter.second);
		}
		for (auto& iter : state->unloadedGraphs) {
			a_refsOut.emplace_back(static_cast<RE::TESObjectREFR*>(iter.first), iter.second);
		}
	}

	GraphManager& gm = *GraphManager::GetSingleton();

#ifdef TARGET_GAME_SF
	static Util::Call5Hook<void(RE::IAnimationGraphManagerHolder*, RE::BSAnimationUpdateData*, void*)> GraphUpdateHook(118488, 0x61, "IAnimationGraphManagerHolder::UpdateAnimationGraphManager",
		[](RE::IAnimationGraphManagerHolder* a_graphHolder, RE::BSAnimationUpdateData* a_updateData, void* a_graph) {
			std::shared_lock l{ gm.stateLock };
			auto& m = gm.state->loadedGraphs;
			if (auto iter = m.find(a_graphHolder); iter != m.end()) {
				auto& g = iter->second;

				bool modelCulled = a_updateData->modelCulled;
				a_updateData->modelCulled = modelCulled || !g->GetRequiresBaseTransforms();
				PERF_TIMER_START(start);
				GraphUpdateHook(a_graphHolder, a_updateData, a_graph);
				PERF_TIMER_END(start, baseUpdateTime);

				std::unique_lock gl{ g->lock };
				PERF_TIMER_COPY_VALUE(baseUpdateTime, g->baseUpdateMs);
				g->Update(a_updateData->timeDelta, !modelCulled, nullptr);

				if (g->GetRequiresDetach()) {
					l.unlock();
					gl.unlock();
					gm.DetachGraph(static_cast<RE::TESObjectREFR*>(a_graphHolder));
				}
			} else {
				l.unlock();
				GraphUpdateHook(a_graphHolder, a_updateData, a_graph);
			}
		});

	static Util::VFuncHook<void*(void*, void*)> PerspectiveSwitchHook(422984, 0x1, "::HandlePlayerPerspectiveSwitchForEyeTracking",
		[](void* a1, void* a2) -> void* {
			void* res = PerspectiveSwitchHook(a1, a2);

			if (auto g = gm.GetGraph(RE::PlayerCharacter::GetSingleton(), false); g) {
				std::unique_lock l{ g->lock };
				g->flags.set(Graph::FLAGS::kRequiresEyeTrackUpdate);
			}

			return res;
		});

	static Util::VFuncHook<void*(RE::Actor*, RE::NiAVObject*, bool, bool)> ActorSet3DHook(422688, 0xA8, "Actor::Set3D",
		[](RE::Actor* a_this, RE::NiAVObject* a_3d, bool a_flag1, bool a_flag2) -> void* {
			void* result = ActorSet3DHook(a_this, a_3d, a_flag1, a_flag2);
			gm.ProcessActor3DChange(a_this, a_3d);
			return result;
		});
#endif
	
#ifdef TARGET_GAME_F4
	static Util::Call5Hook<void(RE::BSAnimationGraphManager*, RE::BSAnimationUpdateData*)> GraphUpdateHook(1492656, 0x5C, "BSAnimationGraphManager::Update",
		[](RE::BSAnimationGraphManager* a_graphManager, RE::BSAnimationUpdateData* a_updateData) {
			RE::BSAutoLock managerLock{ a_graphManager->updateLock };
			if (a_graphManager->activeGraph >= a_graphManager->graph.size()) {
				return;
			}
			auto& bsGraph = a_graphManager->graph[a_graphManager->activeGraph];

			std::shared_lock l{ gm.stateLock };
			auto& m = gm.state->loadedGraphs;
			if (auto iter = m.find(bsGraph->managedRef); iter != m.end()) {
				auto& g = iter->second;
				bool visible = a_updateData->visible;
				a_updateData->visible = visible && g->GetRequiresBaseTransforms();

				PERF_TIMER_START(start);
				GraphUpdateHook(a_graphManager, a_updateData);
				PERF_TIMER_END(start, baseTime);

				std::unique_lock gl{ g->lock };
				PERF_TIMER_COPY_VALUE(baseTime, g->baseUpdateMs);
				g->Update(a_updateData->timeDelta, visible, bsGraph.get());

				if (g->GetRequiresDetach()) {
					l.unlock();
					gm.DetachGraph(bsGraph->managedRef);
				}
			} else {
				l.unlock();
				GraphUpdateHook(a_graphManager, a_updateData);
			}
		});

	static Util::VFuncHook<void*(void*, void*, void*)> PerspectiveSwitchHook(1522330, 0x1, "::HandlePlayerPerspectiveSwitchForEyeTracking",
		[](void* a1, void* a2, void* a3) -> void* {
			void* res = PerspectiveSwitchHook(a1, a2, a3);

			auto player = RE::PlayerCharacter::GetSingleton();
			if (auto g = gm.GetGraph(player, false); g) {
				std::unique_lock l{ g->lock };
				g->On3DChange(static_cast<RE::NiNode*>(player->Get3D(!player->Is3rdPersonVisible())));
			}

			return res;
		});

	static Util::DetourHook<void(RE::TESObjectREFR*, RE::NiAVObject*)> Set3DHook(678342, "TESObjectREFR::Set3DVerySimple",
		[](RE::TESObjectREFR* a_this, RE::NiAVObject* a_3d) {
			Set3DHook(a_this, a_3d);

			auto a_actor = a_this->As<RE::Actor>();
			if (!a_actor)
				return;

			gm.ProcessActor3DChange(a_actor, a_actor->Get3D());
		});

	static Util::Call5Hook<void(RE::TESObjectREFR*)> CleanUpRefHook(1008116, 0x4F, "TESObjectREFR::CleanUpRef",
		[](RE::TESObjectREFR* a_this) {
			CleanUpRefHook(a_this);

			if (!a_this)
				return;

			std::shared_lock ls{ gm.stateLock };
			if (gm.state->managedRefs.contains(a_this->formID)) {
				ls.unlock();
				logger::trace("Game is deleting managed ref {:08X}, detaching graph...", a_this->formID);
				gm.DetachGraph(a_this);
			}
		});

	static Util::DetourHook<bool(RE::PlayerCharacter*, bool, bool)> TryDialogueCameraHook(1169982, "PlayerCharacter::TryDialogueCamera",
		[](RE::PlayerCharacter* a_this, bool a_unk1, bool a_unk2) -> bool {
			if (!gm.playerIsManaged.load()) {
				return TryDialogueCameraHook(a_this, a_unk1, a_unk2);
			} else {
				return false;
			}
		});
#endif
}