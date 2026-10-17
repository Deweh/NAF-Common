#include "Backend.h"
#include "Manager.h"
#include "ShapeMorpher.h"
#include "TriFile.h"
#include "Util/General.h"

#if defined TARGET_GAME_F4
// First: it orders the D3D headers around Renderer.h.
#include "UI/DX11Renderer.h"

namespace Morph::Backend
{
	namespace
	{
		struct ActorEntry
		{
			const RE::NiAVObject* root = nullptr;  // Compared only.
			uint64_t appliedVersion = UINT64_MAX;
			uint64_t dataGeneration = UINT64_MAX;
			bool pending = false;    // A shape is waiting for its vertex range or its topology.
			bool needsBind = false;  // A morph file was still loading at the last bind.
			bool posed = false;      // A shape's shading follows the pose, so it's visited every frame.
			std::vector<std::unique_ptr<ShapeMorpher>> shapes;
		};

		struct State
		{
			// Actors whose equipment was attached since the last flush (from any thread).
			std::mutex attachedLock;
			std::unordered_set<uint32_t> attached;

			// Main thread only.
			std::unordered_map<uint32_t, ActorEntry> actors;
			uint64_t dataGeneration = UINT64_MAX;
		};

		State& GetState()
		{
			static State state;
			return state;
		}

		// Whether the actor's 3D is on screen: its root fade node passed culling this frame or the last, and isn't faded
		// out. The culling pass stamps the frame number on each fade node it doesn't cull (BSFadeNode::OnVisible, from
		// BSCullingGroup::ProcessNodeCuller, before it looks at fading), and the game's animation scheduler counts a
		// reference stamped no earlier than the previous frame as visible (ShouldUpdateFadeNode). OnVisible doesn't keep
		// the stamp while fading is off, or on a node that ignores fading once it's fully faded in, so those count as
		// visible. Faded out is as BSFadeNode::IsVisible has it. Main thread.
		bool IsVisible(RE::Actor* a_actor)
		{
			static REL::Relocation<const int32_t*> frame{ REL::ID(734919) };      // Incremented by Main::OnIdle.
			static REL::Relocation<const bool*> fadeUpdates{ REL::ID(1445134) };  // Whether OnVisible updates fading.
			static REL::Relocation<const bool*> fadeHides{ REL::ID(1220201) };    // Whether a faded-out node is hidden.
			constexpr uint64_t kIgnoreFade = 1ull << 15;

			auto root = a_actor ? a_actor->Get3D(false) : nullptr;
			auto fade = root ? root->IsFadeNode() : nullptr;
			if (!fade)
				return true;
			const bool ignoresFade = (fade->GetFlags() & kIgnoreFade) != 0;
			if (!ignoresFade && *fadeHides && fade->currentFade <= 0.0f)
				return false;
			return ignoresFade || !*fadeUpdates || fade->frameCounter >= *frame - 1;
		}

		const RE::BSFixedString& GetBodyTriKey()
		{
			static RE::BSFixedString key{ "BODYTRI" };
			return key;
		}

		// NiObjectNET::AddExtraData(NiExtraData*): adds it under its own name, taking a reference.
		bool AddExtraData(RE::NiObjectNET* a_object, RE::NiExtraData* a_data)
		{
			using func_t = bool (*)(RE::NiObjectNET*, RE::NiExtraData*);
			static REL::Relocation<func_t> func{ REL::ID(573050) };
			return func(a_object, a_data);
		}

		template <class F>
		void ForEachTriShape(RE::NiAVObject* a_root, F&& a_func)
		{
			std::vector<RE::NiAVObject*> stack{ a_root };
			while (!stack.empty()) {
				auto object = stack.back();
				stack.pop_back();
				if (!object)
					continue;
				if (auto node = object->IsNode()) {
					for (auto& child : node->children) {
						if (child)
							stack.push_back(child.get());
					}
					continue;
				}
				auto shape = object->IsTriShape();
				if (shape && !object->IsDynamicTriShape())
					a_func(shape);
			}
		}

		// The TRI file of a mesh is named by BODYTRI extra data on its root, but attaching the mesh to an actor moves its
		// skinned shapes out from under the root, which is then dropped. So each shape gets the same extra data first.
		void TagShapes(RE::NiNode* a_object)
		{
			auto extra = a_object->GetExtraData(GetBodyTriKey());
			if (!extra)
				return;
			ForEachTriShape(a_object, [&](RE::BSTriShape* a_shape) {
				if (!a_shape->GetExtraData(GetBodyTriKey()))
					AddExtraData(a_shape, extra);
			});
		}

		// A shape's TRI sources. TRI files name shapes as the mesh file does, but a shape's name in game can differ from
		// that, so when its BODYTRI file has a single shape, that's the one, and its name is what the extra files are
		// matched by.
		// Sets a_pending if a file the shape needs is still loading; the sources are incomplete then.
		std::vector<ShapeMorpher::Source> FindSources(RE::BSTriShape* a_shape, const TriLibrary::ExtraFiles& a_extraFiles, bool& a_pending)
		{
			std::vector<ShapeMorpher::Source> sources;
			std::string name = ToLowerName(a_shape->name.c_str());
			if (auto extra = ::netimmerse_cast<RE::NiStringExtraData*>(a_shape->GetExtraData(GetBodyTriKey())); extra && !extra->data.empty()) {
				bool loading = false;
				auto file = Manager::GetSingleton()->GetTriFiles().GetGameFile(extra->data.c_str(), loading);
				if (loading) {
					a_pending = true;
					return {};
				}
				if (file) {
					if (auto shape = file->FindShape(name)) {
						sources.push_back({ file, shape });
					} else if (file->GetShapes().size() == 1) {
						const auto& [fileName, fileShape] = *file->GetShapes().begin();
						logger::info("Morphs: shape '{}' isn't named in {}; using its only shape, '{}'.", a_shape->name.c_str(), file->GetName(), fileName);
						name = fileName;
						sources.push_back({ file, &fileShape });
					} else {
						std::string names;
						for (const auto& [fileName, fileShape] : file->GetShapes()) names += (names.empty() ? "" : ", ") + fileName;
						logger::warn("Morphs: shape '{}' isn't named in {} (it has {}).", a_shape->name.c_str(), file->GetName(), names);
					}
				}
			}
			for (const auto& file : a_extraFiles) {
				if (auto shape = file->FindShape(name))
					sources.push_back({ file, shape });
			}
			return sources;
		}

		// Matches the actor's current shapes with morphers: keeps those still valid (re-reading their morph files if
		// a_reload), makes new ones for new shapes.
		// Shapes whose morph files are still loading are left as they are and bound on a later frame (needsBind).
		void Bind(ActorEntry& a_entry, RE::NiAVObject* a_root, bool a_reload)
		{
			bool pending = false;
			const auto extraFiles = Manager::GetSingleton()->GetTriFiles().GetExtraFiles(pending);
			if (pending) {
				a_entry.needsBind = true;
				return;
			}
			std::vector<std::unique_ptr<ShapeMorpher>> bound;
			ForEachTriShape(a_root, [&](RE::BSTriShape* a_shape) {
				auto existing = std::ranges::find_if(a_entry.shapes, [&](const auto& a_morpher) { return a_morpher && a_morpher->GetShape() == a_shape; });
				const bool keep = existing != a_entry.shapes.end() && (*existing)->IsValid();
				if (keep && !a_reload) {
					bound.push_back(std::move(*existing));
					return;
				}
				bool shapePending = false;
				auto sources = FindSources(a_shape, extraFiles, shapePending);
				if (shapePending) {
					pending = true;
					if (keep)
						bound.push_back(std::move(*existing));
					return;
				}
				if (keep) {
					// A shape whose file no longer has it keeps its morpher, with no sources, so its morphs are undone.
					(*existing)->SetSources(std::move(sources));
					bound.push_back(std::move(*existing));
				} else if (!sources.empty()) {
					bound.push_back(std::make_unique<ShapeMorpher>(a_shape, std::move(sources)));
				}
			});
			a_entry.shapes = std::move(bound);
			a_entry.root = a_root;
			a_entry.needsBind = pending;
		}

		ActorEntry* Prepare(State& a_state, uint32_t a_formId, RE::Actor* a_actor, bool a_forceBind)
		{
			auto& entry = a_state.actors[a_formId];
			auto root = a_actor ? a_actor->Get3D(false) : nullptr;
			if (!root) {
				entry.shapes.clear();
				entry.root = nullptr;
				entry.appliedVersion = UINT64_MAX;
				return nullptr;
			}
			std::erase_if(entry.shapes, [](const auto& a_morpher) { return !a_morpher->IsValid(); });
			const bool reload = entry.dataGeneration != a_state.dataGeneration;
			if (a_forceBind || reload || entry.needsBind || root != entry.root) {
				Bind(entry, root, reload);
				// A reload isn't done until every file it needed has loaded.
				if (!entry.needsBind)
					entry.dataGeneration = a_state.dataGeneration;
			}
			return &entry;
		}

		void Flush()
		{
			auto& state = GetState();
			auto renderer = RE::BSGraphics::RendererData::GetSingleton();
			auto context = renderer ? reinterpret_cast<ID3D11DeviceContext*>(renderer->context) : nullptr;
			if (!context)
				return;

			auto manager = Manager::GetSingleton();
			const uint64_t generation = manager->GetDataGeneration();
			// Reload already dropped the old files; each actor rebinds to the new ones.
			if (generation != state.dataGeneration)
				state.dataGeneration = generation;
			std::unordered_set<uint32_t> attached;
			{
				std::scoped_lock l{ state.attachedLock };
				attached.swap(state.attached);
			}

			manager->ForEach([&](uint32_t a_formId, const std::shared_ptr<ActorMorphs>& a_morphs) {
				const uint64_t version = a_morphs->GetVersion();
				auto existing = state.actors.find(a_formId);
				const bool wasAttached = attached.contains(a_formId);
				if (existing != state.actors.end()) {
					const auto& e = existing->second;
					if (!e.pending && !e.posed && !wasAttached && e.appliedVersion == version && e.dataGeneration == state.dataGeneration)
						return;
				}
				auto actor = RE::TESForm::GetFormByID<RE::Actor>(a_formId);
				// Posed shading follows the pose, so it's redone every frame, but only while the actor is visible.
				if (existing != state.actors.end()) {
					const auto& e = existing->second;
					if (!e.pending && !wasAttached && e.appliedVersion == version && e.dataGeneration == state.dataGeneration && !IsVisible(actor))
						return;
				}
				auto entry = Prepare(state, a_formId, actor, wasAttached);
				if (!entry)
					return;
				const auto totals = a_morphs->GetTotals();
				bool pending = false, posed = false;
				for (auto& shape : entry->shapes) {
					if (!shape->Apply(totals, context))
						pending = true;
					posed = posed || shape->IsPoseDependent();
				}
				entry->appliedVersion = version;
				entry->pending = pending || entry->needsBind;
				entry->posed = posed;
			});
		}

		// BipedAnim::AttachSkinnedObject(NiNode* object, NiNode* parent, BIPED_OBJECT slot, bool): moves a loaded
		// mesh's skinned shapes under the actor's skeleton.
		Util::DetourHook<RE::NiAVObject*(RE::BipedAnim*, RE::NiNode*, RE::NiNode*, int32_t, bool)> AttachHook(1575810, "BipedAnim::AttachSkinnedObject",
			[](RE::BipedAnim* a_biped, RE::NiNode* a_object, RE::NiNode* a_parent, int32_t a_slot, bool a_flag) -> RE::NiAVObject* {
				if (a_object)
					TagShapes(a_object);
				auto result = AttachHook(a_biped, a_object, a_parent, a_slot, a_flag);
				if (a_biped) {
					if (auto refr = a_biped->actorRef.get()) {
						auto& state = GetState();
						std::scoped_lock l{ state.attachedLock };
						state.attached.insert(refr->GetFormID());
					}
				}
				return result;
			});

		// Main::DrawWorld_PreRender(int): sets up the world draw, on the main thread with the renderer locked. Morphs
		// are uploaded here so the frame about to be drawn has them.
		Util::DetourHook<void(void*, int32_t)> PreRenderHook(378257, "Main::DrawWorld_PreRender",
			[](void* a_main, int32_t a_destination) {
				Flush();
				PreRenderHook(a_main, a_destination);
			});
	}

	std::vector<ShapeInfo> GetShapes(RE::Actor* a_actor)
	{
		std::vector<ShapeInfo> result;
		if (!a_actor)
			return result;
		auto& state = GetState();
		auto entry = Prepare(state, a_actor->GetFormID(), a_actor, true);
		if (!entry)
			return result;
		for (const auto& morpher : entry->shapes) {
			auto& info = result.emplace_back();
			auto shape = morpher->GetShape();
			info.name = shape->name.c_str();
			info.vertices = shape->numVertices;
			info.takenOver = morpher->IsTakenOver();
			info.state = morpher->GetStateName();
			info.active = morpher->GetActiveCount();
			info.moved = morpher->GetMovedCount();
			morpher->ListMorphs(info.morphs);
			std::ranges::sort(info.morphs);
		}
		// Bound outside a flush: make sure the next one applies the current values to any new shapes.
		entry->appliedVersion = UINT64_MAX;
		return result;
	}
}
#else
namespace Morph::Backend
{
	std::vector<ShapeInfo> GetShapes(RE::Actor*)
	{
		return {};
	}
}
#endif
