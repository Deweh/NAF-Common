#include "UI/ImguiMenu.h"
#include "UI/StudioMenuEx.h"

#include "RE/CommonMisc.h"
#include "Util/String.h"
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
		ImGui::GetIO().MouseDrawCursor = m_isOpen;
		if(m_isOpen){
			::SetCursor(nullptr);
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

		auto studioMenu = StudioMenuEx::GetSingleton();
		if (studioMenu->HasOpenProject()) {
			studioMenu->Draw(m_renderer);
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(1100.0f, 760.0f), ImGuiCond_FirstUseEver);
		const bool wasOpen = m_isOpen;
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
		struct ProjectEntry { std::string name; std::filesystem::path path; };
		static std::vector<ProjectEntry> existingProjects;
		static std::filesystem::path selectedProject;
		static bool creatingNewProject = false;
		static char newProjectName[128]{};
		static std::string status;
		static bool refreshProjects = true;
		const auto projectsPath = Util::String::GetDataPath() / "Projects";
		if (refreshProjects) {
			existingProjects.clear();
			std::error_code ec;
			std::filesystem::create_directories(projectsPath, ec);
			if (!ec) {
				for (const auto& entry : std::filesystem::directory_iterator(projectsPath, ec)) {
					if (entry.is_regular_file() && Util::String::CaseInsensitiveCompare(entry.path().extension().string(), ".nafproject")) {
						existingProjects.push_back({ entry.path().stem().string(), entry.path() });
					}
				}
				std::ranges::sort(existingProjects, [](const auto& a, const auto& b) { return Util::String::CaseInsensitiveLess{}(a.name, b.name); });
			}
			status = ec ? "Could not enumerate projects: " + ec.message() : "";
			refreshProjects = false;
		}

		ImGui::TextUnformatted("Animation Studio Setup");
		ImGui::Separator();

		ImGui::BeginChild("##studio_setup", ImVec2(0.0f, 0.0f), false);
		ImGui::TextWrapped("Select an existing animation project or create a new one.");
		ImGui::Spacing();

		if (!existingProjects.empty()) {
			ImGui::TextUnformatted("Existing Projects");
			ImGui::BeginChild("##project_list", ImVec2(0.0f, 180.0f), true);
			for (const auto& project : existingProjects) {
				const bool isSelected = selectedProject == project.path && !creatingNewProject;
				if (ImGui::Selectable(project.name.c_str(), isSelected)) {
					selectedProject = project.path;
					creatingNewProject = false;
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

			const std::string projectName = newProjectName;
			const bool hasProjectName = !projectName.empty() && projectName != "." && projectName != ".." && projectName.find_first_of("<>:\"/\\|?*") == std::string::npos && static_cast<unsigned char>(projectName.back()) > 0x20 && projectName.back() != '.';
			const bool nameTaken = hasProjectName && std::ranges::any_of(existingProjects, [&](const auto& a_project) { return Util::String::CaseInsensitiveCompare(a_project.name, projectName); });
			if (!hasProjectName || nameTaken) {
				ImGui::BeginDisabled();
			}

			if (ImGui::Button("Create")) {
				creatingNewProject = false;
				StudioMenuEx::GetSingleton()->NewProject(projectName);
				newProjectName[0] = '\0';
				refreshProjects = true;
				ImGui::EndChild();
				return;
			}

			if (!hasProjectName || nameTaken) {
				ImGui::EndDisabled();
			}
			if (nameTaken) {
				ImGui::SameLine();
				ImGui::TextDisabled("A project with this name already exists.");
			}
		}

		if (!selectedProject.empty()) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Selected Project: %s", selectedProject.stem().string().c_str());
			if (ImGui::Button("Open Project")) {
				const auto result = StudioMenuEx::GetSingleton()->OpenProject(selectedProject);
				status = result ? "" : result.error();
				if (result) {
					selectedProject.clear();
					refreshProjects = true;
					ImGui::EndChild();
					return;
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Refresh")) refreshProjects = true;
		if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());

		ImGui::EndChild();
	}
}
