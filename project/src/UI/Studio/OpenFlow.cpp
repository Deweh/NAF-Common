#include "UI/StudioMenuEx.h"

#include "Animation/Studio/StudioMath.h"
#include "UI/Studio/StudioSession.h"
#include "imgui/ImGuizmo.h"

// The steps between reading a saved project and editing it: binding its actor slots, then placing its scene center.
namespace UI
{
	using namespace Studio;
	using Animation::Studio::GetTranslation;

	void StudioMenuEx::DrawBindingDialog()
	{
		auto& pending = *m_pendingOpen;
		const auto slots = pending.file.GetActorSlots();
		bool open = false;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
		ImGui::SetNextWindowSizeConstraints({ 520.0f, 0.0f }, { FLT_MAX, FLT_MAX });
		const auto title = std::format("Open {}###StudioExBindActors", pending.path.stem().string());
		if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("Choose an actor for each actor in this project.");
			ImGui::TextDisabled("Only loaded actors that use the same skeleton are listed.");
			ImGui::Spacing();

			if (ImGui::BeginTable("##slots", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Project Actor", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("Bind To", ImGuiTableColumnFlags_WidthFixed, 300.0f);
				ImGui::TableHeadersRow();
				const auto consoleActor = GetConsoleSelectedActor();
				for (size_t i = 0; i < slots.size(); ++i) {
					const auto& slot = slots[i];
					auto& bound = pending.actors[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(slot.name.c_str());
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skeleton: %s", slot.skeletonId.c_str());
					if (!slot.bindingHintName.empty()) ImGui::TextDisabled("Last bound to %s", slot.bindingHintName.c_str());

					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-FLT_MIN);
					const auto preview = bound ? GetActorLabel(bound.get()) : std::string("Choose an actor...");
					if (ImGui::BeginCombo("##actor", preview.c_str())) {
						std::vector<RE::Actor*> candidates;
						const auto addCandidate = [&](RE::Actor* a_actor) {
							if (a_actor && !std::ranges::contains(candidates, a_actor) && ProjectEx::CheckActorCompatibility(slot.skeletonId, slot.gender, a_actor)) candidates.push_back(a_actor);
						};
						addCandidate(slot.bindingHint != 0 ? RE::TESForm::GetFormByID<RE::Actor>(slot.bindingHint) : nullptr);
						addCandidate(consoleActor);
						for (const auto actor : GetNearbyActors()) addCandidate(actor);

						if (candidates.empty()) ImGui::TextDisabled("No loaded actor uses this skeleton.");
						for (const auto actor : candidates) {
							const bool usedElsewhere = std::ranges::any_of(pending.actors, [&](const auto& a_other) { return a_other.get() == actor && &a_other != &bound; });
							auto label = GetActorLabel(actor);
							if (actor->GetFormID() == slot.bindingHint) label += "  (last bound)";
							if (actor == consoleActor) label += "  (console selection)";
							if (ImGui::Selectable(label.c_str(), bound.get() == actor, usedElsewhere ? ImGuiSelectableFlags_Disabled : 0)) {
								bound.reset(actor);
								pending.error.clear();
							}
						}
						ImGui::EndCombo();
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			if (!pending.error.empty()) {
				ImGui::Spacing();
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 500.0f);
				ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", pending.error.c_str());
				ImGui::PopTextWrapPos();
			}

			ImGui::Spacing();
			const bool complete = std::ranges::all_of(pending.actors, [](const auto& a_actor) { return a_actor != nullptr; });
			ImGui::BeginDisabled(!complete);
			open = ImGui::Button("Open", { 120.0f, 0.0f });
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", { 120.0f, 0.0f })) m_openCancelled = true;
		}
		ImGui::End();

		// Opening replaces the pending state this function was drawing.
		if (open) FinishOpen();
	}

	void StudioMenuEx::FinishOpen()
	{
		std::vector<RE::Actor*> actors;
		ScenePlacementStep placement;
		for (const auto& actor : m_pendingOpen->actors) {
			actors.push_back(actor.get());
			placement.originals.emplace_back(actor, ProjectEx::GetWorldPlacement(actor.get()));
		}
		auto result = Serialization::StudioProjectImport::Build(m_pendingOpen->file, actors);
		if (!result) {
			m_pendingOpen->error = std::move(result.error());
			return;
		}

		// The scene starts centered on the first actor, or in front of the camera when there are none.
		auto center = actors.empty() ? GetPlacementInFrontOfCamera(kDefaultSceneCenterDistance) : ProjectEx::GetWorldPlacement(actors.front());
		if (center) result->project->SetSceneCenter(*center);

		auto pending = std::move(*m_pendingOpen);
		m_pendingOpen.reset();
		auto warnings = std::move(result->warnings);
		StartSession(std::move(result->project), std::move(pending.path));
		m_session->ReadEditorJson(pending.file.GetEditorJson(), warnings);
		m_placingScene = std::move(placement);
		if (!warnings.empty()) m_session->popups.ShowWarnings(Popup::kLoadWarnings, std::move(warnings));
	}

	void StudioMenuEx::DrawScenePlacement(DX11Renderer* a_renderer)
	{
		bool place = false;
		bool cancel = false;
		auto& project = m_session->GetProject();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos({ viewport->GetCenter().x, viewport->WorkPos.y + 40.0f }, ImGuiCond_Appearing, { 0.5f, 0.0f });
		if (ImGui::Begin("Place Scene###StudioExPlaceScene", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Place %s in the world.", m_session->name.c_str());
			ImGui::TextDisabled("Drag the gizmo to move and turn the scene. Its actors follow.");
			ImGui::TextDisabled("Hold the right mouse button to look around, and use WASD / Q / E to move.");
			ImGui::Spacing();
			place = ImGui::Button("Place", { 120.0f, 0.0f });
			ImGui::SameLine();
			cancel = ImGui::Button("Cancel", { 120.0f, 0.0f });
		}
		ImGui::End();

		ozz::math::Float4x4 view;
		ozz::math::Float4x4 projection;
		bool orthographic = false;
		if (a_renderer && a_renderer->GetGizmoCameraMatrices(view, projection, orthographic)) {
			DrawSceneCenter(a_renderer, project.GetSceneCenter());
			// Where each actor is headed; the actors themselves catch up a frame later.
			const auto center = ProjectEx::ToMatrix(project.GetSceneCenter());
			for (const auto id : project.GetObjects()) {
				const auto placement = project.GetActorPlacement(id);
				if (!placement) continue;
				const auto world = center * ProjectEx::ToMatrix(*placement);
				a_renderer->DrawWorldLine(ToNiPoint(GetTranslation(center)), ToNiPoint(GetTranslation(world)), kColorSceneCenter);
				a_renderer->DrawWorldDiamond(ToNiPoint(GetTranslation(world)), 6.0f, kColorSceneCenter, 2.0f);
			}

			const auto& io = ImGui::GetIO();
			auto matrix = center;
			ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
			ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
			ImGuizmo::SetOrthographic(orthographic);
			ImGuizmo::SetGizmoSizeClipSpace(0.1f);
			if (ImGuizmo::Manipulate(reinterpret_cast<const float*>(&view), reinterpret_cast<const float*>(&projection), ImGuizmo::TRANSLATE | ImGuizmo::ROTATE_Z, ImGuizmo::LOCAL, reinterpret_cast<float*>(&matrix))) {
				project.SetSceneCenter(ProjectEx::ToPlacement(matrix));
			}
		}

		if (place) {
			m_placingScene.reset();
			m_session->SetStatus(std::format("Opened {}.", m_session->name));
		} else if (cancel) {
			// Put the actors back where they were.
			auto originals = std::move(m_placingScene->originals);
			CloseProject();
			for (const auto& [actor, placement] : originals) ProjectEx::HoldActor(actor.get(), placement);
		}
	}
}
