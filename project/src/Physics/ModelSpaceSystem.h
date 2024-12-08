#pragma once

namespace Physics
{
	class ModelSpaceSystem
	{
	public:
		static constexpr float FIXED_TIMESTEP{ 1.0f / 60.0f };
		static constexpr uint8_t MAX_STEPS_PER_RUN{ 4 };

		struct SimulationStepData
		{
			ozz::math::SimdFloat4 rootAcceleration = ozz::math::simd_float4::zero();
			float interpolationRatio = 0.0f;
			uint8_t requiredSteps = 0;
		};

		ozz::math::SimdFloat4 accumulatedMovement = ozz::math::simd_float4::zero();
		ozz::math::SimdFloat4 prevRootVelocity = ozz::math::simd_float4::zero();
		float accumulatedTime = 0.0f;
		float movementTime = 0.0f;
		SimulationStepData simData;

		void Update(float a_deltaTime, const ozz::math::Float4x4& a_rootTransform, const ozz::math::Float4x4& a_prevRootTransform);
	};
}