#include "Correctives.h"
#include "Animation/Ozz.h"
#include "Manager.h"
#include "Settings/Settings.h"

#if defined TARGET_GAME_F4
#include "Backend.h"

namespace Morph::Correctives
{
	namespace
	{
		// Corrective outputs this small count as 0, so an actor near rest doesn't get morph values (and a copy of its
		// meshes) for nothing.
		constexpr float kNegligible = 1e-4f;

		struct ActorEntry
		{
			const RE::NiAVObject* root = nullptr;  // Compared only.
			const RE::TESRace* race = nullptr;     // Compared only.
			uint64_t generation = UINT64_MAX;
			std::shared_ptr<const Animation::OzzSkeleton> skeleton;
			std::shared_ptr<DriverRuntime> runtime;
			// Each joint's node in the actor's 3D, or null where its model lacks it (the rest local is used then).
			std::vector<RE::NiPointer<RE::NiAVObject>> nodes;
			bool written = false;  // Has corrective values in the manager.
			uint32_t seen = 0;
		};

		struct State
		{
			std::unordered_map<uint32_t, ActorEntry> actors;
			uint32_t frame = 0;
		};

		State& GetState()
		{
			static State state;
			return state;
		}

		void Clear(uint32_t a_formId, ActorEntry& a_entry)
		{
			if (!a_entry.written)
				return;
			if (auto morphs = Manager::GetSingleton()->Get(a_formId, false))
				morphs->Clear(Layer::kCorrective);
			a_entry.written = false;
		}

		// Resolves the actor's drivers against its skeleton and its 3D, again when either or the drivers change. Returns
		// false if it has no drivers.
		bool Prepare(ActorEntry& a_entry, RE::Actor* a_actor, RE::NiAVObject* a_root)
		{
			auto& library = Manager::GetSingleton()->GetDrivers();
			const uint64_t generation = library.GetGeneration();
			if (a_entry.root == a_root && a_entry.race == a_actor->race && a_entry.generation == generation)
				return a_entry.runtime && !a_entry.runtime->Empty();

			a_entry.root = a_root;
			a_entry.race = a_actor->race;
			a_entry.nodes.clear();
			a_entry.skeleton = Settings::GetSkeleton(a_actor);
			a_entry.runtime = a_entry.skeleton ? DriverRuntime::Build(*a_entry.skeleton, library.Get(a_entry.skeleton->name), library.GetGeneration()) : nullptr;
			a_entry.generation = a_entry.runtime ? a_entry.runtime->GetGeneration() : generation;
			if (!a_entry.runtime || a_entry.runtime->Empty())
				return false;
			for (const auto name : a_entry.skeleton->data->joint_names()) {
				a_entry.nodes.emplace_back(a_root->GetObjectByName(name));
			}
			return true;
		}

		// The drivers on the actor's current pose. The nodes' locals are what the animation update just wrote, laid out
		// as the graph writes them (Animation::Graph::On3DChange).
		void Evaluate(const ActorEntry& a_entry, Values& a_out, std::vector<DriverDebug>* a_debug = nullptr)
		{
			const auto& runtime = *a_entry.runtime;
			runtime.Evaluate([&](int32_t i) {
				const auto& node = a_entry.nodes[i];
				return node ? *reinterpret_cast<const ozz::math::Float4x4*>(&node->local) : runtime.GetRestLocal(i);
			},
				a_out, a_debug);
		}

		void Update(State& a_state, RE::Actor* a_actor)
		{
			auto root = a_actor->Get3D(false);
			if (!root)
				return;
			const uint32_t formId = a_actor->GetFormID();
			auto& entry = a_state.actors[formId];
			entry.seen = a_state.frame;
			// Off screen, the values stay as they were.
			if (!Backend::IsVisible(a_actor))
				return;
			if (!Prepare(entry, a_actor, root)) {
				Clear(formId, entry);
				return;
			}

			Values values;
			Evaluate(entry, values);
			std::erase_if(values, [](const auto& a_value) { return std::abs(a_value.second) < kNegligible; });
			auto manager = Manager::GetSingleton();
			if (auto morphs = manager->Get(formId, !values.empty())) {
				morphs->SetAll(Layer::kCorrective, "", values);
				entry.written = !values.empty();
			}
		}
	}

	void Update()
	{
		auto& state = GetState();
		state.frame++;

		if (!Manager::GetSingleton()->GetCorrectivesEnabled()) {
			for (auto& [formId, entry] : state.actors) Clear(formId, entry);
			state.actors.clear();
			return;
		}

		if (auto player = RE::PlayerCharacter::GetSingleton())
			Update(state, player);
		if (auto processLists = RE::ProcessLists::GetSingleton()) {
			for (const auto& handle : processLists->highActorHandles) {
				if (auto actor = handle.get())
					Update(state, actor.get());
			}
		}

		// Actors that left the high process or lost their 3D.
		std::erase_if(state.actors, [&](auto& a_pair) {
			if (a_pair.second.seen == state.frame)
				return false;
			Clear(a_pair.first, a_pair.second);
			return true;
		});
	}

	std::vector<DriverDebug> Debug(RE::Actor* a_actor)
	{
		std::vector<DriverDebug> result;
		auto root = a_actor ? a_actor->Get3D(false) : nullptr;
		if (!root)
			return result;
		// A scratch entry, so the console doesn't disturb the per-frame one.
		ActorEntry entry;
		if (!Prepare(entry, a_actor, root))
			return result;
		Values values;
		Evaluate(entry, values, &result);
		return result;
	}
}
#else
namespace Morph::Correctives
{
	void Update() {}

	std::vector<DriverDebug> Debug(RE::Actor*)
	{
		return {};
	}
}
#endif
