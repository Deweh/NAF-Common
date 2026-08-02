#include "StudioProjectImport.h"

#include "StudioProjectFormat.h"
#include "Animation/Studio/Modifiers/Hand.h"
#include "Animation/Studio/Modifiers/IKChain.h"
#include "Animation/Studio/Modifiers/SplineIK.h"
#include "Animation/Studio/Skeleton.h"
#include "Util/String.h"

#include <fstream>
#include <simdjson.h>

namespace Serialization
{
	namespace
	{
		using ProjectEx = Animation::Studio::ProjectEx;
		using ObjectId = ProjectEx::ObjectId;
		using SequenceId = ProjectEx::SequenceId;
		using PropertyId = ProjectEx::PropertyId;
		using Element = simdjson::dom::element;
		namespace Format = StudioProjectFormat;

		constexpr uint64_t kMaximumJsonSize = 64ull * 1024ull * 1024ull;

		// ---- Parsed contents ----

		struct FileProperty
		{
			ObjectId object{ ProjectEx::kNoObject };
			std::optional<std::string> bone;  // Actor bones only.
			std::optional<uint16_t> finger;   // Hand fingers only.
			PropertyId property{ PropertyId::kTranslation };
		};

		// Same alternative order as ProjectEx::Value.
		using KeyList = std::variant<std::vector<ProjectEx::Key<float>>, std::vector<ProjectEx::Key<bool>>,
			std::vector<ProjectEx::Key<ozz::math::Float3>>, std::vector<ProjectEx::Key<ozz::math::Quaternion>>>;

		struct FileTrack
		{
			FileProperty property;
			KeyList keys;
		};

		struct FileSequence
		{
			SequenceId id{ ProjectEx::kNoSequence };
			std::string name;
			float frameRate{ 30.0f };
			int32_t frameCount{ 30 };
			bool loopSmoothing{ false };
			std::vector<FileTrack> tracks;
			std::vector<FileProperty> loopSmoothedProperties;
		};

		struct FileLiveValue
		{
			FileProperty property;
			ProjectEx::Value value;
			bool edited{ false };
		};

		struct FileBonePose
		{
			std::string bone;
			ozz::math::Float3 translation{ ozz::math::Float3::zero() };
			ozz::math::Quaternion rotation{ ozz::math::Quaternion::identity() };
			ozz::math::Float3 scale{ ozz::math::Float3::one() };
		};

		struct FileActor
		{
			size_t slot{ 0 };
			ProjectEx::ScenePlacement placement;
			std::vector<ObjectId> modifiers;
			std::vector<std::pair<std::string, bool>> maskOverrides;  // Optional. Bone, masked.
		};

		struct FileLocator
		{
			ObjectId parent{ ProjectEx::kNoObject };
			std::optional<std::string> parentBone;  // Empty for the actor root.
			ozz::math::Float3 translation{ ozz::math::Float3::zero() };
			ozz::math::Quaternion rotation{ ozz::math::Quaternion::identity() };
			ozz::math::Float3 scale{ ozz::math::Float3::one() };
		};

		struct FileIKChain
		{
			ObjectId actor{ ProjectEx::kNoObject };
			std::array<std::string, 3> bones;  // Upper, middle, end.
			ozz::math::Float3 middleAxis{ 0.0f, 0.0f, 1.0f };
			ObjectId effector{ ProjectEx::kNoObject };
			ObjectId pole{ ProjectEx::kNoObject };
			float weight{ 1.0f };
			float soften{ 0.95f };
			float twist{ 0.0f };
			bool enabled{ true };
			float shoulderWeight{ 0.5f };
			float twistDistribution{ 1.0f };
			std::optional<std::string> shoulderBone;
			std::optional<ozz::math::Float3> shoulderNeutral;  // Version 6. Without it, the end bone's rest position.
			ProjectEx::IKChainSettings settings;  // Without its shoulder bone, which is resolved by name.
		};

		struct FileFinger
		{
			std::vector<std::string> bones;
			std::vector<ozz::math::Float3> curlAxes;
			ozz::math::Float3 spreadAxis{ 0.0f, 0.0f, 1.0f };
			float spreadScale{ 1.0f };
			std::vector<float> maxCurl;
			float maxSpread{ 0.0f };
			bool flip{ false };
			std::vector<float> straighten;  // Empty before version 5.
			std::vector<float> maxExtend;
			float curl{ 0.0f };
			float spread{ 0.0f };
		};

		struct FileHand
		{
			ObjectId actor{ ProjectEx::kNoObject };
			std::string handBone;
			std::vector<FileFinger> fingers;
			float curl{ 0.0f };
			float spread{ 0.0f };
			float weight{ 1.0f };
			bool enabled{ true };
		};

		struct FileSplineIK
		{
			ObjectId actor{ ProjectEx::kNoObject };
			std::vector<std::string> bones;  // The path, start to end.
			std::vector<ozz::math::Float3> aimAxes;
			std::vector<ozz::math::Float3> upAxes;
			std::vector<ObjectId> controls;
			std::vector<std::pair<ozz::math::Float3, ozz::math::Float3>> controlAxes;  // Aim, up.
			ProjectEx::SplineIKSettings settings;
			float weight{ 1.0f };
			float twist{ 0.0f };
			float stretch{ 0.0f };
			float tangentLength{ 1.0f };
			float orient{ 1.0f };
			bool enabled{ true };
		};

		// Same alternative order as ProjectEx::ObjectType.
		using FileObjectData = std::variant<FileActor, FileLocator, FileIKChain, FileHand, FileSplineIK>;

		// ---- Reading ----

		[[noreturn]] void Fail(std::string a_message)
		{
			throw std::runtime_error(std::move(a_message));
		}

		std::optional<Element> OptionalField(Element a_object, std::string_view a_name)
		{
			Element result;
			if (a_object.at_key(a_name).get(result) != simdjson::SUCCESS) return std::nullopt;
			return result;
		}

		Element Field(Element a_object, std::string_view a_name)
		{
			const auto result = OptionalField(a_object, a_name);
			if (!result) Fail(std::format("A required field (\"{}\") is missing.", a_name));
			return *result;
		}

		uint64_t ReadUInt(Element a_element, std::string_view a_name)
		{
			uint64_t value;
			if (a_element.get(value) != simdjson::SUCCESS) Fail(std::format("\"{}\" must be a non-negative integer.", a_name));
			return value;
		}

		uint32_t ReadUInt32(Element a_object, std::string_view a_name)
		{
			const auto value = ReadUInt(Field(a_object, a_name), a_name);
			if (value > std::numeric_limits<uint32_t>::max()) Fail(std::format("\"{}\" is out of range.", a_name));
			return static_cast<uint32_t>(value);
		}

		int32_t ReadInt32(Element a_object, std::string_view a_name)
		{
			int64_t value;
			if (Field(a_object, a_name).get(value) != simdjson::SUCCESS || value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
				Fail(std::format("\"{}\" must be a 32-bit integer.", a_name));
			}
			return static_cast<int32_t>(value);
		}

		float ReadFloat(Element a_element, std::string_view a_name)
		{
			double value;
			if (a_element.get(value) != simdjson::SUCCESS || !std::isfinite(static_cast<float>(value))) Fail(std::format("\"{}\" must be a finite number.", a_name));
			return static_cast<float>(value);
		}

		bool ReadBool(Element a_element, std::string_view a_name)
		{
			bool value;
			if (a_element.get(value) != simdjson::SUCCESS) Fail(std::format("\"{}\" must be true or false.", a_name));
			return value;
		}

		std::string ReadString(Element a_element, std::string_view a_name)
		{
			std::string_view value;
			if (a_element.get(value) != simdjson::SUCCESS) Fail(std::format("\"{}\" must be a string.", a_name));
			return std::string(value);
		}

		simdjson::dom::array ReadArray(Element a_element, std::string_view a_name)
		{
			simdjson::dom::array value;
			if (a_element.get(value) != simdjson::SUCCESS) Fail(std::format("\"{}\" must be an array.", a_name));
			return value;
		}

		template <size_t N>
		std::array<float, N> ReadFloats(Element a_element, std::string_view a_name)
		{
			const auto array = ReadArray(a_element, a_name);
			if (array.size() != N) Fail(std::format("\"{}\" must have {} components.", a_name, N));
			std::array<float, N> result;
			size_t i = 0;
			for (const Element component : array) result[i++] = ReadFloat(component, a_name);
			return result;
		}

		ozz::math::Float3 ReadFloat3(Element a_element, std::string_view a_name)
		{
			const auto values = ReadFloats<3>(a_element, a_name);
			return { values[0], values[1], values[2] };
		}

		std::vector<std::string> ReadStrings(Element a_element, std::string_view a_name)
		{
			std::vector<std::string> result;
			for (const Element item : ReadArray(a_element, a_name)) result.push_back(ReadString(item, a_name));
			return result;
		}

		std::vector<float> ReadFloatArray(Element a_element, std::string_view a_name)
		{
			std::vector<float> result;
			for (const Element item : ReadArray(a_element, a_name)) result.push_back(ReadFloat(item, a_name));
			return result;
		}

		std::vector<ozz::math::Float3> ReadFloat3Array(Element a_element, std::string_view a_name)
		{
			std::vector<ozz::math::Float3> result;
			for (const Element item : ReadArray(a_element, a_name)) result.push_back(ReadFloat3(item, a_name));
			return result;
		}

		// Fields added after version 3 are optional, with the defaults a new object would have.
		float ReadOptionalFloat(Element a_object, std::string_view a_name, float a_default)
		{
			const auto field = OptionalField(a_object, a_name);
			return field ? ReadFloat(*field, a_name) : a_default;
		}

		bool ReadOptionalBool(Element a_object, std::string_view a_name, bool a_default)
		{
			const auto field = OptionalField(a_object, a_name);
			return field ? ReadBool(*field, a_name) : a_default;
		}

		ozz::math::Quaternion NormalizeRotation(const ozz::math::Quaternion& a_value)
		{
			if (!(ozz::math::Dot(a_value, a_value) > 0.000001f)) Fail("A rotation has zero length.");
			return ozz::math::NormalizeSafe(a_value, ozz::math::Quaternion::identity());
		}

		ozz::math::Quaternion ReadQuaternion(Element a_element, std::string_view a_name)
		{
			const auto values = ReadFloats<4>(a_element, a_name);
			return NormalizeRotation({ values[0], values[1], values[2], values[3] });
		}

		// The value type each property holds, as a ProjectEx::Value index. Bones only belong to actors, and fingers
		// to hands.
		size_t GetValueIndex(ProjectEx::ObjectType a_type, bool a_hasBone, bool a_hasFinger, PropertyId a_property)
		{
			const auto is = [&](std::initializer_list<PropertyId> a_properties) { return std::ranges::contains(a_properties, a_property); };
			const bool transform = is({ PropertyId::kTranslation, PropertyId::kRotation, PropertyId::kScale });
			bool valid = false;
			switch (a_type) {
			case ProjectEx::ObjectType::kActor:
				valid = a_hasBone && !a_hasFinger && transform;
				break;
			case ProjectEx::ObjectType::kLocator:
				valid = !a_hasBone && !a_hasFinger && transform;
				break;
			case ProjectEx::ObjectType::kIKChain:
				valid = !a_hasBone && !a_hasFinger && is({ PropertyId::kWeight, PropertyId::kSoften, PropertyId::kTwist, PropertyId::kEnabled, PropertyId::kShoulderWeight, PropertyId::kTwistDistribution });
				break;
			case ProjectEx::ObjectType::kHand:
				valid = !a_hasBone && (a_hasFinger ? is({ PropertyId::kCurl, PropertyId::kSpread }) : is({ PropertyId::kCurl, PropertyId::kSpread, PropertyId::kWeight, PropertyId::kEnabled }));
				break;
			case ProjectEx::ObjectType::kSplineIK:
				valid = !a_hasBone && !a_hasFinger && is({ PropertyId::kWeight, PropertyId::kTwist, PropertyId::kStretch, PropertyId::kTangentLength, PropertyId::kOrient, PropertyId::kEnabled });
				break;
			}
			if (!valid) Fail("A property does not belong to its object.");
			switch (a_property) {
			case PropertyId::kTranslation:
			case PropertyId::kScale:
				return 2;
			case PropertyId::kRotation:
				return 3;
			case PropertyId::kEnabled:
				return 1;
			default:
				return 0;
			}
		}

		ProjectEx::Value ReadValue(Element a_element, size_t a_valueIndex)
		{
			switch (a_valueIndex) {
			case 0:
				return ReadFloat(a_element, "value");
			case 1:
				return ReadBool(a_element, "value");
			case 2:
				return ReadFloat3(a_element, "value");
			default:
				return ReadQuaternion(a_element, "value");
			}
		}

		class BinaryReader
		{
		public:
			explicit BinaryReader(std::span<const std::byte> a_data) :
				m_data(a_data)
			{}

			template <class T>
			T Read()
			{
				static_assert(std::is_trivially_copyable_v<T>);
				if (sizeof(T) > m_data.size() - m_offset) Fail("The project file is truncated.");
				T value;
				std::memcpy(std::addressof(value), m_data.data() + m_offset, sizeof(T));
				m_offset += sizeof(T);
				return value;
			}

			float ReadFloat()
			{
				const auto value = Read<float>();
				if (!std::isfinite(value)) Fail("A keyframe value is not a finite number.");
				return value;
			}

			template <class T>
			T ReadValue()
			{
				if constexpr (std::is_same_v<T, float>) {
					return ReadFloat();
				} else if constexpr (std::is_same_v<T, bool>) {
					const auto value = Read<uint8_t>();
					if (value > 1) Fail("A keyframe value is not a valid boolean.");
					return value != 0;
				} else if constexpr (std::is_same_v<T, ozz::math::Float3>) {
					const float x = ReadFloat();
					const float y = ReadFloat();
					const float z = ReadFloat();
					return { x, y, z };
				} else {
					const float x = ReadFloat();
					const float y = ReadFloat();
					const float z = ReadFloat();
					const float w = ReadFloat();
					return NormalizeRotation({ x, y, z, w });
				}
			}

		private:
			std::span<const std::byte> m_data;
			size_t m_offset{ 0 };
		};

		template <class T>
		std::vector<ProjectEx::Key<T>> ReadKeys(std::span<const std::byte> a_data, size_t a_count)
		{
			BinaryReader reader(a_data);
			std::vector<ProjectEx::Key<T>> keys;
			keys.reserve(a_count);
			for (size_t i = 0; i < a_count; ++i) {
				ProjectEx::Key<T> key;
				key.frame = reader.Read<int32_t>();
				const auto interpolation = reader.Read<uint8_t>();
				const auto tangentMode = reader.Read<uint8_t>();
				key.value = reader.ReadValue<T>();
				if (key.frame < 0 || (!keys.empty() && key.frame <= keys.back().frame)) Fail("A track's keyframes are not in order.");
				if (interpolation > static_cast<uint8_t>(ProjectEx::Interpolation::kStep)) Fail("A keyframe has an unknown interpolation.");
				if (tangentMode != static_cast<uint8_t>(ProjectEx::TangentMode::kAuto)) Fail("A keyframe has an unknown tangent mode.");
				// Booleans can only step.
				key.interpolation = std::is_same_v<T, bool> ? ProjectEx::Interpolation::kStep : static_cast<ProjectEx::Interpolation>(interpolation);
				key.tangentMode = ProjectEx::TangentMode::kAuto;
				key.inTangent = key.value;
				key.outTangent = key.value;
				keys.push_back(key);
			}
			return keys;
		}

		KeyList ReadKeyList(size_t a_valueIndex, std::span<const std::byte> a_data, size_t a_count)
		{
			switch (a_valueIndex) {
			case 0:
				return ReadKeys<float>(a_data, a_count);
			case 1:
				return ReadKeys<bool>(a_data, a_count);
			case 2:
				return ReadKeys<ozz::math::Float3>(a_data, a_count);
			default:
				return ReadKeys<ozz::math::Quaternion>(a_data, a_count);
			}
		}

		template <size_t N>
		size_t FindName(const std::array<std::string_view, N>& a_names, std::string_view a_name, std::string_view a_what)
		{
			const auto iter = std::ranges::find(a_names, a_name);
			if (iter == a_names.end()) Fail(std::format("Unknown {} \"{}\".", a_what, a_name));
			return static_cast<size_t>(std::distance(a_names.begin(), iter));
		}

		std::optional<std::string> ReadOptionalBone(Element a_object, std::string_view a_name)
		{
			const auto bone = OptionalField(a_object, a_name);
			return bone ? std::optional{ ReadString(*bone, a_name) } : std::nullopt;
		}

		// Plugin-relative first, so the hint survives load order changes.
		RE::TESFormID ResolveBindingHint(Element a_hint)
		{
			RE::TESForm* form = nullptr;
			const auto plugin = OptionalField(a_hint, "plugin");
			const auto localFormId = OptionalField(a_hint, "localFormId");
			if (plugin && localFormId) {
				if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
					form = dataHandler->LookupForm(static_cast<RE::TESFormID>(ReadUInt(*localFormId, "localFormId")), ReadString(*plugin, "plugin"));
				}
			} else if (const auto formId = OptionalField(a_hint, "formId")) {
				form = RE::TESForm::GetFormByID(static_cast<RE::TESFormID>(ReadUInt(*formId, "formId")));
			}
			return form && form->As<RE::Actor>() ? form->GetFormID() : 0;
		}

		RE::SEX ReadGender(Element a_object)
		{
			const auto gender = ReadString(Field(a_object, "gender"), "gender");
			return gender == "male" ? RE::SEX::kMale : gender == "female" ? RE::SEX::kFemale : RE::SEX::kNone;
		}

		struct Container
		{
			std::vector<std::byte> contents;
			uint32_t version{ 0 };
			size_t jsonSize{ 0 };  // The JSON follows the header; any binary data follows the JSON.
		};

		// Reads a whole file and checks its header. a_what names the kind of file in messages. The caller checks the version.
		std::expected<Container, std::string> ReadContainer(const std::filesystem::path& a_path, const std::array<char, 8>& a_magic, std::string_view a_what)
		{
			std::ifstream stream(a_path, std::ios::binary | std::ios::ate);
			if (!stream) return std::unexpected(std::format("Could not open the {} file.", a_what));
			const auto fileSize = static_cast<size_t>(stream.tellg());
			if (fileSize < Format::kHeaderSize) return std::unexpected(std::format("The {} file is truncated.", a_what));
			stream.seekg(0);
			Container result;
			result.contents.resize(fileSize);
			stream.read(reinterpret_cast<char*>(result.contents.data()), static_cast<std::streamsize>(fileSize));
			if (!stream) return std::unexpected(std::format("Could not read the {} file.", a_what));

			BinaryReader header(result.contents);
			std::array<char, 8> magic{};
			for (char& value : magic) value = header.Read<char>();
			result.version = header.Read<uint32_t>();
			const auto jsonSize = header.Read<uint64_t>();
			if (magic != a_magic) return std::unexpected(std::format("This is not a NAF Studio {}.", a_what));
			if (jsonSize > kMaximumJsonSize || jsonSize > fileSize - Format::kHeaderSize) return std::unexpected(std::format("The {} file is corrupt (invalid metadata size).", a_what));
			result.jsonSize = static_cast<size_t>(jsonSize);
			return result;
		}

		// Any object but an actor. a_currentShoulder is false for files older than the current shoulder schema.
		FileObjectData ReadObjectData(Element a_item, ProjectEx::ObjectType a_type, const std::string& a_name, bool a_currentShoulder)
		{
			switch (a_type) {
			case ProjectEx::ObjectType::kLocator:
				{
					FileLocator locator;
					const auto parent = Field(a_item, "parent");
					locator.parent = ReadUInt32(parent, "object");
					locator.parentBone = ReadOptionalBone(parent, "bone");
					const auto defaults = Field(a_item, "defaults");
					locator.translation = ReadFloat3(Field(defaults, "translation"), "translation");
					locator.rotation = ReadQuaternion(Field(defaults, "rotation"), "rotation");
					locator.scale = ReadFloat3(Field(defaults, "scale"), "scale");
					return locator;
				}
			case ProjectEx::ObjectType::kIKChain:
				{
					FileIKChain chain;
					chain.actor = ReadUInt32(a_item, "actor");
					const auto bones = ReadArray(Field(a_item, "bones"), "bones");
					if (bones.size() != chain.bones.size()) Fail("An IK chain must name three bones.");
					size_t i = 0;
					for (const Element bone : bones) chain.bones[i++] = ReadString(bone, "bones");
					chain.middleAxis = ReadFloat3(Field(a_item, "middleAxis"), "middleAxis");
					chain.effector = ReadUInt32(a_item, "effector");
					chain.pole = ReadUInt32(a_item, "pole");
					const auto defaults = Field(a_item, "defaults");
					chain.weight = ReadFloat(Field(defaults, "weight"), "weight");
					chain.soften = ReadFloat(Field(defaults, "soften"), "soften");
					chain.twist = ReadFloat(Field(defaults, "twist"), "twist");
					chain.enabled = ReadBool(Field(defaults, "enabled"), "enabled");
					chain.twistDistribution = ReadOptionalFloat(defaults, "twistDistribution", chain.twistDistribution);
					// Older shoulder weights tuned a different solve: keep the default instead.
					if (a_currentShoulder) chain.shoulderWeight = ReadOptionalFloat(defaults, "shoulderWeight", chain.shoulderWeight);
					if (const auto shoulder = OptionalField(a_item, "shoulder")) {
						chain.shoulderBone = ReadOptionalBone(*shoulder, "bone");
						chain.settings.shoulderMaxAngle = ReadOptionalFloat(*shoulder, "maxAngle", chain.settings.shoulderMaxAngle);
						if (const auto neutral = OptionalField(*shoulder, "neutral")) chain.shoulderNeutral = ReadFloat3(*neutral, "neutral");
					}
					return chain;
				}
			case ProjectEx::ObjectType::kHand:
				{
					FileHand hand;
					hand.actor = ReadUInt32(a_item, "actor");
					hand.handBone = ReadString(Field(a_item, "handBone"), "handBone");
					for (const Element fingerItem : ReadArray(Field(a_item, "fingers"), "fingers")) {
						FileFinger finger;
						finger.bones = ReadStrings(Field(fingerItem, "bones"), "bones");
						finger.curlAxes = ReadFloat3Array(Field(fingerItem, "curlAxes"), "curlAxes");
						finger.spreadAxis = ReadFloat3(Field(fingerItem, "spreadAxis"), "spreadAxis");
						finger.spreadScale = ReadFloat(Field(fingerItem, "spreadScale"), "spreadScale");
						finger.maxCurl = ReadFloatArray(Field(fingerItem, "maxCurl"), "maxCurl");
						finger.maxSpread = ReadFloat(Field(fingerItem, "maxSpread"), "maxSpread");
						finger.flip = ReadBool(Field(fingerItem, "flip"), "flip");
						// Version 5. Without them, curl 0 stays the rest pose, as the hand was made.
						if (const auto straighten = OptionalField(fingerItem, "straighten")) finger.straighten = ReadFloatArray(*straighten, "straighten");
						if (const auto maxExtend = OptionalField(fingerItem, "maxExtend")) finger.maxExtend = ReadFloatArray(*maxExtend, "maxExtend");
						if (finger.straighten.size() != finger.maxExtend.size() || (!finger.straighten.empty() && finger.straighten.size() != finger.bones.size())) {
							Fail(std::format("Hand \"{}\" has an invalid finger.", a_name));
						}
						if (finger.bones.empty() || finger.curlAxes.size() != finger.bones.size() || finger.maxCurl.size() != finger.bones.size()) {
							Fail(std::format("Hand \"{}\" has an invalid finger.", a_name));
						}
						hand.fingers.push_back(std::move(finger));
					}
					if (hand.fingers.empty() || hand.fingers.size() >= ProjectEx::kSelf) Fail(std::format("Hand \"{}\" has an invalid number of fingers.", a_name));
					const auto defaults = Field(a_item, "defaults");
					hand.curl = ReadFloat(Field(defaults, "curl"), "curl");
					hand.spread = ReadFloat(Field(defaults, "spread"), "spread");
					hand.weight = ReadFloat(Field(defaults, "weight"), "weight");
					hand.enabled = ReadBool(Field(defaults, "enabled"), "enabled");
					const auto fingerDefaults = ReadArray(Field(defaults, "fingers"), "fingers");
					if (fingerDefaults.size() != hand.fingers.size()) Fail(std::format("Hand \"{}\" has mismatched finger defaults.", a_name));
					size_t i = 0;
					for (const Element fingerDefault : fingerDefaults) {
						hand.fingers[i].curl = ReadFloat(Field(fingerDefault, "curl"), "curl");
						hand.fingers[i].spread = ReadFloat(Field(fingerDefault, "spread"), "spread");
						++i;
					}
					return hand;
				}
			case ProjectEx::ObjectType::kSplineIK:
				{
					FileSplineIK spline;
					spline.actor = ReadUInt32(a_item, "actor");
					spline.bones = ReadStrings(Field(a_item, "bones"), "bones");
					spline.aimAxes = ReadFloat3Array(Field(a_item, "aimAxes"), "aimAxes");
					spline.upAxes = ReadFloat3Array(Field(a_item, "upAxes"), "upAxes");
					for (const Element control : ReadArray(Field(a_item, "controls"), "controls")) spline.controls.push_back(static_cast<ObjectId>(ReadUInt(control, "controls")));
					for (const Element axes : ReadArray(Field(a_item, "controlAxes"), "controlAxes")) {
						spline.controlAxes.push_back({ ReadFloat3(Field(axes, "aim"), "aim"), ReadFloat3(Field(axes, "up"), "up") });
					}
					if (spline.bones.size() < 3 || spline.aimAxes.size() != spline.bones.size() || spline.upAxes.size() != spline.bones.size() ||
						spline.controls.size() < 2 || spline.controlAxes.size() != spline.controls.size()) {
						Fail(std::format("Spline IK \"{}\" is malformed.", a_name));
					}
					spline.settings.rootFollowsCurve = ReadBool(Field(a_item, "rootFollowsCurve"), "rootFollowsCurve");
					spline.settings.alignTip = ReadBool(Field(a_item, "alignTip"), "alignTip");
					const auto defaults = Field(a_item, "defaults");
					spline.weight = ReadFloat(Field(defaults, "weight"), "weight");
					spline.twist = ReadFloat(Field(defaults, "twist"), "twist");
					spline.stretch = ReadFloat(Field(defaults, "stretch"), "stretch");
					spline.tangentLength = ReadFloat(Field(defaults, "tangentLength"), "tangentLength");
					spline.orient = ReadOptionalFloat(defaults, "orient", spline.orient);
					spline.enabled = ReadBool(Field(defaults, "enabled"), "enabled");
					return spline;
				}
			default:
				Fail("An actor is not allowed here.");
			}
		}
	}

	struct StudioProjectImport::FileObject
	{
		ObjectId id{ ProjectEx::kNoObject };
		std::string name;
		FileObjectData data;
	};

	struct StudioProjectImport::ProjectFile
	{
		ObjectId nextObjectId{ 1 };
		SequenceId nextSequenceId{ 1 };
		SequenceId activeSequence{ ProjectEx::kNoSequence };
		int32_t frame{ 0 };
		std::vector<FileObject> objects;
		std::vector<FileSequence> sequences;
		std::vector<FileLiveValue> liveValues;
		std::vector<ActorSlot> slots;
		std::string editorJson;
	};

	std::span<const StudioProjectImport::ActorSlot> StudioProjectImport::PendingProject::GetActorSlots() const
	{
		return m_file->slots;
	}

	const std::string& StudioProjectImport::PendingProject::GetEditorJson() const
	{
		return m_file->editorJson;
	}

	std::expected<StudioProjectImport::PendingProject, std::string> StudioProjectImport::Read(const std::filesystem::path& a_path)
	{
		try {
			const auto container = ReadContainer(a_path, Format::kMagic, "project");
			if (!container) return std::unexpected(container.error());
			const auto& contents = container->contents;
			const auto version = container->version;
			if (version < Format::kOldestReadableVersion) return std::unexpected("This project was saved by an older version of NAF Studio, and can no longer be opened.");
			if (version > Format::kVersion) return std::unexpected("This project was saved by a newer version of NAF Studio.");
			const auto binary = std::span<const std::byte>(contents).subspan(Format::kHeaderSize + container->jsonSize);

			simdjson::dom::parser parser;
			const simdjson::padded_string jsonText(reinterpret_cast<const char*>(contents.data() + Format::kHeaderSize), container->jsonSize);
			Element root;
			if (const auto error = parser.parse(jsonText).get(root); error != simdjson::SUCCESS) {
				return std::unexpected(std::string("The project file is corrupt: ") + simdjson::error_message(error));
			}
			if (ReadUInt32(root, "version") != version) return std::unexpected("The project file is corrupt (mismatched versions).");

			auto file = std::make_shared<ProjectFile>();
			file->nextObjectId = ReadUInt32(root, "nextObjectId");
			file->nextSequenceId = ReadUInt32(root, "nextSequenceId");
			file->activeSequence = ReadUInt32(root, "activeSequence");
			file->frame = (std::max)(ReadInt32(root, "frame"), 0);

			// ---- Objects ----

			std::map<ObjectId, size_t> objectIndices;
			for (const Element item : ReadArray(Field(root, "objects"), "objects")) {
				FileObject object;
				object.id = ReadUInt32(item, "id");
				object.name = ReadString(Field(item, "name"), "name");
				if (object.id == ProjectEx::kNoObject || !objectIndices.emplace(object.id, file->objects.size()).second) Fail("Object IDs must be unique and non-zero.");

				const auto type = static_cast<ProjectEx::ObjectType>(FindName(Format::kObjectTypeNames, ReadString(Field(item, "type"), "type"), "object type"));
				switch (type) {
				case ProjectEx::ObjectType::kActor:
					{
						ActorSlot slot{ .id = object.id, .name = object.name, .skeletonId = ReadString(Field(item, "skeleton"), "skeleton") };
						slot.gender = ReadGender(item);
						if (const auto hint = OptionalField(item, "bindingHint")) {
							slot.bindingHint = ResolveBindingHint(*hint);
							if (const auto name = OptionalField(*hint, "name")) slot.bindingHintName = ReadString(*name, "name");
						}

						FileActor actor{ .slot = file->slots.size() };
						const auto placement = Field(item, "placement");
						actor.placement = { ReadFloat3(Field(placement, "translation"), "translation"), ReadFloat(Field(placement, "heading"), "heading") };
						for (const Element modifier : ReadArray(Field(item, "modifiers"), "modifiers")) actor.modifiers.push_back(static_cast<ObjectId>(ReadUInt(modifier, "modifiers")));
						if (const auto mask = OptionalField(item, "boneMask")) {
							for (const Element entry : ReadArray(*mask, "boneMask")) {
								actor.maskOverrides.emplace_back(ReadString(Field(entry, "bone"), "bone"), ReadBool(Field(entry, "masked"), "masked"));
							}
						}
						file->slots.push_back(std::move(slot));
						object.data = std::move(actor);
						break;
					}
				default:
					object.data = ReadObjectData(item, type, object.name, version >= Format::kShoulderSchemaVersion);
					break;
				}
				file->objects.push_back(std::move(object));
			}

			const auto getType = [&](ObjectId a_id) -> std::optional<ProjectEx::ObjectType> {
				const auto iter = objectIndices.find(a_id);
				if (iter == objectIndices.end()) return std::nullopt;
				return static_cast<ProjectEx::ObjectType>(file->objects[iter->second].data.index());
			};
			const auto isType = [&](ObjectId a_id, ProjectEx::ObjectType a_type) { return getType(a_id) == a_type; };

			// The actor a modifier belongs to, or kNoObject for objects that aren't modifiers.
			const auto modifierActor = [&](const FileObject& a_object) {
				return std::visit([](const auto& a_data) -> ObjectId {
					if constexpr (requires { a_data.actor; }) return a_data.actor;
					else return ProjectEx::kNoObject;
				}, a_object.data);
			};

			// Every reference must point at an object of the right type, each locator serves at most one modifier,
			// and each actor's modifiers must be exactly the modifiers that name it.
			std::set<ObjectId> listedModifiers;
			std::set<ObjectId> ownedLocators;
			const auto own = [&](ObjectId a_locator) { return isType(a_locator, ProjectEx::ObjectType::kLocator) && ownedLocators.insert(a_locator).second; };
			for (const auto& object : file->objects) {
				if (const auto locator = std::get_if<FileLocator>(&object.data)) {
					const bool valid = locator->parent == ProjectEx::kNoObject ? !locator->parentBone :
					                   isType(locator->parent, ProjectEx::ObjectType::kActor) || (isType(locator->parent, ProjectEx::ObjectType::kLocator) && !locator->parentBone);
					if (!valid) Fail(std::format("Locator \"{}\" has an invalid parent.", object.name));
				} else if (const auto chain = std::get_if<FileIKChain>(&object.data)) {
					if (!isType(chain->actor, ProjectEx::ObjectType::kActor) || !own(chain->effector) || !own(chain->pole)) {
						Fail(std::format("IK chain \"{}\" has invalid references.", object.name));
					}
				} else if (const auto hand = std::get_if<FileHand>(&object.data)) {
					if (!isType(hand->actor, ProjectEx::ObjectType::kActor)) Fail(std::format("Hand \"{}\" has invalid references.", object.name));
				} else if (const auto spline = std::get_if<FileSplineIK>(&object.data)) {
					if (!isType(spline->actor, ProjectEx::ObjectType::kActor) || !std::ranges::all_of(spline->controls, own)) {
						Fail(std::format("Spline IK \"{}\" has invalid references.", object.name));
					}
				} else if (const auto actor = std::get_if<FileActor>(&object.data)) {
					for (const auto modifier : actor->modifiers) {
						const auto iter = objectIndices.find(modifier);
						if (iter == objectIndices.end() || modifierActor(file->objects[iter->second]) != object.id || !listedModifiers.insert(modifier).second) {
							Fail(std::format("Actor \"{}\" has invalid modifiers.", object.name));
						}
					}
				}
			}
			for (const auto& object : file->objects) {
				if (modifierActor(object) != ProjectEx::kNoObject && !listedModifiers.contains(object.id)) Fail(std::format("\"{}\" is not attached to its actor.", object.name));
			}
			if (!objectIndices.empty()) file->nextObjectId = (std::max)(file->nextObjectId, objectIndices.rbegin()->first + 1);

			const auto readProperty = [&](Element a_item) {
				FileProperty property;
				property.object = ReadUInt32(a_item, "object");
				property.bone = ReadOptionalBone(a_item, "bone");
				if (const auto finger = OptionalField(a_item, "finger")) property.finger = static_cast<uint16_t>((std::min)(ReadUInt(*finger, "finger"), uint64_t{ ProjectEx::kSelf }));
				property.property = static_cast<PropertyId>(FindName(Format::kPropertyNames, ReadString(Field(a_item, "property"), "property"), "property"));
				const auto type = getType(property.object);
				if (!type) Fail("A property refers to an object that does not exist.");
				const size_t valueIndex = GetValueIndex(*type, property.bone.has_value(), property.finger.has_value(), property.property);
				if (property.finger) {
					const auto& hand = std::get<FileHand>(file->objects[objectIndices.at(property.object)].data);
					if (*property.finger >= hand.fingers.size()) Fail("A property refers to a finger that does not exist.");
				}
				return std::pair{ std::move(property), valueIndex };
			};

			// ---- Sequences ----

			std::set<SequenceId> sequenceIds;
			for (const Element item : ReadArray(Field(root, "sequences"), "sequences")) {
				FileSequence sequence;
				sequence.id = ReadUInt32(item, "id");
				if (sequence.id == ProjectEx::kNoSequence || !sequenceIds.insert(sequence.id).second) Fail("Sequence IDs must be unique and non-zero.");
				sequence.name = ReadString(Field(item, "name"), "name");
				sequence.frameRate = ReadFloat(Field(item, "frameRate"), "frameRate");
				sequence.frameCount = ReadInt32(item, "frameCount");
				sequence.loopSmoothing = ReadBool(Field(item, "loopSmoothing"), "loopSmoothing");
				if (!(sequence.frameRate > 0.0f) || sequence.frameCount < 1) Fail(std::format("Sequence \"{}\" has an invalid frame rate or length.", sequence.name));

				std::set<std::tuple<ObjectId, std::optional<std::string>, std::optional<uint16_t>, PropertyId>> trackedProperties;
				for (const Element trackItem : ReadArray(Field(item, "tracks"), "tracks")) {
					auto [property, valueIndex] = readProperty(trackItem);
					if (!trackedProperties.emplace(property.object, property.bone, property.finger, property.property).second) Fail(std::format("Sequence \"{}\" has duplicate tracks.", sequence.name));
					if (FindName(Format::kValueTypeNames, ReadString(Field(trackItem, "type"), "type"), "value type") != valueIndex) Fail("A track's value type does not match its property.");

					const uint64_t offset = ReadUInt(Field(trackItem, "offset"), "offset");
					const uint64_t count = ReadUInt(Field(trackItem, "count"), "count");
					const uint64_t keySize = Format::kKeyHeaderSize + Format::kValueSizes[valueIndex];
					if (count == 0 || offset > binary.size() || count > (binary.size() - offset) / keySize) Fail("A track's keyframe data is out of range.");
					const auto keyData = binary.subspan(static_cast<size_t>(offset), static_cast<size_t>(count * keySize));
					sequence.tracks.push_back({ std::move(property), ReadKeyList(valueIndex, keyData, static_cast<size_t>(count)) });
				}
				for (const Element smoothed : ReadArray(Field(item, "loopSmoothedProperties"), "loopSmoothedProperties")) {
					sequence.loopSmoothedProperties.push_back(readProperty(smoothed).first);
				}
				file->sequences.push_back(std::move(sequence));
			}
			if (file->sequences.empty()) Fail("The project has no sequences.");
			if (!sequenceIds.contains(file->activeSequence)) Fail("The active sequence does not exist.");
			file->nextSequenceId = (std::max)(file->nextSequenceId, *sequenceIds.rbegin() + 1);

			// ---- Live values ----

			for (const Element item : ReadArray(Field(root, "liveValues"), "liveValues")) {
				auto [property, valueIndex] = readProperty(item);
				auto value = ReadValue(Field(item, "value"), valueIndex);
				file->liveValues.push_back({ std::move(property), std::move(value), ReadBool(Field(item, "edited"), "edited") });
			}

			// ---- Editor ----

			if (const auto editor = OptionalField(root, "editor"); editor && editor->is_object()) file->editorJson = simdjson::minify(*editor);

			PendingProject pending;
			pending.m_file = std::move(file);
			return pending;
		} catch (const std::exception& e) {
			return std::unexpected(std::string("The project file is corrupt: ") + e.what());
		}
	}

	std::expected<StudioProjectImport::Result, std::string> StudioProjectImport::Build(const PendingProject& a_pending, std::span<RE::Actor* const> a_actors)
	{
		const auto& file = *a_pending.m_file;
		if (a_actors.size() != file.slots.size()) return std::unexpected("Every actor slot must be bound.");
		for (size_t i = 0; i < a_actors.size(); ++i) {
			if (!a_actors[i]) return std::unexpected(std::format("\"{}\" is not bound.", file.slots[i].name));
			if (std::ranges::count(a_actors, a_actors[i]) > 1) return std::unexpected("The same actor is bound to more than one slot.");
		}

		Result result;
		auto& warnings = result.warnings;

		// Bind every actor first, so a failure leaves nothing attached to the game.
		std::map<ObjectId, std::unique_ptr<Animation::Studio::ActorObject>> actors;
		std::map<ObjectId, std::shared_ptr<const Animation::OzzSkeleton>> skeletons;
		std::map<ObjectId, std::unordered_map<std::string, int32_t>> boneIndices;
		for (const auto& object : file.objects) {
			const auto fileActor = std::get_if<FileActor>(&object.data);
			if (!fileActor) continue;
			const auto& slot = file.slots[fileActor->slot];
			auto actor = std::make_unique<Animation::Studio::ActorObject>();
			actor->id = object.id;
			actor->name = object.name;
			actor->skeletonId = slot.skeletonId;
			actor->gender = slot.gender;
			actor->placement = fileActor->placement;
			if (const auto bound = ProjectEx::LoadActorBinding(*actor, a_actors[fileActor->slot]); !bound) {
				return std::unexpected(std::format("Could not bind \"{}\": {}", slot.name, bound.error()));
			}
			auto& indices = boneIndices[object.id];
			const auto& skeleton = *actor->skeleton->data;
			for (int32_t bone = 0; bone < skeleton.num_joints(); ++bone) indices.emplace(skeleton.joint_names()[bone], bone);
			// Overrides that now match the skeleton's default are dropped, as SetBoneMasked would.
			size_t missingMaskBones = 0;
			for (const auto& [name, masked] : fileActor->maskOverrides) {
				const auto bone = indices.find(name);
				if (bone == indices.end()) {
					++missingMaskBones;
				} else if (masked != !actor->skeleton->defaultBoneMask[bone->second]) {
					actor->maskOverrides[bone->second] = masked;
				}
			}
			if (missingMaskBones > 0) {
				warnings.push_back(std::format("Actor \"{}\": {} bone(s) with a mask override no longer exist.", object.name, missingMaskBones));
			}
			skeletons.emplace(object.id, actor->skeleton);
			actors.emplace(object.id, std::move(actor));
		}

		const auto findBone = [&](ObjectId a_actor, const std::string& a_name) -> std::optional<int32_t> {
			const auto indices = boneIndices.find(a_actor);
			if (indices == boneIndices.end()) return std::nullopt;
			const auto bone = indices->second.find(a_name);
			return bone != indices->second.end() ? std::optional{ bone->second } : std::nullopt;
		};

		auto project = std::make_unique<ProjectEx>();
		project->m_sequences.clear();
		// Types were validated by Read.
		const auto findObject = [&]<class T>(ObjectId a_id) { return static_cast<T*>(project->FindObject(a_id)); };

		// ---- Objects ----

		std::set<ObjectId> removedModifiers;
		const auto skeletonOf = [&](ObjectId a_actor) -> const ozz::animation::Skeleton& { return *skeletons.at(a_actor)->data; };
		for (const auto& object : file.objects) {
			if (std::holds_alternative<FileActor>(object.data)) {
				project->m_objects.PushBack(std::move(actors[object.id]));
			} else if (auto built = BuildObject(object, skeletonOf, findBone, warnings)) {
				project->m_objects.PushBack(std::move(built));
			} else {
				removedModifiers.insert(object.id);
			}
		}

		for (const auto& object : file.objects) {
			const auto data = std::get_if<FileActor>(&object.data);
			if (!data) continue;
			auto& modifiers = findObject.operator()<Animation::Studio::ActorObject>(object.id)->modifiers;
			for (const auto modifier : data->modifiers) {
				if (!removedModifiers.contains(modifier)) modifiers.push_back(modifier);
			}
		}

		// Parents are attached one at a time, so each can be checked against the ones already in place.
		for (const auto& object : file.objects) {
			const auto data = std::get_if<FileLocator>(&object.data);
			if (!data || data->parent == ProjectEx::kNoObject) continue;
			ProjectEx::ParentRef parent{ data->parent, -1 };
			if (data->parentBone) {
				if (const auto bone = findBone(data->parent, *data->parentBone)) {
					parent.bone = *bone;
				} else {
					warnings.push_back(std::format("Locator \"{}\" was attached to its actor's root: bone \"{}\" no longer exists.", object.name, *data->parentBone));
				}
			}
			auto& locator = *findObject.operator()<Animation::Studio::LocatorObject>(object.id);
			if (project->WouldCreateLocatorLoop(object.id, parent)) {
				warnings.push_back(std::format("Locator \"{}\" was moved to world space: its parent would have formed a loop.", object.name));
				continue;
			}
			locator.parent = parent;
			if (project->HasSelfDependentModifier()) {
				locator.parent = {};
				warnings.push_back(std::format("Locator \"{}\" was moved to world space: its parent made an IK chain or spline depend on itself.", object.name));
			}
		}

		// ---- Sequences ----

		// Properties of removed modifiers are dropped silently; the modifier's warning covers them.
		const auto resolveProperty = [&](const FileProperty& a_property, size_t& a_missingBones) -> std::optional<ProjectEx::PropertyRef> {
			if (removedModifiers.contains(a_property.object)) return std::nullopt;
			ProjectEx::PropertyRef ref{ a_property.object, ProjectEx::kSelf, a_property.property };
			if (a_property.finger) ref.element = *a_property.finger;  // Validated against the hand by Read.
			if (a_property.bone) {
				const auto bone = findBone(a_property.object, *a_property.bone);
				if (!bone) {
					++a_missingBones;
					return std::nullopt;
				}
				ref.element = static_cast<uint16_t>(*bone);
			}
			return ref;
		};

		for (const auto& data : file.sequences) {
			Animation::Studio::Sequence sequence{ .id = data.id, .name = data.name, .frameRate = data.frameRate, .frameCount = data.frameCount, .loopSmoothing = data.loopSmoothing };
			size_t missingBones = 0;
			for (const auto& track : data.tracks) {
				const auto ref = resolveProperty(track.property, missingBones);
				if (!ref) continue;
				auto curve = std::visit([](const auto& a_keys) -> Animation::Studio::AnyCurve {
					using KeyT = typename std::remove_cvref_t<decltype(a_keys)>::value_type;
					return Animation::Studio::Curve<decltype(KeyT::value)>{ a_keys };
				}, track.keys);
				sequence.tracks.emplace(*ref, Animation::Studio::Track{ std::move(curve) });
			}
			if (missingBones > 0) {
				warnings.push_back(std::format("{} track(s) in \"{}\" were dropped because their bones no longer exist.", missingBones, data.name));
			}
			size_t ignored = 0;
			for (const auto& property : data.loopSmoothedProperties) {
				if (const auto ref = resolveProperty(property, ignored)) sequence.loopSmoothedProperties.insert(*ref);
			}
			project->m_sequences.push_back(std::move(sequence));
		}

		project->m_nextObjectId = file.nextObjectId;
		project->m_nextSequenceId = file.nextSequenceId;
		project->m_activeSequence = file.activeSequence;
		project->m_frame = file.frame;

		// ---- Live values ----

		std::vector<Animation::Studio::DiscardedValue> liveValues;
		size_t ignored = 0;
		for (const auto& value : file.liveValues) {
			if (const auto ref = resolveProperty(value.property, ignored)) liveValues.push_back({ *ref, value.value, value.edited });
		}
		project->FinishLoad(liveValues);

		result.project = std::move(project);
		return result;
	}

	// ---- Rigs ----

	struct StudioProjectImport::RigFile
	{
		ObjectId actor{ ProjectEx::kNoObject };  // The id the rig's objects use for its actor.
		std::string skeletonId;
		RE::SEX gender{ RE::SEX::kNone };
		std::vector<FileBonePose> pose;
		std::vector<std::pair<std::string, bool>> maskOverrides;  // Bone, masked.
		std::vector<FileObject> objects;                          // Locators and modifiers.
		RigSummary summary;
		std::string editorJson;
	};

	const std::string& StudioProjectImport::PendingRig::GetSkeletonId() const
	{
		return m_file->skeletonId;
	}

	RE::SEX StudioProjectImport::PendingRig::GetGender() const
	{
		return m_file->gender;
	}

	const StudioProjectImport::RigSummary& StudioProjectImport::PendingRig::GetSummary() const
	{
		return m_file->summary;
	}

	const std::string& StudioProjectImport::PendingRig::GetEditorJson() const
	{
		return m_file->editorJson;
	}

	std::expected<StudioProjectImport::PendingRig, std::string> StudioProjectImport::ReadRig(const std::filesystem::path& a_path)
	{
		try {
			const auto container = ReadContainer(a_path, Format::kRigMagic, "rig");
			if (!container) return std::unexpected(container.error());
			if (container->version > Format::kRigVersion) return std::unexpected("This rig was saved by a newer version of NAF Studio.");

			simdjson::dom::parser parser;
			const simdjson::padded_string jsonText(reinterpret_cast<const char*>(container->contents.data() + Format::kHeaderSize), container->jsonSize);
			Element root;
			if (const auto error = parser.parse(jsonText).get(root); error != simdjson::SUCCESS) {
				return std::unexpected(std::string("The rig file is corrupt: ") + simdjson::error_message(error));
			}
			if (ReadUInt32(root, "version") != container->version) return std::unexpected("The rig file is corrupt (mismatched versions).");

			auto file = std::make_shared<RigFile>();
			file->actor = ReadUInt32(root, "actor");
			if (file->actor == ProjectEx::kNoObject) Fail("The rig's actor ID must be non-zero.");
			file->skeletonId = ReadString(Field(root, "skeleton"), "skeleton");
			file->gender = ReadGender(root);

			for (const Element item : ReadArray(Field(root, "pose"), "pose")) {
				file->pose.push_back({ ReadString(Field(item, "bone"), "bone"), ReadFloat3(Field(item, "translation"), "translation"),
					ReadQuaternion(Field(item, "rotation"), "rotation"), ReadFloat3(Field(item, "scale"), "scale") });
			}
			for (const Element entry : ReadArray(Field(root, "boneMask"), "boneMask")) {
				file->maskOverrides.emplace_back(ReadString(Field(entry, "bone"), "bone"), ReadBool(Field(entry, "masked"), "masked"));
			}

			std::map<ObjectId, size_t> objectIndices;
			for (const Element item : ReadArray(Field(root, "objects"), "objects")) {
				FileObject object;
				object.id = ReadUInt32(item, "id");
				object.name = ReadString(Field(item, "name"), "name");
				if (object.id == ProjectEx::kNoObject || object.id == file->actor || !objectIndices.emplace(object.id, file->objects.size()).second) {
					Fail("Object IDs must be unique, non-zero, and different from the actor's.");
				}
				const auto type = static_cast<ProjectEx::ObjectType>(FindName(Format::kObjectTypeNames, ReadString(Field(item, "type"), "type"), "object type"));
				if (type == ProjectEx::ObjectType::kActor) Fail("A rig can't hold actors.");
				object.data = ReadObjectData(item, type, object.name, container->version >= Format::kShoulderSchemaRigVersion);
				file->objects.push_back(std::move(object));
			}

			// Locators hang from the scene (relative to the actor), the actor, or another rig locator. Every modifier
			// belongs to the actor, and each locator serves at most one modifier.
			const auto findLocator = [&](ObjectId a_id) -> const FileLocator* {
				const auto iter = objectIndices.find(a_id);
				return iter != objectIndices.end() ? std::get_if<FileLocator>(&file->objects[iter->second].data) : nullptr;
			};
			std::set<ObjectId> ownedLocators;
			const auto own = [&](ObjectId a_locator) { return findLocator(a_locator) && ownedLocators.insert(a_locator).second; };
			auto& summary = file->summary;
			for (const auto& object : file->objects) {
				if (const auto locator = std::get_if<FileLocator>(&object.data)) {
					const bool valid = locator->parent == file->actor || (!locator->parentBone && (locator->parent == ProjectEx::kNoObject || findLocator(locator->parent)));
					if (!valid) Fail(std::format("Locator \"{}\" has an invalid parent.", object.name));
					// Parents are walked from here up; a loop would never reach the actor or the scene.
					size_t depth = 0;
					for (auto parent = locator; parent; parent = findLocator(parent->parent)) {
						if (++depth > file->objects.size()) Fail(std::format("Locator \"{}\" is part of a parent loop.", object.name));
					}
				} else if (const auto chain = std::get_if<FileIKChain>(&object.data)) {
					if (chain->actor != file->actor || !own(chain->effector) || !own(chain->pole)) Fail(std::format("IK chain \"{}\" has invalid references.", object.name));
					++summary.ikChains;
				} else if (const auto hand = std::get_if<FileHand>(&object.data)) {
					if (hand->actor != file->actor) Fail(std::format("Hand \"{}\" has invalid references.", object.name));
					++summary.hands;
				} else if (const auto spline = std::get_if<FileSplineIK>(&object.data)) {
					if (spline->actor != file->actor || !std::ranges::all_of(spline->controls, own)) Fail(std::format("Spline IK \"{}\" has invalid references.", object.name));
					++summary.splineIKs;
				}
			}
			summary.posedBones = file->pose.size();
			summary.maskOverrides = file->maskOverrides.size();

			if (const auto editor = OptionalField(root, "editor"); editor && editor->is_object()) file->editorJson = simdjson::minify(*editor);

			PendingRig pending;
			pending.m_file = std::move(file);
			return pending;
		} catch (const std::exception& e) {
			return std::unexpected(std::string("The rig file is corrupt: ") + e.what());
		}
	}

	std::expected<ProjectEx::RigResult, std::string> StudioProjectImport::ApplyRig(const PendingRig& a_rig, ProjectEx& a_project, ObjectId a_actor, const ProjectEx::RigOptions& a_options)
	{
		const auto& file = *a_rig.m_file;
		const auto object = a_project.FindObject(a_actor);
		const auto actor = object && object->GetType() == ProjectEx::ObjectType::kActor ? static_cast<const Animation::Studio::ActorObject*>(object) : nullptr;
		if (!actor || !actor->skeleton) return std::unexpected("The actor must be bound to apply a rig.");
		if (!Util::String::CaseInsensitiveCompare(file.skeletonId, actor->skeletonId)) {
			return std::unexpected(std::format("This rig is for skeleton \"{}\", and the actor uses \"{}\".", file.skeletonId, actor->skeletonId));
		}

		std::vector<std::string> warnings;
		if (file.gender != RE::SEX::kNone && actor->gender != RE::SEX::kNone && file.gender != actor->gender) {
			warnings.push_back(std::format("This rig was made for a {} actor.", file.gender == RE::SEX::kMale ? "male" : "female"));
		}

		const auto& skeleton = *actor->skeleton->data;
		std::unordered_map<std::string, int32_t> boneIndices;
		for (int32_t bone = 0; bone < skeleton.num_joints(); ++bone) boneIndices.emplace(skeleton.joint_names()[bone], bone);
		const auto findBone = [&](ObjectId, const std::string& a_name) -> std::optional<int32_t> {
			const auto bone = boneIndices.find(a_name);
			return bone != boneIndices.end() ? std::optional{ bone->second } : std::nullopt;
		};

		ProjectEx::RigContents contents{ .actor = file.actor };
		size_t missingPoseBones = 0;
		size_t missingMaskBones = 0;
		if (a_options.pose) {
			for (const auto& pose : file.pose) {
				if (const auto bone = findBone(file.actor, pose.bone)) contents.pose.emplace_back(*bone, Animation::Studio::LocatorDefaults{ pose.translation, pose.rotation, pose.scale });
				else ++missingPoseBones;
			}
		}
		if (a_options.boneMask) {
			for (const auto& [name, masked] : file.maskOverrides) {
				if (const auto bone = findBone(file.actor, name)) contents.maskOverrides[*bone] = masked;
				else ++missingMaskBones;
			}
		}
		if (missingPoseBones > 0) warnings.push_back(std::format("{} posed bone(s) don't exist on this skeleton.", missingPoseBones));
		if (missingMaskBones > 0) warnings.push_back(std::format("{} bone(s) with a mask override don't exist on this skeleton.", missingMaskBones));

		const auto skeletonOf = [&](ObjectId) -> const ozz::animation::Skeleton& { return skeleton; };
		for (const auto& item : file.objects) {
			// Modifiers that won't be applied are skipped here, so they don't warn about their bones.
			const auto type = static_cast<ProjectEx::ObjectType>(item.data.index());
			if (type != ProjectEx::ObjectType::kLocator && !a_options.Applies(type)) continue;
			auto built = BuildObject(item, skeletonOf, findBone, warnings);
			if (!built) continue;
			if (const auto data = std::get_if<FileLocator>(&item.data)) {
				auto& locator = static_cast<Animation::Studio::LocatorObject&>(*built);
				locator.parent = { data->parent, -1 };
				if (data->parentBone) {
					if (const auto bone = findBone(file.actor, *data->parentBone)) {
						locator.parent.bone = *bone;
					} else {
						warnings.push_back(std::format("Locator \"{}\" was attached to the actor's root: bone \"{}\" doesn't exist.", item.name, *data->parentBone));
					}
				}
			}
			contents.objects.push_back(std::move(built));
		}

		auto result = a_project.ApplyRig(a_actor, std::move(contents), a_options);
		result.warnings.insert(result.warnings.begin(), warnings.begin(), warnings.end());
		return result;
	}

	std::unique_ptr<Animation::Studio::SceneObject> StudioProjectImport::BuildObject(const FileObject& a_object, const std::function<const ozz::animation::Skeleton&(ObjectId)>& a_skeleton,
		const std::function<std::optional<int32_t>(ObjectId, const std::string&)>& a_findBone, std::vector<std::string>& a_warnings)
	{
		// Resolves every bone name, or none.
		const auto findBones = [&](ObjectId a_actor, std::span<const std::string> a_names) -> std::optional<std::vector<int32_t>> {
			std::vector<int32_t> result;
			for (const auto& name : a_names) {
				const auto bone = a_findBone(a_actor, name);
				if (!bone) return std::nullopt;
				result.push_back(*bone);
			}
			return result;
		};

		if (std::holds_alternative<FileLocator>(a_object.data)) {
			const auto& data = std::get<FileLocator>(a_object.data);
			auto locator = std::make_unique<Animation::Studio::LocatorObject>();
			locator->id = a_object.id;
			locator->name = a_object.name;
			locator->transform.translation = { data.translation, data.translation };
			locator->transform.rotation = { data.rotation, data.rotation };
			locator->transform.scale = { data.scale, data.scale };
			return locator;
		} else if (std::holds_alternative<FileHand>(a_object.data)) {
			const auto& data = std::get<FileHand>(a_object.data);
			const auto& skeleton = a_skeleton(data.actor);
			auto hand = std::make_unique<Animation::Studio::HandObject>();
			hand->id = a_object.id;
			hand->name = a_object.name;
			hand->actor = data.actor;
			hand->handBone = a_findBone(data.actor, data.handBone).value_or(-1);
			bool valid = hand->handBone >= 0;
			for (const auto& finger : data.fingers) {
				const auto bones = findBones(data.actor, finger.bones);
				valid = valid && bones.has_value();
				if (!valid) break;
				hand->settings.fingers.push_back({ *bones, finger.curlAxes, finger.spreadAxis, finger.spreadScale, finger.maxCurl, finger.maxSpread, finger.flip, finger.straighten, finger.maxExtend });
				auto& properties = hand->fingers.emplace_back();
				properties.curl = { finger.curl, finger.curl };
				properties.spread = { finger.spread, finger.spread };
			}
			if (!valid || !Animation::Studio::IsValidHand(skeleton, hand->handBone, hand->settings)) {
				a_warnings.push_back(std::format("Finger controls \"{}\" were removed: their bones don't match the bound actor's skeleton.", a_object.name));
				return nullptr;
			}
			hand->curl = { data.curl, data.curl };
			hand->spread = { data.spread, data.spread };
			hand->weight = { data.weight, data.weight };
			hand->enabled = { data.enabled, data.enabled };
			return hand;
		} else if (std::holds_alternative<FileSplineIK>(a_object.data)) {
			const auto& data = std::get<FileSplineIK>(a_object.data);
			const auto bones = findBones(data.actor, data.bones);
			if (!bones || Animation::Studio::GetBonePath(a_skeleton(data.actor), bones->front(), bones->back()) != *bones) {
				a_warnings.push_back(std::format("Spline IK \"{}\" was removed: its bones don't form a chain on the bound actor's skeleton.", a_object.name));
				return nullptr;
			}
			auto spline = std::make_unique<Animation::Studio::SplineIKObject>();
			spline->id = a_object.id;
			spline->name = a_object.name;
			spline->actor = data.actor;
			spline->bones = *bones;
			spline->aimAxes = data.aimAxes;
			spline->upAxes = data.upAxes;
			spline->controls = data.controls;
			for (const auto& [aim, up] : data.controlAxes) spline->controlAxes.push_back({ aim, up });
			spline->settings = data.settings;
			spline->weight = { data.weight, data.weight };
			spline->twist = { data.twist, data.twist };
			spline->stretch = { data.stretch, data.stretch };
			spline->tangentLength = { data.tangentLength, data.tangentLength };
			spline->orient = { data.orient, data.orient };
			spline->enabled = { data.enabled, data.enabled };
			return spline;
		} else {
			const auto& data = std::get<FileIKChain>(a_object.data);
			const auto upper = a_findBone(data.actor, data.bones[0]);
			const auto middle = a_findBone(data.actor, data.bones[1]);
			const auto end = a_findBone(data.actor, data.bones[2]);
			const auto& skeleton = a_skeleton(data.actor);
			if (!upper || !middle || !end || !Animation::Studio::IsValidChain(skeleton, *upper, *middle, *end)) {
				a_warnings.push_back(std::format("IK chain \"{}\" was removed: its bones don't form a chain on the bound actor's skeleton.", a_object.name));
				return nullptr;
			}
			auto chain = std::make_unique<Animation::Studio::IKChainObject>();
			chain->settings = data.settings;
			if (data.shoulderBone) {
				const auto shoulder = a_findBone(data.actor, *data.shoulderBone);
				if (shoulder && Animation::Studio::IsValidShoulder(skeleton, *shoulder, *upper)) {
					chain->settings.shoulderBone = *shoulder;
					chain->settings.shoulderNeutral = data.shoulderNeutral ? *data.shoulderNeutral : Animation::Studio::GetRestShoulderNeutral(skeleton, *shoulder, *end);
				} else {
					a_warnings.push_back(std::format("IK chain \"{}\" lost its shoulder: bone \"{}\" is missing or no longer above the limb.", a_object.name, *data.shoulderBone));
				}
			}
			chain->shoulderWeight = { data.shoulderWeight, data.shoulderWeight };
			chain->twistDistribution = { data.twistDistribution, data.twistDistribution };
			chain->id = a_object.id;
			chain->name = a_object.name;
			chain->actor = data.actor;
			chain->upperBone = *upper;
			chain->middleBone = *middle;
			chain->endBone = *end;
			chain->middleAxis = data.middleAxis;
			chain->effector = data.effector;
			chain->pole = data.pole;
			chain->weight = { data.weight, data.weight };
			chain->soften = { data.soften, data.soften };
			chain->twist = { data.twist, data.twist };
			chain->enabled = { data.enabled, data.enabled };
			return chain;
		}
	}
}
