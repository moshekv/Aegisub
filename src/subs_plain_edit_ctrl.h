// Copyright (c) 2026, Aegisub Project http://www.aegisub.org/
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#pragma once

#include "subs_edit_ctrl_base.h"

#include <libaegisub/signal.h>

#include <vector>
#include <wx/textctrl.h>

class wxPopupWindow;
class wxStaticText;

/// @class SubsPlainTextEditCtrl
/// @brief A native text control subtitle editor with working bidirectional text
///
/// Scintilla supports bidirectional text only on Windows and macOS, and not at
/// all on GTK, so this uses the platform's own text control, which lays out
/// bidirectional text natively. It has the same syntax highlighting, spell
/// checking, thesaurus, context menu and calltips as the Scintilla editor.
///
/// Two things differ from the text it edits:
///
/// - The buffer starts with an invisible direction mark (see .cpp), so buffer
///   positions are one more than text positions.
/// - Positions are counted in wxString units (characters on GTK, UTF-16 code
///   units on Windows) whereas the rest of Aegisub uses UTF-8 byte offsets.
///
/// Every offset crossing the boundary goes through BytesToPos/PosToBytes.
/// wxString units match the native control on GTK and Windows; on macOS,
/// where NSTextView counts UTF-16 units, characters outside the BMP would
/// shift offsets. This is untested there.
class SubsPlainTextEditCtrl final : public wxTextCtrl, public SubsEditCtrlBase {
	/// Guards against reacting to our own styling and text updates
	bool updating = false;

	/// The direction mark currently at the start of the buffer
	wxUniChar mark;

	/// Last selection seen, to synthesise the change events wxTextCtrl lacks
	long last_sel_start = -1;
	long last_sel_end = -1;

	/// Caret position after the last edit, to tell when a new undo step starts
	long edit_caret = -1;

	/// Calltip window, created on first use
	wxPopupWindow *calltip = nullptr;
	wxStaticText *calltip_label = nullptr;
	wxString calltip_markup;
	wxPoint calltip_pt;

	std::vector<agi::signal::Connection> connections;

	void SetStyles();
	void UpdateStyle() override;
	void Subscribe(std::string const& name);

	/// Put line_text into the buffer behind the mark for its direction,
	/// with the caret at the given character index of line_text
	void SetBuffer(long caret);

	void OnText();
	void OnKeyDown(wxKeyEvent &event);
	/// @return Whether the selection changed and was announced
	bool CheckSelectionChanged();
	void KeepSelectionOffMark();

	void UpdateCallTip();
	void HideCallTip();

	long BytesToPos(int bytes) const;
	int PosToBytes(long pos) const;

	void ReplaceTextUtf8(std::string const& text) override;
	wxTextEntryBase *AsTextEntry() override { return this; }

public:
	SubsPlainTextEditCtrl(wxWindow *parent, wxSize size, long style, agi::Context *context);

	void Paste() override;
	void Copy() override;
	void Cut() override;
	void SelectAll() override;

	wxWindow *AsWindow() override { return this; }
	void SetTextTo(std::string const& text) override;
	std::string GetTextUtf8() const override { return line_text; }

	int GetInsertionPointBytes() const override;
	void SetInsertionPointBytes(int pos) override;
	int GetSelectionStartBytes() const override;
	int GetSelectionEndBytes() const override;
	void SetSelectionStartBytes(int pos) override;
	void SetSelectionEndBytes(int pos) override;
};
