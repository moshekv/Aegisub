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

#include "subs_edit_ctrl.h"

#include "ass_dialogue.h"
#include "compat.h"
#include "options.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "text_selection_controller.h"
#include "utils.h"

#include <libaegisub/ass/dialogue_parser.h>
#include <libaegisub/calltip_provider.h>
#include <libaegisub/character_count.h>
#include <libaegisub/spellchecker.h>

#include <functional>

#include <wx/clipbrd.h>
#include <wx/settings.h>

SubsTextEditCtrl::SubsTextEditCtrl(wxWindow* parent, wxSize wsize, long style, agi::Context *context)
: wxStyledTextCtrl(parent, -1, wxDefaultPosition, wsize, style)
, SubsEditCtrlBase(context)
{
	osx::ime::inject(this);

	// Set properties
	SetWrapMode(wxSTC_WRAP_WORD);
	SetMarginWidth(1,0);
	UsePopUp(wxSTC_POPUP_NEVER);
	SetStyles();

	// Set hotkeys
	CmdKeyClear(wxSTC_KEY_RETURN,wxSTC_KEYMOD_CTRL);
	CmdKeyClear(wxSTC_KEY_RETURN,wxSTC_KEYMOD_SHIFT);
	CmdKeyClear(wxSTC_KEY_RETURN,wxSTC_KEYMOD_NORM);
	CmdKeyClear(wxSTC_KEY_TAB,wxSTC_KEYMOD_NORM);
	CmdKeyClear(wxSTC_KEY_TAB,wxSTC_KEYMOD_SHIFT);
	CmdKeyClear('D',wxSTC_KEYMOD_CTRL);
	CmdKeyClear('L',wxSTC_KEYMOD_CTRL);
	CmdKeyClear('L',wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_SHIFT);
	CmdKeyClear('T',wxSTC_KEYMOD_CTRL);
	CmdKeyClear('T',wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_SHIFT);
	CmdKeyClear('U',wxSTC_KEYMOD_CTRL);

	Bind(wxEVT_CHAR_HOOK, &SubsTextEditCtrl::OnKeyDown, this);

	Bind(wxEVT_STC_UPDATEUI, &SubsTextEditCtrl::OnUpdateUI, this);
	Bind(wxEVT_STC_MODIFIED, &SubsTextEditCtrl::OnModified, this);
	SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT | wxSTC_STARTACTION);

	Bind(wxEVT_CONTEXT_MENU, &SubsTextEditCtrl::OnContextMenu, this);
	Bind(wxEVT_IDLE, std::bind(&SubsTextEditCtrl::UpdateCallTip, this));
	Bind(wxEVT_STC_DOUBLECLICK, &SubsTextEditCtrl::OnDoubleClick, this);
	Bind(wxEVT_STC_STYLENEEDED, [this](wxStyledTextEvent&) {
		{
			std::string text = GetTextRaw().data();
			if (text == line_text) return;
			line_text = std::move(text);
		}

		UpdateStyle();
	});

	BindConnection(OPT_SUB("Subtitle/Edit Box/Font Face", &SubsTextEditCtrl::SetStyles, this));
	BindConnection(OPT_SUB("Subtitle/Edit Box/Font Size", &SubsTextEditCtrl::SetStyles, this));
	Subscribe("Normal");
	Subscribe("Comment");
	Subscribe("Drawing Command");
	Subscribe("Drawing X");
	Subscribe("Drawing Y");
	BindConnection(OPT_SUB("Colour/Subtitle/Syntax/Underline/Drawing Endpoint", &SubsTextEditCtrl::SetStyles, this));
	Subscribe("Brackets");
	Subscribe("Slashes");
	Subscribe("Tags");
	Subscribe("Error");
	Subscribe("Parameters");
	Subscribe("Line Break");
	Subscribe("Karaoke Template");
	Subscribe("Karaoke Variable");

	BindConnection(OPT_SUB("Colour/Subtitle/Background", &SubsTextEditCtrl::SetStyles, this));
	BindConnection(OPT_SUB("Subtitle/Highlight/Syntax", &SubsTextEditCtrl::UpdateStyle, this));
	BindConnection(OPT_SUB("App/Call Tips", &SubsTextEditCtrl::UpdateCallTip, this));

	BindContextMenu();

}

SubsTextEditCtrl::~SubsTextEditCtrl() {
}

void SubsTextEditCtrl::Subscribe(std::string const& name) {
	BindConnection(OPT_SUB("Colour/Subtitle/Syntax/" + name, &SubsTextEditCtrl::SetStyles, this));
	BindConnection(OPT_SUB("Colour/Subtitle/Syntax/Background/" + name, &SubsTextEditCtrl::SetStyles, this));
	BindConnection(OPT_SUB("Colour/Subtitle/Syntax/Bold/" + name, &SubsTextEditCtrl::SetStyles, this));
}

BEGIN_EVENT_TABLE(SubsTextEditCtrl,wxStyledTextCtrl)
	EVT_KILL_FOCUS(SubsTextEditCtrl::OnLoseFocus)
END_EVENT_TABLE()

void SubsTextEditCtrl::OnLoseFocus(wxFocusEvent &event) {
	CallTipCancel();
	event.Skip();
}

void SubsTextEditCtrl::OnKeyDown(wxKeyEvent &event) {
	if (osx::ime::process_key_event(this, event)) return;
	event.Skip();

	bool linebreak = event.GetKeyCode() == WXK_RETURN && event.GetModifiers() == wxMOD_SHIFT;
#ifndef __WXMAC__
	bool hardspace = event.GetKeyCode() == WXK_SPACE && event.GetModifiers() == (wxMOD_CMD | wxMOD_SHIFT);
#else
	bool hardspace = event.GetKeyCode() == WXK_SPACE && event.GetModifiers() == wxMOD_ALT;
#endif

	// Workaround for wxSTC eating tabs.
	if (event.GetKeyCode() == WXK_TAB)
		Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
	else if (linebreak || hardspace) {
		if (linebreak)
			InsertAtSelection(OPT_GET("Subtitle/Edit Box/Soft Line Break")->GetBool() ? "\\n" : "\\N");
		else
			InsertAtSelection("\\h");
		event.Skip(false);
	}
}

void SubsTextEditCtrl::SetSyntaxStyle(int id, wxFont &font, std::string const& name, wxColor const& default_background) {
	StyleSetFont(id, font);
	StyleSetBold(id, OPT_GET("Colour/Subtitle/Syntax/Bold/" + name)->GetBool());
	StyleSetForeground(id, to_wx(OPT_GET("Colour/Subtitle/Syntax/" + name)->GetColor()));
	const agi::OptionValue *background = OPT_GET("Colour/Subtitle/Syntax/Background/" + name);
	if (background->GetType() == agi::OptionType::Color)
		StyleSetBackground(id, to_wx(background->GetColor()));
	else
		StyleSetBackground(id, default_background);
}

void SubsTextEditCtrl::SetStyles() {
	wxFont font = *wxNORMAL_FONT;
	font.SetEncoding(wxFONTENCODING_DEFAULT); // this solves problems with some fonts not working properly
	wxString fontname = FontFace("Subtitle/Edit Box");
	if (!fontname.empty()) font.SetFaceName(fontname);
	font.SetPointSize(OPT_GET("Subtitle/Edit Box/Font Size")->GetInt());

	auto default_background = to_wx(OPT_GET("Colour/Subtitle/Background")->GetColor());

	namespace ss = agi::ass::SyntaxStyle;
	SetSyntaxStyle(ss::NORMAL, font, "Normal", default_background);
	SetSyntaxStyle(ss::COMMENT, font, "Comment", default_background);
	SetSyntaxStyle(ss::DRAWING_CMD, font, "Drawing Command", default_background);
	SetSyntaxStyle(ss::DRAWING_X, font, "Drawing X", default_background);
	SetSyntaxStyle(ss::DRAWING_Y, font, "Drawing Y", default_background);
	SetSyntaxStyle(ss::DRAWING_ENDPOINT_X, font, "Drawing X", default_background);
	SetSyntaxStyle(ss::DRAWING_ENDPOINT_Y, font, "Drawing Y", default_background);
	StyleSetUnderline(ss::DRAWING_ENDPOINT_X, OPT_GET("Colour/Subtitle/Syntax/Underline/Drawing Endpoint")->GetBool());
	StyleSetUnderline(ss::DRAWING_ENDPOINT_Y, OPT_GET("Colour/Subtitle/Syntax/Underline/Drawing Endpoint")->GetBool());
	SetSyntaxStyle(ss::OVERRIDE, font, "Brackets", default_background);
	SetSyntaxStyle(ss::PUNCTUATION, font, "Slashes", default_background);
	SetSyntaxStyle(ss::TAG, font, "Tags", default_background);
	SetSyntaxStyle(ss::ERROR, font, "Error", default_background);
	SetSyntaxStyle(ss::PARAMETER, font, "Parameters", default_background);
	SetSyntaxStyle(ss::LINE_BREAK, font, "Line Break", default_background);
	SetSyntaxStyle(ss::KARAOKE_TEMPLATE, font, "Karaoke Template", default_background);
	SetSyntaxStyle(ss::KARAOKE_VARIABLE, font, "Karaoke Variable", default_background);

	SetCaretForeground(StyleGetForeground(ss::NORMAL));
	StyleSetBackground(wxSTC_STYLE_DEFAULT, default_background);

	// Misspelling indicator
	IndicatorSetStyle(0,wxSTC_INDIC_SQUIGGLE);
	IndicatorSetForeground(0,wxColour(255,0,0));

	// IME pending text indicator
	IndicatorSetStyle(1, wxSTC_INDIC_PLAIN);
	IndicatorSetUnder(1, true);
}

void SubsTextEditCtrl::UpdateStyle() {
	UpdateTokens();

	cursor_pos = -1;
	UpdateCallTip();

	StartStyling(0);

	if (!OPT_GET("Subtitle/Highlight/Syntax")->GetBool()) {
		SetStyling(line_text.size(), 0);
		return;
	}

	if (line_text.empty()) return;

	SetIndicatorCurrent(0);
	size_t pos = 0;
	for (auto const& style_range : agi::ass::SyntaxHighlight(line_text, tokenized_line, spellchecker.get())) {
		if (style_range.type == agi::ass::SyntaxStyle::SPELLING) {
			SetStyling(style_range.length, agi::ass::SyntaxStyle::NORMAL);
			IndicatorFillRange(pos, style_range.length);
		}
		else {
			SetStyling(style_range.length, style_range.type);
			IndicatorClearRange(pos, style_range.length);
		}
		pos += style_range.length;
	}
}

void SubsTextEditCtrl::UpdateCallTip() {
	if (!OPT_GET("App/Call Tips")->GetBool()) return;

	int pos = GetCurrentPos();
	if (pos == cursor_pos) return;
	cursor_pos = pos;

	agi::Calltip new_calltip = agi::GetCalltip(tokenized_line, line_text, pos);

	if (!new_calltip.text) {
		CallTipCancel();
		return;
	}

	if (!CallTipActive() || calltip_position != new_calltip.tag_position || calltip_text != new_calltip.text)
		CallTipShow(new_calltip.tag_position, wxString::FromUTF8Unchecked(new_calltip.text));

	calltip_position = new_calltip.tag_position;
	calltip_text = new_calltip.text;

	CallTipSetHighlight(new_calltip.highlight_start, new_calltip.highlight_end);
}

bool SubsTextEditCtrl::ProcessImeKeyEvent(wxKeyEvent &event) {
	return osx::ime::process_key_event(this, event);
}

void SubsTextEditCtrl::OnUpdateUI(wxStyledTextEvent &event) {
	if (on_selection_changed)
		on_selection_changed((event.GetUpdated() & wxSTC_UPDATE_SELECTION) != 0);
}

void SubsTextEditCtrl::OnModified(wxStyledTextEvent &event) {
	if (on_text_modified)
		on_text_modified((event.GetModificationType() & wxSTC_STARTACTION) != 0);
}

void SubsTextEditCtrl::SetTextTo(std::string const& text) {
	osx::ime::invalidate(this);
	SetEvtHandlerEnabled(false);
	Freeze();

	auto insertion_point = GetInsertionPoint();
	if (static_cast<size_t>(insertion_point) > line_text.size())
		line_text = GetTextRaw().data();
	auto old_pos = agi::CharacterCount(std::string_view(line_text).substr(0, insertion_point), 0);
	line_text.clear();

	if (context) {
		context->textSelectionController->SetSelection(0, 0);
		SetTextRaw(text.c_str());
		auto pos = agi::IndexOfCharacter(text, old_pos);
		context->textSelectionController->SetSelection(pos, pos);
	}
	else {
		SetSelection(0, 0);
		SetTextRaw(text.c_str());
		auto pos = agi::IndexOfCharacter(text, old_pos);
		SetSelection(pos, pos);
	}

	SetEvtHandlerEnabled(true);
	Thaw();
}

void SubsTextEditCtrl::Paste() {
	PasteClipboard();
}

void SubsTextEditCtrl::OnContextMenu(wxContextMenuEvent &event) {
	wxPoint pos = event.GetPosition();
	int activePos;
	if (pos == wxDefaultPosition)
		activePos = GetCurrentPos();
	else
		activePos = PositionFromPoint(ScreenToClient(pos));

	ShowContextMenu(activePos);
}

void SubsTextEditCtrl::OnDoubleClick(wxStyledTextEvent &evt) {
	int pos = evt.GetPosition();
	if (pos == -1 && !tokenized_line.empty()) {
		auto tok = tokenized_line.back();
		SetSelection(line_text.size() - tok.length, line_text.size());
	}
	else {
		auto bounds = GetBoundsOfWordAtPosition(evt.GetPosition());
		if (bounds.second != 0)
			SetSelection(bounds.first, bounds.first + bounds.second);
		else
			evt.Skip();
	}
}
