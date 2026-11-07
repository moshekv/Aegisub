// Copyright (c) 2005, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#pragma once

#include "subs_edit_ctrl_base.h"

#include <libaegisub/signal.h>

#include <string>
#include <vector>
#include <wx/stc/stc.h>

/// @class SubsTextEditCtrl
/// @brief A Scintilla control with spell checking and syntax highlighting
class SubsTextEditCtrl final : public wxStyledTextCtrl, public SubsEditCtrlBase, private agi::signal::ConnectionScope {
	/// Text of the currently shown calltip, to avoid flickering from
	/// pointlessly reshowing the current tip
	std::string calltip_text;

	/// Position of the currently show calltip
	size_t calltip_position = 0;

	/// Cursor position which the current calltip is for
	int cursor_pos;


	void OnContextMenu(wxContextMenuEvent &);
	void OnDoubleClick(wxStyledTextEvent&);
	void OnLoseFocus(wxFocusEvent &event);
	void OnKeyDown(wxKeyEvent &event);
	void OnUpdateUI(wxStyledTextEvent &event);
	void OnModified(wxStyledTextEvent &event);

	void SetSyntaxStyle(int id, wxFont &font, std::string const& name, wxColor const& default_background);
	void Subscribe(std::string const& name);

	void StyleSpellCheck();
	void UpdateCallTip();
	void SetStyles();

	void UpdateStyle() override;
	void ReplaceTextUtf8(std::string const& text) override { SetTextRaw(text.c_str()); }
	wxTextEntryBase *AsTextEntry() override { return this; }

public:
	SubsTextEditCtrl(wxWindow* parent, wxSize size, long style, agi::Context *context);
	~SubsTextEditCtrl();

	void Paste() override;

	// SubsEditCtrlBase. Scintilla is natively UTF-8 byte based, so these are
	// straight pass-throughs.
	wxWindow *AsWindow() override { return this; }
	bool ProcessImeKeyEvent(wxKeyEvent &event) override;
	void SetTextTo(std::string const& text) override;
	std::string GetTextUtf8() const override {
		auto data = const_cast<SubsTextEditCtrl *>(this)->GetTextRaw();
		return std::string(data.data(), data.length());
	}
	int GetInsertionPointBytes() const override { return const_cast<SubsTextEditCtrl *>(this)->GetInsertionPoint(); }
	void SetInsertionPointBytes(int pos) override { SetInsertionPoint(pos); }
	int GetSelectionStartBytes() const override { return const_cast<SubsTextEditCtrl *>(this)->GetSelectionStart(); }
	int GetSelectionEndBytes() const override { return const_cast<SubsTextEditCtrl *>(this)->GetSelectionEnd(); }
	void SetSelectionStartBytes(int pos) override { SetSelectionStart(pos); }
	void SetSelectionEndBytes(int pos) override { SetSelectionEnd(pos); }

	DECLARE_EVENT_TABLE()
};
