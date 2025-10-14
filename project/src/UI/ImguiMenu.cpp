#include "UI/ImguiMenu.h"

#include "RE/CommonMisc.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_internal.h"
#undef max

namespace UI
{
	ImguiMenu* ImguiMenu::GetSingleton()
	{
		static ImguiMenu singleton;
		return &singleton;
	}

	void ImguiMenu::Install(DX11Renderer* a_renderer)
	{
		GetSingleton()->Attach(a_renderer);
	}

	void ImguiMenu::Attach(DX11Renderer* a_renderer)
	{
		m_renderer = a_renderer;
		a_renderer->SetDrawCallback(&ImguiMenu::OnFrameThunk);
	}

	void ImguiMenu::SetOpen(bool a_open)
	{
		if (m_isOpen == a_open) {
			return;
		}

		m_isOpen = a_open;
        if(m_renderer) {
            m_renderer->SetMouseReleased(m_isOpen);
        }
	}

	bool ImguiMenu::IsOpen() const
	{
		return m_isOpen;
	}

	void ImguiMenu::Draw()
	{
		if (!m_renderer) {
			return;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
			SetOpen(!IsOpen());
		}

		if (!m_isOpen) {
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(1100.0f, 760.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("NAF Menu", &m_isOpen, ImGuiWindowFlags_MenuBar)) {
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("##naf_tabs")) {
            if (ImGui::BeginTabItem("Inspector")) {
				ImGui::TextUnformatted("Example inspector content goes here.");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Animation Studio")) {
				DrawStudioTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void ImguiMenu::OnFrameThunk(DX11Renderer* a_renderer)
	{
		a_renderer;
		GetSingleton()->Draw();
	}

	void ImguiMenu::DrawStudioTab()
	{
		static std::vector<std::string> existingProjects{
			"WalkCycle",
			"CombatIdle",
			"DialogueGesture"
		};
		static std::string selectedProject;
		static bool creatingNewProject = false;
		static char newProjectName[128]{};

		ImGui::TextUnformatted("Animation Studio Setup");
		ImGui::Separator();

		ImGui::BeginChild("##studio_setup", ImVec2(0.0f, 0.0f), false);
		ImGui::TextWrapped("Select an existing animation project or create a new one.");
		ImGui::Spacing();

		if (!existingProjects.empty()) {
			ImGui::TextUnformatted("Existing Projects");
			ImGui::BeginChild("##project_list", ImVec2(0.0f, 180.0f), true);
			for (const auto& projectName : existingProjects) {
				const bool isSelected = selectedProject == projectName && !creatingNewProject;
				if (ImGui::Selectable(projectName.c_str(), isSelected)) {
					selectedProject = projectName;
					creatingNewProject = false;
					// TODO: Pass selectedProject to StudioMenu when available.
				}
			}
			ImGui::EndChild();
		} else {
			ImGui::TextDisabled("No animation projects found.");
		}

		ImGui::Spacing();
		if (ImGui::Button(creatingNewProject ? "Cancel New Project" : "New Animation Project")) {
			creatingNewProject = !creatingNewProject;
			if (!creatingNewProject) {
				newProjectName[0] = '\0';
			} else {
				selectedProject.clear();
			}
		}

		if (creatingNewProject) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextUnformatted("Create New Project");
			ImGui::InputText("Project Name", newProjectName, sizeof(newProjectName));

			const bool hasProjectName = newProjectName[0] != '\0';
			if (!hasProjectName) {
				ImGui::BeginDisabled();
			}

			if (ImGui::Button("Create")) {
				selectedProject = newProjectName;
				existingProjects.push_back(selectedProject);
				creatingNewProject = false;
				newProjectName[0] = '\0';
				// TODO: Pass selectedProject to StudioMenu when available.
			}

			if (!hasProjectName) {
				ImGui::EndDisabled();
			}
		}

		if (!selectedProject.empty()) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Selected Project: %s", selectedProject.c_str());
			ImGui::TextDisabled("StudioMenu handoff pending implementation.");
		}

		ImGui::EndChild();
	}
}
