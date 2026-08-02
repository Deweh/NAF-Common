#include "IKChain.h"

#include "Animation/Studio/Skeleton.h"
#include "Animation/Studio/StudioMath.h"
#include "Util/Ozz.h"

namespace Animation::Studio
{
	namespace
	{
		// a_value up to half of a_limit, then easing toward a_limit without a hard stop.
		float SoftLimit(float a_value, float a_limit)
		{
			if (!(a_limit > 0.0f)) return 0.0f;
			const float knee = 0.5f * a_limit;
			if (a_value <= knee) return a_value;
			const float range = a_limit - knee;
			return knee + range * (1.0f - std::exp((knee - a_value) / range));
		}
	}

	PropertyPtr IKChainObject::FindProperty(uint16_t a_element, PropertyId a_property)
	{
		if (a_element != kSelf) return {};
		switch (a_property) {
		case PropertyId::kWeight:
			return &weight;
		case PropertyId::kSoften:
			return &soften;
		case PropertyId::kTwist:
			return &twist;
		case PropertyId::kEnabled:
			return &enabled;
		case PropertyId::kShoulderWeight:
			return &shoulderWeight;
		case PropertyId::kTwistDistribution:
			return &twistDistribution;
		default:
			return {};
		}
	}

	void IKChainObject::ForEachProperty(const PropertyVisitor& a_visitor)
	{
		a_visitor(kSelf, PropertyId::kWeight, &weight);
		a_visitor(kSelf, PropertyId::kSoften, &soften);
		a_visitor(kSelf, PropertyId::kTwist, &twist);
		a_visitor(kSelf, PropertyId::kEnabled, &enabled);
		a_visitor(kSelf, PropertyId::kShoulderWeight, &shoulderWeight);
		a_visitor(kSelf, PropertyId::kTwistDistribution, &twistDistribution);
	}

	void IKChainObject::Remap(ObjectId a_actor, const std::function<ObjectId(ObjectId)>& a_locator)
	{
		ModifierObject::Remap(a_actor, a_locator);
		effector = a_locator(effector);
		pole = a_locator(pole);
	}

	int32_t IKChainObject::GetWriteRoot(const ozz::animation::Skeleton& a_skeleton) const
	{
		if (settings.shoulderBone >= 0 && IsValidShoulder(a_skeleton, settings.shoulderBone, upperBone)) return settings.shoulderBone;
		return upperBone;
	}

	bool IKChainObject::DrivesBone(ozz::span<const int16_t> a_parents, int32_t a_bone) const
	{
		if (!enabled.live) return false;
		for (int32_t joint = endBone; joint >= 0 && joint < static_cast<int32_t>(a_parents.size()); joint = a_parents[joint]) {
			if (joint == a_bone) return true;
			if (joint == upperBone) break;
		}
		return false;
	}

	void IKChainObject::SetSettings(const ModifierSettings& a_settings)
	{
		if (const auto typed = std::get_if<IKChainSettings>(&a_settings)) settings = *typed;
	}

	void IKChainObject::Solve(ModifierContext& a_context)
	{
		shoulderGuide.reset();
		if (!enabled.live || weight.live <= 0.0f) return;
		const auto owner = a_context.FindActor(actor);
		if (!owner || !owner->hasRoot || !owner->skeleton || !owner->pose) return;
		const auto& skeleton = *owner->skeleton->data;
		const int32_t jointCount = skeleton.num_joints();
		for (const int32_t bone : { upperBone, middleBone, endBone }) {
			if (bone < 0 || bone >= jointCount) return;
		}

		const auto effectorWorld = a_context.ResolveLocatorWorld(effector);
		const auto poleWorld = a_context.ResolveLocatorWorld(pole);
		const auto inverseRoot = TryInvert(owner->rootWorld);
		if (!effectorWorld || !poleWorld || !inverseRoot) return;

		const auto targetModel = TransformPoint(*inverseRoot, GetTranslation(*effectorWorld));
		// Moves the upper bone, so everything below reads the model after it.
		SolveShoulder(*owner, targetModel);
		const auto poleVector = TransformPoint(*inverseRoot, GetTranslation(*poleWorld)) - GetTranslation(owner->model[upperBone]);
		if (ozz::math::Length(poleVector) <= 0.00001f) return;

		auto& pose = owner->pose->Back();
		ozz::math::SimdQuaternion corrections[2];
		ozz::animation::IKTwoBoneJob job;
		job.target = ozz::math::simd_float4::Load3PtrU(&targetModel.x);
		job.pole_vector = ozz::math::simd_float4::Load3PtrU(&poleVector.x);
		job.mid_axis = ozz::math::simd_float4::Load3PtrU(&middleAxis.x);
		job.weight = std::clamp(weight.live, 0.0f, 1.0f);
		job.soften = std::clamp(soften.live, 0.0f, 1.0f);
		job.twist_angle = twist.live;
		job.start_joint = &owner->model[upperBone];
		job.mid_joint = &owner->model[middleBone];
		job.end_joint = &owner->model[endBone];
		job.start_joint_correction = &corrections[0];
		job.mid_joint_correction = &corrections[1];
		job.reached = &reached;
		if (!job.Run()) return;
		Util::Ozz::MultiplySoATransformQuaternion(upperBone, corrections[0], pose);
		Util::Ozz::MultiplySoATransformQuaternion(middleBone, corrections[1], pose);
		owner->RunLocalToModel(upperBone);

		// Align the end bone with the effector's rotation, blended by weight. Its twist about the lower limb is
		// spread over the bones above it first, so the lower limb doesn't candy-wrap.
		DistributeTwist(*owner, GetRotation(*inverseRoot * *effectorWorld), job.weight);
		const int16_t endParent = skeleton.joint_parents()[endBone];
		const auto endParentWorld = endParent == ozz::animation::Skeleton::kNoParent ? owner->rootWorld : owner->rootWorld * owner->model[endParent];
		const auto parentInverse = TryInvert(endParentWorld);
		if (!parentInverse) return;
		ozz::math::Float3 translation;
		ozz::math::Quaternion targetRotation;
		ozz::math::Float3 scale;
		if (!ozz::math::ToAffine(*parentInverse * *effectorWorld, &translation, &targetRotation, &scale)) return;
		const auto current = ToQuaternion(Util::Ozz::GetSoATransformQuaternion(endBone, pose));
		const auto rotation = SlerpShortest(current, targetRotation, job.weight);
		Util::Ozz::ApplySoATransformQuaternion(endBone, ToSimdQuaternion(rotation), pose);
		owner->RunLocalToModel(endBone);
	}

	void IKChainObject::SolveShoulder(ActorObject& a_actor, const ozz::math::Float3& a_targetModel)
	{
		const int32_t shoulder = settings.shoulderBone;
		if (shoulder < 0 || shoulder >= static_cast<int32_t>(a_actor.model.size())) return;

		const auto& model = a_actor.model;
		const auto shoulderPosition = GetTranslation(model[shoulder]);
		const auto upperPosition = GetTranslation(model[upperBone]);
		const float length = ozz::math::Length(GetTranslation(model[middleBone]) - upperPosition) +
		                     ozz::math::Length(GetTranslation(model[endBone]) - GetTranslation(model[middleBone]));
		const auto aim = upperPosition - shoulderPosition;
		const float radius = ozz::math::Length(aim);
		if (length <= 0.00001f || radius <= 0.00001f) return;
		const auto aimDirection = aim / radius;

		// A damped track toward a dummy point an arm's length out along the shoulder, which moves with the effector's
		// offset from neutral. Neutral lives in the shoulder's parent space, so the torso moving doesn't count. The aim
		// is the shoulder's own pose, so with the effector at neutral nothing moves and its keys pass through.
		const int16_t parent = a_actor.skeleton->data->joint_parents()[shoulder];
		const auto neutral = parent == ozz::animation::Skeleton::kNoParent ? settings.shoulderNeutral : TransformPoint(model[parent], settings.shoulderNeutral);
		const auto dummy = shoulderPosition + aimDirection * (radius + length) + (a_targetModel - neutral);
		shoulderGuide = ShoulderGuide{ shoulderPosition, neutral, dummy };

		const auto toDummy = dummy - shoulderPosition;
		const float distance = ozz::math::Length(toDummy);
		if (distance <= 0.00001f) return;
		const auto toDirection = toDummy / distance;
		const float fullAngle = std::acos(std::clamp(ozz::math::Dot(aimDirection, toDirection), -1.0f, 1.0f));
		const float amount = std::clamp(shoulderWeight.live, 0.0f, 1.0f) * std::clamp(weight.live, 0.0f, 1.0f);
		const float angle = SoftLimit(fullAngle * amount, settings.shoulderMaxAngle);
		if (angle <= 0.00001f) return;
		const auto axis = NormalizeOr(ozz::math::Cross(aimDirection, toDirection), AnyPerpendicular(aimDirection));

		// A model-space swing about the shoulder, as a local correction: local' = local * (Rs^-1 * q * Rs).
		const auto shoulderRotation = GetRotation(model[shoulder]);
		const auto swing = ozz::math::Quaternion::FromAxisAngle(axis, angle);
		const auto correction = ozz::math::Conjugate(shoulderRotation) * swing * shoulderRotation;
		Util::Ozz::MultiplySoATransformQuaternion(shoulder, ToSimdQuaternion(ozz::math::NormalizeSafe(correction, ozz::math::Quaternion::identity())), a_actor.pose->Back());
		a_actor.RunLocalToModel(shoulder);
	}

	void IKChainObject::DistributeTwist(ActorObject& a_actor, const ozz::math::Quaternion& a_targetModel, float a_weight) const
	{
		const float share = std::clamp(twistDistribution.live, 0.0f, 1.0f) * a_weight;
		if (share <= 0.0f) return;
		const auto path = GetBonePath(*a_actor.skeleton->data, middleBone, endBone);
		if (path.size() < 3) return;

		const auto& model = a_actor.model;
		const auto middlePosition = GetTranslation(model[middleBone]);
		const auto endPosition = GetTranslation(model[endBone]);
		const auto limb = endPosition - middlePosition;
		const float lengthSquared = ozz::math::Dot(limb, limb);
		if (lengthSquared <= 0.00001f) return;
		const auto limbAxis = limb / std::sqrt(lengthSquared);

		// The twist part of the turn from the end bone's model rotation to the target's: the part about the lower limb.
		const auto delta = a_targetModel * ozz::math::Conjugate(GetRotation(model[endBone]));
		float twist = 2.0f * std::atan2(ozz::math::Dot(ozz::math::Float3{ delta.x, delta.y, delta.z }, limbAxis), delta.w);
		if (twist > ozz::math::kPi) {
			twist -= 2.0f * ozz::math::kPi;
		} else if (twist < -ozz::math::kPi) {
			twist += 2.0f * ozz::math::kPi;
		}
		twist *= share;
		if (std::abs(twist) <= 0.00001f) return;

		// Each bone between takes the twist up to how far along the lower limb it sits, less what its parents already
		// gave it. A model-space turn q on a bone is local' = local * (R^-1 * q * R) with R its model rotation before
		// any of these turns; down a chain, the model rotations compose to q1 * ... * qk * R.
		auto& pose = a_actor.pose->Back();
		const int32_t endParent = path[path.size() - 2];
		auto total = ozz::math::Quaternion::identity();
		float applied = 0.0f;
		for (size_t i = 1; i + 1 < path.size(); ++i) {
			const int32_t bone = path[i];
			const auto position = GetTranslation(model[bone]);
			const float along = std::clamp(ozz::math::Dot(position - middlePosition, limb) / lengthSquared, 0.0f, 1.0f);
			const float step = along * twist - applied;
			applied = along * twist;
			if (std::abs(step) <= 0.00001f) continue;
			// About the line through this bone and the end bone, so the end bone stays where the solve put it.
			const auto turn = ozz::math::Quaternion::FromAxisAngle(NormalizeOr(endPosition - position, limbAxis), step);
			const auto rotation = GetRotation(model[bone]);
			const auto correction = ozz::math::Conjugate(rotation) * turn * rotation;
			Util::Ozz::MultiplySoATransformQuaternion(bone, ToSimdQuaternion(ozz::math::NormalizeSafe(correction, ozz::math::Quaternion::identity())), pose);
			total = total * turn;
		}

		// Undo the turn the end bone inherits, keeping its model rotation: local' = P^-1 * total^-1 * P * local.
		const auto parentRotation = GetRotation(model[endParent]);
		const auto keep = ozz::math::Conjugate(parentRotation) * ozz::math::Conjugate(total) * parentRotation;
		const auto local = ToQuaternion(Util::Ozz::GetSoATransformQuaternion(endBone, pose));
		Util::Ozz::ApplySoATransformQuaternion(endBone, ToSimdQuaternion(ozz::math::NormalizeSafe(keep * local, ozz::math::Quaternion::identity())), pose);
		a_actor.RunLocalToModel(path[1]);
	}

	bool IsValidChain(const ozz::animation::Skeleton& a_skeleton, int32_t a_upperBone, int32_t a_middleBone, int32_t a_endBone)
	{
		const int32_t jointCount = a_skeleton.num_joints();
		for (const int32_t bone : { a_upperBone, a_middleBone, a_endBone }) {
			if (bone < 0 || bone >= jointCount) return false;
		}
		const auto parents = a_skeleton.joint_parents();
		return a_upperBone != a_middleBone && a_middleBone != a_endBone &&
		       IsDescendantOrSelf(parents, a_middleBone, a_upperBone) && IsDescendantOrSelf(parents, a_endBone, a_middleBone);
	}

	bool IsValidShoulder(const ozz::animation::Skeleton& a_skeleton, int32_t a_shoulder, int32_t a_upperBone)
	{
		return a_shoulder != a_upperBone && GetBonePath(a_skeleton, a_shoulder, a_upperBone).size() >= 2;
	}

	int32_t DetectShoulder(const ozz::animation::Skeleton& a_skeleton, int32_t a_upperBone)
	{
		const auto parents = a_skeleton.joint_parents();
		const int32_t shoulder = parents[a_upperBone];
		if (shoulder == ozz::animation::Skeleton::kNoParent || parents[shoulder] == ozz::animation::Skeleton::kNoParent) return -1;
		const auto subtreeSize = [&](int32_t a_root) {
			int32_t count = 0;
			for (int32_t joint = 0; joint < a_skeleton.num_joints(); ++joint) {
				if (IsDescendantOrSelf(parents, joint, a_root)) ++count;
			}
			return count;
		};
		const int32_t limbSize = subtreeSize(a_upperBone);
		for (int32_t joint = 0; joint < a_skeleton.num_joints(); ++joint) {
			if (joint != a_upperBone && parents[joint] == shoulder && subtreeSize(joint) > limbSize) return -1;
		}
		return shoulder;
	}

	ozz::math::Float3 ToShoulderNeutral(const ozz::animation::Skeleton& a_skeleton, std::span<const ozz::math::Float4x4> a_model,
		int32_t a_shoulder, const ozz::math::Float3& a_targetModel)
	{
		if (a_shoulder < 0 || a_shoulder >= a_skeleton.num_joints() || a_shoulder >= static_cast<int32_t>(a_model.size())) return a_targetModel;
		const int16_t parent = a_skeleton.joint_parents()[a_shoulder];
		if (parent == ozz::animation::Skeleton::kNoParent) return a_targetModel;
		const auto inverse = TryInvert(a_model[parent]);
		return inverse ? TransformPoint(*inverse, a_targetModel) : a_targetModel;
	}

	ozz::math::Float3 GetRestShoulderNeutral(const ozz::animation::Skeleton& a_skeleton, int32_t a_shoulder, int32_t a_endBone)
	{
		const auto rest = GetRestModel(a_skeleton);
		if (!rest || a_endBone < 0 || a_endBone >= static_cast<int32_t>(rest->size())) return ozz::math::Float3::zero();
		return ToShoulderNeutral(a_skeleton, *rest, a_shoulder, GetTranslation((*rest)[a_endBone]));
	}

	std::expected<ModifierBuild, std::string> BuildIKChain(const ActorObject& a_actor, const ozz::math::Float4x4& a_sceneFromModel,
		int32_t a_upperBone, int32_t a_middleBone, int32_t a_endBone, ObjectId& a_nextId)
	{
		const auto& skeleton = *a_actor.skeleton->data;
		const auto& model = a_actor.model;
		const auto upperPosition = GetTranslation(model[a_upperBone]);
		const auto middlePosition = GetTranslation(model[a_middleBone]);
		const auto endPosition = GetTranslation(model[a_endBone]);
		const auto middleInverse = TryInvert(model[a_middleBone]);
		if (!middleInverse) return std::unexpected("The middle bone transform is not invertible.");
		const auto middleAxis = NormalizeOr(ozz::math::Cross(TransformPoint(*middleInverse, upperPosition), TransformPoint(*middleInverse, endPosition)), { 0.0f, 0.0f, 1.0f });

		// The effector starts on the end bone. The pole sits off the middle bone, out along the upper -> middle
		// direction and the bend normal.
		ozz::math::Float3 effectorTranslation;
		ozz::math::Quaternion effectorRotation;
		ozz::math::Float3 effectorScale;
		if (!ozz::math::ToAffine(a_sceneFromModel * model[a_endBone], &effectorTranslation, &effectorRotation, &effectorScale)) {
			effectorTranslation = TransformPoint(a_sceneFromModel, endPosition);
			effectorRotation = ozz::math::Quaternion::identity();
		}
		const auto poleModel = middlePosition + NormalizeOr(middlePosition - upperPosition, { 1.0f, 0.0f, 0.0f }) * 20.0f +
		                       NormalizeOr(ozz::math::Cross(endPosition - upperPosition, middlePosition - upperPosition), { 0.0f, 1.0f, 0.0f }) * 30.0f;

		const std::string chainName = std::format("{} IK", skeleton.joint_names()[a_endBone]);
		const auto makeLocator = [&](std::string a_name, const ozz::math::Float3& a_translation, const ozz::math::Quaternion& a_rotation) {
			auto locator = std::make_unique<LocatorObject>();
			locator->id = a_nextId++;
			locator->name = std::move(a_name);
			// The initial placement doubles as the default, so switching sequences doesn't send it to the scene center.
			locator->transform.translation = { a_translation, a_translation };
			locator->transform.rotation = { a_rotation, a_rotation };
			return locator;
		};
		auto effector = makeLocator(chainName + " Effector", effectorTranslation, Normalized(effectorRotation));
		auto pole = makeLocator(chainName + " Pole", TransformPoint(a_sceneFromModel, poleModel), ozz::math::Quaternion::identity());

		auto chain = std::make_unique<IKChainObject>();
		chain->id = a_nextId++;
		chain->name = chainName;
		chain->actor = a_actor.id;
		chain->upperBone = a_upperBone;
		chain->middleBone = a_middleBone;
		chain->endBone = a_endBone;
		chain->middleAxis = middleAxis;
		chain->settings.shoulderBone = DetectShoulder(skeleton, a_upperBone);
		// The current pose is the relaxed one until Set Neutral says otherwise.
		chain->settings.shoulderNeutral = ToShoulderNeutral(skeleton, model, chain->settings.shoulderBone, endPosition);
		chain->effector = effector->id;
		chain->pole = pole->id;

		ModifierBuild result;
		result.locators.push_back(std::move(effector));
		result.locators.push_back(std::move(pole));
		result.modifier = std::move(chain);
		return result;
	}
}
