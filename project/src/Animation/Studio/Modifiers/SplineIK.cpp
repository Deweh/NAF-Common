#include "SplineIK.h"

#include "SplineMath.h"

#include "Animation/Studio/StudioMath.h"
#include "Util/Ozz.h"

namespace Animation::Studio
{
	PropertyPtr SplineIKObject::FindProperty(uint16_t a_element, PropertyId a_property)
	{
		if (a_element != kSelf) return {};
		switch (a_property) {
		case PropertyId::kWeight:
			return &weight;
		case PropertyId::kTwist:
			return &twist;
		case PropertyId::kStretch:
			return &stretch;
		case PropertyId::kTangentLength:
			return &tangentLength;
		case PropertyId::kOrient:
			return &orient;
		case PropertyId::kEnabled:
			return &enabled;
		default:
			return {};
		}
	}

	void SplineIKObject::ForEachProperty(const PropertyVisitor& a_visitor)
	{
		a_visitor(kSelf, PropertyId::kEnabled, &enabled);
		a_visitor(kSelf, PropertyId::kWeight, &weight);
		a_visitor(kSelf, PropertyId::kTwist, &twist);
		a_visitor(kSelf, PropertyId::kStretch, &stretch);
		a_visitor(kSelf, PropertyId::kTangentLength, &tangentLength);
		a_visitor(kSelf, PropertyId::kOrient, &orient);
	}

	void SplineIKObject::Remap(ObjectId a_actor, const std::function<ObjectId(ObjectId)>& a_locator)
	{
		ModifierObject::Remap(a_actor, a_locator);
		for (auto& control : controls) control = a_locator(control);
	}

	std::vector<int32_t> SplineIKObject::GetWriteRoots(const ozz::animation::Skeleton&) const
	{
		if (bones.empty()) return {};
		return { bones.front() };
	}

	bool SplineIKObject::DrivesBone(ozz::span<const int16_t>, int32_t a_bone) const
	{
		return enabled.live && std::ranges::contains(bones, a_bone);
	}

	void SplineIKObject::SetSettings(const ModifierSettings& a_settings)
	{
		if (const auto typed = std::get_if<SplineIKSettings>(&a_settings)) settings = *typed;
	}

	void SplineIKObject::Solve(ModifierContext& a_context)
	{
		curve.clear();
		if (!enabled.live || weight.live <= 0.0f) return;
		const auto owner = a_context.FindActor(actor);
		if (!owner || !owner->hasRoot || !owner->skeleton || !owner->pose) return;
		const auto& skeleton = *owner->skeleton->data;
		const size_t boneCount = bones.size();
		const size_t controlCount = controls.size();
		if (boneCount < 2 || controlCount < 2 || aimAxes.size() != boneCount || upAxes.size() != boneCount || controlAxes.size() != controlCount) return;
		if (std::ranges::any_of(bones, [&](int32_t a_bone) { return a_bone < 0 || a_bone >= skeleton.num_joints(); })) return;
		const auto inverseRoot = TryInvert(owner->rootWorld);
		if (!inverseRoot) return;

		SplineInput input;
		std::optional<ozz::math::Quaternion> tipRotation;
		for (size_t k = 0; k < controlCount; ++k) {
			const auto world = a_context.ResolveLocatorWorld(controls[k]);
			if (!world) return;
			const auto model = *inverseRoot * *world;
			const auto rotation = GetRotation(model);
			input.controls.push_back(GetTranslation(model));
			input.controlUps.push_back(ozz::math::TransformVector(rotation, NormalizeOr(controlAxes[k].up, { 0.0f, 0.0f, 1.0f })));
			if (k == 0) input.startAim = ozz::math::TransformVector(rotation, NormalizeOr(controlAxes[k].aim, { 1.0f, 0.0f, 0.0f }));
			if (k + 1 == controlCount) {
				input.endAim = ozz::math::TransformVector(rotation, NormalizeOr(controlAxes[k].aim, { 1.0f, 0.0f, 0.0f }));
				tipRotation = rotation;
			}
		}

		auto& model = owner->model;
		std::vector<ozz::math::Quaternion> fkRotations(boneCount);
		std::vector<ozz::math::Float3> fkScales(boneCount, ozz::math::Float3::one());
		input.lengths.assign(boneCount, 0.0f);
		for (size_t j = 0; j < boneCount; ++j) {
			fkRotations[j] = GetRotation(model[bones[j]]);
			ozz::math::Float3 translation, scale;
			ozz::math::Quaternion rotation;
			if (ozz::math::ToAffine(model[bones[j]], &translation, &rotation, &scale)) fkScales[j] = scale;
			if (j > 0) input.lengths[j] = ozz::math::Length(GetTranslation(model[bones[j]]) - GetTranslation(model[bones[j - 1]]));
		}
		input.root = GetTranslation(model[bones.front()]);
		input.rootFollowsCurve = settings.rootFollowsCurve;
		input.tangentLength = (std::max)(tangentLength.live, 0.0f);
		input.twist = twist.live;
		input.stretch = stretch.live;
		const auto result = SolveSpline(input);
		curve = result.curve;

		auto& pose = owner->pose->Back();
		const float clampedWeight = std::clamp(weight.live, 0.0f, 1.0f);
		// Orient splits how the bones follow the curve: by turning (rotation, bone lengths kept) or by moving onto
		// it (translation, bone lengths stretch). Skin weighted for physics-driven bones handles moving far better.
		const float clampedOrient = std::clamp(orient.live, 0.0f, 1.0f);
		const int16_t startParent = skeleton.joint_parents()[bones.front()];
		const auto parentModel = startParent == ozz::animation::Skeleton::kNoParent ? ozz::math::Float4x4::identity() : model[startParent];

		// The first bone only moves when it follows the curve.
		auto parentPosition = input.root;  // The previous bone's new model position.
		if (settings.rootFollowsCurve) {
			if (const auto parentInverse = TryInvert(parentModel)) {
				parentPosition = input.root + (result.positions.front() - input.root) * clampedWeight;
				SetPoseTranslation(pose, bones.front(), TransformPoint(*parentInverse, parentPosition));
			}
		}

		auto parentRotation = GetRotation(parentModel);
		for (size_t j = 0; j < boneCount; ++j) {
			// Bone j's parent is bone j - 1, already solved: parentRotation and parentPosition are its new model values.
			if (j > 0) {
				auto translation = GetPoseTranslation(pose, bones[j]);
				// Stretching scales the bone's offset from its parent.
				if (input.stretch > 0.0f && input.lengths[j] > 0.00001f) {
					const float ratio = ozz::math::Length(result.positions[j] - result.positions[j - 1]) / input.lengths[j];
					translation = translation * (1.0f + (ratio - 1.0f) * clampedWeight);
				}
				const auto& parentScale = fkScales[j - 1];
				if (clampedOrient < 1.0f) {
					// The offset that puts the bone at its solved position, given where its parent now is.
					const auto fkPosition = GetTranslation(model[bones[j]]);
					const auto desired = fkPosition + (result.positions[j] - fkPosition) * clampedWeight;
					const auto offset = ozz::math::TransformVector(ozz::math::Conjugate(parentRotation), desired - parentPosition);
					const auto divide = [](float a_value, float a_scale) { return std::abs(a_scale) > 0.00001f ? a_value / a_scale : a_value; };
					const ozz::math::Float3 positional{ divide(offset.x, parentScale.x), divide(offset.y, parentScale.y), divide(offset.z, parentScale.z) };
					translation = translation + (positional - translation) * (1.0f - clampedOrient);
				}
				SetPoseTranslation(pose, bones[j], translation);
				parentPosition = parentPosition + ozz::math::TransformVector(parentRotation, translation * parentScale);
			}

			auto target = RotationBetweenFrames(aimAxes[j], upAxes[j], result.aims[j], result.ups[j]);
			if (j + 1 == boneCount && settings.alignTip && tipRotation) target = *tipRotation;
			const auto rotation = SlerpShortest(fkRotations[j], target, clampedWeight * clampedOrient);
			const auto local = ozz::math::NormalizeSafe(ozz::math::Conjugate(parentRotation) * rotation, ozz::math::Quaternion::identity());
			Util::Ozz::ApplySoATransformQuaternion(bones[j], ToSimdQuaternion(local), pose);
			parentRotation = rotation;
		}
		owner->RunLocalToModel(bones.front());
	}

	std::expected<ModifierBuild, std::string> BuildSplineIK(const ActorObject& a_actor, const ozz::math::Float4x4& a_sceneFromModel,
		const std::vector<int32_t>& a_path, uint32_t a_controlCount, ObjectId& a_nextId)
	{
		const auto& path = a_path;
		const size_t boneCount = path.size();
		const auto& model = a_actor.model;
		std::vector<ozz::math::Float3> positions(boneCount);
		std::vector<ozz::math::Quaternion> rotations(boneCount);
		std::vector<float> lengths(boneCount, 0.0f);
		std::vector<float> arcs(boneCount, 0.0f);
		for (size_t j = 0; j < boneCount; ++j) {
			positions[j] = GetTranslation(model[path[j]]);
			rotations[j] = GetRotation(model[path[j]]);
			if (j > 0) {
				lengths[j] = ozz::math::Length(positions[j] - positions[j - 1]);
				arcs[j] = arcs[j - 1] + lengths[j];
			}
		}

		// Controls go on bones at distinct points, so bones sharing a point can't take two controls.
		const auto candidates = GetSplineControlCandidates(lengths);
		if (candidates.size() < 2) return std::unexpected("The bones are too close together to form a curve.");
		const size_t controlCount = std::clamp<size_t>(a_controlCount, 2, candidates.size());

		// Each bone's aim axis points at the next bone apart from it; the tip continues the direction it was reached by.
		const auto segments = GetSplineAimSegments(lengths);
		std::vector<ozz::math::Float3> aimAxes(boneCount);
		for (size_t j = 0; j < boneCount; ++j) {
			const auto direction = positions[segments[j].second] - positions[segments[j].first];
			const auto inverse = TryInvert(model[path[j]]);
			aimAxes[j] = NormalizeOr(inverse ? TransformDirection(*inverse, direction) : direction, { 1.0f, 0.0f, 0.0f });
		}

		// Controls spread evenly by arc length over the candidates, always including the start and end bones.
		std::vector<size_t> controlBones(controlCount);
		for (size_t k = 0, previous = 0; k < controlCount; ++k) {
			const size_t lowest = k == 0 ? 0 : previous + 1;
			const size_t highest = candidates.size() - controlCount + k;
			const float target = arcs.back() * static_cast<float>(k) / static_cast<float>(controlCount - 1);
			size_t best = lowest;
			for (size_t c = lowest; c <= highest; ++c) {
				if (std::abs(arcs[candidates[c]] - target) < std::abs(arcs[candidates[best]] - target)) best = c;
			}
			controlBones[k] = candidates[best];
			previous = best;
		}

		const auto names = a_actor.skeleton->data->joint_names();
		const std::string name = std::format("{} Spline IK", names[path.back()]);
		ModifierBuild result;
		std::vector<SplineIKObject::ControlAxes> controlAxes;
		SplineInput input;
		for (size_t k = 0; k < controlCount; ++k) {
			const size_t j = controlBones[k];
			ozz::math::Float3 translation;
			ozz::math::Quaternion rotation;
			ozz::math::Float3 scale;
			if (!ozz::math::ToAffine(a_sceneFromModel * model[path[j]], &translation, &rotation, &scale)) {
				translation = TransformPoint(a_sceneFromModel, positions[j]);
				rotation = ozz::math::Quaternion::identity();
			}
			auto control = std::make_unique<LocatorObject>();
			control->id = a_nextId++;
			control->name = std::format("{} Control {}", name, k + 1);
			control->transform.translation = { translation, translation };
			control->transform.rotation = { Normalized(rotation), Normalized(rotation) };
			result.locators.push_back(std::move(control));

			// The control starts with its bone's rotation, so it shares the bone's axes. Up is the bone's basis
			// axis least parallel to its aim.
			const auto& aim = aimAxes[j];
			const std::array<ozz::math::Float3, 3> basis{ ozz::math::Float3{ 1.0f, 0.0f, 0.0f }, ozz::math::Float3{ 0.0f, 1.0f, 0.0f }, ozz::math::Float3{ 0.0f, 0.0f, 1.0f } };
			const auto& least = *std::ranges::min_element(basis, {}, [&](const ozz::math::Float3& a_axis) { return std::abs(ozz::math::Dot(a_axis, aim)); });
			const SplineIKObject::ControlAxes axes{ aim, Orthogonalize(least, aim, AnyPerpendicular(aim)) };
			controlAxes.push_back(axes);

			input.controls.push_back(positions[j]);
			input.controlUps.push_back(ozz::math::TransformVector(rotations[j], axes.up));
		}

		// Calibrate each bone's up axis against the frame the solve produces for the untouched controls, so the
		// spline starts out matching the pose's roll.
		input.startAim = ozz::math::TransformVector(rotations[controlBones.front()], controlAxes.front().aim);
		input.endAim = ozz::math::TransformVector(rotations[controlBones.back()], controlAxes.back().aim);
		input.lengths = lengths;
		input.root = positions.front();
		const auto solved = SolveSpline(input);
		std::vector<ozz::math::Float3> upAxes(boneCount);
		for (size_t j = 0; j < boneCount; ++j) {
			upAxes[j] = Orthogonalize(ozz::math::TransformVector(ozz::math::Conjugate(rotations[j]), solved.ups[j]), aimAxes[j], AnyPerpendicular(aimAxes[j]));
		}

		auto spline = std::make_unique<SplineIKObject>();
		spline->id = a_nextId++;
		spline->name = name;
		spline->actor = a_actor.id;
		spline->bones = path;
		spline->aimAxes = std::move(aimAxes);
		spline->upAxes = std::move(upAxes);
		spline->controlAxes = std::move(controlAxes);
		for (const auto& control : result.locators) spline->controls.push_back(control->id);
		result.modifier = std::move(spline);
		return result;
	}

	size_t GetSplineIKControlLimit(const ActorObject& a_actor, const std::vector<int32_t>& a_path)
	{
		std::vector<float> lengths(a_path.size(), 0.0f);
		for (size_t j = 1; j < a_path.size(); ++j) {
			lengths[j] = ozz::math::Length(GetTranslation(a_actor.model[a_path[j]]) - GetTranslation(a_actor.model[a_path[j - 1]]));
		}
		return GetSplineControlCandidates(lengths).size();
	}
}
