#include "ModelSpaceSystem.h"

namespace Physics
{
	void ModelSpaceSystem::Update(float a_deltaTime, const ozz::math::Float4x4& a_rootTransform, const ozz::math::Float4x4& a_prevRootTransform)
	{
		using namespace ozz::math;
		SimdInt4 invertible;
		const Float4x4 rootInverseWS = Invert(a_rootTransform, &invertible);

		// Get root movement transformed into model-space.
		const SimdFloat4 relativeRoot = a_rootTransform.cols[3] - a_prevRootTransform.cols[3];
		const SimdFloat4 worldMovementMS = TransformVector(rootInverseWS, relativeRoot);
		accumulatedMovement = accumulatedMovement + worldMovementMS;

		// Add to accumulated time.
		movementTime += a_deltaTime;
		accumulatedTime += a_deltaTime;

		uint8_t stepsToPerform = accumulatedTime > 0.0f ? static_cast<uint8_t>(accumulatedTime / FIXED_TIMESTEP) : 0;
		stepsToPerform = std::min(stepsToPerform, MAX_STEPS_PER_RUN);
		accumulatedTime = std::fmodf(accumulatedTime, FIXED_TIMESTEP);

		if (stepsToPerform > 0) {
			// Calculate root acceleration in model-space.
			const SimdFloat4 deltaInvSimd = simd_float4::Load1(1.0f / movementTime);
			const SimdFloat4 worldVelocity = accumulatedMovement * deltaInvSimd;
			const SimdFloat4 worldAcceleration = (worldVelocity - prevRootVelocity) * deltaInvSimd;
			accumulatedMovement = simd_float4::zero();
			movementTime = 0.0f;
			prevRootVelocity = worldVelocity;
			simData.rootAcceleration = worldAcceleration;
			simData.interpolationRatio = accumulatedTime > 0.0f ? (accumulatedTime / FIXED_TIMESTEP) : 0.0f;
		}

		simData.requiredSteps = stepsToPerform;
	}
}