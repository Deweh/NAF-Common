#include "GameBridge.h"

#include "SceneObjects.h"

#include "Animation/Generator.h"
#include "Animation/GraphManager.h"
#include "Settings/Settings.h"
#include "Util/String.h"

namespace Animation::Studio
{
	namespace
	{
		// Plays an actor's Studio pose: each graph update, on the animation thread, it copies the pose buffer's
		// published (front) pose into the graph's output, along with the bone mask.
		class StudioGenerator final : public Generator
		{
		public:
			explicit StudioGenerator(std::shared_ptr<const SharedPose> a_pose) :
				m_pose(std::move(a_pose))
			{
				paused = true;
			}

			std::span<ozz::math::SoaTransform> Generate(PoseCache& a_cache, IAnimEventHandler*) override
			{
				if (!m_output.is_valid()) m_output = a_cache.acquire_handle();
				auto destination = m_output.get();
				m_pose->ReadFront([&](const SharedPose::Buffer& a_pose) {
					std::copy_n(a_pose.begin(), (std::min)(destination.size(), a_pose.size()), destination.begin());
				});
				m_pose->ReadMask(m_maskVersion, [&](const std::vector<bool>& a_mask) { m_mask = a_mask; });
				return destination;
			}

			void AdvanceTime(float) override {}
			size_t GetSizeBytes() override { return sizeof(StudioGenerator) + m_mask.capacity() / 8; }
			const std::vector<bool>* GetBoneMask() override { return m_mask.empty() ? nullptr : &m_mask; }

		private:
			PoseCache::Handle m_output;
			std::shared_ptr<const SharedPose> m_pose;
			std::vector<bool> m_mask;  // Copied on the animation thread, where the graph reads it.
			uint32_t m_maskVersion{ 0 };
		};
	}

	std::shared_ptr<const OzzSkeleton> GetActorSkeleton(RE::Actor* a_actor)
	{
		std::shared_ptr<const OzzSkeleton> skeleton;
		GraphManager::GetSingleton()->VisitGraph(a_actor, [&](Graph* a_graph) {
			skeleton = a_graph->skeleton;
			return skeleton != nullptr;
		}, true);
		return skeleton && skeleton->data ? skeleton : nullptr;
	}

	std::string GetActorSkeletonId(RE::Actor* a_actor)
	{
		return Settings::GetSkeletonIdentifier(a_actor).c_str();
	}

	std::string GetActorDisplayName(RE::Actor* a_actor)
	{
		if (const char* name = a_actor->GetDisplayFullName(); name && name[0] != '\0') return name;
		return std::format("Actor [{:08X}]", a_actor->GetFormID());
	}

	std::expected<void, std::string> CheckActorCompatibility(std::string_view a_skeletonId, RE::SEX a_gender, RE::Actor* a_actor)
	{
		if (!a_actor) return std::unexpected("No actor was supplied.");
		if (!a_skeletonId.empty() && !Util::String::CaseInsensitiveCompare(a_skeletonId, GetActorSkeletonId(a_actor))) {
			return std::unexpected("The actor uses a different skeleton.");
		}
		if (const auto npc = a_actor->GetNPC(); npc && a_gender != RE::SEX::kNone && npc->GetSex() != a_gender) {
			return std::unexpected("The actor has the wrong gender for this slot.");
		}
		return {};
	}

	ScenePlacement GetWorldPlacement(const RE::TESObjectREFR* a_reference)
	{
		if (!a_reference) return {};
		const auto& location = a_reference->data.location;
		return { { location.x, location.y, location.z }, a_reference->data.angle.z };
	}

	void HoldActor(RE::Actor* a_actor, const ScenePlacement& a_world)
	{
		GraphManager::GetSingleton()->VisitGraph(a_actor, [&](Graph* a_graph) {
			a_graph->rootTransform.translate = { a_world.translation.x, a_world.translation.y, a_world.translation.z };
			a_graph->rootTransform.rotate = { 0.0f, 0.0f, a_world.heading };
			a_graph->flags.set(Graph::FLAGS::kLockHeading);
			if (a_graph->flags.none(Graph::FLAGS::kPositionLocked)) a_graph->SetLockPosition(true);
			return true;
		});
	}

	void AttachStudioGenerator(ActorObject& a_actor)
	{
		a_actor.PublishBoneMask();
		if (!a_actor.actor || !a_actor.pose) return;
		GraphManager::GetSingleton()->AttachGenerator(a_actor.actor.get(), std::make_unique<StudioGenerator>(a_actor.pose), 0.0f);
	}

	void DetachStudioGenerator(ActorObject& a_actor, float a_transitionTime)
	{
		if (!a_actor.actor) return;
		GraphManager::GetSingleton()->DetachGenerator(a_actor.actor.get(), a_transitionTime);
	}
}
