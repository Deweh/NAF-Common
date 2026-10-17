#pragma once
#include "Drivers.h"
#include "TriFile.h"
#include "Values.h"

namespace Morph
{
	// The morph system's single owner. Everything the system shares lives here, for the life of the plugin:
	//   - each actor's morph values (ActorMorphs), by form ID;
	//   - the corrective drivers read from Data/NAF/Correctives (DriverLibrary);
	//   - the TRI files read for meshes (TriLibrary);
	//   - the data generation, bumped when morph files and drivers are re-read;
	//   - whether correctives run.
	//
	// Everything else is derived from this and owned by whoever derives it, rebuilt when the data generation or driver
	// generation changes:
	//   - a Graph owns its skeleton's DriverRuntime (resolved drivers), and keeps its target's ActorMorphs;
	//   - the frontend's Backend owns its per-actor bound shapes (ShapeMorpher), which keep the TriFiles they read from.
	//
	// ActorMorphs entries are shared: a holder's pointer stays valid after Remove, but its writes no longer reach the
	// frontend, which only reads entries still in the manager. Entries are created on first use and not removed yet.
	class Manager
	{
	public:
		static Manager* GetSingleton();

		// The actor's morphs, created if a_create.
		std::shared_ptr<ActorMorphs> Get(uint32_t a_formId, bool a_create);
		void Remove(uint32_t a_formId);
		void ForEach(const std::function<void(uint32_t, const std::shared_ptr<ActorMorphs>&)>& a_func) const;

		[[nodiscard]] DriverLibrary& GetDrivers() { return drivers; }
		[[nodiscard]] TriLibrary& GetTriFiles() { return triFiles; }

		// Bumped by Reload, so frontends rebind their shapes to the re-read morph files.
		[[nodiscard]] uint64_t GetDataGeneration() const { return dataGeneration.load(std::memory_order_acquire); }
		// Re-reads drivers and drops the loaded morph files, which are read again on next use.
		void Reload();

		// Whether actors' corrective drivers run (on by default). While off, graphs clear their corrective morphs.
		void SetCorrectivesEnabled(bool a_enabled) { correctivesEnabled.store(a_enabled, std::memory_order_relaxed); }
		[[nodiscard]] bool GetCorrectivesEnabled() const { return correctivesEnabled.load(std::memory_order_relaxed); }

	private:
		mutable std::shared_mutex lock;
		std::unordered_map<uint32_t, std::shared_ptr<ActorMorphs>> actors;
		DriverLibrary drivers;
		TriLibrary triFiles;
		std::atomic<uint64_t> dataGeneration{ 0 };
		std::atomic<bool> correctivesEnabled{ true };
	};

	// Not implemented yet. Correctives currently run only for actors with a NAF graph (Graph::UpdateCorrectiveMorphs),
	// which already knows when its actor is visible, so off-screen actors cost nothing. The plan for every loaded actor:
	// once per frame on the main thread, walk the high process actors (ProcessLists::highActorHandles) that are 3D-loaded
	// and visible, skip any with a NAF graph, read the driver bones' locals straight from their skeleton nodes
	// (NiAVObject::local, after the game's animation update), evaluate the race skeleton's drivers the same way and write
	// Layer::kCorrective. Behind a setting, with a cap on actor count, since each morphed actor gets its own copy of every
	// morphed mesh's vertices.
	void UpdateActorsWithoutGraphs();
}
