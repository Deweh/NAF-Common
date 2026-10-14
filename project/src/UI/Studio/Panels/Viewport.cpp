#include "UI/Studio/Panels/Viewport.h"

#include "UI/Studio/StudioSession.h"
#include "UI/Studio/StudioCamera.h"
#include "Animation/Studio/StudioMath.h"
#include "imgui/ImGuizmo.h"

#include <nlohmann/json.hpp>

namespace UI::Studio
{
	namespace
	{
		using Animation::Studio::GetTranslation;
		using Animation::Studio::MakeTranslation;
		using Animation::Studio::TryInvert;

		// The chain using a locator as its effector or pole, if any.
		std::optional<std::pair<ProjectEx::ObjectId, ProjectEx::IKChainInfo>> FindOwningChain(const ProjectEx& a_project, ProjectEx::ObjectId a_locator)
		{
			const auto owner = a_project.GetLocatorOwner(a_locator);
			if (const auto chain = a_project.GetIKChain(owner)) return std::pair{ owner, *chain };
			return std::nullopt;
		}
	}

	Viewport::Viewport(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{
		auto& style = ImGuizmo::GetStyle();
		style.RotationLineThickness = 8.0f;
		style.RotationOuterLineThickness = 4.0f;
	}

	void Viewport::Draw(Renderer* a_renderer)
	{
		m_hovered.reset();
		if (!a_renderer) return;

		struct OverlayItem
		{
			ProjectEx::ElementRef element;
			ScreenSpaceDiamond diamond;
			ImU32 color;
			float thickness;
		};
		std::vector<OverlayItem> items;
		const auto& io = ImGui::GetIO();
		const bool mouseFree = !io.WantCaptureMouse && !m_session.camera.IsMouseLooking();
		if (view.sceneCenter) DrawSceneCenter(a_renderer, m_project->GetSceneCenter());

		for (const auto id : m_project->GetObjects()) {
			const auto type = m_project->GetObjectType(id);
			if (type == ProjectEx::ObjectType::kActor && view.bones) {
				const auto root = m_project->GetWorldTransform({ id });
				const auto skeleton = m_project->GetSkeleton(id);
				const auto model = m_project->GetModelTransforms(id);
				if (!root || !skeleton || model.size() < static_cast<size_t>(skeleton->num_joints())) continue;
				const auto parents = skeleton->joint_parents();
				const auto driven = m_project->GetDrivenBones(id);
				const auto boneWorld = [&](int32_t a_bone) { return GetTranslation(*root * model[a_bone]); };
				for (int32_t bone = 0; bone < skeleton->num_joints(); ++bone) {
					const auto position = boneWorld(bone);
					ozz::math::Float3 start;
					if (parents[bone] != ozz::animation::Skeleton::kNoParent) {
						start = boneWorld(parents[bone]);
					} else {
						// Root bones get a short stub pointing up.
						start = position;
						start.z -= 8.0f * kScaleMultiplier;
					}
					const ProjectEx::ElementRef element{ id, static_cast<uint16_t>(bone) };
					const auto display = m_session.GetBoneDisplay(element);
					if (!display.visible) continue;
					// Width follows bone length, so short bones like fingers don't hide the mesh around them.
					const float halfWidth = std::clamp(ozz::math::Length(position - start) * 0.1f, 0.25f * kScaleMultiplier, 1.5f * kScaleMultiplier) * display.scale;
					ScreenSpaceDiamond diamond;
					if (!a_renderer->BuildScreenSpaceDiamond(ToNiPoint(start), ToNiPoint(position), halfWidth, 0.0f, diamond)) continue;
					items.push_back({ element, diamond, bone < static_cast<int32_t>(driven.size()) && driven[bone] ? kColorBoneIK : kColorBone, 1.5f });
				}
			} else if (type == ProjectEx::ObjectType::kLocator && view.locators) {
				const auto world = m_project->GetWorldTransform({ id });
				ScreenSpaceDiamond diamond;
				if (!world || !a_renderer->BuildScreenSpaceDiamond(ToNiPoint(GetTranslation(*world)), 4.0f * kScaleMultiplier, diamond)) continue;
				ImU32 color = kColorLocator;
				if (const auto owner = FindOwningChain(*m_project, id)) {
					color = owner->second.pole == id ? kColorPole : owner->second.reached ? kColorEffectorReached : kColorEffectorUnreached;
				} else if (m_project->GetLocatorOwner(id) != ProjectEx::kNoObject) {
					color = kColorSplineControl;
				}
				items.push_back({ { id }, diamond, color, 2.0f });
			} else if (type == ProjectEx::ObjectType::kIKChain && view.ikLines) {
				const auto chain = m_project->GetIKChain(id);
				if (!chain) continue;
				const auto line = [&](ProjectEx::ObjectId a_locator, int32_t a_bone) {
					const auto locatorWorld = m_project->GetWorldTransform({ a_locator });
					const auto boneWorld = m_project->GetWorldTransform({ chain->actor, static_cast<uint16_t>(a_bone) });
					if (locatorWorld && boneWorld) a_renderer->DrawWorldLine(ToNiPoint(GetTranslation(*boneWorld)), ToNiPoint(GetTranslation(*locatorWorld)), kColorIKLine);
				};
				line(chain->effector, chain->endBone);
				line(chain->pole, chain->middleBone);
				// While the chain or its effector is selected: the shoulder's neutral point, the effector's offset from
				// it, and the dummy point the shoulder tracks. Chains without a shoulder have no guide.
				if (chain->shoulderGuide && (m_session.IsSelected({ id, ProjectEx::kSelf }) || m_session.IsSelected({ chain->effector, ProjectEx::kSelf }))) {
					const auto& guide = *chain->shoulderGuide;
					if (const auto effectorWorld = m_project->GetWorldTransform({ chain->effector })) {
						a_renderer->DrawWorldLine(ToNiPoint(guide.neutral), ToNiPoint(GetTranslation(*effectorWorld)), kColorShoulderGuide);
					}
					a_renderer->DrawWorldLine(ToNiPoint(guide.shoulder), ToNiPoint(guide.dummy), kColorShoulderGuide);
					for (const auto& point : { guide.neutral, guide.dummy }) {
						ScreenSpaceDiamond diamond;
						if (a_renderer->BuildScreenSpaceDiamond(ToNiPoint(point), 2.5f * kScaleMultiplier, diamond)) a_renderer->DrawWorldDiamond(diamond, kColorShoulderGuide, 1.5f);
					}
				}
			} else if (type == ProjectEx::ObjectType::kSplineIK && view.ikLines) {
				const auto curve = m_project->GetSplineIKCurve(id);
				for (size_t i = 1; i < curve.size(); ++i) a_renderer->DrawWorldLine(ToNiPoint(curve[i - 1]), ToNiPoint(curve[i]), kColorSplineCurve);
			}
		}

		// Hover picks the nearest item under the mouse.
		float nearest = (std::numeric_limits<float>::max)();
		for (const auto& item : items) {
			if (mouseFree && item.diamond.depth < nearest && a_renderer->IsScreenPointInsideDiamond(io.MousePos, item.diamond)) {
				nearest = item.diamond.depth;
				m_hovered = item.element;
			}
		}
		for (const auto& item : items) {
			const bool selected = m_session.IsSelected(item.element);
			const ImU32 color = selected ? kColorSelected : m_hovered == item.element ? kColorHovered : item.color;
			a_renderer->DrawWorldDiamond(item.diamond, color, selected ? item.thickness + 1.0f : item.thickness);
		}

		// The gizmo draws over the overlay, and takes clicks before it.
		DrawGizmo(a_renderer);

		if (mouseFree && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !m_session.IsInteracting()) {
			const auto mode = m_session.GetClickSelectMode();
			if (m_hovered) {
				m_session.Select(*m_hovered, mode);
				m_session.reveal = *m_hovered;
			} else if (mode == SelectMode::kReplace) {
				m_session.selection.clear();
			}
		}
	}

	void Viewport::DrawGizmo(Renderer* a_renderer)
	{
		if (m_session.gizmoCancelled) {
			// ImGuizmo keeps dragging until release; don't let it restart the cancelled transform.
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
			m_session.gizmoCancelled = false;
		}
		auto gizmo = std::get_if<GizmoInteraction>(&m_session.interaction);
		if (!gizmo && m_session.IsInteracting()) return;

		auto targets = gizmo ? std::vector<GizmoTarget>{} : CollectGizmoTargets();
		if (!gizmo && targets.empty()) return;

		ozz::math::Float4x4 view;
		ozz::math::Float4x4 projection;
		bool orthographic = false;
		if (!a_renderer->GetGizmoCameraMatrices(view, projection, orthographic)) return;

		const bool multiple = gizmo ? gizmo->targets.size() > 1 : targets.size() > 1;
		const auto mode = multiple || m_session.gizmoSpace == GizmoSpace::kWorld ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
		const auto operation = m_session.gizmoMode == GizmoMode::kTranslate ? ImGuizmo::TRANSLATE : m_session.gizmoMode == GizmoMode::kRotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;

		ozz::math::Float4x4 matrix;
		if (gizmo) {
			matrix = gizmo->pivot;
		} else {
			// Pivot on the active element; world-aligned in world space.
			const auto active = m_session.GetActiveElement();
			const auto pivotTarget = std::ranges::find(targets, active.value_or(ProjectEx::ElementRef{}), &GizmoTarget::element);
			const auto& pivotWorld = (pivotTarget != targets.end() ? *pivotTarget : targets.back()).startWorld;
			matrix = mode == ImGuizmo::LOCAL ? pivotWorld : MakeTranslation(GetTranslation(pivotWorld));
		}
		const auto before = matrix;

		const auto& io = ImGui::GetIO();
		ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
		ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
		ImGuizmo::SetOrthographic(orthographic);
		ImGuizmo::SetGizmoSizeClipSpace(0.1f);
		const bool changed = ImGuizmo::Manipulate(reinterpret_cast<const float*>(&view), reinterpret_cast<const float*>(&projection), operation, mode, reinterpret_cast<float*>(&matrix));

		if (!ImGuizmo::IsUsing()) {
			if (gizmo) m_session.EndInteraction();
			return;
		}
		if (!gizmo) {
			std::ranges::stable_sort(targets, {}, [&](const GizmoTarget& a_target) { return m_session.GetHierarchyDepth(a_target.element); });
			m_session.BeginInteraction(GizmoInteraction{ std::move(targets), before, before }, "Transform");
			gizmo = std::get_if<GizmoInteraction>(&m_session.interaction);
		}
		gizmo->pivot = matrix;
		if (!changed) return;

		const auto pivotInverse = TryInvert(gizmo->pivotStart);
		if (!pivotInverse) return;
		const auto delta = matrix * *pivotInverse;
		auto linear = delta;
		linear.cols[3] = ozz::math::simd_float4::w_axis();

		const uint8_t mask = m_session.gizmoMode == GizmoMode::kTranslate ? ProjectEx::kTransformTranslation :
		                     m_session.gizmoMode == GizmoMode::kRotate    ? ProjectEx::kTransformRotation :
		                                                            ProjectEx::kTransformScale;
		const auto property = m_session.gizmoMode == GizmoMode::kTranslate ? ProjectEx::PropertyId::kTranslation :
		                      m_session.gizmoMode == GizmoMode::kRotate    ? ProjectEx::PropertyId::kRotation :
		                                                             ProjectEx::PropertyId::kScale;
		std::vector<ProjectEx::WorldTransformEdit> edits;
		std::vector<ProjectEx::PropertyRef> keyed;
		edits.reserve(gizmo->targets.size());
		keyed.reserve(gizmo->targets.size());
		for (const auto& target : gizmo->targets) {
			ozz::math::Float4x4 world;
			if (m_session.gizmoMode == GizmoMode::kTranslate) {
				world = delta * target.startWorld;
			} else {
				// Rotate and scale each target about its own origin, so bones don't stretch away from their parents.
				const auto origin = GetTranslation(target.startWorld);
				world = MakeTranslation(origin) * linear * MakeTranslation(-origin) * target.startWorld;
			}
			edits.push_back({ target.element, world });
			keyed.push_back({ target.element.object, target.element.element, property });
		}
		m_project->SetWorldTransforms(edits, mask);
		m_session.AutoKey(keyed);
	}

	std::vector<GizmoTarget> Viewport::CollectGizmoTargets(size_t a_limit)
	{
		// Bones not driven by IK, locators, and actors (their placement: translation and heading only). IK chains
		// have no transform of their own.
		std::vector<GizmoTarget> targets;
		for (const auto& element : m_session.selection) {
			const auto type = m_project->GetObjectType(element.object);
			const bool movable = m_session.IsBone(element)                          ? !m_project->IsDrivenByModifier(element) :
			                     type == ProjectEx::ObjectType::kLocator ? true :
			                     type == ProjectEx::ObjectType::kActor   ? m_session.gizmoMode != GizmoMode::kScale :
			                                                               false;
			if (!movable) continue;
			if (const auto world = m_project->GetWorldTransform(element)) targets.push_back({ element, *world });
			if (targets.size() >= a_limit) break;
		}
		return targets;
	}

	void Viewport::WriteSettings(nlohmann::json& a_editor) const
	{
		a_editor["view"] = { { "bones", view.bones }, { "locators", view.locators }, { "ikLines", view.ikLines }, { "sceneCenter", view.sceneCenter } };
	}

	void Viewport::ReadSettings(const nlohmann::json& a_editor)
	{
		const auto viewJson = a_editor.find("view");
		if (viewJson == a_editor.end() || !viewJson->is_object()) return;
		view.bones = viewJson->value("bones", view.bones);
		view.locators = viewJson->value("locators", view.locators);
		view.ikLines = viewJson->value("ikLines", view.ikLines);
		view.sceneCenter = viewJson->value("sceneCenter", view.sceneCenter);
	}
}
