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

	void Body::CalculatePropertyConstants(const UpdateContext& a_context, SpringWithBodyProperties& a_props, SubStepConstants::PropertyConstants& a_constantsOut)
	{
		const SimdFloat4 massSimd = simd_float4::Load1(a_props.mass);
		const SimdFloat4 gravityForce = a_props.gravity * massSimd;
		const SimdFloat4 inertiaForce = -a_context.system->simData.rootAcceleration * massSimd;
		a_constantsOut.force = gravityForce + inertiaForce;
		a_constantsOut.massInverse = 1.0f / a_props.mass;
	}

	void Body::BeginStepUpdate(const UpdateContext& a_context, SubStepConstants& a_constants)
	{
		// Calculate sub-step-constant forces.
		if (a_context.linearProps) {
			CalculatePropertyConstants(a_context, *a_context.linearProps, a_constants.linear);
		}
		if (a_context.angularProps) {
			CalculatePropertyConstants(a_context, *a_context.angularProps, a_constants.angular);
		}

		a_constants.restPositionMS = a_context.animatedTransform.cols[3];
		a_constants.restRotationMS = Util::Ozz::ToNormalizedQuaternion(a_context.animatedTransform);
	}

	void Body::ProcessPhysicsStep(const UpdateContext& a_context, const SubStepConstants& a_constants)
	{
		if (a_context.linearProps) {
			ProcessLinearStep(a_context, a_constants);
			if (a_context.linearConstraint) {
				a_context.linearConstraint->Apply({
					.property = &position,
					.constrainedTo = a_constants.restPositionMS,
					.massInverse = a_constants.linear.massInverse
				});
			}
		}
		if (a_context.angularProps) {
			ProcessAngularStep(a_context, a_constants);
			if (a_context.angularConstraint) {
				a_context.angularConstraint->Apply({
					.property = &rotation,
					.constrainedTo = a_constants.restRotationMS,
					.massInverse = a_constants.angular.massInverse
				});
			}
		}
	}

	void Body::ProcessLinearStep(const UpdateContext& a_context, const SubStepConstants& a_constants)
	{
		const SimdFloat4 springForces = a_context.linearProps->spring.CalculateLinearForces(position, a_constants.restPositionMS);
		const SimdFloat4 totalForce = springForces + a_constants.linear.force;
		const SimdFloat4 acceleration = totalForce * simd_float4::Load1(a_constants.linear.massInverse);

		IntegrateLinearStep(position, acceleration);
	}

	void Body::ProcessAngularStep(const UpdateContext& a_context, const SubStepConstants& a_constants)
	{
		const SimdQuaternion currentRot = rotation.current;
		const SimdFloat4 springTorques = a_context.angularProps->spring.CalculateAngularTorques(rotation, a_constants.restRotationMS);
		const SimdFloat4 externalTorque = Cross3(a_constants.angular.force, TransformVector(currentRot, a_context.angularProps->upAxis));
		const SimdFloat4 totalTorque = springTorques + SetW(externalTorque, simd_float4::zero());
		const SimdFloat4 acceleration = totalTorque * simd_float4::Load1(a_constants.angular.massInverse);

		IntegrateAngularStep(rotation, acceleration);
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