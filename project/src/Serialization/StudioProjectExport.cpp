#include "StudioProjectExport.h"
#include "Util/Game.h"

#include "StudioProjectFormat.h"
#include "Animation/Studio/Modifiers/Hand.h"
#include "Animation/Studio/Modifiers/IKChain.h"
#include "Animation/Studio/Modifiers/SplineIK.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace Serialization
{
	namespace
	{
		using ProjectEx = Animation::Studio::ProjectEx;
		using json = nlohmann::ordered_json;
		namespace Format = StudioProjectFormat;

		template <class T>
		void Append(std::vector<std::byte>& a_data, const T& a_value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto bytes = std::as_bytes(std::span{ std::addressof(a_value), 1 });
			a_data.insert(a_data.end(), bytes.begin(), bytes.end());
		}

		void AppendValue(std::vector<std::byte>& a_data, float a_value) { Append(a_data, a_value); }
		void AppendValue(std::vector<std::byte>& a_data, bool a_value) { Append(a_data, static_cast<uint8_t>(a_value)); }
		void AppendValue(std::vector<std::byte>& a_data, const ozz::math::Float3& a_value)
		{
			Append(a_data, a_value.x);
			Append(a_data, a_value.y);
			Append(a_data, a_value.z);
		}
		void AppendValue(std::vector<std::byte>& a_data, const ozz::math::Quaternion& a_value)
		{
			Append(a_data, a_value.x);
			Append(a_data, a_value.y);
			Append(a_data, a_value.z);
			Append(a_data, a_value.w);
		}

		json ToJson(float a_value) { return a_value; }
		json ToJson(bool a_value) { return a_value; }
		json ToJson(const ozz::math::Float3& a_value) { return json::array({ a_value.x, a_value.y, a_value.z }); }
		json ToJson(const ozz::math::Quaternion& a_value) { return json::array({ a_value.x, a_value.y, a_value.z, a_value.w }); }
		json ToJson(const ProjectEx::Value& a_value)
		{
			return std::visit([](const auto& a_typed) { return ToJson(a_typed); }, a_value);
		}

		// Plugin-relative, so the hint survives load order changes. Forms created at runtime only have their form ID.
		json MakeBindingHint(RE::TESFormID a_formId)
		{
			json hint{ { "formId", a_formId } };
			const auto form = a_formId != 0 ? Util::Game::LookupForm(a_formId) : nullptr;
			if (const auto pluginFormId = form ? Util::Game::GetPluginFormID(a_formId) : std::nullopt) {
				hint["plugin"] = pluginFormId->plugin;
				hint["localFormId"] = pluginFormId->localFormId;
			}
			if (const auto actor = form ? form->As<RE::Actor>() : nullptr) {
				if (const char* name = actor->GetDisplayFullName(); name && name[0] != '\0') hint["name"] = name;
			}
			return hint;
		}

		std::string BoneName(const ProjectEx& a_project, ProjectEx::ObjectId a_object, int32_t a_bone)
		{
			const auto skeleton = a_project.GetSkeleton(a_object);
			if (!skeleton || a_bone < 0 || a_bone >= skeleton->num_joints()) {
				throw std::runtime_error(std::format("A reference to bone {} of object {} could not be resolved.", a_bone, a_object));
			}
			return skeleton->joint_names()[a_bone];
		}

		json BoneNames(const ProjectEx& a_project, ProjectEx::ObjectId a_object, std::span<const int32_t> a_bones)
		{
			auto result = json::array();
			for (const auto bone : a_bones) result.push_back(BoneName(a_project, a_object, bone));
			return result;
		}

		json Float3Array(std::span<const ozz::math::Float3> a_values)
		{
			auto result = json::array();
			for (const auto& value : a_values) result.push_back(ToJson(value));
			return result;
		}

		json TransformJson(const ozz::math::Float3& a_translation, const ozz::math::Quaternion& a_rotation, const ozz::math::Float3& a_scale)
		{
			return { { "translation", ToJson(a_translation) }, { "rotation", ToJson(a_rotation) }, { "scale", ToJson(a_scale) } };
		}

		std::expected<void, std::string> WriteFile(const std::filesystem::path& a_path, const std::array<char, 8>& a_magic, uint32_t a_version, std::string_view a_json, std::span<const std::byte> a_binary)
		{
			std::error_code ec;
			std::filesystem::create_directories(a_path.parent_path(), ec);
			if (ec) return std::unexpected("Could not create the folder: " + ec.message());

			auto temporary = a_path;
			temporary += ".tmp";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				if (!file) return std::unexpected("Could not open the file for writing.");
				const uint64_t jsonSize = a_json.size();
				file.write(a_magic.data(), a_magic.size());
				file.write(reinterpret_cast<const char*>(&a_version), sizeof(a_version));
				file.write(reinterpret_cast<const char*>(&jsonSize), sizeof(jsonSize));
				file.write(a_json.data(), static_cast<std::streamsize>(a_json.size()));
				file.write(reinterpret_cast<const char*>(a_binary.data()), static_cast<std::streamsize>(a_binary.size()));
				file.close();
				if (!file) {
					std::filesystem::remove(temporary, ec);
					return std::unexpected("Failed while writing the file.");
				}
			}

			// Keep the previous version, then replace the file in one step so it's never left half-written.
			if (std::filesystem::exists(a_path, ec)) {
				auto backup = a_path;
				backup += ".bak";
				std::filesystem::copy_file(a_path, backup, std::filesystem::copy_options::overwrite_existing, ec);
				if (ec) {
					std::filesystem::remove(temporary, ec);
					return std::unexpected("Could not back up the previous file: " + ec.message());
				}
			}
			std::filesystem::rename(temporary, a_path, ec);
			if (ec) {
				const auto message = ec.message();
				std::filesystem::remove(temporary, ec);
				return std::unexpected("Could not replace the file: " + message);
			}
			return {};
		}
	}

	json StudioProjectExport::WriteObject(const ProjectEx& a_project, ProjectEx::ObjectId a_id, bool a_live)
	{
		const auto& object = *a_project.FindObject(a_id);
		const auto value = [&](const auto& a_property) { return a_live ? a_property.live : a_property.defaultValue; };
		json item{
			{ "id", object.id },
			{ "type", Format::kObjectTypeNames[static_cast<size_t>(object.GetType())] },
			{ "name", object.name }
		};
		switch (object.GetType()) {
		case ProjectEx::ObjectType::kActor:
			throw std::logic_error("Actors are written by the project.");
		case ProjectEx::ObjectType::kLocator:
			{
				const auto& locator = static_cast<const Animation::Studio::LocatorObject&>(object);
				json parent{ { "object", locator.parent.object } };
				if (locator.parent.bone >= 0) parent["bone"] = BoneName(a_project, locator.parent.object, locator.parent.bone);
				item["parent"] = std::move(parent);
				const auto& transform = locator.transform;
				item["defaults"] = TransformJson(value(transform.translation), value(transform.rotation), value(transform.scale));
				break;
			}
		case ProjectEx::ObjectType::kIKChain:
			{
				const auto& chain = static_cast<const Animation::Studio::IKChainObject&>(object);
				item["actor"] = chain.actor;
				item["bones"] = BoneNames(a_project, chain.actor, std::array{ chain.upperBone, chain.middleBone, chain.endBone });
				item["middleAxis"] = ToJson(chain.middleAxis);
				item["effector"] = chain.effector;
				item["pole"] = chain.pole;
				item["defaults"] = {
					{ "weight", value(chain.weight) },
					{ "soften", value(chain.soften) },
					{ "twist", value(chain.twist) },
					{ "enabled", value(chain.enabled) },
					{ "shoulderWeight", value(chain.shoulderWeight) },
					{ "twistDistribution", value(chain.twistDistribution) }
				};
				const auto& settings = chain.settings;
				json shoulder{ { "maxAngle", settings.shoulderMaxAngle }, { "neutral", ToJson(settings.shoulderNeutral) } };
				if (settings.shoulderBone >= 0) shoulder["bone"] = BoneName(a_project, chain.actor, settings.shoulderBone);
				item["shoulder"] = std::move(shoulder);
				break;
			}
		case ProjectEx::ObjectType::kHand:
			{
				const auto& hand = static_cast<const Animation::Studio::HandObject&>(object);
				item["actor"] = hand.actor;
				item["handBone"] = BoneName(a_project, hand.actor, hand.handBone);
				auto& fingers = item["fingers"] = json::array();
				for (const auto& finger : hand.settings.fingers) {
					json fingerItem{
						{ "bones", BoneNames(a_project, hand.actor, finger.bones) },
						{ "curlAxes", Float3Array(finger.curlAxes) },
						{ "spreadAxis", ToJson(finger.spreadAxis) },
						{ "spreadScale", finger.spreadScale },
						{ "maxCurl", finger.maxCurl },
						{ "maxSpread", finger.maxSpread },
						{ "flip", finger.flip }
					};
					// Absent on hands made before version 5, where curl 0 is the rest pose.
					if (!finger.straighten.empty()) {
						fingerItem["straighten"] = finger.straighten;
						fingerItem["maxExtend"] = finger.maxExtend;
					}
					fingers.push_back(std::move(fingerItem));
				}
				auto fingerDefaults = json::array();
				for (const auto& finger : hand.fingers) fingerDefaults.push_back({ { "curl", value(finger.curl) }, { "spread", value(finger.spread) } });
				item["defaults"] = {
					{ "curl", value(hand.curl) },
					{ "spread", value(hand.spread) },
					{ "weight", value(hand.weight) },
					{ "enabled", value(hand.enabled) },
					{ "fingers", std::move(fingerDefaults) }
				};
				break;
			}
		case ProjectEx::ObjectType::kSplineIK:
			{
				const auto& spline = static_cast<const Animation::Studio::SplineIKObject&>(object);
				item["actor"] = spline.actor;
				item["bones"] = BoneNames(a_project, spline.actor, spline.bones);
				item["aimAxes"] = Float3Array(spline.aimAxes);
				item["upAxes"] = Float3Array(spline.upAxes);
				item["controls"] = spline.controls;
				auto& controlAxes = item["controlAxes"] = json::array();
				for (const auto& axes : spline.controlAxes) controlAxes.push_back({ { "aim", ToJson(axes.aim) }, { "up", ToJson(axes.up) } });
				item["rootFollowsCurve"] = spline.settings.rootFollowsCurve;
				item["alignTip"] = spline.settings.alignTip;
				item["defaults"] = {
					{ "weight", value(spline.weight) },
					{ "twist", value(spline.twist) },
					{ "stretch", value(spline.stretch) },
					{ "tangentLength", value(spline.tangentLength) },
					{ "orient", value(spline.orient) },
					{ "enabled", value(spline.enabled) }
				};
				break;
			}
		}
		return item;
	}

	std::expected<void, std::string> StudioProjectExport::Save(const std::filesystem::path& a_path, const ProjectEx& a_project, std::string_view a_editorJson)
	{
		try {
			std::vector<std::byte> binary;

			// Object, then the element (a bone name for actors, a finger index for hands), then property name.
			const auto propertyJson = [&](const ProjectEx::PropertyRef& a_ref) {
				json result{ { "object", a_ref.object } };
				if (a_ref.element != ProjectEx::kSelf) {
					const auto object = a_project.FindObject(a_ref.object);
					if (object && object->GetType() == ProjectEx::ObjectType::kHand) result["finger"] = a_ref.element;
					else result["bone"] = BoneName(a_project, a_ref.object, a_ref.element);
				}
				result["property"] = Format::kPropertyNames[static_cast<size_t>(a_ref.property)];
				return result;
			};

			json root;
			root["version"] = Format::kVersion;
			root["nextObjectId"] = a_project.m_nextObjectId;
			root["nextSequenceId"] = a_project.m_nextSequenceId;
			root["activeSequence"] = a_project.m_activeSequence;
			root["frame"] = a_project.m_frame;

			auto& objects = root["objects"] = json::array();
			for (const auto& object : a_project.m_objects) {
				if (object->GetType() != ProjectEx::ObjectType::kActor) {
					objects.push_back(WriteObject(a_project, object->id, false));
					continue;
				}
				const auto& actor = static_cast<const Animation::Studio::ActorObject&>(*object);
				json item{
					{ "id", actor.id },
					{ "type", Format::kObjectTypeNames[static_cast<size_t>(actor.GetType())] },
					{ "name", actor.name }
				};
				item["skeleton"] = actor.skeletonId;
				item["gender"] = actor.gender == RE::SEX::kMale ? "male" : actor.gender == RE::SEX::kFemale ? "female" : "any";
				item["placement"] = { { "translation", ToJson(actor.placement.translation) }, { "heading", actor.placement.heading } };
				item["modifiers"] = actor.modifiers;
				item["bindingHint"] = MakeBindingHint(actor.actor ? actor.actor->GetFormID() : actor.bindingHint);
				if (!actor.maskOverrides.empty()) {
					auto& mask = item["boneMask"] = json::array();
					for (const auto& [bone, masked] : actor.maskOverrides) mask.push_back({ { "bone", BoneName(a_project, actor.id, bone) }, { "masked", masked } });
				}
				objects.push_back(std::move(item));
			}

			auto& sequences = root["sequences"] = json::array();
			for (const auto& sequence : a_project.m_sequences) {
				json item{
					{ "id", sequence.id },
					{ "name", sequence.name },
					{ "frameRate", sequence.frameRate },
					{ "frameCount", sequence.frameCount },
					{ "loopSmoothing", sequence.loopSmoothing }
				};
				auto& tracks = item["tracks"] = json::array();
				for (const auto& [ref, track] : sequence.tracks) {
					auto entry = propertyJson(ref);
					entry["type"] = Format::kValueTypeNames[track.curve.index()];
					entry["offset"] = binary.size();
					std::visit([&](const auto& a_curve) {
						entry["count"] = a_curve.keys.size();
						for (const auto& key : a_curve.keys) {
							Append(binary, key.frame);
							Append(binary, static_cast<uint8_t>(key.interpolation));
							Append(binary, static_cast<uint8_t>(key.tangentMode));
							AppendValue(binary, key.value);
						}
					}, track.curve);
					tracks.push_back(std::move(entry));
				}
				auto& smoothed = item["loopSmoothedProperties"] = json::array();
				for (const auto& ref : sequence.loopSmoothedProperties) smoothed.push_back(propertyJson(ref));
				auto& bezierModes = item["bezierModes"] = json::array();
				for (const auto& [ref, mode] : sequence.bezierModes) {
					auto entry = propertyJson(ref);
					entry["mode"] = Format::kBezierModeNames[static_cast<size_t>(mode)];
					bezierModes.push_back(std::move(entry));
				}
				sequences.push_back(std::move(item));
			}

			// Live values that loading can't recreate from defaults and sampling: pending edits, and untracked
			// properties left away from their default (such as a property whose keys were all deleted).
			const auto active = a_project.GetActiveSequence();
			auto& liveValues = root["liveValues"] = json::array();
			for (const auto& object : a_project.m_objects) {
				// Property access is non-const on scene objects; nothing is modified here.
				const_cast<Animation::Studio::SceneObject&>(*object).ForEachProperty([&](uint16_t a_element, ProjectEx::PropertyId a_property, Animation::Studio::PropertyPtr a_ptr) {
					const ProjectEx::PropertyRef ref{ object->id, a_element, a_property };
					const auto live = Animation::Studio::GetLiveValue(a_ptr);
					const bool edited = Animation::Studio::IsEdited(a_ptr);
					const bool tracked = active && active->tracks.contains(ref);
					if (!edited && (tracked || live == Animation::Studio::GetDefaultValue(a_ptr))) return;
					auto entry = propertyJson(ref);
					entry["value"] = ToJson(live);
					entry["edited"] = edited;
					liveValues.push_back(std::move(entry));
				});
			}

			if (!a_editorJson.empty()) root["editor"] = json::parse(a_editorJson);
			return WriteFile(a_path, Format::kMagic, Format::kVersion, root.dump(), binary);
		} catch (const std::exception& e) {
			return std::unexpected(std::string("Could not save the project: ") + e.what());
		}
	}

	std::expected<void, std::string> StudioProjectExport::SaveRig(const std::filesystem::path& a_path, ProjectEx& a_project, ProjectEx::ObjectId a_actor, std::string_view a_editorJson)
	{
		try {
			// The object if it has type T, otherwise null.
			const auto find = [&]<class T>(ProjectEx::ObjectId a_id) -> const T* {
				const auto object = a_project.FindObject(a_id);
				return object && object->GetType() == T::kType ? static_cast<const T*>(object) : nullptr;
			};
			const auto actor = find.operator()<Animation::Studio::ActorObject>(a_actor);
			if (!actor || !actor->skeleton) return std::unexpected("The actor must be bound to save its rig.");

			json root;
			root["version"] = Format::kRigVersion;
			root["actor"] = actor->id;
			root["skeleton"] = actor->skeletonId;
			root["gender"] = actor->gender == RE::SEX::kMale ? "male" : actor->gender == RE::SEX::kFemale ? "female" : "any";

			// Live bone values, where they differ from the rest pose.
			auto& pose = root["pose"] = json::array();
			for (size_t bone = 0; bone < actor->bones.size(); ++bone) {
				const auto& properties = actor->bones[bone];
				const auto differs = [](const auto& a_property) { return !(a_property.live == a_property.defaultValue); };
				if (!differs(properties.translation) && !differs(properties.rotation) && !differs(properties.scale)) continue;
				auto item = TransformJson(properties.translation.live, properties.rotation.live, properties.scale.live);
				item["bone"] = BoneName(a_project, actor->id, static_cast<int32_t>(bone));
				pose.push_back(std::move(item));
			}

			auto& mask = root["boneMask"] = json::array();
			for (const auto& [bone, masked] : actor->maskOverrides) mask.push_back({ { "bone", BoneName(a_project, actor->id, bone) }, { "masked", masked } });

			// Each modifier after its locators, in the actor's modifier order. Only the modifiers' own locators are kept.
			std::set<ProjectEx::ObjectId> rigLocators;
			std::vector<std::pair<ProjectEx::ObjectId, std::vector<ProjectEx::ObjectId>>> modifiers;
			for (const auto id : actor->modifiers) {
				auto& [modifier, locators] = modifiers.emplace_back(id, std::vector<ProjectEx::ObjectId>{});
				if (const auto object = Animation::Studio::AsModifier(a_project.FindObject(id))) locators = object->GetLocators();
				rigLocators.insert(locators.begin(), locators.end());
			}
			auto& objects = root["objects"] = json::array();
			for (const auto& [modifier, locators] : modifiers) {
				for (const auto id : locators) {
					auto item = WriteObject(a_project, id, true);
					const auto [parent, transform] = a_project.GetRigLocator(*actor, *find.operator()<Animation::Studio::LocatorObject>(id), rigLocators);
					json parentJson{ { "object", parent.object } };
					if (parent.bone >= 0) parentJson["bone"] = BoneName(a_project, parent.object, parent.bone);
					item["parent"] = std::move(parentJson);
					item["defaults"] = TransformJson(transform.translation, transform.rotation, transform.scale);
					objects.push_back(std::move(item));
				}
				objects.push_back(WriteObject(a_project, modifier, true));
			}

			if (!a_editorJson.empty()) root["editor"] = json::parse(a_editorJson);
			return WriteFile(a_path, Format::kRigMagic, Format::kRigVersion, root.dump(), {});
		} catch (const std::exception& e) {
			return std::unexpected(std::string("Could not save the rig: ") + e.what());
		}
	}
}
