#pragma once
#include "Ozz.h"
#include "Graph.h"
#include "Sequencer.h"
#include "Util/Event.h"

namespace Animation
{
	class Graph;
	class Generator;

	class GraphManager :
		public Util::Event::Dispatcher<SequencePhaseChangeEvent>
	{
	public:
		struct PersistentState
		{
			std::unordered_map<RE::IAnimationGraphManagerHolder*, std::shared_ptr<Graph>> loadedGraphs;
			std::unordered_map<RE::IAnimationGraphManagerHolder*, std::shared_ptr<Graph>> unloadedGraphs;
			std::unordered_set<uint32_t> managedRefs;
		};

		struct SaveData
		{
			struct BlendGraphVariable
			{
				std::string name;
				float value;
			};

			struct RefData
			{
				uint32_t formId;
				uint32_t syncOwner;
				std::string animFile;
				float localTime;
				float speedMult;
				std::vector<BlendGraphVariable> blendVars;
			};

			std::vector<RefData> refs;
		};

		std::shared_mutex stateLock;
		std::unique_ptr<PersistentState> state = std::make_unique<PersistentState>();
		std::atomic<bool> playerIsManaged = false;

		void CreateSaveData(SaveData& a_data);
		void LoadSaveData(const SaveData& a_data);

		static GraphManager* GetSingleton();
		bool LoadAndStartAnimation(RE::Actor* a_actor, const std::string_view a_filePath, const std::string_view a_animId = "", float a_transitionTime = 1.0f);
		void StartSequence(RE::Actor* a_actor, std::vector<Sequencer::PhaseData>&& a_phaseData);
		bool AdvanceSequence(RE::Actor* a_actor, bool a_smooth = true);
		bool SetSequencePhase(RE::Actor* a_actor, size_t a_idx);
		size_t GetSequencePhase(RE::Actor* a_actor);
		bool SetAnimationSpeed(RE::Actor* a_actor, float a_speed);
		float GetAnimationSpeed(RE::Actor* a_actor);
		void SyncGraphs(const std::vector<RE::Actor*>& a_actors);
		void StopSyncing(RE::Actor* a_actor);
		bool SetProceduralVariable(RE::Actor* a_actor, const std::string_view a_name, float a_value);
		float GetProceduralVariable(RE::Actor* a_actor, const std::string_view a_name);
		void SetGraphControlsPosition(RE::Actor* a_actor, bool a_controls);
		bool AttachGenerator(RE::Actor* a_actor, std::unique_ptr<Generator> a_gen, float a_transitionTime);
		bool DetachGenerator(RE::Actor* a_actor, float a_transitionTime);
		bool DetachGraph(RE::TESObjectREFR* a_graphHolder);

		template <typename F>
		inline bool VisitGraph(RE::Actor* a_actor, F a_visitFunc, bool a_create = false)
		{
			if (!a_actor)
				return false;

			auto g = GetGraph(a_actor, a_create);
			if (!g) {
				return false;
			}

			std::unique_lock l{ g->lock };
			return a_visitFunc(g.get());
		}
		
		void GetAllGraphs(std::vector<std::pair<RE::TESObjectREFR*, std::weak_ptr<Graph>>>& a_refsOut);
		void Reset();
		std::shared_ptr<Graph> GetGraph(RE::Actor* a_actor, bool a_create);
		void SetGraphLoaded(RE::IAnimationGraphManagerHolder* a_graph, bool a_loaded);
		void ProcessActor3DChange(RE::Actor* a_actor, RE::NiAVObject* a_3d);

	private:
		std::shared_ptr<Graph> GetGraphLockless(RE::Actor* a_actor, bool a_create);
	};
}