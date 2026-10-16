#pragma once

#include "UI/Studio/StudioWidgets.h"

namespace UI::Studio
{
	class StudioSession;

	// Gizmo and keying options, then the active element's properties.
	class Properties
	{
	public:
		explicit Properties(StudioSession& a_session);
		void Draw();

		// Shared with the outliner's context menus.
		void DrawShoulderMenu(ProjectEx::ObjectId a_chain);
		void DrawHandPresetMenu(ProjectEx::ObjectId a_hand);

	private:
		void DrawTransformProperties(ProjectEx::ElementRef a_element);
		void DrawIKChainProperties(ProjectEx::ObjectId a_chain);
		void DrawHandProperties(ProjectEx::ObjectId a_hand);
		void DrawSplineIKProperties(ProjectEx::ObjectId a_spline);
		void DrawCorrectiveDriverProperties(ProjectEx::ObjectId a_drivers);
		// A keyable float with its key indicator, as a slider (or an angle slider in degrees).
		void DrawFloatProperty(const ProjectEx::PropertyRef& a_property, const char* a_label, float a_min, float a_max, bool a_angle);
		bool DrawKeyIndicator(const ProjectEx::PropertyRef& a_property);
		// Call right after a field's widget. Applies the field's value (when a_changed) inside the field's interaction,
		// so a whole drag or text entry is one undo step.
		void TrackFieldEdit(const char* a_description, bool a_changed, const std::function<void()>& a_apply);

		StudioSession& m_session;
		ProjectEx* m_project;
	};
}
