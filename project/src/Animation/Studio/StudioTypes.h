#pragma once
#include "Animation/Ozz.h"

// The Studio's shared vocabulary: ids, references, values and settings. Used by the model, the UI and the file format.
namespace Animation::Studio
{
	using ObjectId = uint32_t;
	using SequenceId = uint32_t;

	inline constexpr ObjectId kNoObject = 0;
	inline constexpr SequenceId kNoSequence = 0;
	// PropertyRef::element value that addresses the object itself rather than one of its sub-elements (bones).
	inline constexpr uint16_t kSelf = 0xFFFF;

	// The file format's object type names, and the importer's FileObject variant, follow this order.
	enum class ObjectType : uint8_t
	{
		kActor,
		kLocator,
		kIKChain,
		kHand,
		kSplineIK
	};

	enum class PropertyId : uint8_t
	{
		kTranslation,
		kRotation,
		kScale,
		kWeight,
		kSoften,
		kTwist,
		kEnabled,
		kShoulderWeight,
		kCurl,
		kSpread,
		kStretch,
		kTangentLength,
		kOrient,
		kTwistDistribution
	};

	enum class Interpolation : uint8_t
	{
		kLinear,
		kBezier,
		kStep
	};

	enum class TangentMode : uint8_t
	{
		kAuto
	};

	// Addresses a single animatable property. Tracks, history and the UI all refer to properties this way.
	struct PropertyRef
	{
		ObjectId object{ kNoObject };
		uint16_t element{ kSelf };
		PropertyId property{ PropertyId::kTranslation };

		auto operator<=>(const PropertyRef&) const = default;
	};

	// A track holds at most one key per frame, so a key is fully identified by its track and frame.
	struct KeyRef
	{
		PropertyRef property;
		int32_t frame{ 0 };

		auto operator<=>(const KeyRef&) const = default;
	};

	// Addresses something with a transform: a bone (element = bone index) or the object itself (kSelf).
	struct ElementRef
	{
		ObjectId object{ kNoObject };
		uint16_t element{ kSelf };

		auto operator<=>(const ElementRef&) const = default;
	};

	enum TransformMask : uint8_t
	{
		kTransformTranslation = 1 << 0,
		kTransformRotation = 1 << 1,
		kTransformScale = 1 << 2,
		kTransformAll = kTransformTranslation | kTransformRotation | kTransformScale
	};

	struct ParentRef
	{
		ObjectId object{ kNoObject };  // kNoObject means scene space. Otherwise an actor or a locator.
		int32_t bone{ -1 };            // Actors only. -1 means the actor root.

		auto operator<=>(const ParentRef&) const = default;
	};

	// A position and heading, the way the game places references: heading is in radians about Z, and a heading
	// of 0 faces +Y.
	struct ScenePlacement
	{
		ozz::math::Float3 translation{ ozz::math::Float3::zero() };
		float heading{ 0.0f };

		bool operator==(const ScenePlacement&) const = default;
	};

	// A translation, rotation and scale, such as a locator's defaults or a rig's bone pose.
	struct LocatorDefaults
	{
		ozz::math::Float3 translation{ ozz::math::Float3::zero() };
		ozz::math::Quaternion rotation{ ozz::math::Quaternion::identity() };
		ozz::math::Float3 scale{ ozz::math::Float3::one() };

		bool operator==(const LocatorDefaults&) const = default;
	};

	template <class T>
	struct Key
	{
		int32_t frame{ 0 };
		Interpolation interpolation{ Interpolation::kLinear };
		TangentMode tangentMode{ TangentMode::kAuto };
		T value{};
		T inTangent{};
		T outTangent{};

		bool operator==(const Key&) const = default;
	};

	// The alternative order of Value, AnyKey, AnyCurve and PropertyPtr must match (PropertyPtr is offset by its
	// monostate). SceneObjects.cpp checks it.
	using Value = std::variant<float, bool, ozz::math::Float3, ozz::math::Quaternion>;
	using AnyKey = std::variant<Key<float>, Key<bool>, Key<ozz::math::Float3>, Key<ozz::math::Quaternion>>;

	// Views returned by ProjectEx's getters. Any string_views stay valid until the next mutation.
	struct SequenceInfo
	{
		SequenceId id{ kNoSequence };
		std::string_view name;
		float frameRate{ 30.0f };
		int32_t frameCount{ 30 };
		bool loopSmoothing{ false };
	};

	// Loop smoothing (shown as "Cyclic" in the UI) makes a curve periodic over [0, frameCount): sampling and
	// auto tangents wrap from the last key back around to the first, so keys don't need to sit on the loop
	// boundary. It's on for a property when it's on for the property or for its sequence.
	struct LoopSmoothingInfo
	{
		bool property{ false };
		bool sequence{ false };
		bool applied{ false };  // Enabled, and the curve has at least one key in [0, frameCount].
	};

	// ---- Modifier settings ----
	// Not keyable. Each Set*Settings call is its own undo step.

	struct IKChainSettings
	{
		int32_t shoulderBone{ -1 };  // A strict ancestor of the upper bone that swings toward the target, or -1.
		float shoulderMaxAngle{ 30.0f * ozz::math::kDegreeToRadian };  // A soft limit: the swing eases into it.
		// Where the effector counts as relaxed, in the space of the shoulder's parent. The shoulder aims at a point an
		// arm's length out along itself, moved by the effector's offset from here.
		ozz::math::Float3 shoulderNeutral{ 0.0f, 0.0f, 0.0f };

		bool operator==(const IKChainSettings&) const = default;
	};

	struct FingerSetup
	{
		std::vector<int32_t> bones;                        // Base to tip.
		std::vector<ozz::math::Float3> curlAxes;           // Per bone, in that bone's local space.
		ozz::math::Float3 spreadAxis{ 0.0f, 0.0f, 1.0f };  // In the base bone's local space.
		float spreadScale{ 1.0f };                         // Signed: positive spread fans the finger away from the hand's middle.
		std::vector<float> maxCurl;                        // Radians, per bone.
		float maxSpread{ 15.0f * ozz::math::kDegreeToRadian };
		bool flip{ false };
		// Radians about each curl axis from the rest pose to straight, so curl 0 is a flat finger. Empty on hands
		// saved before version 5, where curl 0 is the rest pose and maxCurl applies both ways.
		std::vector<float> straighten;
		std::vector<float> maxExtend;  // Radians per bone that curl -1 bends back past straight. Empty with straighten.

		bool operator==(const FingerSetup&) const = default;
	};

	struct HandSettings
	{
		std::vector<FingerSetup> fingers;  // Thumb first when one was found, then by distance from it.

		bool operator==(const HandSettings&) const = default;
	};

	struct SplineIKSettings
	{
		bool rootFollowsCurve{ false };  // Otherwise the first bone keeps its FK position and the curve is offset onto it.
		bool alignTip{ true };           // The last bone takes the last control's rotation.

		bool operator==(const SplineIKSettings&) const = default;
	};

	using ModifierSettings = std::variant<IKChainSettings, HandSettings, SplineIKSettings>;

	// What the shoulder aimed at: the effector's relaxed position and the point the shoulder tracks.
	struct ShoulderGuide
	{
		ozz::math::Float3 shoulder;
		ozz::math::Float3 neutral;
		ozz::math::Float3 dummy;
	};

	struct IKChainInfo
	{
		ObjectId actor{ kNoObject };
		int32_t upperBone{ -1 };
		int32_t middleBone{ -1 };
		int32_t endBone{ -1 };
		ObjectId effector{ kNoObject };
		ObjectId pole{ kNoObject };
		bool reached{ true };
		IKChainSettings settings;
		// The shoulder's last solve, in world space. Nullopt without a shoulder.
		std::optional<ShoulderGuide> shoulderGuide;
	};

	struct HandInfo
	{
		ObjectId actor{ kNoObject };
		int32_t handBone{ -1 };
		HandSettings settings;
	};

	struct SplineIKInfo
	{
		ObjectId actor{ kNoObject };
		std::vector<int32_t> bones;  // Start to end.
		std::vector<ObjectId> controls;
		SplineIKSettings settings;
	};

	// Clipboard payload. The key's frame is part of the key.
	struct KeyData
	{
		PropertyRef property;
		AnyKey key;
	};

	struct WorldTransformEdit
	{
		ElementRef element;
		ozz::math::Float4x4 world;
	};

	// ---- Rigs ----

	struct RigOptions
	{
		bool pose{ true };
		bool ikChains{ true };
		bool hands{ true };
		bool splineIKs{ true };
		bool boneMask{ true };
		bool replaceModifiers{ true };  // Removes the actor's modifiers of the applied types first.

		[[nodiscard]] bool Applies(ObjectType a_type) const
		{
			return (a_type == ObjectType::kIKChain && ikChains) || (a_type == ObjectType::kHand && hands) || (a_type == ObjectType::kSplineIK && splineIKs);
		}
	};

	struct RigResult
	{
		std::vector<std::string> warnings;
		std::vector<PropertyRef> poseChanges;  // Bone properties the pose changed, so the caller can key them.
	};
}
