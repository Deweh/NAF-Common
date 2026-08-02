#pragma once

#include "UI/Studio/StudioWidgets.h"

namespace UI::Studio
{
	class StudioSession;

	// The bar across the top: the main menus, the sequence selector, the frame readout and the status line.
	class OptionsBar
	{
	public:
		explicit OptionsBar(StudioSession& a_session);
		void Draw();

	private:
		void DrawMainMenuBar();
		void DrawCreateMenuItems();
		void DrawSequenceSelector();

		StudioSession& m_session;
		ProjectEx* m_project;
	};
}
