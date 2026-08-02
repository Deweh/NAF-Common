#include "UI/Studio/Dialogs/RigDialogs.h"

#include "UI/Studio/StudioSession.h"
#include "Serialization/StudioProjectExport.h"
#include "Util/String.h"

#include <nlohmann/json.hpp>

namespace UI::Studio
{
	RigDialogs::RigDialogs(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	namespace
	{
		std::filesystem::path GetRigDirectory()
		{
			return Util::String::GetDataPath() / "Rigs";
		}

		std::string DescribeRig(const Serialization::StudioProjectImport::RigSummary& a_summary)
		{
			return std::format("{} IK chain(s), {} finger control(s), {} spline IK, {} posed bone(s), {} mask override(s)", a_summary.ikChains, a_summary.hands, a_summary.splineIKs,
				a_summary.posedBones, a_summary.maskOverrides);
		}
	}

	void RigDialogs::OpenSave(ProjectEx::ObjectId a_actor)
	{
		m_actor = m_project->GetBoundActor(a_actor) ? a_actor : m_session.GetDefaultActor();
		m_error.clear();
		m_name = SanitizeFileName(std::string{ m_project->GetObjectName(m_actor) });
	}

	void RigDialogs::OpenApply(ProjectEx::ObjectId a_actor)
	{
		m_actor = m_project->GetBoundActor(a_actor) ? a_actor : m_session.GetDefaultActor();
		m_error.clear();
		// Key Pose follows Auto Key each time the dialog opens.
		m_keyPose = m_session.autoKey;
		Scan();
	}

	void RigDialogs::Draw()
	{
		DrawSave();
		DrawApply();
	}

	std::filesystem::path RigDialogs::GetSavePath() const
	{
		return GetRigDirectory() / Utf8ToPath(SanitizeFileName(m_name) + ".nafrig");
	}

	void RigDialogs::Scan()
	{
		m_entries.clear();
		m_index = -1;
		std::error_code ec;
		for (const auto& file : std::filesystem::directory_iterator(GetRigDirectory(), ec)) {
			if (!file.is_regular_file(ec) || Util::String::ToLower(PathToUtf8(file.path().extension())) != ".nafrig") continue;
			m_entries.push_back({ PathToUtf8(file.path().stem()), file.path(), Serialization::StudioProjectImport::ReadRig(file.path()) });
		}
		std::ranges::sort(m_entries, [](const Entry& a_left, const Entry& a_right) { return Util::String::ToLower(a_left.name) < Util::String::ToLower(a_right.name); });
	}

	void RigDialogs::DrawSave()
	{
		if (!ImGui::BeginPopupModal(Popups::GetName(Popup::kSaveRig), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
		const float width = ImGui::GetFontSize() * 24.0f;

		DrawBoundActorCombo(*m_project, m_actor, width);
		const auto modifiers = m_project->GetModifiers(m_actor);
		const auto count = [&](ProjectEx::ObjectType a_type) { return std::ranges::count(modifiers, std::optional{ a_type }, [&](auto a_id) { return m_project->GetObjectType(a_id); }); };
		ImGui::TextDisabled("%zd IK chain(s), %zd finger control(s), %zd spline IK", count(ProjectEx::ObjectType::kIKChain), count(ProjectEx::ObjectType::kHand),
			count(ProjectEx::ObjectType::kSplineIK));
		ImGui::TextDisabled("Also saves the pose, the bone mask and the bone display.");
		ImGui::Spacing();

		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(width);
		const bool entered = InputTextString("Name", nullptr, m_name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		const bool hasName = m_name.find_first_not_of(" \t") != std::string::npos;
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
		if (hasName) {
			const auto path = GetSavePath();
			ImGui::TextDisabled("%s", GetDisplayPath(path).c_str());
			std::error_code ec;
			if (std::filesystem::exists(path, ec)) ImGui::TextColored(kTextWarning, "A rig with this name exists and will be replaced.");
		}
		if (!m_error.empty()) ImGui::TextColored(kTextError, "%s", m_error.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();

		const bool canSave = hasName && m_project->GetBoundActor(m_actor);
		ImGui::BeginDisabled(!canSave);
		const bool confirmed = ImGui::Button("Save", { 120.0f, 0.0f }) || (entered && canSave);
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", { 120.0f, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
		else if (confirmed) Save();
		ImGui::EndPopup();
	}

	void RigDialogs::Save()
	{
		const auto path = GetSavePath();
		if (auto saved = Serialization::StudioProjectExport::SaveRig(path, *m_project, m_actor, WriteEditorJson(m_actor)); !saved) {
			m_error = std::move(saved.error());
			return;
		}
		m_session.SetStatus(std::format("Saved rig {}", PathToUtf8(path.stem())));
		ImGui::CloseCurrentPopup();
	}

	void RigDialogs::DrawApply()
	{
		if (!ImGui::BeginPopupModal(Popups::GetName(Popup::kApplyRig), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
		const float width = ImGui::GetFontSize() * 24.0f;

		DrawBoundActorCombo(*m_project, m_actor, width);
		const auto skeletonId = m_project->GetSkeletonId(m_actor);
		const auto usable = [&](const Entry& a_entry) { return a_entry.rig && Util::String::CaseInsensitiveCompare(a_entry.rig->GetSkeletonId(), skeletonId); };
		if (m_index >= 0 && (m_index >= static_cast<int32_t>(m_entries.size()) || !usable(m_entries[m_index]))) m_index = -1;

		// Rigs for other skeletons, and files that can't be read, are listed but can't be picked.
		if (ImGui::BeginListBox("Rig", { width, ImGui::GetTextLineHeightWithSpacing() * 8.0f })) {
			for (int32_t i = 0; i < static_cast<int32_t>(m_entries.size()); ++i) {
				const auto& entry = m_entries[i];
				ImGui::PushID(i);
				ImGui::BeginDisabled(!usable(entry));
				if (ImGui::Selectable(entry.name.c_str(), i == m_index)) m_index = i;
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					if (!entry.rig) ImGui::SetTooltip("%s", entry.rig.error().c_str());
					else if (!usable(entry)) ImGui::SetTooltip("For skeleton \"%s\".", entry.rig->GetSkeletonId().c_str());
				}
				ImGui::PopID();
			}
			if (m_entries.empty()) ImGui::TextDisabled("No rigs saved yet.");
			ImGui::EndListBox();
		}
		if (ImGui::Button("Refresh")) Scan();

		const auto rig = m_index >= 0 ? &*m_entries[m_index].rig : nullptr;
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
		if (rig) ImGui::TextDisabled("%s", DescribeRig(rig->GetSummary()).c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();

		ImGui::Checkbox("Pose", &m_options.pose);
		ImGui::Indent();
		ImGui::BeginDisabled(!m_options.pose);
		ImGui::Checkbox("Key Pose", &m_keyPose);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Key the bones the pose changes at the current frame.\nOtherwise they're unkeyed edits, which a frame change overwrites on tracked bones.");
		ImGui::EndDisabled();
		ImGui::Unindent();
		ImGui::Checkbox("IK Chains", &m_options.ikChains);
		ImGui::Checkbox("Finger Controls", &m_options.hands);
		ImGui::Checkbox("Spline IK", &m_options.splineIKs);
		ImGui::Checkbox("Replace Existing", &m_options.replaceModifiers);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove the actor's own modifiers of the kinds checked above first.");
		ImGui::Checkbox("Bone Mask", &m_options.boneMask);
		ImGui::Checkbox("Bone Display", &m_applyBoneDisplay);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which bones are shown in the viewport, and their size. Not part of undo.");

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
		if (!m_error.empty()) ImGui::TextColored(kTextError, "%s", m_error.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();

		ImGui::BeginDisabled(!rig || !m_project->GetBoundActor(m_actor));
		const bool confirmed = ImGui::Button("Apply", { 120.0f, 0.0f });
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", { 120.0f, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
		else if (confirmed) Apply();
		ImGui::EndPopup();
	}

	void RigDialogs::Apply()
	{
		const auto& entry = m_entries[m_index];
		// Keying the pose is a UI preference, so it's done here, in the same undo step.
		m_project->BeginEdit("Apply rig");
		auto result = Serialization::StudioProjectImport::ApplyRig(*entry.rig, *m_project, m_actor, m_options);
		if (result && m_options.pose && m_keyPose && !result->poseChanges.empty()) m_project->KeyCurrentValue(result->poseChanges, m_session.defaultInterpolation);
		m_project->EndEdit();
		if (!result) {
			m_error = std::move(result.error());
			return;
		}

		auto warnings = std::move(result->warnings);
		if (m_applyBoneDisplay) ReadEditorJson(entry.rig->GetEditorJson(), m_actor, warnings);
		m_session.SetStatus(std::format("Applied rig {} to {}", entry.name, m_project->GetObjectName(m_actor)));
		ImGui::CloseCurrentPopup();
		if (!warnings.empty()) {
			m_session.popups.ShowWarnings(Popup::kRigWarnings, std::move(warnings));
		}
	}

	std::string RigDialogs::WriteEditorJson(ProjectEx::ObjectId a_actor) const
	{
		using json = nlohmann::json;
		const auto skeleton = m_project->GetSkeleton(a_actor);
		auto boneDisplay = json::array();
		for (const auto& [bone, display] : m_session.boneDisplay) {
			if (bone.object != a_actor || !skeleton || bone.element >= skeleton->num_joints()) continue;
			boneDisplay.push_back({ { "bone", skeleton->joint_names()[bone.element] }, { "visible", display.visible }, { "scale", display.scale } });
		}
		return json{ { "boneDisplay", std::move(boneDisplay) } }.dump();
	}

	void RigDialogs::ReadEditorJson(std::string_view a_json, ProjectEx::ObjectId a_actor, std::vector<std::string>& a_warnings)
	{
		const auto skeleton = m_project->GetSkeleton(a_actor);
		if (a_json.empty() || !skeleton) return;
		using json = nlohmann::json;
		try {
			const auto editor = json::parse(a_json);
			const auto boneDisplay = editor.find("boneDisplay");
			if (boneDisplay == editor.end() || !boneDisplay->is_array()) return;
			std::unordered_map<std::string_view, uint16_t> bones;
			for (int32_t i = 0; i < skeleton->num_joints(); ++i) bones.emplace(skeleton->joint_names()[i], static_cast<uint16_t>(i));

			// The rig's display replaces the actor's, like the rest of the rig.
			m_session.ResetBoneDisplay(a_actor);
			for (const auto& item : *boneDisplay) {
				const auto bone = bones.find(item.at("bone").get<std::string>());
				if (bone == bones.end()) continue;
				const float scale = item.value("scale", 1.0f);
				const BoneDisplay display{ item.value("visible", true), std::isfinite(scale) ? std::clamp(scale, 0.1f, 4.0f) : 1.0f };
				if (!display.visible || display.scale != 1.0f) m_session.boneDisplay[{ a_actor, bone->second }] = display;
			}
		} catch (const std::exception&) {
			a_warnings.push_back("The rig's bone display could not be read.");
		}
	}
}
