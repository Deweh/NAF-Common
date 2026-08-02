#include "UI/Studio/Panels/HistoryPanel.h"

#include "UI/Studio/StudioSession.h"

namespace UI::Studio
{
	HistoryPanel::HistoryPanel(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	void HistoryPanel::Draw()
	{
		if (!m_session.windows.history) return;
		if (!ImGui::Begin(kHistoryWindow, &m_session.windows.history)) {
			ImGui::End();
			return;
		}
		const size_t head = m_project->GetHistoryHead();
		ImGui::BeginDisabled(m_session.IsInteracting());
		if (ImGui::Selectable("(Oldest state)", head == 0)) m_project->JumpToHistory(0);
		for (size_t i = 0; i < m_project->GetHistorySize(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			// Entries past the head are undone and can be redone.
			if (i >= head) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			if (ImGui::Selectable(std::string{ m_project->GetHistoryDescription(i) }.c_str(), head == i + 1)) m_project->JumpToHistory(i + 1);
			if (i >= head) ImGui::PopStyleColor();
			ImGui::PopID();
		}
		ImGui::EndDisabled();
		ImGui::End();
	}
}
