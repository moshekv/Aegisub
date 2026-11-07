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

// Split out of subs_edit_ctrl.cpp so that the Scintilla and native editors
// share it.

#include "subs_edit_ctrl_base.h"

#include "ass_dialogue.h"
#include "command/command.h"
#include "compat.h"
#include "format.h"
#include "options.h"
#include "include/aegisub/context.h"
#include "include/aegisub/spellchecker.h"
#include "selection_controller.h"
#include "subs_edit_ctrl.h"
#include "thesaurus.h"
#include "utils.h"

#include <libaegisub/ass/dialogue_parser.h>
#include <libaegisub/spellchecker.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <wx/intl.h>
#include <wx/menu.h>
#include <wx/textentry.h>
#include <wx/window.h>

// Maximum number of languages (locales)
// It should be above 100 (at least 242) and probably not more than 1000
#define LANGS_MAX 1000

/// Event ids
// Check menu.h for id range allocation before editing this enum
enum {
	EDIT_MENU_SPLIT_PRESERVE = (wxID_HIGHEST + 1) + 4000,
	EDIT_MENU_SPLIT_ESTIMATE,
	EDIT_MENU_SPLIT_VIDEO,
	EDIT_MENU_CUT,
	EDIT_MENU_COPY,
	EDIT_MENU_PASTE,
	EDIT_MENU_SELECT_ALL,
	EDIT_MENU_ADD_TO_DICT,
	EDIT_MENU_REMOVE_FROM_DICT,
	EDIT_MENU_SUGGESTION,
	EDIT_MENU_SUGGESTIONS,
	EDIT_MENU_THESAURUS = (wxID_HIGHEST + 1) + 5000,
	EDIT_MENU_THESAURUS_SUGS,
	EDIT_MENU_DIC_LANGUAGE = (wxID_HIGHEST + 1) + 6000,
	EDIT_MENU_DIC_LANGS,
	EDIT_MENU_THES_LANGUAGE = EDIT_MENU_DIC_LANGUAGE + LANGS_MAX,
	EDIT_MENU_THES_LANGS
};

SubsEditCtrlBase *CreateSubsEditCtrl(wxWindow *parent, wxSize size, long style, agi::Context *context) {
	return new SubsTextEditCtrl(parent, size, style, context);
}

SubsEditCtrlBase::SubsEditCtrlBase(agi::Context *context)
: spellchecker(SpellCheckerFactory::GetSpellChecker())
, thesaurus(std::make_unique<Thesaurus>())
, context(context)
{
}

SubsEditCtrlBase::~SubsEditCtrlBase() {
}

void SubsEditCtrlBase::BindContextMenu() {
	wxWindow *w = AsWindow();

	w->Bind(wxEVT_MENU, [this](wxCommandEvent&) { AsTextEntry()->Cut(); }, EDIT_MENU_CUT);
	w->Bind(wxEVT_MENU, [this](wxCommandEvent&) { AsTextEntry()->Copy(); }, EDIT_MENU_COPY);
	w->Bind(wxEVT_MENU, [this](wxCommandEvent&) { AsTextEntry()->Paste(); }, EDIT_MENU_PASTE);
	w->Bind(wxEVT_MENU, [this](wxCommandEvent&) { AsTextEntry()->SelectAll(); }, EDIT_MENU_SELECT_ALL);

	if (context) {
		w->Bind(wxEVT_MENU, std::bind(&cmd::call, "edit/line/split/preserve", context), EDIT_MENU_SPLIT_PRESERVE);
		w->Bind(wxEVT_MENU, std::bind(&cmd::call, "edit/line/split/estimate", context), EDIT_MENU_SPLIT_ESTIMATE);
		w->Bind(wxEVT_MENU, std::bind(&cmd::call, "edit/line/split/video", context), EDIT_MENU_SPLIT_VIDEO);
	}

	w->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		if (spellchecker) spellchecker->AddWord(currentWord);
		UpdateStyle();
		AsWindow()->SetFocus();
	}, EDIT_MENU_ADD_TO_DICT);

	w->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		if (spellchecker) spellchecker->RemoveWord(currentWord);
		UpdateStyle();
		AsWindow()->SetFocus();
	}, EDIT_MENU_REMOVE_FROM_DICT);

	w->Bind(wxEVT_MENU, &SubsEditCtrlBase::OnUseSuggestion, this, EDIT_MENU_SUGGESTIONS, EDIT_MENU_THESAURUS - 1);
	w->Bind(wxEVT_MENU, &SubsEditCtrlBase::OnUseSuggestion, this, EDIT_MENU_THESAURUS_SUGS, EDIT_MENU_DIC_LANGUAGE - 1);
	w->Bind(wxEVT_MENU, &SubsEditCtrlBase::OnSetDicLanguage, this, EDIT_MENU_DIC_LANGS, EDIT_MENU_THES_LANGUAGE - 1);
	w->Bind(wxEVT_MENU, &SubsEditCtrlBase::OnSetThesLanguage, this, EDIT_MENU_THES_LANGS, EDIT_MENU_THES_LANGS + LANGS_MAX);
}

void SubsEditCtrlBase::UpdateTokens() {
	AssDialogue *diag = context ? context->selectionController->GetActiveLine() : nullptr;
	bool template_line = diag && diag->Comment && boost::istarts_with(diag->Effect.get(), "template");

	tokenized_line = agi::ass::TokenizeDialogueBody(line_text, template_line);
	agi::ass::SplitWords(line_text, tokenized_line);
}

void SubsEditCtrlBase::InsertAtSelection(std::string const& text) {
	std::string old = GetTextUtf8();
	int start = GetSelectionStartBytes(), end = GetSelectionEndBytes();
	if (start > end) std::swap(start, end);

	std::string data(old, 0, start);
	data += text;
	int caret = static_cast<int>(data.size());
	data.append(old, end, std::string::npos);

	ReplaceTextUtf8(data);
	SetSelectionStartBytes(caret);
	SetSelectionEndBytes(caret);
}

void SubsEditCtrlBase::PasteClipboard() {
	std::string data = GetClipboard();

	boost::replace_all(data, "\r\n", "\\N");
	boost::replace_all(data, "\n", "\\N");
	boost::replace_all(data, "\r", "\\N");

	InsertAtSelection(data);
}

void SubsEditCtrlBase::ShowContextMenu(int activePos) {
	currentWordPos = GetBoundsOfWordAtPosition(activePos);
	currentWord = line_text.substr(currentWordPos.first, currentWordPos.second);

	wxMenu menu;
	if (spellchecker) {
		AddSpellCheckerEntries(menu);

		// Append language list
		menu.Append(-1, _("Spell checker language"), GetLanguagesMenu(
			EDIT_MENU_DIC_LANGS,
			to_wx(OPT_GET("Tool/Spell Checker/Language")->GetString()),
			to_wx(spellchecker->GetLanguageList())));
		menu.AppendSeparator();
	}

	AddThesaurusEntries(menu);

	// Standard actions
	bool has_selection = GetSelectionStartBytes() != GetSelectionEndBytes();
	menu.Append(EDIT_MENU_CUT,_("Cu&t"))->Enable(has_selection);
	menu.Append(EDIT_MENU_COPY,_("&Copy"))->Enable(has_selection);
	menu.Append(EDIT_MENU_PASTE,_("&Paste"))->Enable(AsTextEntry()->CanPaste());
	menu.AppendSeparator();
	menu.Append(EDIT_MENU_SELECT_ALL,_("Select &All"));

	// Split
	if (context) {
		menu.AppendSeparator();
		menu.Append(EDIT_MENU_SPLIT_PRESERVE, _("Split at cursor (preserve times)"));
		menu.Append(EDIT_MENU_SPLIT_ESTIMATE, _("Split at cursor (estimate times)"));
		cmd::Command *split_video = cmd::get("edit/line/split/video");
		menu.Append(EDIT_MENU_SPLIT_VIDEO, split_video->StrMenu(context))->Enable(split_video->Validate(context));
	}

	AsWindow()->PopupMenu(&menu);
}

void SubsEditCtrlBase::AddSpellCheckerEntries(wxMenu &menu) {
	if (currentWord.empty()) return;

	if (spellchecker->CanRemoveWord(currentWord))
		menu.Append(EDIT_MENU_REMOVE_FROM_DICT, fmt_tl("Remove \"%s\" from dictionary", currentWord));

	sugs = spellchecker->GetSuggestions(currentWord);
	if (spellchecker->CheckWord(currentWord)) {
		if (sugs.empty())
			menu.Append(EDIT_MENU_SUGGESTION,_("No spell checker suggestions"))->Enable(false);
		else {
			auto subMenu = new wxMenu;
			for (size_t i = 0; i < sugs.size(); ++i)
				subMenu->Append(EDIT_MENU_SUGGESTIONS+i, to_wx(sugs[i]));

			menu.Append(-1, fmt_tl("Spell checker suggestions for \"%s\"", currentWord), subMenu);
		}
	}
	else {
		if (sugs.empty())
			menu.Append(EDIT_MENU_SUGGESTION,_("No correction suggestions"))->Enable(false);

		for (size_t i = 0; i < sugs.size(); ++i)
			menu.Append(EDIT_MENU_SUGGESTIONS+i, to_wx(sugs[i]));

		// Append "add word"
		menu.Append(EDIT_MENU_ADD_TO_DICT, fmt_tl("Add \"%s\" to dictionary", currentWord))->Enable(spellchecker->CanAddWord(currentWord));
	}
}

void SubsEditCtrlBase::AddThesaurusEntries(wxMenu &menu) {
	if (currentWord.empty()) return;

	auto results = thesaurus->Lookup(currentWord);

	thesSugs.clear();

	if (results.size()) {
		auto thesMenu = new wxMenu;

		int curThesEntry = 0;
		for (auto const& result : results) {
			// Single word, insert directly
			if (result.second.empty()) {
				thesMenu->Append(EDIT_MENU_THESAURUS_SUGS+curThesEntry, to_wx(result.first));
				thesSugs.push_back(result.first);
				++curThesEntry;
			}
			// Multiple, create submenu
			else {
				auto subMenu = new wxMenu;
				for (auto const& sug : result.second) {
					subMenu->Append(EDIT_MENU_THESAURUS_SUGS+curThesEntry, to_wx(sug));
					thesSugs.push_back(sug);
					++curThesEntry;
				}

				thesMenu->Append(-1, to_wx(result.first), subMenu);
			}
		}

		menu.Append(-1, fmt_tl("Thesaurus suggestions for \"%s\"", currentWord), thesMenu);
	}
	else
		menu.Append(EDIT_MENU_THESAURUS,_("No thesaurus suggestions"))->Enable(false);

	// Append language list
	menu.Append(-1,_("Thesaurus language"), GetLanguagesMenu(
		EDIT_MENU_THES_LANGS,
		to_wx(OPT_GET("Tool/Thesaurus/Language")->GetString()),
		to_wx(thesaurus->GetLanguageList())));
	menu.AppendSeparator();
}

wxMenu *SubsEditCtrlBase::GetLanguagesMenu(int base_id, wxString const& curLang, wxArrayString const& langs) {
	auto languageMenu = new wxMenu;
	languageMenu->AppendRadioItem(base_id, _("Disable"))->Check(curLang.empty());

	for (size_t i = 0; i < langs.size(); ++i)
		languageMenu->AppendRadioItem(base_id + i + 1, LocalizedLanguageName(langs[i]))->Check(langs[i] == curLang);

	return languageMenu;
}

void SubsEditCtrlBase::OnUseSuggestion(wxCommandEvent &event) {
	std::string suggestion;
	int sugIdx = event.GetId() - EDIT_MENU_THESAURUS_SUGS;
	if (sugIdx >= 0)
		suggestion = thesSugs[sugIdx];
	else
		suggestion = sugs[event.GetId() - EDIT_MENU_SUGGESTIONS];

	size_t pos;
	while ((pos = suggestion.rfind('(')) != std::string::npos) {
		// If there's only one suggestion for a word it'll be in the form "(noun) word",
		// so we need to trim the "(noun) " part
		if (pos == 0) {
			pos = suggestion.find(')');
			if (pos != std::string::npos) {
				if (pos + 1< suggestion.size() && suggestion[pos + 1] == ' ') ++pos;
				suggestion.erase(0, pos + 1);
			}
			break;
		}

		// Some replacements have notes about their usage after the word in the
		// form "word (generic term)" that we need to remove (plus the leading space)
		suggestion.resize(pos - 1);
	}

	std::string new_text = GetTextUtf8();
	ReplaceTextUtf8(new_text.replace(currentWordPos.first, currentWordPos.second, suggestion));

	SetSelectionStartBytes(currentWordPos.first);
	SetSelectionEndBytes(currentWordPos.first + static_cast<int>(suggestion.size()));
	AsWindow()->SetFocus();
}

void SubsEditCtrlBase::OnSetDicLanguage(wxCommandEvent &event) {
	std::vector<std::string> langs = spellchecker->GetLanguageList();

	int index = event.GetId() - EDIT_MENU_DIC_LANGS - 1;
	std::string lang;
	if (index >= 0)
		lang = langs[index];

	OPT_SET("Tool/Spell Checker/Language")->SetString(lang);

	UpdateStyle();
}

void SubsEditCtrlBase::OnSetThesLanguage(wxCommandEvent &event) {
	if (!thesaurus) return;

	std::vector<std::string> langs = thesaurus->GetLanguageList();

	int index = event.GetId() - EDIT_MENU_THES_LANGS - 1;
	std::string lang;
	if (index >= 0) lang = langs[index];
	OPT_SET("Tool/Thesaurus/Language")->SetString(lang);

	UpdateStyle();
}

std::pair<int, int> SubsEditCtrlBase::GetBoundsOfWordAtPosition(int pos) {
	int len = 0;
	for (auto const& tok : tokenized_line) {
		if (len + (int)tok.length > pos) {
			if (tok.type == agi::ass::DialogueTokenType::WORD)
				return {len, tok.length};
			return {0, 0};
		}
		len += tok.length;
	}

	return {0, 0};
}
