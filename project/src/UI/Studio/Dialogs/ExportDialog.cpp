#include "UI/Studio/Dialogs/ExportDialog.h"

#include "UI/Studio/StudioSession.h"
#include "Serialization/GLTFExport.h"
#include "Settings/Settings.h"
#include "Util/String.h"

#include <nlohmann/json.hpp>

namespace UI::Studio
{
	namespace
	{
		// How long an export may bake each frame. The UI keeps running between slices.
		constexpr std::chrono::milliseconds kBakeBudget{ 8 };
		// Indexed by the GLTFExport level.
		constexpr std::array<const char*, 5> kCompressionNames{ "Lossless", "Level 1", "Level 2", "Level 3", "Level 4" };
	}

	ExportDialog::ExportDialog(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	void ExportDialog::Open()
	{
		m_task.reset();
		m_error.clear();

		m_actor = m_session.GetDefaultActor();

		const auto sequence = GetActiveSequenceInfo(*m_project);
		m_sampleRate = m_lastSampleRate.value_or(sequence ? sequence->frameRate : 30.0f);
		m_path = GetDefaultPath(m_actor);
		m_pathEdited = false;
	}

	void ExportDialog::Draw()
	{
		if (!ImGui::BeginPopupModal(Popups::GetName(Popup::kExportGLB), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
		const float width = ImGui::GetFontSize() * 28.0f;
		const auto close = [&] {
			m_task.reset();
			ImGui::CloseCurrentPopup();
		};

		if (m_task && m_task->write.valid()) {
			// Encoding and writing, on the worker. Short, and can't be cancelled.
			ImGui::TextUnformatted("Writing...");
			ImGui::TextDisabled("%s", PathToUtf8(m_task->path).c_str());
			if (m_task->write.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				if (auto result = m_task->write.get()) {
					m_lastSampleRate = m_sampleRate;
					m_directory = m_task->path.parent_path();
					m_session.SetStatus(std::format("Exported {} frames to {}", m_task->job.sampleCount, PathToUtf8(m_task->path)));
					close();
				} else {
					m_error = std::move(result.error());
					m_task.reset();
				}
			}
		} else if (m_task) {
			auto& job = m_task->job;
			ImGui::ProgressBar(static_cast<float>(job.baked) / static_cast<float>(job.sampleCount), { width, 0.0f }, std::format("Baking {} / {}", job.baked, job.sampleCount).c_str());
			if (ImGui::Button("Cancel", { 120.0f, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				close();
			} else if (const auto done = m_project->ContinueBake(job, kBakeBudget); !done) {
				m_error = done.error();
				m_task.reset();
			} else if (*done) {
				// The writer reads the face morph names, which are loaded on first use; load them on this thread.
				Settings::GetFaceMorphs();
				m_task->write = std::async(std::launch::async, [animation = std::move(job.animation), skeleton = job.skeleton, path = m_task->path, level = m_task->compression]() {
					return Serialization::GLTFExport::ExportToFile(path, animation.get(), skeleton->data.get(), level);
				});
			}
		} else {
			const auto sequence = GetActiveSequenceInfo(*m_project);
			if (sequence) {
				ImGui::Text("Sequence: %.*s", static_cast<int>(sequence->name.size()), sequence->name.data());
				ImGui::TextDisabled("%d frames at %.2f fps (%.2f s)", sequence->frameCount, sequence->frameRate, sequence->frameCount / sequence->frameRate);
			}
			ImGui::Spacing();

			if (DrawBoundActorCombo(*m_project, m_actor, width) && !m_pathEdited) m_path = GetDefaultPath(m_actor);

			ImGui::SetNextItemWidth(width);
			ImGui::DragFloat("Sample Rate", &m_sampleRate, 0.1f, 1.0f, 240.0f, "%.2f fps", ImGuiSliderFlags_AlwaysClamp);
			const size_t samples = sequence ? ProjectEx::GetBakeSampleCount(sequence->frameCount, sequence->frameRate, m_sampleRate) : 0;
			ImGui::TextDisabled("%zu samples, from the first frame to the last.", samples);

			ImGui::SetNextItemWidth(width);
			ImGui::Combo("Compression", &m_compression, kCompressionNames.data(), static_cast<int>(kCompressionNames.size()));
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lossless keeps every sample. Higher levels drop keys that interpolation\nrebuilds within a rising tolerance, for smaller files.");

			ImGui::SetNextItemWidth(width);
			if (InputTextString("File", nullptr, m_path)) m_pathEdited = true;
			const bool hasPath = m_path.find_first_not_of(" \t") != std::string::npos;

			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
			if (hasPath) {
				const auto path = ResolvePath();
				if (PathToUtf8(path) != m_path) ImGui::TextDisabled("%s", PathToUtf8(path).c_str());
				std::error_code ec;
				if (std::filesystem::exists(path, ec)) ImGui::TextColored(kTextWarning, "This file exists and will be replaced.");
			}
			if (const size_t edits = m_project->CountUnkeyedEdits()) {
				ImGui::TextColored(kTextWarning, "%zu unkeyed %s won't be exported. Baking samples the keys, like playback does.", edits, edits == 1 ? "edit" : "edits");
			}
			if (!m_error.empty()) ImGui::TextColored(kTextError, "%s", m_error.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();

			ImGui::BeginDisabled(!m_project->GetBoundActor(m_actor) || samples == 0 || !hasPath);
			const bool confirmed = ImGui::Button("Export", { 120.0f, 0.0f });
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", { 120.0f, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close();
			else if (confirmed) Start();
		}
		ImGui::EndPopup();
	}

	void ExportDialog::Start()
	{
		m_error.clear();
		auto job = m_project->BeginBake(m_actor, m_sampleRate);
		if (!job) {
			m_error = std::move(job.error());
			return;
		}
		m_task = Task{ .job = std::move(*job), .path = ResolvePath(), .compression = static_cast<uint8_t>(m_compression) };
	}

	std::string ExportDialog::GetDefaultPath(ProjectEx::ObjectId a_actor) const
	{
		const auto sequence = GetActiveSequenceInfo(*m_project);
		const auto name = SanitizeFileName(std::format("{}_{}_{}", m_session.name, sequence ? sequence->name : std::string_view{}, m_project->GetObjectName(a_actor)));
		const auto directory = m_directory.empty() ? Util::String::GetDataPath() / "Exports" : m_directory;
		const auto path = directory / Utf8ToPath(name + ".glb");
		// Shown relative to the game folder when it's inside it, the way ResolvePath reads it back.
		return GetDisplayPath(path);
	}

	void ExportDialog::WriteSettings(nlohmann::json& a_editor) const
	{
		auto& exportJson = a_editor["export"] = nlohmann::json{ { "compression", m_compression } };
		if (m_lastSampleRate) exportJson["sampleRate"] = *m_lastSampleRate;
		if (!m_directory.empty()) exportJson["directory"] = PathToUtf8(m_directory);
	}

	void ExportDialog::ReadSettings(const nlohmann::json& a_editor)
	{
		const auto exportJson = a_editor.find("export");
		if (exportJson == a_editor.end() || !exportJson->is_object()) return;
		m_compression = std::clamp(exportJson->value("compression", 0), 0, static_cast<int32_t>(kCompressionNames.size()) - 1);
		if (const auto rate = exportJson->find("sampleRate"); rate != exportJson->end() && rate->is_number()) {
			if (const float value = rate->get<float>(); std::isfinite(value) && value > 0.0f) m_lastSampleRate = value;
		}
		if (const auto directory = exportJson->find("directory"); directory != exportJson->end() && directory->is_string()) {
			m_directory = Utf8ToPath(directory->get<std::string>());
		}
	}

	std::filesystem::path ExportDialog::ResolvePath() const
	{
		auto path = Utf8ToPath(m_path);
		if (path.is_relative()) path = Util::String::GetGamePath() / path;
		if (Util::String::ToLower(PathToUtf8(path.extension())) != ".glb") path += ".glb";
		return path;
	}
}
