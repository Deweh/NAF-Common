#pragma once

#include "UI/Studio/StudioWidgets.h"

namespace UI::Studio
{
	class StudioSession;

	// Actors with their skeletons, modifiers and locators, then the scene's locators. Every element has a context menu.
	class Outliner
	{
	public:
		explicit Outliner(StudioSession& a_session);
		void Draw();

	private:
		void DrawActor(ProjectEx::ObjectId a_actor);
		// Which locators each object has, once per frame: drawing every level from the object list was quadratic.
		void BuildLocatorChildren();
		void DrawLocators(const ProjectEx::ParentRef& a_parent);
		// Click selection, reveal scrolling and the context menu for the item just drawn.
		void DrawItemBehavior(ProjectEx::ElementRef a_element);
		void DrawElementContextMenu(ProjectEx::ElementRef a_element);
		void DrawBoneDisplayMenu();
		void DrawParentMenu(ProjectEx::ObjectId a_locator);
		// Bezier Mode for every selected element's properties. An actor brings its modifiers, and a modifier its
		// effector, pole or controls. Bones also get hierarchy items.
		void DrawBezierModeMenu();

		StudioSession& m_session;
		ProjectEx* m_project;
		std::string m_filter;
		std::unordered_map<ProjectEx::ObjectId, std::vector<ProjectEx::ObjectId>> m_locatorChildren;  // Parent object -> its locators, not counting owned ones.
	};
}
