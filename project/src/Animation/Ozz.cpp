#include "Ozz.h"
#include "FileManager.h"
#include "Transform.h"
#include "Generator.h"
#include "Ozz.h"

size_t Animation::OzzAnimation::GetSize()
{
	size_t result = 0;

	if (data) {
		result += data->size();
	}

	if (faceData) {
		for (auto& t : faceData->tracks) {
			result += t.size();
		}
	}

	return result;
}

void Animation::OzzAnimation::SampleBoneAnimation(float a_time, const ozz::span<ozz::math::SoaTransform>& a_transformsOut, IBasicAnimationContext* a_context)
{
	ozz::animation::SamplingJob sampleJob;
	sampleJob.animation = data.get();
	sampleJob.context = &static_cast<Context*>(a_context)->context;
	sampleJob.output = a_transformsOut;
	sampleJob.ratio = a_time;
	sampleJob.Run();
}

void Animation::OzzAnimation::SampleFaceAnimation(float a_time, const std::span<float>& a_morphsOut)
{
	ozz::animation::FloatTrackSamplingJob trackSampleJob;
	trackSampleJob.ratio = a_time;
	for (size_t i = 0; i < FACE_MORPHS_SIZE; i++) {
		trackSampleJob.result = &a_morphsOut[i];
		trackSampleJob.track = &faceData->tracks[i];
		trackSampleJob.Run();
	}
}

void Animation::OzzAnimation::SampleEventTrack(float a_time, float a_timeStep, IAnimEventHandler* a_eventHandler)
{
}

bool Animation::OzzAnimation::HasBoneAnimation()
{
	return data != nullptr;
}

bool Animation::OzzAnimation::HasFaceAnimation()
{
	return faceData != nullptr;
}

float Animation::OzzAnimation::GetDuration()
{
	if (data) {
		return data->duration();
	} else if(faceData) {
		return faceData->duration;
	} else {
		return 0.0f;
	}
}

std::unique_ptr<Animation::IBasicAnimationContext> Animation::OzzAnimation::CreateContext()
{
	if (data) {
		auto result = std::make_unique<Context>();
		result->context.Resize(data->num_tracks());
		return result;
	} else {
		return nullptr;
	}
}
