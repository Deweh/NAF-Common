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

		// Calculate required physics steps & interpolation ratio.
		uint8_t stepsToPerform = 0;
		while (accumulatedTime >= FIXED_TIMESTEP) {
			++stepsToPerform;
			accumulatedTime -= FIXED_TIMESTEP;
		}
		simData.requiredSteps = std::min(stepsToPerform, MAX_STEPS_PER_RUN);
		simData.interpolationRatio = accumulatedTime > 0.0f ? (accumulatedTime / FIXED_TIMESTEP) : 0.0f;

		// If any physics steps are being performed this update, calculate root acceleration in model-space based on accumulated movement.
		if (stepsToPerform > 0) {
			const SimdFloat4 deltaInvSimd = simd_float4::Load1(1.0f / movementTime);
			const SimdFloat4 worldVelocity = accumulatedMovement * deltaInvSimd;
			const SimdFloat4 worldAcceleration = (worldVelocity - prevRootVelocity) * deltaInvSimd;
			accumulatedMovement = simd_float4::zero();
			movementTime = 0.0f;
			prevRootVelocity = worldVelocity;
			simData.rootAcceleration = worldAcceleration;
			simData.interpolationRatio = accumulatedTime > 0.0f ? (accumulatedTime / FIXED_TIMESTEP) : 0.0f;
		}
	}
}