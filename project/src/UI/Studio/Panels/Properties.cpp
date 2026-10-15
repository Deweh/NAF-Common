#include "UI/Studio/Panels/Properties.h"

#include "UI/Studio/StudioSession.h"
#include "Animation/Studio/Modifiers/Hand.h"

namespace UI::Studio
{
	Properties::Properties(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	void Properties::Draw()
	{
		if (!m_session.windows.properties) return;
		if (!ImGui::Begin(kPropertiesWindow, &m_session.windows.properties)) {
			ImGui::End();
			return;
		}

		ImGui::SeparatorText("Gizmo");
		const auto modeButton = [&](const char* a_label, GizmoMode a_mode, Action a_action) {
			if (ImGui::RadioButton(a_label, m_session.gizmoMode == a_mode)) m_session.gizmoMode = a_mode;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", GetShortcutText(a_action).c_str());
			ImGui::SameLine();
		};
		modeButton("Translate", GizmoMode::kTranslate, Action::kGizmoTranslate);
		modeButton("Rotate", GizmoMode::kRotate, Action::kGizmoRotate);
		modeButton("Scale", GizmoMode::kScale, Action::kGizmoScale);
		ImGui::NewLine();

		const bool multiple = m_session.viewport.CollectGizmoTargets(2).size() > 1;
		ImGui::BeginDisabled(multiple);
		if (ImGui::RadioButton("Local", m_session.gizmoSpace == GizmoSpace::kLocal && !multiple)) m_session.gizmoSpace = GizmoSpace::kLocal;
		ImGui::SameLine();
		if (ImGui::RadioButton("World", m_session.gizmoSpace == GizmoSpace::kWorld || multiple)) m_session.gizmoSpace = GizmoSpace::kWorld;
		ImGui::EndDisabled();
		if (multiple) {
			ImGui::SameLine();
			ImGui::TextDisabled("(multiple selected)");
		}

		ImGui::SeparatorText("Keying");
		ImGui::Checkbox("Auto Key", &m_session.autoKey);
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::BeginCombo("Interpolation", GetInterpolationName(m_session.defaultInterpolation))) {
			for (const auto interpolation : { ProjectEx::Interpolation::kLinear, ProjectEx::Interpolation::kBezier, ProjectEx::Interpolation::kStep }) {
				if (ImGui::Selectable(GetInterpolationName(interpolation), m_session.defaultInterpolation == interpolation)) m_session.defaultInterpolation = interpolation;
			}
			ImGui::EndCombo();
		}

		const auto active = m_session.GetActiveElement();
		ImGui::SeparatorText(active ? m_session.GetElementLabel(*active).c_str() : "Selection");
		if (!active) {
			ImGui::TextDisabled("Nothing selected.");
			ImGui::End();
			return;
		}
		if (m_session.selection.size() > 1) ImGui::TextDisabled("%zu selected; showing the active element.", m_session.selection.size());

		switch (*m_project->GetObjectType(active->object)) {
		case ProjectEx::ObjectType::kActor:
			if (m_session.IsBone(*active)) {
				DrawTransformProperties(*active);

				ImGui::SeparatorText("Display");
				const auto bones = m_session.GetSelectedBones(false);
				auto display = m_session.GetBoneDisplay(*active);
				if (ImGui::Checkbox("Visible", &display.visible)) m_session.SetBoneDisplay(bones, display.visible, std::nullopt);
				if (ImGui::SliderFloat("Display Size", &display.scale, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) m_session.SetBoneDisplay(bones, std::nullopt, display.scale);
				if (bones.size() > 1) ImGui::TextDisabled("Applies to all %zu selected bones.", bones.size());

				ImGui::SeparatorText("Game Output");
				bool masked = m_project->IsBoneMasked(*active);
				if (ImGui::Checkbox("Masked", &masked)) m_project->SetBoneMasked(bones, masked);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Masked bones aren't written to the game, so edits to them don't show on the actor.");
				if (bones.size() > 1) ImGui::TextDisabled("Applies to all %zu selected bones.", bones.size());
			} else {
				if (const auto actor = m_project->GetBoundActor(active->object)) {
					ImGui::Text("Bound to %s", GetActorLabel(actor).c_str());
				} else {
					ImGui::TextDisabled("Unbound. Right-click the actor in the Outliner to bind it.");
				}

				ImGui::SeparatorText("Placement");
				ImGui::TextDisabled("Relative to the scene center.");
				if (auto placement = m_project->GetActorPlacement(active->object)) {
					const auto apply = [&] {
						m_project->BeginEdit("Move actor");
						m_project->SetActorPlacement(active->object, *placement);
						m_project->EndEdit();
					};
					float translation[3]{ placement->translation.x, placement->translation.y, placement->translation.z };
					const bool moved = ImGui::DragFloat3("Position", translation, 0.1f * kScaleMultiplier);
					TrackFieldEdit("Move actor", moved, [&] {
						placement->translation = { translation[0], translation[1], translation[2] };
						apply();
					});
					float heading = placement->heading * ozz::math::kRadianToDegree;
					const bool turned = ImGui::DragFloat("Heading", &heading, 0.5f, 0.0f, 0.0f, "%.1f deg");
					TrackFieldEdit("Turn actor", turned, [&] {
						placement->heading = std::remainder(heading, 360.0f) * ozz::math::kDegreeToRadian;
						apply();
					});
				}
			}
			break;
		case ProjectEx::ObjectType::kLocator:
			{
				const auto parent = m_project->GetLocatorParent(active->object).value_or(ProjectEx::ParentRef{});
				std::string parentLabel = "Scene";
				if (parent.object != ProjectEx::kNoObject) {
					parentLabel = m_session.GetElementLabel({ parent.object });
					if (parent.bone >= 0) parentLabel += " / " + m_session.GetElementLabel({ parent.object, static_cast<uint16_t>(parent.bone) });
				}
				ImGui::TextDisabled("Parent: %s", parentLabel.c_str());
				DrawTransformProperties(*active);
				break;
			}
		case ProjectEx::ObjectType::kIKChain:
			DrawIKChainProperties(active->object);
			break;
		case ProjectEx::ObjectType::kHand:
			DrawHandProperties(active->object);
			break;
		case ProjectEx::ObjectType::kSplineIK:
			DrawSplineIKProperties(active->object);
			break;
		}
		ImGui::End();
	}

	void Properties::DrawTransformProperties(ProjectEx::ElementRef a_element)
	{
		using PropertyId = ProjectEx::PropertyId;
		const bool driven = m_project->IsDrivenByModifier(a_element);
		if (driven) ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.6f, 1.0f), "Driven by IK.");
		ImGui::BeginDisabled(driven);

		const ProjectEx::PropertyRef translationRef{ a_element.object, a_element.element, PropertyId::kTranslation };
		if (const auto translation = m_project->GetValue<ozz::math::Float3>(translationRef)) {
			DrawKeyIndicator(translationRef);
			ImGui::SameLine();
			float values[3]{ translation->x, translation->y, translation->z };
			const bool changed = ImGui::DragFloat3("Translation", values, 0.1f * kScaleMultiplier);
			TrackFieldEdit("Edit translation", changed, [&] { m_session.ApplyFieldValue(translationRef, ozz::math::Float3{ values[0], values[1], values[2] }); });
		}

		const ProjectEx::PropertyRef rotationRef{ a_element.object, a_element.element, PropertyId::kRotation };
		if (const auto rotation = m_project->GetValue<ozz::math::Quaternion>(rotationRef)) {
			DrawKeyIndicator(rotationRef);
			ImGui::SameLine();
			const auto euler = ozz::math::ToEuler(*rotation) * ozz::math::kRadianToDegree;
			float values[3]{ euler.x, euler.y, euler.z };
			const bool changed = ImGui::DragFloat3("Rotation (YPR)", values, 0.5f, 0.0f, 0.0f, "%.1f");
			TrackFieldEdit("Edit rotation", changed, [&] {
				const ozz::math::Float3 radians{ values[0] * ozz::math::kDegreeToRadian, values[1] * ozz::math::kDegreeToRadian, values[2] * ozz::math::kDegreeToRadian };
				m_session.ApplyFieldValue(rotationRef, ozz::math::Quaternion::FromEuler(radians));
			});
		}

		const ProjectEx::PropertyRef scaleRef{ a_element.object, a_element.element, PropertyId::kScale };
		if (const auto scale = m_project->GetValue<ozz::math::Float3>(scaleRef)) {
			DrawKeyIndicator(scaleRef);
			ImGui::SameLine();
			float values[3]{ scale->x, scale->y, scale->z };
			const bool changed = ImGui::DragFloat3("Scale", values, 0.01f);
			TrackFieldEdit("Edit scale", changed, [&] { m_session.ApplyFieldValue(scaleRef, ozz::math::Float3{ values[0], values[1], values[2] }); });
		}

		ImGui::EndDisabled();
	}

	void Properties::DrawIKChainProperties(ProjectEx::ObjectId a_chain)
	{
		using PropertyId = ProjectEx::PropertyId;
		const auto chain = m_project->GetIKChain(a_chain);
		if (!chain) return;

		const auto boneLabel = [&](int32_t a_bone) { return m_session.GetElementLabel({ chain->actor, static_cast<uint16_t>(a_bone) }); };
		ImGui::TextDisabled("%s > %s > %s", boneLabel(chain->upperBone).c_str(), boneLabel(chain->middleBone).c_str(), boneLabel(chain->endBone).c_str());
		ImGui::TextColored(chain->reached ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f), chain->reached ? "Target reached" : "Target out of reach");

		const ProjectEx::PropertyRef enabledRef{ a_chain, ProjectEx::kSelf, PropertyId::kEnabled };
		if (auto enabled = m_project->GetValue<bool>(enabledRef)) {
			DrawKeyIndicator(enabledRef);
			ImGui::SameLine();
			if (ImGui::Checkbox("Enabled", std::addressof(*enabled))) m_session.ApplyFieldValue(enabledRef, *enabled);
		}

		DrawFloatProperty({ a_chain, ProjectEx::kSelf, PropertyId::kWeight }, "Weight", 0.0f, 1.0f, false);
		DrawFloatProperty({ a_chain, ProjectEx::kSelf, PropertyId::kSoften }, "Soften", 0.0f, 1.0f, false);
		DrawFloatProperty({ a_chain, ProjectEx::kSelf, PropertyId::kTwist }, "Twist", -180.0f, 180.0f, true);
		DrawFloatProperty({ a_chain, ProjectEx::kSelf, PropertyId::kTwistDistribution }, "Twist Distribution", 0.0f, 1.0f, false);

		ImGui::SeparatorText("Shoulder");
		auto settings = chain->settings;
		const auto skeleton = m_project->GetSkeleton(chain->actor);
		const char* shoulderName = skeleton && settings.shoulderBone >= 0 ? skeleton->joint_names()[settings.shoulderBone] : "None";
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("Shoulder Bone", shoulderName)) {
			DrawShoulderMenu(a_chain);
			ImGui::EndCombo();
		}
		if (settings.shoulderBone >= 0) {
			DrawFloatProperty({ a_chain, ProjectEx::kSelf, PropertyId::kShoulderWeight }, "Shoulder Weight", 0.0f, 1.0f, false);
			// Not keyable. The field interaction folds a whole drag into one undo step.
			float maxAngle = settings.shoulderMaxAngle;
			const bool angleChanged = ImGui::SliderAngle("Max Shoulder Angle", &maxAngle, 0.0f, 90.0f);
			TrackFieldEdit("Edit shoulder angle", angleChanged, [&] {
				settings.shoulderMaxAngle = maxAngle;
				m_project->SetIKChainSettings(a_chain, settings);
			});
			if (ImGui::Button("Set Neutral")) m_project->SetIKChainShoulderNeutral(a_chain);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Makes the effector's current position the relaxed one.\nThe shoulder only moves as the effector moves away from it, relative to the shoulder's parent.");
			}
		}

		ImGui::Separator();
		for (const auto& [label, locator] : { std::pair{ "Effector", chain->effector }, std::pair{ "Pole", chain->pole } }) {
			if (ImGui::Selectable(std::format("{}: {}", label, m_session.GetElementLabel({ locator })).c_str())) m_session.Select({ locator, ProjectEx::kSelf }, SelectMode::kReplace);
		}
	}

	void Properties::DrawShoulderMenu(ProjectEx::ObjectId a_chain)
	{
		const auto chain = m_project->GetIKChain(a_chain);
		const auto skeleton = chain ? m_project->GetSkeleton(chain->actor) : nullptr;
		if (!skeleton) return;
		auto settings = chain->settings;
		if (ImGui::Selectable("None", settings.shoulderBone < 0)) {
			settings.shoulderBone = -1;
			m_project->SetIKChainSettings(a_chain, settings);
		}
		// Any bone above the limb, nearest first.
		const auto parents = skeleton->joint_parents();
		for (int32_t bone = parents[chain->upperBone]; bone != ozz::animation::Skeleton::kNoParent; bone = parents[bone]) {
			ImGui::PushID(bone);
			if (ImGui::Selectable(skeleton->joint_names()[bone], settings.shoulderBone == bone) && settings.shoulderBone != bone) {
				settings.shoulderBone = bone;
				if (!m_project->SetIKChainSettings(a_chain, settings)) m_session.SetStatus("That shoulder would make the chain depend on its own result.");
			}
			ImGui::PopID();
		}
	}

	void Properties::DrawFloatProperty(const ProjectEx::PropertyRef& a_property, const char* a_label, float a_min, float a_max, bool a_angle)
	{
		auto value = m_project->GetValue<float>(a_property);
		if (!value) return;
		DrawKeyIndicator(a_property);
		ImGui::SameLine();
		const bool changed = a_angle ? ImGui::SliderAngle(a_label, std::addressof(*value), a_min, a_max) : ImGui::SliderFloat(a_label, std::addressof(*value), a_min, a_max);
		TrackFieldEdit(std::format("Edit {}", GetPropertyName(a_property.property)).c_str(), changed, [&] { m_session.ApplyFieldValue(a_property, *value); });
	}

	void Properties::DrawHandPresetMenu(ProjectEx::ObjectId a_hand)
	{
		const auto hand = m_project->GetHand(a_hand);
		if (!hand) return;
		const size_t fingerCount = hand->settings.fingers.size();
		for (const auto& preset : Animation::Studio::kHandPresets) {
			if (ImGui::MenuItem(preset.name)) {
				m_session.ApplyHandPose(a_hand, [&](size_t a_finger) {
					return std::pair{ preset.GetCurl(a_finger, fingerCount), 0.0f };
				}, std::format("Finger preset: {}", preset.name).c_str());
			}
		}
	}

	void Properties::DrawHandProperties(ProjectEx::ObjectId a_hand)
	{
		using PropertyId = ProjectEx::PropertyId;
		const auto hand = m_project->GetHand(a_hand);
		const auto skeleton = hand ? m_project->GetSkeleton(hand->actor) : nullptr;
		if (!skeleton) return;
		const auto names = skeleton->joint_names();

		const ProjectEx::PropertyRef enabledRef{ a_hand, ProjectEx::kSelf, PropertyId::kEnabled };
		if (auto enabled = m_project->GetValue<bool>(enabledRef)) {
			DrawKeyIndicator(enabledRef);
			ImGui::SameLine();
			if (ImGui::Checkbox("Enabled", std::addressof(*enabled))) m_session.ApplyFieldValue(enabledRef, *enabled);
		}
		DrawFloatProperty({ a_hand, ProjectEx::kSelf, PropertyId::kWeight }, "Weight", 0.0f, 1.0f, false);
		DrawFloatProperty({ a_hand, ProjectEx::kSelf, PropertyId::kCurl }, "Fist", -1.0f, 1.0f, false);
		DrawFloatProperty({ a_hand, ProjectEx::kSelf, PropertyId::kSpread }, "Spread", -1.0f, 1.0f, false);

		if (ImGui::Button("Presets...")) ImGui::OpenPopup("##hand_presets");
		if (ImGui::BeginPopup("##hand_presets")) {
			DrawHandPresetMenu(a_hand);
			ImGui::EndPopup();
		}

		ImGui::SeparatorText("Fingers");
		const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 2.0f) * 0.5f;
		for (size_t i = 0; i < hand->settings.fingers.size(); ++i) {
			const auto finger = static_cast<uint16_t>(i);
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextUnformatted(names[hand->settings.fingers[i].bones.front()]);
			for (const auto& [property, label, min] : { std::tuple{ PropertyId::kCurl, "##curl", -1.0f }, std::tuple{ PropertyId::kSpread, "##spread", -1.0f } }) {
				const ProjectEx::PropertyRef ref{ a_hand, finger, property };
				auto value = m_project->GetValue<float>(ref);
				if (!value) continue;
				if (property == PropertyId::kSpread) ImGui::SameLine();
				DrawKeyIndicator(ref);
				ImGui::SameLine();
				ImGui::SetNextItemWidth((std::max)(width - ImGui::GetFrameHeight(), 40.0f));
				const bool changed = ImGui::SliderFloat(label, std::addressof(*value), min, 1.0f, property == PropertyId::kCurl ? "Curl %.2f" : "Spread %.2f");
				TrackFieldEdit(std::format("Edit {}", GetPropertyLabel(*m_project, ref)).c_str(), changed, [&] { m_session.ApplyFieldValue(ref, *value); });
			}
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("Setup")) {
			// Not keyable. The field interaction folds a whole drag into one undo step.
			auto settings = hand->settings;
			const auto apply = [&] { m_project->SetHandSettings(a_hand, settings); };
			for (size_t i = 0; i < settings.fingers.size(); ++i) {
				auto& finger = settings.fingers[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::SeparatorText(names[finger.bones.front()]);
				if (ImGui::Checkbox("Flip Curl", &finger.flip)) apply();
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use when this finger curls backward.");
				const bool flat = !finger.straighten.empty();
				if (!flat) ImGui::TextDisabled("Made before curl 0 meant straight. Recreate to update.");
				for (size_t k = 0; k < finger.maxCurl.size(); ++k) {
					ImGui::PushID(static_cast<int>(k));
					const auto bone = names[finger.bones[k]];
					const bool curlChanged = ImGui::SliderAngle(std::format("Max Curl ({})", bone).c_str(), &finger.maxCurl[k], 0.0f, 180.0f);
					TrackFieldEdit("Edit finger setup", curlChanged, apply);
					if (flat && k < finger.maxExtend.size()) {
						const bool extendChanged = ImGui::SliderAngle(std::format("Max Extend ({})", bone).c_str(), &finger.maxExtend[k], 0.0f, 90.0f);
						TrackFieldEdit("Edit finger setup", extendChanged, apply);
						const bool straightenChanged = ImGui::SliderAngle(std::format("Straighten ({})", bone).c_str(), &finger.straighten[k], -90.0f, 90.0f);
						TrackFieldEdit("Edit finger setup", straightenChanged, apply);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns this joint from its rest pose to straight, at curl 0.");
					}
					ImGui::PopID();
				}
				const bool spreadChanged = ImGui::SliderAngle("Max Spread", &finger.maxSpread, 0.0f, 90.0f);
				TrackFieldEdit("Edit finger setup", spreadChanged, apply);
				ImGui::PopID();
			}
		}
	}

	void Properties::DrawSplineIKProperties(ProjectEx::ObjectId a_spline)
	{
		using PropertyId = ProjectEx::PropertyId;
		const auto spline = m_project->GetSplineIK(a_spline);
		if (!spline || spline->bones.empty()) return;
		const auto boneLabel = [&](int32_t a_bone) { return m_session.GetElementLabel({ spline->actor, static_cast<uint16_t>(a_bone) }); };
		ImGui::TextDisabled("%s > %s (%zu bones)", boneLabel(spline->bones.front()).c_str(), boneLabel(spline->bones.back()).c_str(), spline->bones.size());

		const ProjectEx::PropertyRef enabledRef{ a_spline, ProjectEx::kSelf, PropertyId::kEnabled };
		if (auto enabled = m_project->GetValue<bool>(enabledRef)) {
			DrawKeyIndicator(enabledRef);
			ImGui::SameLine();
			if (ImGui::Checkbox("Enabled", std::addressof(*enabled))) m_session.ApplyFieldValue(enabledRef, *enabled);
		}
		DrawFloatProperty({ a_spline, ProjectEx::kSelf, PropertyId::kWeight }, "Weight", 0.0f, 1.0f, false);
		DrawFloatProperty({ a_spline, ProjectEx::kSelf, PropertyId::kTwist }, "Twist", -180.0f, 180.0f, true);
		DrawFloatProperty({ a_spline, ProjectEx::kSelf, PropertyId::kStretch }, "Stretch", 0.0f, 1.0f, false);
		DrawFloatProperty({ a_spline, ProjectEx::kSelf, PropertyId::kTangentLength }, "Tangent Length", 0.0f, 3.0f, false);
		DrawFloatProperty({ a_spline, ProjectEx::kSelf, PropertyId::kOrient }, "Orient", 0.0f, 1.0f, false);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("1: the bones turn to follow the curve, keeping their lengths.\n0: the bones keep their rotation and move onto the curve, stretching.\nLow values suit bones skinned for physics, such as breasts.");

		auto settings = spline->settings;
		bool changed = ImGui::Checkbox("Root Follows Curve", &settings.rootFollowsCurve);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: the first bone keeps its animated position, and the curve bends to start there.");
		changed |= ImGui::Checkbox("Align Tip", &settings.alignTip);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("The last bone takes the last control's rotation.");
		if (changed) m_project->SetSplineIKSettings(a_spline, settings);

		ImGui::SeparatorText("Controls");
		for (const auto control : spline->controls) {
			ImGui::PushID(static_cast<int>(control));
			if (ImGui::Selectable(m_session.GetElementLabel({ control }).c_str(), m_session.IsSelected({ control }))) m_session.Select({ control, ProjectEx::kSelf }, m_session.GetClickSelectMode());
			ImGui::PopID();
		}
	}

	bool Properties::DrawKeyIndicator(const ProjectEx::PropertyRef& a_property)
	{
		const float height = ImGui::GetFrameHeight();
		ImGui::PushID(static_cast<int>(a_property.property));
		const bool clicked = ImGui::InvisibleButton("##key", { height * 0.75f, height });

		const auto frames = m_project->GetKeyFrames(a_property);
		const bool keyedHere = std::ranges::contains(frames, m_project->GetFrame());
		const bool edited = m_project->IsEdited(a_property);
		const auto loop = m_project->GetLoopSmoothing(a_property);
		const bool loopEnabled = loop.property || loop.sequence;
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const ImVec2 center{ (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
		const ImU32 color = edited ? kColorEdited : keyedHere ? kColorKeyHere : frames.empty() ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : kColorKey;
		auto drawList = ImGui::GetWindowDrawList();
		DrawDiamond(drawList, center, height * 0.25f, keyedHere || edited ? color : 0, color);
		// Cyclic marker: green when it's taking effect, orange when enabled but no key is inside the sequence.
		if (loopEnabled) drawList->AddCircle({ max.x - 2.0f, min.y + 4.0f }, 2.5f, loop.applied ? kColorEffectorReached : kColorEdited, 0, 1.5f);

		if (ImGui::IsItemHovered()) {
			const char* loopText = !loopEnabled ? "" : loop.applied ? "\nCyclic." : "\nCyclic, but has no keys inside the sequence.";
			const char* bezierText = m_project->GetBezierMode(a_property) == ProjectEx::BezierMode::kContinuous ? "\nBezier mode: Continuous." : "";
			ImGui::SetTooltip("%s%s%s\nClick to %s a key at this frame. Right-click for options.",
				edited ? "Changed but not keyed." : keyedHere ? "Keyed at this frame." : frames.empty() ? "Not animated." : "Animated.",
				loopText, bezierText, keyedHere && !edited ? "remove" : "set");
		}
		if (ImGui::BeginPopupContextItem("##key_options")) {
			if (ImGui::MenuItem("Cyclic", nullptr, loop.property)) m_project->SetLoopSmoothing(std::span{ &a_property, 1 }, !loop.property);
			if (loop.sequence) ImGui::TextDisabled("Also on for the whole sequence.");
			m_session.DrawBezierModeMenu(std::span{ &a_property, 1 });
			ImGui::EndPopup();
		}
		ImGui::PopID();
		if (!clicked) return false;
		if (keyedHere && !edited) {
			const ProjectEx::KeyRef key{ a_property, m_project->GetFrame() };
			m_project->DeleteKeys(std::span{ &key, 1 });
		} else {
			m_project->KeyCurrentValue(std::span{ &a_property, 1 }, m_session.defaultInterpolation);
		}
		return true;
	}

	void Properties::TrackFieldEdit(const char* a_description, bool a_changed, const std::function<void()>& a_apply)
	{
		// Opened before applying: a slider sets its value on the frame it activates (it jumps to the click), and that
		// belongs to the edit. Closed after: a typed value is committed on the frame the field deactivates.
		// a_apply mustn't submit ImGui items, which would replace the field as the last item.
		const ImGuiID id = ImGui::GetItemID();
		if (ImGui::IsItemActivated()) m_session.BeginInteraction(FieldEditInteraction{ id }, a_description);
		if (a_changed) a_apply();
		if (ImGui::IsItemDeactivated()) {
			if (const auto edit = std::get_if<FieldEditInteraction>(&m_session.interaction); edit && edit->id == id) m_session.EndInteraction();
		}
	}
}
