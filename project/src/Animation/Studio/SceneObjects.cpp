#include "SceneObjects.h"

#include "Curve.h"
#include "StudioMath.h"

namespace Animation::Studio
{
	namespace
	{
		constexpr bool kAlternativesAligned = []<size_t... I>(std::index_sequence<I...>) {
			return ((std::is_same_v<std::variant_alternative_t<I, AnyKey>, Key<std::variant_alternative_t<I, Value>>> &&
						std::is_same_v<std::variant_alternative_t<I, AnyCurve>, Curve<std::variant_alternative_t<I, Value>>> &&
						std::is_same_v<std::variant_alternative_t<I + 1, PropertyPtr>, Property<std::variant_alternative_t<I, Value>>*>) &&
					...);
		}(std::make_index_sequence<std::variant_size_v<Value>>{});
		static_assert(kAlternativesAligned, "Value, AnyKey, AnyCurve and PropertyPtr alternatives must be in the same order.");
	}

	// ---- Properties ----

	bool MatchesType(PropertyPtr a_property, size_t a_valueIndex)
	{
		return !std::holds_alternative<std::monostate>(a_property) && a_property.index() - 1 == a_valueIndex;
	}

	Value GetLiveValue(PropertyPtr a_property)
	{
		return std::visit([](auto a_ptr) -> Value {
			if constexpr (std::is_same_v<decltype(a_ptr), std::monostate>) {
				return {};
			} else {
				return a_ptr->live;
			}
		}, a_property);
	}

	Value GetDefaultValue(PropertyPtr a_property)
	{
		return std::visit([](auto a_ptr) -> Value {
			if constexpr (std::is_same_v<decltype(a_ptr), std::monostate>) {
				return {};
			} else {
				return a_ptr->defaultValue;
			}
		}, a_property);
	}

	bool IsEdited(PropertyPtr a_property)
	{
		return std::visit([](auto a_ptr) {
			if constexpr (std::is_same_v<decltype(a_ptr), std::monostate>) {
				return false;
			} else {
				return a_ptr->edited;
			}
		}, a_property);
	}

	void SetLiveValue(PropertyPtr a_property, const Value& a_value, bool a_edited)
	{
		std::visit([&](auto a_ptr) {
			if constexpr (!std::is_same_v<decltype(a_ptr), std::monostate>) {
				if (const auto value = std::get_if<decltype(a_ptr->live)>(&a_value)) {
					a_ptr->live = *value;
					a_ptr->edited = a_edited;
				}
			}
		}, a_property);
	}

	void ResetLiveValue(PropertyPtr a_property)
	{
		std::visit([](auto a_ptr) {
			if constexpr (!std::is_same_v<decltype(a_ptr), std::monostate>) {
				a_ptr->live = a_ptr->defaultValue;
				a_ptr->edited = false;
			}
		}, a_property);
	}

	PropertyPtr TransformProperties::Find(PropertyId a_property)
	{
		switch (a_property) {
		case PropertyId::kTranslation:
			return &translation;
		case PropertyId::kRotation:
			return &rotation;
		case PropertyId::kScale:
			return &scale;
		default:
			return {};
		}
	}

	void TransformProperties::ForEach(uint16_t a_element, const PropertyVisitor& a_visitor)
	{
		a_visitor(a_element, PropertyId::kTranslation, &translation);
		a_visitor(a_element, PropertyId::kRotation, &rotation);
		a_visitor(a_element, PropertyId::kScale, &scale);
	}

	LocatorDefaults TransformProperties::GetDefaults() const
	{
		return { translation.defaultValue, rotation.defaultValue, scale.defaultValue };
	}

	LocatorDefaults TransformProperties::GetLive() const
	{
		return { translation.live, Normalized(rotation.live), scale.live };
	}

	ozz::math::Float4x4 TransformProperties::GetLiveMatrix() const
	{
		return ToMatrix(GetLive());
	}

	// ---- Actor ----

	PropertyPtr ActorObject::FindProperty(uint16_t a_element, PropertyId a_property)
	{
		return a_element < bones.size() ? bones[a_element].Find(a_property) : PropertyPtr{};
	}

	void ActorObject::ForEachProperty(const PropertyVisitor& a_visitor)
	{
		for (size_t i = 0; i < bones.size(); ++i) {
			bones[i].ForEach(static_cast<uint16_t>(i), a_visitor);
		}
	}

	void ActorObject::InitializePose(std::shared_ptr<const OzzSkeleton> a_skeleton, bool a_resetBones)
	{
		const auto& data = *a_skeleton->data;
		const auto restPoses = data.joint_rest_poses();
		const auto jointCount = static_cast<size_t>(data.num_joints());

		if (a_resetBones) {
			const auto rest = UnpackSoaTransforms({ restPoses.data(), restPoses.size() }, jointCount);
			bones.assign(jointCount, {});
			for (size_t i = 0; i < jointCount; ++i) {
				auto& bone = bones[i];
				bone.translation = { rest[i].translation, rest[i].translation };
				bone.rotation = { rest[i].rotation, rest[i].rotation };
				bone.scale = { rest[i].scale, rest[i].scale };
			}
		}

		pose = std::make_shared<SharedPose>(SharedPose::Buffer(restPoses.begin(), restPoses.end()));
		model.assign(jointCount, ozz::math::Float4x4::identity());
		skeleton = std::move(a_skeleton);
	}

	void ActorObject::PackPose()
	{
		auto& buffer = pose->Back();
		const size_t count = bones.size();
		buffer.resize((count + 3) / 4);
		for (size_t i = 0; i < buffer.size(); ++i) {
			float t[3][4], r[4][4], s[3][4];
			for (size_t lane = 0; lane < 4; ++lane) {
				const size_t joint = i * 4 + lane;
				auto translation = ozz::math::Float3::zero();
				auto rotation = ozz::math::Quaternion::identity();
				auto scale = ozz::math::Float3::one();
				if (joint < count) {
					const auto& bone = bones[joint];
					translation = bone.translation.live;
					rotation = Normalized(bone.rotation.live);
					scale = bone.scale.live;
				}
				t[0][lane] = translation.x, t[1][lane] = translation.y, t[2][lane] = translation.z;
				r[0][lane] = rotation.x, r[1][lane] = rotation.y, r[2][lane] = rotation.z, r[3][lane] = rotation.w;
				s[0][lane] = scale.x, s[1][lane] = scale.y, s[2][lane] = scale.z;
			}
			using ozz::math::simd_float4::LoadPtrU;
			buffer[i].translation = ozz::math::SoaFloat3::Load(LoadPtrU(t[0]), LoadPtrU(t[1]), LoadPtrU(t[2]));
			buffer[i].rotation = ozz::math::SoaQuaternion::Load(LoadPtrU(r[0]), LoadPtrU(r[1]), LoadPtrU(r[2]), LoadPtrU(r[3]));
			buffer[i].scale = ozz::math::SoaFloat3::Load(LoadPtrU(s[0]), LoadPtrU(s[1]), LoadPtrU(s[2]));
		}
	}

	void ActorObject::RunLocalToModel(int32_t a_from)
	{
		ozz::animation::LocalToModelJob job;
		job.skeleton = skeleton->data.get();
		job.input = ozz::make_span(pose->Back());
		job.output = ozz::make_span(model);
		job.from = a_from;
		job.Run();
	}

	bool ActorObject::IsBoneMasked(int32_t a_bone) const
	{
		if (!skeleton || a_bone < 0 || a_bone >= static_cast<int32_t>(skeleton->defaultBoneMask.size())) return false;
		const auto bone = maskOverrides.find(a_bone);
		return bone != maskOverrides.end() ? bone->second : !skeleton->defaultBoneMask[a_bone];
	}

	void ActorObject::PublishBoneMask()
	{
		if (!skeleton || !pose) return;
		auto mask = skeleton->defaultBoneMask;
		for (const auto& [bone, masked] : maskOverrides) {
			if (bone >= 0 && bone < static_cast<int32_t>(mask.size())) mask[bone] = !masked;
		}
		pose->SetMask(std::move(mask));
	}

	// ---- Locator ----

	PropertyPtr LocatorObject::FindProperty(uint16_t a_element, PropertyId a_property)
	{
		return a_element == kSelf ? transform.Find(a_property) : PropertyPtr{};
	}

	void LocatorObject::ForEachProperty(const PropertyVisitor& a_visitor)
	{
		transform.ForEach(kSelf, a_visitor);
	}
}
