#pragma once
#include "Animation/Studio/Modifiers/Modifier.h"

namespace Animation::Studio
{
	// Finger curl and spread, applied additively to the local pose before LocalToModel. Properties on kSelf
	// apply to every finger; element i addresses finger i.
	class HandObject final : public ModifierObject
	{
	public:
		static constexpr ObjectType kType = ObjectType::kHand;
		[[nodiscard]] ObjectType GetType() const override { return kType; }
		[[nodiscard]] PropertyPtr FindProperty(uint16_t a_element, PropertyId a_property) override;
		void ForEachProperty(const PropertyVisitor& a_visitor) override;

		void ApplyToPose(ActorObject& a_actor) const override;
		[[nodiscard]] ModifierSettings GetSettings() const override { return settings; }
		void SetSettings(const ModifierSettings& a_settings) override;

		struct FingerProperties
		{
			Property<float> curl{ 0.0f, 0.0f };
			Property<float> spread{ 0.0f, 0.0f };
		};

		int32_t handBone{ -1 };
		HandSettings settings;

		Property<float> curl{ 0.0f, 0.0f };  // "Fist": added to every finger's curl.
		Property<float> spread{ 0.0f, 0.0f };
		Property<float> weight{ 1.0f, 1.0f };
		Property<bool> enabled{ true, true };
		std::vector<FingerProperties> fingers;  // Parallel to settings.fingers.
	};

	// Finger poses by role: thumb, then index to pinky. Curl 0 is a straight finger.
	struct HandPreset
	{
		const char* name;
		std::array<float, 5> curl;

		// The curl for finger a_finger of a hand with a_fingerCount fingers. Fingers past the table use its last
		// entry. A hand with fewer than three fingers has no thumb, so its first finger takes the index role.
		[[nodiscard]] float GetCurl(size_t a_finger, size_t a_fingerCount) const
		{
			const size_t roleOffset = a_fingerCount >= 3 ? 0 : 1;
			return curl[(std::min)(a_finger + roleOffset, curl.size() - 1)];
		}
	};

	// Relaxed comes first: new hands start with it, since curl 0 is flat and would visibly change the hand.
	inline constexpr std::array<HandPreset, 5> kHandPresets{ {
		{ "Relaxed", { 0.2f, 0.25f, 0.3f, 0.35f, 0.4f } },
		{ "Fist", { 0.8f, 1.0f, 1.0f, 1.0f, 1.0f } },
		{ "Point", { 0.7f, 0.0f, 1.0f, 1.0f, 1.0f } },
		{ "Grip", { 0.5f, 0.6f, 0.6f, 0.6f, 0.6f } },
		{ "Flat", { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
	} };
	inline constexpr const HandPreset& kRelaxedHandPreset = kHandPresets[0];

	// Each finger is a chain hanging directly from the hand bone, with per-bone setup of matching length.
	[[nodiscard]] bool IsValidHand(const ozz::animation::Skeleton& a_skeleton, int32_t a_handBone, const HandSettings& a_settings);
	// Finds the fingers below a_handBone and works out their axes and limits from the skeleton's rest pose.
	[[nodiscard]] std::expected<HandSettings, std::string> DetectHand(const ozz::animation::Skeleton& a_skeleton, int32_t a_handBone);
	// Finger controls for the fingers below a_handBone, starting in the Relaxed preset. Takes its id from a_nextId.
	[[nodiscard]] std::expected<ModifierBuild, std::string> BuildHand(const ActorObject& a_actor, int32_t a_handBone, ObjectId& a_nextId);
}
