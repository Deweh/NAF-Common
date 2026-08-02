#pragma once

#include "UI/Studio/StudioWidgets.h"

namespace UI::Studio
{
	class StudioSession;

	// Indexes the popup names in Popups.cpp.
	enum class Popup : uint8_t
	{
		kNone,
		kAddActorMenu,
		kAddActorByFormId,
		kBindActor,
		kRename,
		kNewSequence,
		kSequenceSettings,
		kConfirmClose,
		kLoadWarnings,
		kCreateSplineIK,
		kExportGLB,
		kSaveRig,
		kApplyRig,
		kRigWarnings
	};

	struct PopupRequest
	{
		Popup popup{ Popup::kNone };
		ProjectEx::ObjectId object{ ProjectEx::kNoObject };
		ProjectEx::SequenceId sequence{ ProjectEx::kNoSequence };
	};

	// The Studio's popups. Requested from anywhere, even from inside a menu, and opened at the root on the next
	// frame. Holds the small dialogs' inputs; export and rigs have their own dialogs.
	class Popups
	{
	public:
		explicit Popups(StudioSession& a_session);

		void Request(PopupRequest a_request) { m_pending = a_request; }
		// Lists warnings in the Load Warnings or Rig Warnings popup.
		void ShowWarnings(Popup a_popup, std::vector<std::string> a_warnings);
		void Draw();

		[[nodiscard]] static const char* GetName(Popup a_popup);

	private:
		// Fills a dialog's inputs as it opens.
		void Open(const PopupRequest& a_request);

		StudioSession& m_session;
		ProjectEx* m_project;
		PopupRequest m_pending;
		PopupRequest m_open;
		std::vector<std::string> m_warnings;

		// Inputs.
		std::string m_textInput;
		uint32_t m_formIdInput{ 0 };
		float m_frameRateInput{ 30.0f };
		int32_t m_frameCountInput{ 30 };
		bool m_rescaleKeysInput{ true };
		bool m_loopSmoothingInput{ false };
		int32_t m_controlCountInput{ 3 };
	};
}
