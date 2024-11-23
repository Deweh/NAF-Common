#pragma once

namespace Animation
{
	//constexpr size_t SimdMorphSize{ (RE::BSFaceGenAnimationData::morphSize + 3) / 4 };

	class MorphAnimation;

	template <typename T>
	struct AllocatedArray
	{
		std::unique_ptr<T[]> data;
		size_t size{ 0 };

		void allocate(size_t a_size)
		{
			data = std::make_unique<T>(a_size);
		}

		std::span<T> as_span() const
		{
			return std::span<T>(data.get(), size);
		}
	};

	struct MorphContext
	{
		struct InterpSimdFloat
		{
			ozz::math::SimdFloat4 first;
			ozz::math::SimdFloat4 second;
		};

		float prevRatio = -1.0f;
		AllocatedArray<InterpSimdFloat> cachedKeys;
		AllocatedArray<uint16_t> entries;
		const MorphAnimation* animation{ nullptr };

		float Step(const MorphAnimation* a_anim, float a_ratio);
		void Invalidate();
	};

	struct MorphTrack
	{
		AllocatedArray<uint16_t> keys;
		AllocatedArray<uint16_t> timeIdxs;
	};

	class MorphAnimation
	{
		//std::array<MorphTrack, RE::BSFaceGenAnimationData::morphSize> tracks;
		AllocatedArray<float> timepoints;
		float duration;

		void Sample(float a_time, MorphContext& a_context, const std::span<ozz::math::SimdFloat4>& a_output);
	};
}