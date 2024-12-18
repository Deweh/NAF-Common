#pragma once
#include "Generator.h"
#include "FileManager.h"

namespace Animation
{
	class Graph;

	struct SequencePhaseChangeEvent
	{
		bool exiting{ false };
		RE::NiPointer<RE::TESObjectREFR> target;
		size_t index{ 0 };
		std::string name{ "" };
	};

	class Sequencer
	{
	public:
		enum FLAG : uint8_t
		{
			kNone = 0,
			kForceAdvance = 1u << 0,
			kSmoothAdvance = 1u << 1,
			kLoadingNextAnim = 1u << 2,
			kPausedForLoading = 1u << 3,
			kLoop = 1u << 4,
			kSuspended = 1u << 5
		};

		struct PhaseData
		{
			FileID file;
			int32_t loopCount = 0;
			float transitionTime = 1.0f;
		};

		using phases_vector = std::vector<PhaseData>;
		using phases_iterator = phases_vector::iterator;

		Sequencer(std::vector<PhaseData>&& a_phases);

		xSE::stl::enumeration<FLAG, uint8_t> flags = kNone;
		int32_t loopsRemaining = 0;
		phases_iterator currentPhase;
		phases_vector phases;
		Graph* owner;
		std::shared_ptr<IAnimationFile> loadedAnim = nullptr;
		FileID loadingFile;

		void Update();
		bool Synchronize(Sequencer* a_owner);
		void OnAttachedToGraph(Graph* a_graph);
		void OnGraphLoaded();
		void OnGraphUnloaded();
		bool OnAnimationRequested(const FileID& a_id);
		bool OnAnimationReady(const FileID& a_id, std::shared_ptr<IAnimationFile> a_anim);
		void LoadNextAnimation();
		void AdvancePhase(bool a_init = false);
		std::optional<phases_iterator> GetNextPhase();
		void SetPhase(size_t idx);
		size_t GetPhase();
		void TransitionToLoadedAnimation();
		void SpotLoadCurrentAnimation();
		void Exit();
	};
}