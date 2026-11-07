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

#include "subs_plain_edit_ctrl.h"

#include "compat.h"
#include "options.h"
#include "utils.h"

#include <libaegisub/ass/dialogue_parser.h>
#include <libaegisub/calltip_provider.h>
#include <libaegisub/util.h>

#include <algorithm>

#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/stattext.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <richedit.h>
#endif

namespace {
// GTK and macOS take a paragraph's base direction from its first strong
// character and ignore the widget's direction. For most lines that is a letter
// inside an override tag, such as the "an" of {\an8}, which would lay out
// right-to-left text as left-to-right. An invisible mark of the wanted
// direction kept at the start of the buffer overrides this without touching
// the text itself; it never leaves this control.
const wxUniChar LRM = 0x200E;
const wxUniChar RLM = 0x200F;

wxUniChar direction_mark(std::string const& text) {
	return agi::util::is_rtl(text) ? RLM : LRM;
}

wxFont edit_font() {
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	font.SetEncoding(wxFONTENCODING_DEFAULT);
	wxString fontname = FontFace("Subtitle/Edit Box");
	if (!fontname.empty()) font.SetFaceName(fontname);
	font.SetPointSize(OPT_GET("Subtitle/Edit Box/Font Size")->GetInt());
	return font;
}

bool is_navigation_key(int key) {
	switch (key) {
	case WXK_LEFT: case WXK_RIGHT: case WXK_UP: case WXK_DOWN:
	case WXK_HOME: case WXK_END: case WXK_PAGEUP: case WXK_PAGEDOWN:
	case WXK_NUMPAD_LEFT: case WXK_NUMPAD_RIGHT: case WXK_NUMPAD_UP: case WXK_NUMPAD_DOWN:
	case WXK_NUMPAD_HOME: case WXK_NUMPAD_END: case WXK_NUMPAD_PAGEUP: case WXK_NUMPAD_PAGEDOWN:
	case WXK_SHIFT: case WXK_CONTROL: case WXK_ALT:
		return true;
	default:
		return false;
	}
}
}

SubsPlainTextEditCtrl::SubsPlainTextEditCtrl(wxWindow *parent, wxSize size, long style, agi::Context *context)
: wxTextCtrl(parent, -1, "", wxDefaultPosition, size, style | wxTE_MULTILINE | wxTE_RICH2 | wxTE_NOHIDESEL)
, SubsEditCtrlBase(context)
, mark(LRM)
{
	SetStyles();
	SetBuffer(0);

	Bind(wxEVT_CHAR_HOOK, &SubsPlainTextEditCtrl::OnKeyDown, this);

	// wxTextCtrl has no caret-moved event, so poll after anything that can
	// move the caret or change the selection
	Bind(wxEVT_KEY_UP, [=](wxKeyEvent &e) { e.Skip(); CheckSelectionChanged(); });
	// A mouse selection from the start of the line covers the mark, which could
	// then be dragged elsewhere
	Bind(wxEVT_LEFT_UP, [=](wxMouseEvent &e) { e.Skip(); KeepSelectionOffMark(); CheckSelectionChanged(); });
	Bind(wxEVT_SET_FOCUS, [=](wxFocusEvent &e) { e.Skip(); CheckSelectionChanged(); UpdateCallTip(); });
	Bind(wxEVT_KILL_FOCUS, [=](wxFocusEvent &e) { e.Skip(); HideCallTip(); });

	Bind(wxEVT_TEXT, [=](wxCommandEvent &e) {
		e.Skip();
		if (!updating) OnText();
	});

	Bind(wxEVT_CONTEXT_MENU, [=](wxContextMenuEvent &e) {
		long pos = GetInsertionPoint();
		if (e.GetPosition() != wxDefaultPosition)
			HitTest(ScreenToClient(e.GetPosition()), &pos);
		ShowContextMenu(PosToBytes(pos));
	});

	Subscribe("Normal");
	Subscribe("Comment");
	Subscribe("Drawing Command");
	Subscribe("Drawing X");
	Subscribe("Drawing Y");
	Subscribe("Brackets");
	Subscribe("Slashes");
	Subscribe("Tags");
	Subscribe("Error");
	Subscribe("Parameters");
	Subscribe("Line Break");
	Subscribe("Karaoke Template");
	Subscribe("Karaoke Variable");

	auto restyle = [=](agi::OptionValue const&) { SetStyles(); UpdateStyle(); };
	connections.push_back(OPT_SUB("Subtitle/Edit Box/Font Face", restyle));
	connections.push_back(OPT_SUB("Subtitle/Edit Box/Font Size", restyle));
	connections.push_back(OPT_SUB("Colour/Subtitle/Background", restyle));
	connections.push_back(OPT_SUB("Colour/Subtitle/Syntax/Underline/Drawing Endpoint", restyle));
	connections.push_back(OPT_SUB("Subtitle/Highlight/Syntax", restyle));
	connections.push_back(OPT_SUB("App/Call Tips", [=](agi::OptionValue const&) { UpdateCallTip(); }));

	BindContextMenu();
}

void SubsPlainTextEditCtrl::Subscribe(std::string const& name) {
	auto restyle = [=](agi::OptionValue const&) { SetStyles(); UpdateStyle(); };
	connections.push_back(OPT_SUB("Colour/Subtitle/Syntax/" + name, restyle));
	connections.push_back(OPT_SUB("Colour/Subtitle/Syntax/Background/" + name, restyle));
	connections.push_back(OPT_SUB("Colour/Subtitle/Syntax/Bold/" + name, restyle));
}

long SubsPlainTextEditCtrl::BytesToPos(int bytes) const {
	bytes = std::max(0, std::min(bytes, static_cast<int>(line_text.size())));
	// Never split a UTF-8 sequence, which would make the prefix invalid
	while (bytes > 0 && bytes < static_cast<int>(line_text.size()) && (line_text[bytes] & 0xC0) == 0x80)
		--bytes;
	return 1 + static_cast<long>(wxString::FromUTF8(line_text.data(), bytes).length());
}

int SubsPlainTextEditCtrl::PosToBytes(long pos) const {
	wxString value = to_wx(line_text);
	pos = std::max(0L, std::min(pos - 1, static_cast<long>(value.length())));
	return static_cast<int>(value.Left(pos).utf8_str().length());
}

int SubsPlainTextEditCtrl::GetInsertionPointBytes() const {
	return PosToBytes(GetInsertionPoint());
}

void SubsPlainTextEditCtrl::SetInsertionPointBytes(int pos) {
	SetInsertionPoint(BytesToPos(pos));
	CheckSelectionChanged();
}

int SubsPlainTextEditCtrl::GetSelectionStartBytes() const {
	long from = 0, to = 0;
	GetSelection(&from, &to);
	return PosToBytes(from);
}

int SubsPlainTextEditCtrl::GetSelectionEndBytes() const {
	long from = 0, to = 0;
	GetSelection(&from, &to);
	return PosToBytes(to);
}

void SubsPlainTextEditCtrl::SetSelectionStartBytes(int pos) {
	long from = 0, to = 0;
	GetSelection(&from, &to);
	long start = BytesToPos(pos);
	SetSelection(start, std::max(start, to));
	CheckSelectionChanged();
}

void SubsPlainTextEditCtrl::SetSelectionEndBytes(int pos) {
	long from = 0, to = 0;
	GetSelection(&from, &to);
	long end = BytesToPos(pos);
	SetSelection(std::min(from, end), end);
	CheckSelectionChanged();
}

bool SubsPlainTextEditCtrl::CheckSelectionChanged() {
	long from = 0, to = 0;
	GetSelection(&from, &to);

	// The mark is a caret stop of its own, so Home or a click at the start of
	// the line can land in front of it; move past it so that no keypress is
	// spent crossing an invisible character
	if (from == 0 && to == 0) {
		SetInsertionPoint(1);
		from = to = 1;
	}

	if (from == last_sel_start && to == last_sel_end) return false;
	last_sel_start = from;
	last_sel_end = to;
	if (on_selection_changed) on_selection_changed(true);
	UpdateCallTip();
	return true;
}

void SubsPlainTextEditCtrl::KeepSelectionOffMark() {
	long from = 0, to = 0;
	GetSelection(&from, &to);
	if (from < 1 || to < 1)
		SetSelection(std::max(from, 1L), std::max(to, 1L));
}

void SubsPlainTextEditCtrl::SetBuffer(long caret) {
	wxString value = to_wx(line_text);
	mark = direction_mark(line_text);

	updating = true;
	ChangeValue(wxString(mark) + value);
#ifdef __WXMSW__
	// The rich edit control keeps a paragraph direction of its own rather than
	// taking it from the text, and keeps it when the text is replaced, so set
	// it both ways. SetLayoutDirection is no substitute: for rich edit controls
	// it mirrors the whole window, and wx only ever sets the paragraph flag.
	PARAFORMAT2 pf{};
	pf.cbSize = sizeof(pf);
	pf.dwMask = PFM_RTLPARA;
	pf.wEffects = mark == RLM ? PFE_RTLPARA : 0;
	SetSelection(0, GetLastPosition());
	::SendMessage(static_cast<HWND>(GetHWND()), EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
#endif
	caret = 1 + std::max(0L, std::min(caret, static_cast<long>(value.length())));
	SetSelection(caret, caret);
	updating = false;

	UpdateStyle();
}

void SubsPlainTextEditCtrl::OnText() {
	// Like Scintilla, start a new undo step when the caret was moved since the
	// previous edit, rather than amending a single step for the whole line
	bool start_of_action = last_sel_start != edit_caret || last_sel_end != edit_caret;

	wxString value = GetValue();
	long caret = GetInsertionPoint();

	bool intact = !value.empty() && value[0] == mark;
	if (intact) {
		value.erase(0, 1);
		--caret;
	}
	else {
		// The edit reached the start of the buffer. Either text went in front
		// of the mark (an input method can do that despite OnKeyDown), in which
		// case the old text follows the mark unchanged, or the mark was deleted.
		size_t i = value.find(mark);
		if (i != wxString::npos && value.substr(i + 1) == to_wx(line_text)) {
			value.erase(i, 1);
			if (caret > static_cast<long>(i)) --caret;
		}
	}

	// The platform's own paste shortcuts and drag and drop bypass Paste(), so
	// convert newlines here as well; a literal one would split the line in the file
	bool has_newlines = value.find_first_of("\r\n") != wxString::npos;
	if (has_newlines) {
		wxString before = value.Left(caret), after = value.Mid(caret);
		for (wxString *part : {&before, &after}) {
			part->Replace("\r\n", "\\N");
			part->Replace("\n", "\\N");
			part->Replace("\r", "\\N");
		}
		value = before + after;
		caret = static_cast<long>(before.length());
	}

	line_text = from_wx(value);

	// Rebuilding the buffer disturbs the platform's input method state, so
	// only do it when the text or its direction has to change
	if (!intact || has_newlines || direction_mark(line_text) != mark)
		SetBuffer(caret);
	else
		UpdateStyle();

	if (!CheckSelectionChanged())
		UpdateCallTip();
	edit_caret = last_sel_end;
	if (on_text_modified) on_text_modified(start_of_action);
}

void SubsPlainTextEditCtrl::OnKeyDown(wxKeyEvent &event) {
	int key = event.GetKeyCode();

	if (key == WXK_TAB) {
		Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
		return;
	}

	// ASS dialogue text is a single line; hard line breaks are written \N, so
	// never let a literal newline into the buffer
	if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
		if (event.GetModifiers() == wxMOD_SHIFT)
			InsertAtSelection(OPT_GET("Subtitle/Edit Box/Soft Line Break")->GetBool() ? "\\n" : "\\N");
		return;
	}

	// Anything that edits or copies text must not see the mark: Home puts the
	// caret in front of it and select all includes it
	if (!is_navigation_key(key))
		KeepSelectionOffMark();

	long from = 0, to = 0;
	GetSelection(&from, &to);
	if (key == WXK_BACK && from == to && from <= 1)
		return;

	event.Skip();
}

void SubsPlainTextEditCtrl::SetTextTo(std::string const& text) {
	// Keep the caret where it was, measured in characters so that it survives
	// a change in the number of bytes before it
	long caret = GetInsertionPoint() - 1;
	line_text = text;
	SetBuffer(caret);
	edit_caret = -1;
	if (!CheckSelectionChanged())
		UpdateCallTip();
}

void SubsPlainTextEditCtrl::ReplaceTextUtf8(std::string const& text) {
	long caret = GetInsertionPoint() - 1;
	line_text = text;
	SetBuffer(caret);
	if (on_text_modified) on_text_modified(true);
}

void SubsPlainTextEditCtrl::Paste() {
	PasteClipboard();
}

void SubsPlainTextEditCtrl::Copy() {
	KeepSelectionOffMark();
	wxTextCtrl::Copy();
}

void SubsPlainTextEditCtrl::Cut() {
	KeepSelectionOffMark();
	wxTextCtrl::Cut();
}

void SubsPlainTextEditCtrl::SelectAll() {
	SetSelection(1, GetLastPosition());
	CheckSelectionChanged();
}

void SubsPlainTextEditCtrl::SetStyles() {
	wxFont font = edit_font();

	SetBackgroundColour(to_wx(OPT_GET("Colour/Subtitle/Background")->GetColor()));
	// The caret is drawn in the foreground colour, which otherwise comes from
	// the theme and can match the background (light text in a dark theme)
	wxColour normal = to_wx(OPT_GET("Colour/Subtitle/Syntax/Normal")->GetColor());
	SetForegroundColour(normal);

	wxTextAttr attr;
	attr.SetFont(font);
	attr.SetTextColour(normal);
	SetDefaultStyle(attr);
}

void SubsPlainTextEditCtrl::UpdateStyle() {
	if (updating) return;

	UpdateTokens();

	wxFont font = edit_font();

	auto background = to_wx(OPT_GET("Colour/Subtitle/Background")->GetColor());
	bool underline_endpoints = OPT_GET("Colour/Subtitle/Syntax/Underline/Drawing Endpoint")->GetBool();

	auto style_for = [&](int type) {
		namespace ss = agi::ass::SyntaxStyle;
		const char *name = "Normal";
		switch (type) {
		case ss::COMMENT:            name = "Comment"; break;
		case ss::DRAWING_CMD:        name = "Drawing Command"; break;
		case ss::DRAWING_X:
		case ss::DRAWING_ENDPOINT_X: name = "Drawing X"; break;
		case ss::DRAWING_Y:
		case ss::DRAWING_ENDPOINT_Y: name = "Drawing Y"; break;
		case ss::OVERRIDE:           name = "Brackets"; break;
		case ss::PUNCTUATION:        name = "Slashes"; break;
		case ss::TAG:                name = "Tags"; break;
		case ss::ERROR:              name = "Error"; break;
		case ss::PARAMETER:          name = "Parameters"; break;
		case ss::LINE_BREAK:         name = "Line Break"; break;
		case ss::KARAOKE_TEMPLATE:   name = "Karaoke Template"; break;
		case ss::KARAOKE_VARIABLE:   name = "Karaoke Variable"; break;
		default:                     name = "Normal"; break;
		}

		bool misspelled = type == ss::SPELLING;
		bool endpoint = underline_endpoints && (type == ss::DRAWING_ENDPOINT_X || type == ss::DRAWING_ENDPOINT_Y);

		wxTextAttr attr;
		wxFont f = font;
		f.SetWeight(OPT_GET(std::string("Colour/Subtitle/Syntax/Bold/") + name)->GetBool() ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
#if wxCHECK_VERSION(3, 1, 3)
		attr.SetFont(f);
		// Always set, so that an underline from a previous styling is cleared
		if (misspelled)
			attr.SetFontUnderlined(wxTEXT_ATTR_UNDERLINE_SPECIAL, wxColour(255, 0, 0));
		else
			attr.SetFontUnderlined(endpoint ? wxTEXT_ATTR_UNDERLINE_SOLID : wxTEXT_ATTR_UNDERLINE_NONE);
#else
		f.SetUnderlined(misspelled || endpoint);
		attr.SetFont(f);
#endif
		attr.SetTextColour(to_wx(OPT_GET(std::string("Colour/Subtitle/Syntax/") + name)->GetColor()));
		const agi::OptionValue *bg = OPT_GET(std::string("Colour/Subtitle/Syntax/Background/") + name);
		attr.SetBackgroundColour(bg->GetType() == agi::OptionType::Color ? to_wx(bg->GetColor()) : background);
		return attr;
	};

	// Applying styles moves the caret on some platforms, so restore it
	updating = true;
	long sel_from = 0, sel_to = 0;
	GetSelection(&sel_from, &sel_to);

	if (!line_text.empty() && OPT_GET("Subtitle/Highlight/Syntax")->GetBool()) {
		size_t bytes = 0;
		long pos = 1;
		for (auto const& range : agi::ass::SyntaxHighlight(line_text, tokenized_line, spellchecker.get())) {
			long len = static_cast<long>(wxString::FromUTF8(line_text.data() + bytes, range.length).length());
			if (len > 0) SetStyle(pos, pos + len, style_for(range.type));
			pos += len;
			bytes += range.length;
		}
	}
	else if (!line_text.empty()) {
		SetStyle(1, GetLastPosition(), style_for(agi::ass::SyntaxStyle::NORMAL));
	}

	SetSelection(sel_from, sel_to);
	updating = false;
}

void SubsPlainTextEditCtrl::HideCallTip() {
	if (calltip && calltip->IsShown())
		calltip->Hide();
	calltip_markup.clear();
}

void SubsPlainTextEditCtrl::UpdateCallTip() {
	if (!OPT_GET("App/Call Tips")->GetBool() || !HasFocus()) {
		HideCallTip();
		return;
	}

	agi::Calltip tip = agi::GetCalltip(tokenized_line, line_text, GetInsertionPointBytes());
	if (!tip.text) {
		HideCallTip();
		return;
	}

	std::string text = tip.text;
	auto part = [&](size_t from, size_t to) {
		from = std::min(from, text.size());
		to = std::max(from, std::min(to, text.size()));
		wxString str = to_wx(text.substr(from, to - from));
		str.Replace("&", "&amp;");
		str.Replace("<", "&lt;");
		str.Replace(">", "&gt;");
		return str;
	};
	wxString markup = part(0, tip.highlight_start)
		+ "<b>" + part(tip.highlight_start, tip.highlight_end) + "</b>"
		+ part(tip.highlight_end, text.size());

	// Show it under the start of the tag, like Scintilla's calltips
	wxPoint pt = PositionToCoords(BytesToPos(static_cast<int>(tip.tag_position)));
	if (pt == wxDefaultPosition) pt = wxPoint(0, 0);
	pt.y += GetCharHeight() + 2;

	if (calltip && calltip->IsShown() && markup == calltip_markup && pt == calltip_pt) return;
	calltip_markup = markup;
	calltip_pt = pt;

	if (!calltip) {
		calltip = new wxPopupWindow(this);
		calltip->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
		calltip_label = new wxStaticText(calltip, -1, "", wxPoint(4, 2));
		calltip_label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
	}

	calltip_label->SetLabelMarkup(markup);
	calltip->SetClientSize(calltip_label->GetBestSize() + wxSize(8, 4));
	calltip->Move(ClientToScreen(pt));
	calltip->Show();
}
