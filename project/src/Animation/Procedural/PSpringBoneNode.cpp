#include "PSpringBoneNode.h"
#include "Settings/Settings.h"
#include "Util/Ozz.h"
#include "ozz/base/maths/simd_quaternion.h"

namespace Animation::Procedural
{
	bool SpringPhysicsJob::Run()
	{
		using namespace ozz::math;
		SimdInt4 invertible;
		const Float4x4 rootInverseWS = Invert(*rootTransform, &invertible);
		if (!AreAllTrue1(invertible)) {
			return false;
		}
		const Float4x4 parentInverseMS = Invert(*parentTransform, &invertible);
		if (!AreAllTrue1(invertible)) {
			return false;
		}

		if (!context->initialized) {
			const SimdFloat4& bonePos = boneTransform->cols[3];
			const SimdQuaternion boneRot = Util::Ozz::ToNormalizedQuaternion(*boneTransform);
			const SimdFloat4 zero = simd_float4::zero();
			context->position.current = bonePos;
			context->position.previous = bonePos;
			context->position.velocity = zero;

			context->rotation.current = boneRot;
			context->rotation.previous = boneRot;
			context->rotation.velocity = zero;
			
			context->accumulatedMovement = zero;
			context->prevRootVelocity = zero;
			context->accumulatedTime = 0.0f;
			context->movementTime = 0.0f;
			context->initialized = true;
		}

		// Get root movement transformed into model-space.
		const SimdFloat4 relativeRoot = rootTransform->cols[3] - prevRootTransform->cols[3];
		const SimdFloat4 worldMovementMS = TransformVector(rootInverseWS, relativeRoot);
		context->accumulatedMovement = context->accumulatedMovement + worldMovementMS;
		context->movementTime += context->deltaTime;

		// Add to accumulated time.
		context->accumulatedTime += context->deltaTime;

		// Calculate required physics steps.
		SubStepConstants constants;
		if (context->accumulatedTime >= FIXED_TIMESTEP) {
			BeginStepUpdate(constants);
		}

		uint8_t stepsPerformed = 0;
		while (context->accumulatedTime >= FIXED_TIMESTEP) {
			ProcessPhysicsStep(constants);
			++stepsPerformed;

			if (stepsPerformed > MAX_STEPS_PER_RUN) [[unlikely]] {
				context->accumulatedTime = 0.0f;
			} else {
				context->accumulatedTime -= FIXED_TIMESTEP;
			}
		}

		// Interpolate between previous step and current step, then transform back to local space for final output.
		const float ratio = context->accumulatedTime > 0.0f ? (context->accumulatedTime / FIXED_TIMESTEP) : 0.0f;

		if (positionOutput) {
			const SimdFloat4 interpPosition = Lerp(context->position.previous, context->position.current, simd_float4::Load1(ratio));
			const SimdFloat4 localPos = TransformPoint(parentInverseMS, interpPosition);
			*positionOutput = SetW(localPos, simd_float4::zero());
		}

		if (rotationOutput) {
			if (GetX(Dot4(context->rotation.previous.xyzw, context->rotation.current.xyzw)) < 0.00001f) {
				context->rotation.current = -context->rotation.current;
			}
			Quaternion q0, q1;
			StorePtrU(context->rotation.previous.xyzw, &q0.x);
			StorePtrU(context->rotation.current.xyzw, &q1.x);
			const Quaternion interpRotation = SLerp(q0, q1, ratio);
			const SimdQuaternion localRot = Util::Ozz::ToNormalizedQuaternion(parentInverseMS) *
			                                SimdQuaternion{ .xyzw = simd_float4::LoadPtrU(&interpRotation.x) };
			*rotationOutput = Normalize(localRot);
		}
		
		return true;
	}

	void SpringPhysicsJob::BeginStepUpdate(SubStepConstants& a_constantsOut)
	{
		using namespace ozz::math;

		// Calculate root acceleration in model-space.
		const float deltaTime = context->movementTime;
		const SimdFloat4 deltaInvSimd = simd_float4::Load1(1.0f / deltaTime);
		const SimdFloat4 worldVelocity = context->accumulatedMovement * deltaInvSimd;
		const SimdFloat4 worldAcceleration = (worldVelocity - context->prevRootVelocity) * deltaInvSimd;
		context->accumulatedMovement = simd_float4::zero();
		context->movementTime = 0.0f;
		context->prevRootVelocity = worldVelocity;

		// Calculate sub-step-constant forces.
		const SimdFloat4 massSimd = simd_float4::Load1(mass);
		const SimdFloat4 gravityForce = gravity * massSimd;
		const SimdFloat4 inertiaForce = -worldAcceleration * massSimd;

		a_constantsOut.force = gravityForce + inertiaForce;
		a_constantsOut.restPositionMS = boneTransform->cols[3];
		a_constantsOut.restRotationMS = Util::Ozz::ToNormalizedQuaternion(*boneTransform);
		a_constantsOut.dampingCoeff = simd_float4::Load1((2.0f * std::sqrt(stiffness * mass)) * std::clamp(damping, 0.005f, 1.0f));
		a_constantsOut.massInverse = 1.0f / mass;
	}

	void SpringPhysicsJob::ProcessPhysicsStep(const SubStepConstants& a_constants)
	{
		if (positionOutput) {
			ProcessLinearStep(a_constants);
		}
		if (rotationOutput) {
			ProcessAngularStep(a_constants);	
		}
	}

	void SpringPhysicsJob::ProcessLinearStep(const SubStepConstants& a_constants)
	{
		using namespace ozz::math;
		constexpr float deltaTime = FIXED_TIMESTEP;
		constexpr float dtSquared = deltaTime * deltaTime;

		const ozz::math::SimdFloat4 currentPos = context->position.current;

		// Calculate spring forces.
		const SimdFloat4 displacement = currentPos - a_constants.restPositionMS;
		const SimdFloat4 springForce = displacement * simd_float4::Load1(-stiffness);
		const SimdFloat4 dampingForce = -context->position.velocity * a_constants.dampingCoeff;
		const SimdFloat4 totalForce = springForce + dampingForce + a_constants.force;
		const SimdFloat4 acceleration = totalForce * simd_float4::Load1(a_constants.massInverse);

		// Euler integration.
		const SimdFloat4 dtSimd = simd_float4::Load1(deltaTime);
		context->position.velocity = context->position.velocity + (acceleration * dtSimd);
		context->position.current = currentPos + (context->position.velocity * dtSimd);
		context->position.previous = currentPos;
	}

	void SpringPhysicsJob::ProcessAngularStep(const SubStepConstants& a_constants)
	{
		using namespace ozz::math;
		constexpr float deltaTime = FIXED_TIMESTEP;
		constexpr float dtSquared = deltaTime * deltaTime;
		const float momentOfInteria = mass;

		const SimdQuaternion currentRot = context->rotation.current;

		// Calculate spring torques.
		SimdQuaternion fromRot = currentRot;
		if (GetX(Dot4(fromRot.xyzw, a_constants.restRotationMS.xyzw)) < 0.00001f) {
			fromRot = -fromRot;
		}

		const SimdFloat4 displacement = ToAxisAngle(Conjugate(fromRot) * a_constants.restRotationMS);
		const SimdFloat4 dispAxis = SetW(displacement, simd_float4::zero());
		const SimdFloat4 dispAngle = Swizzle<3, 3, 3, 3>(displacement);
		const SimdFloat4 springTorque = dispAxis * dispAngle * simd_float4::Load1(stiffness);
		const SimdFloat4 dampingTorque = -context->rotation.velocity * a_constants.dampingCoeff;
		const SimdFloat4 externalTorque = Cross3(a_constants.force, TransformPoint(*parentTransform, upAxis));
		const SimdFloat4 totalTorque = springTorque + dampingTorque + SetW(externalTorque, simd_float4::zero());
		const SimdFloat4 acceleration = (totalTorque / simd_float4::Load1(momentOfInteria));

		// Quaternion integration.
		const SimdFloat4 dtSimd = simd_float4::Load1(deltaTime);
		context->rotation.velocity = context->rotation.velocity + (acceleration * dtSimd);
		context->rotation.current = Normalize(currentRot * CalculateDeltaRotation());
		context->rotation.previous = currentRot;
	}

	ozz::math::SimdQuaternion SpringPhysicsJob::CalculateDeltaRotation() const
	{
		using namespace ozz::math;
		constexpr float deltaTime = FIXED_TIMESTEP;

		SimdFloat4 halfAngle = context->rotation.velocity * simd_float4::Load1(deltaTime * 0.5f);
		float length = GetX(Length3(halfAngle));
		if (length > 0.0f) {
			halfAngle = halfAngle * simd_float4::Load1(std::sin(length) / length);
		}
		return SimdQuaternion{ .xyzw = SetW(halfAngle, simd_float4::Load1(std::cos(length))) };
	}

	void PSpringBoneNode::AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime)
	{
		static_cast<InstanceData*>(a_instanceData)->context.deltaTime = a_deltaTime;
	}

	std::unique_ptr<PNodeInstanceData> PSpringBoneNode::CreateInstanceData()
	{
		return std::make_unique<InstanceData>();
	}

	PEvaluationResult PSpringBoneNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		auto inst = static_cast<InstanceData*>(a_instanceData);
		constexpr float valMin = 0.00001f;

		// Get node input data.
		PoseCache::Handle& input = std::get<PoseCache::Handle>(a_evalContext.results[inputs[0]]);
		const float stiffness = std::max(valMin, std::get<float>(a_evalContext.results[inputs[1]]));
		const float damping = std::max(valMin, std::get<float>(a_evalContext.results[inputs[2]]));
		const float mass = std::max(valMin, std::get<float>(a_evalContext.results[inputs[3]]));
		const ozz::math::Float4& gravity = std::get<ozz::math::Float4>(a_evalContext.results[inputs[4]]);

		// Acquire a pose handle for this node's output and copy the input pose to the output pose - we only need to make 1 correction to the pose.
		PoseCache::Handle output = a_poseCache.acquire_handle();
		auto inputSpan = input.get();
		auto outputSpan = output.get();
		std::copy(inputSpan.begin(), inputSpan.end(), outputSpan.begin());

		// If effectively paused, don't run any calculations as a deltaTime of 0 will break velocity updates.
		if (inst->context.deltaTime < 0.00001f)
			return output;

		// Update model-space cache.
		a_evalContext.UpdateModelSpaceCache(outputSpan, ozz::animation::Skeleton::kNoParent, boneIdx);

		// Setup spring job params & run.
		SpringPhysicsJob springJob;
		springJob.stiffness = stiffness;
		springJob.damping = damping;
		springJob.mass = mass;
		springJob.gravity = ozz::math::simd_float4::Load3PtrU(&gravity.x);
		springJob.upAxis = ozz::math::simd_float4::Load3PtrU(&upAxis.x);
		springJob.boneTransform = &a_evalContext.modelSpaceCache[boneIdx];
		springJob.parentTransform = &a_evalContext.modelSpaceCache[parentIdx];
		springJob.rootTransform = a_evalContext.rootTransform;
		springJob.prevRootTransform = a_evalContext.prevRootTransform;
		springJob.context = &inst->context;

		ozz::math::SimdFloat4 positionOutput;
		ozz::math::SimdQuaternion rotationOutput;
		springJob.positionOutput = isLinear ? &positionOutput : nullptr;
		springJob.rotationOutput = isAngular ? &rotationOutput : nullptr;

		if (!springJob.Run())
			return output;

		if (isLinear) {
			Util::Ozz::ApplySoATransformTranslation(boneIdx, positionOutput, outputSpan);
		}

		if (isAngular) {
			Util::Ozz::ApplySoATransformQuaternion(boneIdx, rotationOutput, outputSpan);
		}
		
		return output;
	}

	bool PSpringBoneNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		const RE::BSFixedString& boneName = std::get<RE::BSFixedString>(a_values[0]);
		upAxis = {
			std::get<float>(a_values[1]),
			std::get<float>(a_values[2]),
			std::get<float>(a_values[3]),
		};
		isLinear = std::get<bool>(a_values[4]);
		isAngular = std::get<bool>(a_values[5]);

		const auto idxs = Util::Ozz::GetJointIndexes(a_skeleton->data.get(), boneName.c_str());

		if (!idxs.has_value()) {
			return false;
		}

		boneIdx = idxs->at(0);
		int sParent = a_skeleton->data->joint_parents()[boneIdx];
		if (sParent == ozz::animation::Skeleton::kNoParent) {
			return false;
		}
		parentIdx = sParent;

		return true;
	}
}