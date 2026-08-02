#include "UI/Studio/Panels/Outliner.h"

#include "UI/Studio/StudioSession.h"
#include "Util/String.h"

namespace UI::Studio
{
	Outliner::Outliner(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	void Outliner::Draw()
	{
		if (!m_session.windows.outliner) return;
		if (!ImGui::Begin(kOutlinerWindow, &m_session.windows.outliner)) {
			ImGui::End();
			return;
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		InputTextString("##filter", "Filter bones...", m_filter);

		BuildLocatorChildren();
		ImGui::BeginChild("##outliner_tree");
		for (const auto id : m_project->GetObjects()) {
			if (m_project->GetObjectType(id) == ProjectEx::ObjectType::kActor) DrawActor(id);
		}
		DrawLocators({});

		if (ImGui::BeginPopupContextWindow("##outliner_empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
			if (ImGui::BeginMenu("Add Actor")) {
				m_session.DrawActorSourceMenu([&](RE::Actor* a_actor) { m_session.AddActor(a_actor); }, Popup::kAddActorByFormId, ProjectEx::kNoObject);
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Add Locator")) m_session.AddLocator({}, m_session.GetSpawnTransform());
			ImGui::EndPopup();
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void Outliner::DrawActor(ProjectEx::ObjectId a_actor)
	{
		const ProjectEx::ElementRef element{ a_actor, ProjectEx::kSelf };
		ImGui::PushID(static_cast<int>(a_actor));

		// Open the actor if something inside it is being revealed.
		const bool revealInside = m_session.reveal && *m_session.reveal != element &&
		                          (m_session.reveal->object == a_actor || m_project->GetModifierActor(m_session.reveal->object) == a_actor ||
									  m_project->GetModifierActor(m_project->GetLocatorOwner(m_session.reveal->object)) == a_actor);
		if (revealInside) ImGui::SetNextItemOpen(true);

		const auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
		                   ImGuiTreeNodeFlags_DefaultOpen | (m_session.IsSelected(element) ? ImGuiTreeNodeFlags_Selected : 0);
		const bool open = ImGui::TreeNodeEx("##actor", flags, "%s", m_session.GetElementLabel(element).c_str());
		DrawItemBehavior(element);
		if (!open) {
			ImGui::PopID();
			return;
		}

		// Bones: a hierarchy, or a flat list of matches while filtering.
		if (const auto skeleton = m_project->GetSkeleton(a_actor)) {
			const auto names = skeleton->joint_names();
			const auto parents = skeleton->joint_parents();
			const int32_t jointCount = skeleton->num_joints();
			const auto drawBoneItem = [&](int32_t a_bone, ImGuiTreeNodeFlags a_flags) {
				const ProjectEx::ElementRef bone{ a_actor, static_cast<uint16_t>(a_bone) };
				ImGui::PushID(a_bone);
				// Hidden bones are dimmed.
				const bool hidden = !m_session.GetBoneDisplay(bone).visible;
				if (hidden) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				const bool boneOpen = ImGui::TreeNodeEx("##bone", a_flags | ImGuiTreeNodeFlags_SpanAvailWidth | (m_session.IsSelected(bone) ? ImGuiTreeNodeFlags_Selected : 0), "%s", names[a_bone]);
				if (hidden) ImGui::PopStyleColor();
				DrawItemBehavior(bone);
				if (m_project->IsBoneMasked(bone)) {
					ImGui::SameLine();
					ImGui::TextDisabled("(masked)");
				}
				ImGui::PopID();
				return boneOpen;
			};

			if (!m_filter.empty()) {
				const auto filter = Util::String::ToLower(m_filter);
				for (int32_t bone = 0; bone < jointCount; ++bone) {
					if (Util::String::ToLower(names[bone]).contains(filter)) drawBoneItem(bone, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
				}
			} else {
				// SetNextItemOpen rather than DefaultOpen: DefaultOpen only applies before the node has stored state.
				const bool revealBone = m_session.reveal && m_session.reveal->object == a_actor && m_session.IsBone(*m_session.reveal);
				if (revealBone) ImGui::SetNextItemOpen(true);
				if (ImGui::TreeNodeEx("Skeleton")) {
					std::vector<std::vector<int32_t>> children(jointCount);
					std::vector<int32_t> roots;
					for (int32_t bone = 0; bone < jointCount; ++bone) {
						if (parents[bone] == ozz::animation::Skeleton::kNoParent) roots.push_back(bone);
						else children[parents[bone]].push_back(bone);
					}
					std::vector<bool> revealPath(jointCount, false);
					if (revealBone) {
						for (int32_t joint = parents[m_session.reveal->element]; joint != ozz::animation::Skeleton::kNoParent; joint = parents[joint]) revealPath[joint] = true;
					}
					const auto drawBone = [&](this auto&& a_self, int32_t a_bone) -> void {
						const bool leaf = children[a_bone].empty();
						if (revealPath[a_bone]) ImGui::SetNextItemOpen(true);
						const auto boneFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
						                       (leaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0);
						if (drawBoneItem(a_bone, boneFlags) && !leaf) {
							for (const int32_t child : children[a_bone]) a_self(child);
							ImGui::TreePop();
						}
					};
					for (const int32_t root : roots) drawBone(root);
					ImGui::TreePop();
				}
			}
		} else {
			ImGui::TextDisabled("No skeleton.");
		}

		// Modifiers: IK chains with their effector and pole, splines with their controls, and hands.
		for (const auto modifierId : m_project->GetModifiers(a_actor)) {
			std::vector<ProjectEx::ObjectId> locators;
			if (const auto chain = m_project->GetIKChain(modifierId)) locators = { chain->effector, chain->pole };
			else if (const auto spline = m_project->GetSplineIK(modifierId)) locators = spline->controls;
			const ProjectEx::ElementRef modifierElement{ modifierId, ProjectEx::kSelf };
			ImGui::PushID(static_cast<int>(modifierId));
			if (m_session.reveal && std::ranges::contains(locators, m_session.reveal->object)) ImGui::SetNextItemOpen(true);
			const auto modifierFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
			                           (locators.empty() ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0) |
			                           (m_session.IsSelected(modifierElement) ? ImGuiTreeNodeFlags_Selected : 0);
			const bool modifierOpen = ImGui::TreeNodeEx("##modifier", modifierFlags, "%s", m_session.GetElementLabel(modifierElement).c_str());
			DrawItemBehavior(modifierElement);
			if (modifierOpen && !locators.empty()) {
				for (const auto locator : locators) {
					const ProjectEx::ElementRef locatorElement{ locator, ProjectEx::kSelf };
					ImGui::PushID(static_cast<int>(locator));
					ImGui::TreeNodeEx("##handle", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (m_session.IsSelected(locatorElement) ? ImGuiTreeNodeFlags_Selected : 0),
						"%s", m_session.GetElementLabel(locatorElement).c_str());
					DrawItemBehavior(locatorElement);
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		DrawLocators({ a_actor });
		ImGui::TreePop();
		ImGui::PopID();
	}

	void Outliner::BuildLocatorChildren()
	{
		// Locators owned by an IK chain or spline are listed under it instead.
		m_locatorChildren.clear();
		for (const auto id : m_project->GetObjects()) {
			const auto parent = m_project->GetLocatorParent(id);
			if (parent && m_project->GetLocatorOwner(id) == ProjectEx::kNoObject) m_locatorChildren[parent->object].push_back(id);
		}
	}

	void Outliner::DrawLocators(const ProjectEx::ParentRef& a_parent)
	{
		const auto children = m_locatorChildren.find(a_parent.object);
		if (children == m_locatorChildren.end()) return;
		for (const auto id : children->second) {
			const ProjectEx::ElementRef element{ id, ProjectEx::kSelf };
			ImGui::PushID(static_cast<int>(id));
			const auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
			                   ImGuiTreeNodeFlags_DefaultOpen | (m_session.IsSelected(element) ? ImGuiTreeNodeFlags_Selected : 0);
			const bool open = ImGui::TreeNodeEx("##locator", flags, "%s", m_session.GetElementLabel(element).c_str());
			DrawItemBehavior(element);
			if (open) {
				DrawLocators({ id });
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	void Outliner::DrawItemBehavior(ProjectEx::ElementRef a_element)
	{
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) m_session.Select(a_element, m_session.GetClickSelectMode());
		// Right-clicking an unselected item acts on it alone.
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !m_session.IsSelected(a_element)) m_session.Select(a_element, SelectMode::kReplace);
		if (m_session.reveal == a_element) {
			if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
			m_session.reveal.reset();
		}
		if (ImGui::BeginPopupContextItem()) {
			DrawElementContextMenu(a_element);
			ImGui::EndPopup();
		}
	}

	void Outliner::DrawElementContextMenu(ProjectEx::ElementRef a_element)
	{
		const auto type = m_project->GetObjectType(a_element.object);
		if (!type) return;
		const auto renameShortcut = GetShortcutText(Action::kRename);
		const auto removeShortcut = GetShortcutText(Action::kDeleteSelection);
		const auto remove = [&] {
			ImGui::Separator();
			if (ImGui::MenuItem("Remove", removeShortcut.c_str())) m_session.DeleteSelection();
		};

		if (m_session.IsBone(a_element)) {
			m_session.ActionMenuItem(Action::kCreateIKChain);
			m_session.ActionMenuItem(Action::kCreateSplineIK);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !m_session.GetSplineIKSelection()) ImGui::SetTooltip("Select a start and an end bone, with at least one bone between them.");
			m_session.ActionMenuItem(Action::kCreateFingerControls);
			if (ImGui::MenuItem("Add Locator Here")) m_session.AddLocator({ a_element.object, a_element.element }, std::nullopt);
			ImGui::Separator();
			m_session.ActionMenuItem(Action::kInsertKey);
			if (ImGui::MenuItem("Reset Transform")) m_session.ResetTransforms(m_session.selection);
			if (ImGui::BeginMenu("Display")) {
				DrawBoneDisplayMenu();
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Mask")) {
				const auto bones = m_session.GetSelectedBones(false);
				const auto hierarchy = m_session.GetSelectedBones(true);
				if (ImGui::MenuItem("Mask")) m_project->SetBoneMasked(bones, true);
				if (ImGui::MenuItem("Unmask")) m_project->SetBoneMasked(bones, false);
				if (ImGui::MenuItem("Mask Hierarchy")) m_project->SetBoneMasked(hierarchy, true);
				if (ImGui::MenuItem("Unmask Hierarchy")) m_project->SetBoneMasked(hierarchy, false);
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Select Hierarchy")) {
				const auto parents = m_project->GetSkeleton(a_element.object)->joint_parents();
				for (int32_t joint = 0; joint < static_cast<int32_t>(parents.size()); ++joint) {
					for (int32_t ancestor = parents[joint]; ancestor != ozz::animation::Skeleton::kNoParent; ancestor = parents[ancestor]) {
						if (ancestor != a_element.element) continue;
						const ProjectEx::ElementRef descendant{ a_element.object, static_cast<uint16_t>(joint) };
						if (!m_session.IsSelected(descendant)) m_session.selection.insert(m_session.selection.begin(), descendant);
						break;
					}
				}
			}
			return;
		}

		switch (*type) {
		case ProjectEx::ObjectType::kActor:
			if (ImGui::MenuItem("Rename...", renameShortcut.c_str())) m_session.RequestPopup({ Popup::kRename, a_element.object });
			if (ImGui::BeginMenu("Bind")) {
				m_session.DrawActorSourceMenu([&](RE::Actor* a_actor) { m_session.BindActor(a_element.object, a_actor); }, Popup::kBindActor, a_element.object);
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Unbind", nullptr, false, m_project->GetBoundActor(a_element.object) != nullptr)) m_project->UnbindActor(a_element.object);
			ImGui::Separator();
			if (ImGui::MenuItem("Add Locator")) m_session.AddLocator({ a_element.object }, std::nullopt);
			if (ImGui::MenuItem("Reset Pose")) m_session.ResetTransforms(std::span{ &a_element, 1 });
			if (ImGui::MenuItem("Reset Bone Display")) m_session.ResetBoneDisplay(a_element.object);
			if (ImGui::MenuItem("Reset Bone Mask")) m_project->ResetBoneMask(a_element.object);
			ImGui::Separator();
			{
				const bool bound = m_project->GetBoundActor(a_element.object) != nullptr;
				if (ImGui::MenuItem("Save Rig...", nullptr, false, bound)) m_session.RequestPopup({ Popup::kSaveRig, a_element.object });
				if (ImGui::MenuItem("Apply Rig...", nullptr, false, bound)) m_session.RequestPopup({ Popup::kApplyRig, a_element.object });
			}
			remove();
			break;
		case ProjectEx::ObjectType::kIKChain:
			{
				if (ImGui::MenuItem("Rename...", renameShortcut.c_str())) m_session.RequestPopup({ Popup::kRename, a_element.object });
				const ProjectEx::PropertyRef enabledRef{ a_element.object, ProjectEx::kSelf, ProjectEx::PropertyId::kEnabled };
				const bool enabled = m_project->GetValue<bool>(enabledRef).value_or(false);
				if (ImGui::MenuItem("Enabled", nullptr, enabled)) m_session.ApplyFieldValue(enabledRef, !enabled);
				if (ImGui::BeginMenu("Shoulder")) {
					m_session.properties.DrawShoulderMenu(a_element.object);
					ImGui::EndMenu();
				}
				if (const auto chain = m_project->GetIKChain(a_element.object)) {
					ImGui::Separator();
					if (ImGui::MenuItem("Select Effector")) m_session.Select({ chain->effector, ProjectEx::kSelf }, SelectMode::kReplace);
					if (ImGui::MenuItem("Select Pole")) m_session.Select({ chain->pole, ProjectEx::kSelf }, SelectMode::kReplace);
				}
				remove();
				break;
			}
		case ProjectEx::ObjectType::kHand:
			{
				if (ImGui::MenuItem("Rename...", renameShortcut.c_str())) m_session.RequestPopup({ Popup::kRename, a_element.object });
				const ProjectEx::PropertyRef enabledRef{ a_element.object, ProjectEx::kSelf, ProjectEx::PropertyId::kEnabled };
				const bool enabled = m_project->GetValue<bool>(enabledRef).value_or(false);
				if (ImGui::MenuItem("Enabled", nullptr, enabled)) m_session.ApplyFieldValue(enabledRef, !enabled);
				ImGui::Separator();
				if (ImGui::BeginMenu("Presets")) {
					m_session.properties.DrawHandPresetMenu(a_element.object);
					ImGui::EndMenu();
				}
				// Other hands on the same actor with as many fingers, such as the opposite hand.
				const auto hand = m_project->GetHand(a_element.object);
				std::vector<ProjectEx::ObjectId> targets;
				for (const auto id : hand ? m_project->GetModifiers(hand->actor) : std::span<const ProjectEx::ObjectId>{}) {
					const auto other = m_project->GetHand(id);
					if (id != a_element.object && other && other->settings.fingers.size() == hand->settings.fingers.size()) targets.push_back(id);
				}
				if (ImGui::BeginMenu("Copy Pose To", !targets.empty())) {
					for (const auto target : targets) {
						ImGui::PushID(static_cast<int>(target));
						if (ImGui::MenuItem(m_session.GetElementLabel({ target }).c_str())) {
							const auto source = a_element.object;
							m_project->BeginEdit("Copy finger pose");
							m_session.ApplyHandPose(target, [&](size_t a_finger) {
								const auto finger = static_cast<uint16_t>(a_finger);
								return std::pair{ m_project->GetValue<float>({ source, finger, ProjectEx::PropertyId::kCurl }).value_or(0.0f),
									m_project->GetValue<float>({ source, finger, ProjectEx::PropertyId::kSpread }).value_or(0.0f) };
							}, "Copy finger pose");
							// ApplyHandPose clears the target's Fist and Spread; restore the copied ones.
							for (const auto property : { ProjectEx::PropertyId::kCurl, ProjectEx::PropertyId::kSpread }) {
								const ProjectEx::PropertyRef ref{ target, ProjectEx::kSelf, property };
								if (const auto value = m_project->GetValue<float>({ source, ProjectEx::kSelf, property })) {
									m_project->SetValue(ref, *value);
									m_session.AutoKey(std::span{ &ref, 1 });
								}
							}
							m_project->EndEdit();
						}
						ImGui::PopID();
					}
					ImGui::EndMenu();
				}
				remove();
				break;
			}
		case ProjectEx::ObjectType::kSplineIK:
			{
				if (ImGui::MenuItem("Rename...", renameShortcut.c_str())) m_session.RequestPopup({ Popup::kRename, a_element.object });
				const ProjectEx::PropertyRef enabledRef{ a_element.object, ProjectEx::kSelf, ProjectEx::PropertyId::kEnabled };
				const bool enabled = m_project->GetValue<bool>(enabledRef).value_or(false);
				if (ImGui::MenuItem("Enabled", nullptr, enabled)) m_session.ApplyFieldValue(enabledRef, !enabled);
				if (const auto spline = m_project->GetSplineIK(a_element.object)) {
					ImGui::Separator();
					if (ImGui::MenuItem("Select Controls")) {
						m_session.selection.clear();
						for (const auto control : spline->controls) m_session.selection.push_back({ control, ProjectEx::kSelf });
					}
				}
				remove();
				break;
			}
		case ProjectEx::ObjectType::kLocator:
			if (ImGui::MenuItem("Rename...", renameShortcut.c_str())) m_session.RequestPopup({ Popup::kRename, a_element.object });
			if (ImGui::BeginMenu("Reparent")) {
				DrawParentMenu(a_element.object);
				ImGui::EndMenu();
			}
			ImGui::Separator();
			m_session.ActionMenuItem(Action::kInsertKey);
			if (ImGui::MenuItem("Reset Transform")) m_session.ResetTransforms(m_session.selection);
			if (ImGui::MenuItem("Set Current as Default")) m_session.SetLocatorDefaults(m_session.selection);
			remove();
			break;
		}
	}

	void Outliner::DrawParentMenu(ProjectEx::ObjectId a_locator)
	{
		const auto current = m_project->GetLocatorParent(a_locator).value_or(ProjectEx::ParentRef{});
		const auto apply = [&](ProjectEx::ParentRef a_parent) {
			if (const auto result = m_project->SetLocatorParent(a_locator, a_parent, true); !result) m_session.SetStatus(result.error());
		};

		if (ImGui::MenuItem("Scene", nullptr, current.object == ProjectEx::kNoObject)) apply({});
		ImGui::Separator();
		for (const auto id : m_project->GetObjects()) {
			const auto type = m_project->GetObjectType(id);
			if (type == ProjectEx::ObjectType::kActor) {
				ImGui::PushID(static_cast<int>(id));
				if (ImGui::BeginMenu(m_session.GetElementLabel({ id }).c_str())) {
					if (ImGui::MenuItem("Root", nullptr, current == ProjectEx::ParentRef{ id, -1 })) apply({ id, -1 });
					ImGui::Separator();
					if (const auto skeleton = m_project->GetSkeleton(id)) {
						const auto names = skeleton->joint_names();
						for (int32_t bone = 0; bone < skeleton->num_joints(); ++bone) {
							ImGui::PushID(bone);
							if (ImGui::MenuItem(names[bone], nullptr, current == ProjectEx::ParentRef{ id, bone })) apply({ id, bone });
							ImGui::PopID();
						}
					}
					ImGui::EndMenu();
				}
				ImGui::PopID();
			} else if (type == ProjectEx::ObjectType::kLocator && id != a_locator) {
				ImGui::PushID(static_cast<int>(id));
				if (ImGui::MenuItem(m_session.GetElementLabel({ id }).c_str(), nullptr, current.object == id)) apply({ id, -1 });
				ImGui::PopID();
			}
		}
	}

	void Outliner::DrawBoneDisplayMenu()
	{
		const auto bones = m_session.GetSelectedBones(false);
		const auto hierarchy = m_session.GetSelectedBones(true);
		if (ImGui::MenuItem("Hide")) m_session.SetBoneDisplay(bones, false, std::nullopt);
		if (ImGui::MenuItem("Show")) m_session.SetBoneDisplay(bones, true, std::nullopt);
		if (ImGui::MenuItem("Hide Hierarchy")) m_session.SetBoneDisplay(hierarchy, false, std::nullopt);
		if (ImGui::MenuItem("Show Hierarchy")) m_session.SetBoneDisplay(hierarchy, true, std::nullopt);
		ImGui::Separator();

		float scale = bones.empty() ? 1.0f : m_session.GetBoneDisplay(bones.back()).scale;
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::SliderFloat("Size", &scale, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) m_session.SetBoneDisplay(bones, std::nullopt, scale);
		if (ImGui::MenuItem("Apply Size to Hierarchy")) m_session.SetBoneDisplay(hierarchy, std::nullopt, scale);
		if (ImGui::MenuItem("Reset Size")) m_session.SetBoneDisplay(bones, std::nullopt, 1.0f);
	}
}
