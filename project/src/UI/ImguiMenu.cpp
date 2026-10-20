#include "UI/ImguiMenu.h"
#include "UI/StudioMenuEx.h"

#include "Morph/Manager.h"
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

	void ImguiMenu::Install(Renderer* a_renderer)
	{
		GetSingleton()->Attach(a_renderer);
	}

	void ImguiMenu::Attach(Renderer* a_renderer)
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
			if (ImGui::BeginTabItem("Settings")) {
				DrawSettingsTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void ImguiMenu::OnFrameThunk(Renderer* a_renderer)
	{
		a_renderer;
		GetSingleton()->Draw();
	}

	void ImguiMenu::DrawSettingsTab()
	{
		auto manager = Morph::Manager::GetSingleton();
		const auto hint = [](const char* a_text) {
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("%s", a_text);
		};

		ImGui::SeparatorText("Body morphs");
		bool correctives = manager->GetCorrectivesEnabled();
		if (ImGui::Checkbox("Corrective morphs", &correctives))
			manager->SetCorrectivesEnabled(correctives);
		hint("Pose-space corrective morphs (Data/NAF/Correctives). Other morphs keep applying.");

		ImGui::SeparatorText("Corrective smoothing");
		auto& smoothing = manager->GetSmoothing();
		bool smooth = smoothing.GetEnabled();
		if (ImGui::Checkbox("Corrective smoothing", &smooth))
			smoothing.SetEnabled(smooth);
		hint("Smooths visible actors' skinned, morphable meshes on the GPU, as Blender's Corrective Smooth\n(Length Weight, factor 1) does. Every other morph goes through it.");

		static constexpr const char* kRegions[]{ "Hips and thighs", "Whole body", "Custom (Data/NAF/Smoothing.json)" };
		int region = static_cast<int>(smoothing.GetRegion());
		if (ImGui::Combo("Region", &region, kRegions, IM_ARRAYSIZE(kRegions)))
			smoothing.SetRegion(static_cast<Morph::SmoothRegion>(region));
		hint("Where smoothing applies, by bone; it fades out as smoothly as the smoothing spreads.\nHips and thighs: pelvis, lower spine, buttocks and upper thighs, not the knees. Smoothing joints\nbent far from the rest pose (the shoulders, with the arms down) rounds them off too much.\nCustom: {\"bones\": {\"Pelvis_skin\": 1.0, ...}} in Data/NAF/Smoothing.json, re-read on selecting it\nand by naf morph reload.");

		bool autoSmooth = smoothing.GetAutoSmooth();
		if (ImGui::Checkbox("Auto-smooth creases", &autoSmooth))
			smoothing.SetAutoSmooth(autoSmooth);
		hint("Within the region, smooths out crumpled creases afterwards: where the skin is squashed and\nsharper than at rest, and a few rings around. Detail the body has at rest is left alone.");
		int autoPasses = static_cast<int>(smoothing.GetAutoSmoothPasses());
		if (!autoSmooth)
			ImGui::BeginDisabled();
		if (ImGui::SliderInt("Auto-smooth passes", &autoPasses, 1, static_cast<int>(Morph::SmoothLibrary::kMaxAutoSmoothPasses), "%d", ImGuiSliderFlags_AlwaysClamp))
			smoothing.SetAutoSmoothPasses(static_cast<uint32_t>(autoPasses));
		if (!autoSmooth)
			ImGui::EndDisabled();
		hint("More smooths creases further (default 60).");

		bool stretchDetail = smoothing.GetStretchDetail();
		if (ImGui::Checkbox("Stretch detail", &stretchDetail))
			smoothing.SetStretchDetail(stretchDetail);
		hint("Within the region, the rest detail grows with the skin around it as it stretches, and shrinks\nwhere it's squashed, so stretched skin keeps its shape and creases soften.");

		bool volume = smoothing.GetVolumePreservation();
		if (ImGui::Checkbox("Preserve volume", &volume))
			smoothing.SetVolumePreservation(volume);
		hint("Within the region, keeps smoothing from flattening the buttocks: each buttock's lost volume\nis pushed back out (not into squashed folds).");
		float restore = smoothing.GetVolumeRestore() * 100.0f;
		if (!volume)
			ImGui::BeginDisabled();
		if (ImGui::SliderFloat("Volume restored", &restore, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
			smoothing.SetVolumeRestore(restore / 100.0f);
		if (!volume)
			ImGui::EndDisabled();
		hint("How much of the buttocks' lost volume is put back (default 100%). Lower it if they look\ninflated with the legs back.");

		int passes = static_cast<int>(smoothing.GetRuntimePasses());
		if (ImGui::SliderInt("Runtime passes", &passes, 0, static_cast<int>(Morph::SmoothLibrary::kMaxRuntimePasses), "%d", ImGuiSliderFlags_AlwaysClamp))
			smoothing.SetRuntimePasses(static_cast<uint32_t>(passes));
		hint("Passes run every update on top of the precomputed ones, with the current edge lengths, as Blender\ndoes. More is closer to Blender and costs more GPU time; 0 uses only the precomputed passes.");

		auto settings = smoothing.GetSettings();
		int repeat = static_cast<int>(settings.repeat);
		if (ImGui::InputInt("Precomputed passes", &repeat, 50, 100, ImGuiInputTextFlags_EnterReturnsTrue)) {
			settings.repeat = static_cast<uint32_t>(std::clamp(repeat, static_cast<int>(Morph::SmoothLibrary::kMinRepeat), static_cast<int>(Morph::SmoothLibrary::kMaxRepeat)));
			smoothing.SetSettings(settings);
		}
		hint("Passes precomputed with rest edge lengths: Blender's Repeat (the runtime passes come on top).\nApplied with Enter. Changing it rebuilds every mesh's smoothing data (about a\nsecond each, in the background); meshes keep plain morphs meanwhile.");

		bool pin = settings.pinBoundaries;
		if (ImGui::Checkbox("Pin open edges", &pin)) {
			settings.pinBoundaries = pin;
			smoothing.SetSettings(settings);
		}
		hint("Keeps a mesh's open edges (a body's neck, where the head mesh meets it) where skinning puts them,\nso no gap opens there. Changing it rebuilds every mesh's smoothing data.");

		ImGui::Spacing();
		ImGui::TextDisabled("Not saved: these reset when the game restarts.");
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
