#pragma once
#include "Transform.h"
#include "Ozz.h"
#include "Face/Manager.h"
#include "Procedural/PGraph.h"
#include "SyncInstance.h"
#include "IAnimEventHandler.h"

namespace Animation
{
	class IBasicAnimation;
	class IBasicAnimationContext;

	enum class GenType : uint8_t
	{
		kBase,
		kLinear,
		kProcedural
	};

	class Generator
	{
	public:
		struct ContextData
		{
			std::span<ozz::math::Float4x4> modelSpaceCache;
			ozz::math::Float4x4* prevRootTransform;
			ozz::math::Float4x4* rootTransform;
			PoseCache::Handle* restPose;
			const OzzSkeleton* skeleton;
		};

		bool looped = false;
		bool paused = false;
		float localTime = 0.0f;
		float duration = 0.0f;
		float speed = 1.0f;

		virtual std::span<ozz::math::SoaTransform> Generate(PoseCache& a_cache, IAnimEventHandler* a_eventHandler);
		virtual bool HasFaceAnimation();
		virtual void SetFaceMorphData(Face::MorphData* a_morphData);
		virtual void SetContext(const ContextData& a_context);
		virtual void OnDetaching();
		virtual void AdvanceTime(float a_deltaTime);
		virtual const std::string_view GetSourceFile();
		virtual void Synchronize(Generator* a_other, float a_correctionDelta);
		virtual GenType GetType();
		virtual bool RequiresRestPose();
		virtual size_t GetSizeBytes();

		virtual ~Generator() = default;
	};

	class ProceduralGenerator : public Generator
	{
	public:
		std::shared_ptr<Procedural::PGraph> pGraph;
		Procedural::PGraph::InstanceData pGraphInstance;

		ProceduralGenerator(const std::shared_ptr<Procedural::PGraph>& a_graph);

		bool SetVariable(const std::string_view a_name, float a_value);
		float GetVariable(const std::string_view a_name);
		void ForEachVariable(const std::function<void(const std::string_view, float&)>& a_func);
		virtual std::span<ozz::math::SoaTransform> Generate(PoseCache& a_cache, IAnimEventHandler* a_eventHandler) override;
		virtual void SetContext(const ContextData& a_context) override;
		virtual void AdvanceTime(float deltaTime) override;
		virtual const std::string_view GetSourceFile() override;
		virtual void Synchronize(Generator* a_other, float a_correctionDelta) override;
		virtual GenType GetType() override;
		virtual bool RequiresRestPose() override;
		virtual size_t GetSizeBytes() override;

		virtual ~ProceduralGenerator() = default;
	};

	class LinearClipGenerator : public Generator
	{
	public:
		PoseCache::Handle output;
		std::unique_ptr<IBasicAnimationContext> context = nullptr;
		std::shared_ptr<IBasicAnimation> anim = nullptr;
		Face::MorphData* faceMorphData = nullptr;
		float lastTimeStep = 0.0f;
		const ozz::animation::Skeleton* skeleton = nullptr;

		LinearClipGenerator(const std::shared_ptr<IBasicAnimation>& a_anim);

		virtual std::span<ozz::math::SoaTransform> Generate(PoseCache& cache, IAnimEventHandler* a_eventHandler) override;
		virtual bool HasFaceAnimation() override;
		virtual void SetFaceMorphData(Face::MorphData* morphData) override;
		virtual void SetContext(const ContextData& a_context) override;
		virtual void AdvanceTime(float deltaTime) override;
		virtual const std::string_view GetSourceFile() override;
		virtual void Synchronize(Generator* a_other, float a_correctionDelta);
		virtual GenType GetType();
		virtual size_t GetSizeBytes() override;

		virtual ~LinearClipGenerator() = default;
	};
}