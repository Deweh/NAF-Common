#include "MorphAnimation.h"

namespace Animation
{
	constexpr float MorphToU8{ 255.0f };
	constexpr float U8ToMorph{ 1.0f / 255.0f };

	float MorphContext::Step(const MorphAnimation* a_anim, float a_ratio)
	{
		return 0.0f;
	}

	void MorphContext::Invalidate()
	{

	}

	void MorphAnimation::Sample(float a_time, MorphContext& a_context, const std::span<ozz::math::SimdFloat4>& a_output)
	{

	}
}