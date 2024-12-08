#include "Body.h"
#include "ModelSpaceSystem.h"
#include "Util/Ozz.h"

namespace Physics
{
	using namespace ozz::math;
	constexpr float deltaTime = ModelSpaceSystem::FIXED_TIMESTEP;

	Transform Body::Update(const UpdateContext& a_context)
	{
		const uint8_t steps = a_context.system->simData.requiredSteps;

		SimdInt4 invertible;
		SubStepConstants constants;
		constants.parentInverseMS = Invert(a_context.parentTransform, &invertible);
		constants.parentInverseRotMS = Util::Ozz::ToNormalizedQuaternion(constants.parentInverseMS);

		if (steps > 0) {
			BeginStepUpdate(a_context, constants);
		}

		for (uint8_t i = 0; i < steps; i++) {
			ProcessPhysicsStep(a_context, constants);
		}

		return CalculateInterpolatedTransform(a_context, constants);
	}

	void Body::CalculateSpringConstants(const UpdateContext& a_context, SpringProperties& a_props, SubStepConstants::SpringConstants& a_constantsOut)
	{
		const SimdFloat4 massSimd = simd_float4::Load1(a_props.mass);
		const SimdFloat4 gravityForce = a_props.gravity * massSimd;
		const SimdFloat4 inertiaForce = -a_context.system->simData.rootAcceleration * massSimd;
		a_constantsOut.force = gravityForce + inertiaForce;
		a_constantsOut.dampingCoeff = simd_float4::Load1((2.0f * std::sqrt(a_props.stiffness * a_props.mass)) * a_props.damping);
		a_constantsOut.massInverse = 1.0f / a_props.mass;
	}

	void Body::BeginStepUpdate(const UpdateContext& a_context, SubStepConstants& a_constants)
	{
		// Calculate sub-step-constant forces.
		if (a_context.linearSpring) {
			CalculateSpringConstants(a_context, *a_context.linearSpring, a_constants.linearSpring);
		}
		if (a_context.angularSpring) {
			CalculateSpringConstants(a_context, *a_context.angularSpring, a_constants.angularSpring);
		}

		a_constants.restPositionMS = a_context.animatedTransform.cols[3];
		a_constants.restRotationMS = Util::Ozz::ToNormalizedQuaternion(a_context.animatedTransform);
	}

	void Body::ProcessPhysicsStep(const UpdateContext& a_context, const SubStepConstants& a_constants)
	{
		if (a_context.linearSpring) {
			ProcessLinearSpringStep(a_context, a_constants);
		}
		if (a_context.linearConstraint) {
			const SimdFloat4 relativePosition = position.current - a_constants.restPositionMS;
			const SimdFloat4 constrainedRelative = a_context.linearConstraint->Apply(relativePosition);
			position.current = a_constants.restPositionMS + constrainedRelative;
		}

		if (a_context.angularSpring) {
			ProcessAngularSpringStep(a_context, a_constants);
		}
		if (a_context.angularConstraint) {
			rotation.current = a_context.angularConstraint->Apply(rotation.current);
		}
	}

	void Body::ProcessLinearSpringStep(const UpdateContext& a_context, const SubStepConstants& a_constants)
	{
		const ozz::math::SimdFloat4 currentPos = position.current;

		// Calculate spring forces.
		const SimdFloat4 displacement = currentPos - a_constants.restPositionMS;
		const SimdFloat4 springForce = displacement * simd_float4::Load1(-a_context.linearSpring->stiffness);
		const SimdFloat4 dampingForce = -position.velocity * a_constants.linearSpring.dampingCoeff;
		const SimdFloat4 totalForce = springForce + dampingForce + a_constants.linearSpring.force;
		const SimdFloat4 acceleration = totalForce * simd_float4::Load1(a_constants.linearSpring.massInverse);

		// Euler integration.
		const SimdFloat4 dtSimd = simd_float4::Load1(deltaTime);
		position.velocity = position.velocity + (acceleration * dtSimd);
		position.current = currentPos + (position.velocity * dtSimd);
		position.previous = currentPos;
	}

	void Body::ProcessAngularSpringStep(const UpdateContext& a_context, const SubStepConstants& a_constants)
	{
		const SimdQuaternion currentRot = rotation.current;

		// Calculate spring torques.
		SimdQuaternion fromRot = currentRot;
		if (GetX(Dot4(fromRot.xyzw, a_constants.restRotationMS.xyzw)) < 0.00001f) {
			fromRot = -fromRot;
		}

		const SimdFloat4 displacement = ToAxisAngle(Conjugate(fromRot) * a_constants.restRotationMS);
		const SimdFloat4 dispAxis = SetW(displacement, simd_float4::zero());
		const SimdFloat4 dispAngle = Swizzle<3, 3, 3, 3>(displacement);
		const SimdFloat4 springTorque = dispAxis * dispAngle * simd_float4::Load1(a_context.angularSpring->stiffness);
		const SimdFloat4 dampingTorque = -rotation.velocity * a_constants.angularSpring.dampingCoeff;
		const SimdFloat4 externalTorque = Cross3(a_constants.angularSpring.force, TransformVector(currentRot, a_context.angularSpring->upAxis));
		const SimdFloat4 totalTorque = springTorque + dampingTorque + SetW(externalTorque, simd_float4::zero());
		const SimdFloat4 acceleration = totalTorque * simd_float4::Load1(a_constants.angularSpring.massInverse);

		// Quaternion integration.
		const SimdFloat4 dtSimd = simd_float4::Load1(deltaTime);
		const SimdFloat4 newVel = rotation.velocity + (acceleration * dtSimd);
		rotation.velocity = newVel;
		const SimdFloat4 deltaAngle = Swizzle<0, 0, 0, 0>(Length3(newVel)) * dtSimd;
		if (AreAllTrue1(CmpGt(deltaAngle, simd_float4::zero()))) {
			rotation.current = currentRot * SimdQuaternion::FromAxisAngle(Normalize3(newVel), deltaAngle);
		}
		rotation.previous = currentRot;
	}

	Transform Body::CalculateInterpolatedTransform(const UpdateContext& a_context, const SubStepConstants& a_constants) const
	{
		Transform result;
		const float ratio = a_context.system->simData.interpolationRatio;
		const SimdFloat4 interpPosition = Lerp(position.previous, position.current, simd_float4::Load1(ratio));
		const SimdFloat4 localPos = TransformPoint(a_constants.parentInverseMS, interpPosition);
		result.position = SetW(localPos, simd_float4::zero());

		SimdQuaternion prevRot = rotation.previous;
		if (GetX(Dot4(prevRot.xyzw, rotation.current.xyzw)) < 0.00001f) {
			prevRot = -prevRot;
		}
		Quaternion q0, q1;
		StorePtrU(prevRot.xyzw, &q0.x);
		StorePtrU(rotation.current.xyzw, &q1.x);
		const Quaternion interpRotation = NLerp(q0, q1, ratio);
		const SimdQuaternion localRot = a_constants.parentInverseRotMS *
		                                SimdQuaternion{ .xyzw = simd_float4::LoadPtrU(&interpRotation.x) };
		result.rotation = localRot;
		return result;
	}
}