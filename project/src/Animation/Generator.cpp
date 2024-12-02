#include "Generator.h"
#include "Util/String.h"
#include "Util/Ozz.h"
#include "Procedural/PVariableNode.h"
#include "IBasicAnimation.h"

namespace Animation
{
	std::span<ozz::math::SoaTransform> Generator::Generate(PoseCache&, IAnimEventHandler* a_eventHandler) { return {}; }
	bool Generator::HasFaceAnimation() { return false; }
	void Generator::SetFaceMorphData(Face::MorphData* morphData){}
	void Generator::SetContext(const ContextData& a_context) {}
	void Generator::OnDetaching() {}
	void Generator::AdvanceTime(float deltaTime) { localTime += deltaTime * speed; }
	const std::string_view Generator::GetSourceFile() { return ""; }
	void Generator::Synchronize(Generator* a_other, float a_correctionDelta) {}
	GenType Generator::GetType() { return GenType::kBase; }
	bool Generator::RequiresRestPose() { return false; }
	size_t Generator::GetSizeBytes() { return sizeof(Generator); }

	LinearClipGenerator::LinearClipGenerator(const std::shared_ptr<IBasicAnimation>& a_anim)
	{
		anim = a_anim;
		context = anim->CreateContext();
		duration = anim->GetDuration();

		if (duration != 0.0f) {
			duration = 1.0f / duration;
		}
	}

	namespace detail
	{
		/*
		void CorrectChainScales(const ozz::animation::Skeleton* a_skeleton, const std::span<ozz::math::SoaTransform> a_transforms)
		{
			using namespace ozz::math;

			const SimdFloat4 zero = simd_float4::zero();
			const SimdFloat4 one = SetW(simd_float4::one(), zero);
			const SimdFloat4 epsilon = SetW(simd_float4::Load1(0.00001f), zero);
			SimdFloat4 worldScale = one;

			ozz::animation::IterateJointsDF(*a_skeleton, [&](int _current, int _parent) {
				SimdFloat4 currentScale = Util::Ozz::GetSoATransformScale(_current, a_transforms);

				worldScale = worldScale * currentScale;

				SimdFloat4 worldScaleDiff = _mm_sub_ps(worldScale, one);
				SimdInt4 worldScaleCheck = CmpGt(Abs(worldScaleDiff), epsilon);
				bool needsCorrection =
					AreAllTrue3(worldScaleCheck);

				if (needsCorrection) {
					SimdFloat4 scaleDiff = _mm_sub_ps(currentScale, one);
					SimdInt4 scaleCheck = CmpLt(Abs(scaleDiff), epsilon);
					bool isNeutralScale =
						AreAllTrue3(scaleCheck);

					if (isNeutralScale && _parent >= 0) {
						SimdFloat4 parentScale = Util::Ozz::GetSoATransformScale(_parent, a_transforms);
						SimdFloat4 correction = RcpEstNR(Max(worldScale, epsilon));
						Util::Ozz::ApplySoATransformScale(_parent, parentScale * correction, a_transforms);
						worldScale = one;
					}
				}
			});
		}
		*/
	}

	std::span<ozz::math::SoaTransform> LinearClipGenerator::Generate(PoseCache& a_cache, IAnimEventHandler* a_eventHandler)
	{
		if (!output.is_valid()) {
			output = a_cache.acquire_handle();
		}

		auto outSpan = output.get();
		anim->SampleBoneAnimation(localTime, ozz::make_span(outSpan), context.get());

		/*
		if (useScaleCorrection) {
			detail::CorrectChainScales(skeleton, outSpan);
		}
		*/

		if (anim->HasFaceAnimation() && faceMorphData != nullptr) {
			auto d = faceMorphData->lock();
			anim->SampleFaceAnimation(localTime, d->current);
		}

		anim->SampleEventTrack(localTime, lastTimeStep, a_eventHandler);

		return outSpan;
	}

	bool LinearClipGenerator::HasFaceAnimation()
	{
		return anim->HasFaceAnimation();
	}

	void LinearClipGenerator::SetFaceMorphData(Face::MorphData* morphData)
	{
		faceMorphData = morphData;
	}

	void LinearClipGenerator::SetContext(const ContextData& a_context)
	{
		skeleton = a_context.skeleton->data.get();
	}

	void LinearClipGenerator::AdvanceTime(float deltaTime)
	{
		if (paused) {
			lastTimeStep = 0.0f;
			return;
		}

		float timeStep = (deltaTime * speed) * duration;
		float newTime = localTime + timeStep;

		//Loops within the time ratio 0-1.
		//If the floor is non-zero (outside 0-1), then it's going to loop this frame.
		float flr = floorf(newTime);
		localTime = newTime - flr;
		looped = flr;
		lastTimeStep = timeStep;
	}

	const std::string_view LinearClipGenerator::GetSourceFile()
	{
		return anim->extra.id.file.QPath();
	}

	void LinearClipGenerator::Synchronize(Generator* a_other, float a_correctionDelta)
	{
		if (a_other->GetType() != GenType::kLinear)
			return;

		localTime = a_other->localTime + (a_correctionDelta * duration);
	}

	GenType LinearClipGenerator::GetType()
	{
		return GenType::kLinear;
	}

	size_t LinearClipGenerator::GetSizeBytes()
	{
		return sizeof(LinearClipGenerator) + context->GetSizeBytes();
	}

	ProceduralGenerator::ProceduralGenerator(const std::shared_ptr<Procedural::PGraph>& a_graph)
	{
		pGraph = a_graph;
		a_graph->InitInstanceData(pGraphInstance);
	}

	bool ProceduralGenerator::SetVariable(const std::string_view a_name, float a_value)
	{
		if (auto iter = pGraphInstance.variableMap.find(Util::String::ToLower(a_name)); iter != pGraphInstance.variableMap.end()) {
			iter->second->value = a_value;
			return true;
		} else {
			return false;
		}
	}

	float ProceduralGenerator::GetVariable(const std::string_view a_name)
	{
		if (auto iter = pGraphInstance.variableMap.find(Util::String::ToLower(a_name)); iter != pGraphInstance.variableMap.end()) {
			return iter->second->value;
		} else {
			return 0.0f;
		}
	}

	void ProceduralGenerator::ForEachVariable(const std::function<void(const std::string_view, float&)>& a_func)
	{
		for (auto& v : pGraphInstance.variableMap) {
			a_func(v.first, v.second->value);
		}
	}

	std::span<ozz::math::SoaTransform> ProceduralGenerator::Generate(PoseCache& cache, IAnimEventHandler* a_eventHandler)
	{
		return pGraph->Evaluate(pGraphInstance, cache);
	}

	void ProceduralGenerator::SetContext(const ContextData& a_context)
	{
		pGraphInstance.restPose = a_context.restPose;
		pGraphInstance.prevRootTransform = a_context.prevRootTransform;
		pGraphInstance.rootTransform = a_context.rootTransform;
		pGraphInstance.skeleton = a_context.skeleton->data.get();
		pGraphInstance.modelSpaceCache = a_context.modelSpaceCache;
	}

	void ProceduralGenerator::AdvanceTime(float deltaTime)
	{
		looped = pGraph->AdvanceTime(pGraphInstance, (deltaTime * speed) * static_cast<float>(!paused));
	}

	const std::string_view ProceduralGenerator::GetSourceFile()
	{
		return pGraph->extra.id.file.QPath();
	}

	void ProceduralGenerator::Synchronize(Generator* a_other, float a_correctionDelta)
	{
		if (a_other->GetType() != GenType::kProcedural)
			return;

		auto otherProcGen = static_cast<ProceduralGenerator*>(a_other);
		pGraph->Synchronize(pGraphInstance, otherProcGen->pGraphInstance, otherProcGen->pGraph.get(), a_correctionDelta);
	}

	GenType ProceduralGenerator::GetType()
	{
		return GenType::kProcedural;
	}

	bool ProceduralGenerator::RequiresRestPose()
	{
		return pGraph->needsRestPose;
	}

	size_t ProceduralGenerator::GetSizeBytes()
	{
		return sizeof(ProceduralGenerator) + pGraphInstance.GetSizeBytes();
	}
}