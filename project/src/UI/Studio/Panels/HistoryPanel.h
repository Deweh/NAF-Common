#pragma once

#include "UI/Studio/StudioWidgets.h"

namespace UI::Studio
{
	class StudioSession;

	// The list of commits. Clicking one jumps there; entries past the head are dimmed.
	class HistoryPanel
	{
	public:
		explicit HistoryPanel(StudioSession& a_session);
		void Draw();

	private:
		StudioSession& m_session;
		ProjectEx* m_project;
	};
}
