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

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace agi {
	class SpellChecker;
	struct Context;
	namespace ass { struct DialogueToken; }
}
class Thesaurus;
class wxArrayString;
class wxCommandEvent;
class wxKeyEvent;
class wxMenu;
class wxSize;
class wxString;
class wxTextEntryBase;
class wxWindow;

/// @class SubsEditCtrlBase
/// @brief Shared part of the subtitle text editors
///
/// The interface SubsEditBox, DialogTranslation and TextSelectionController
/// drive the editor through, and what editor implementations have in common:
/// tokenizing, spell checking, the thesaurus and the context menu.
///
/// All offsets are UTF-8 **byte** offsets, matching AssDialogue::Text and the
/// rest of Aegisub.
class SubsEditCtrlBase {
	/// Add the thesaurus suggestions to a menu
	void AddThesaurusEntries(wxMenu &menu);

	/// Add the spell checker suggestions to a menu
	void AddSpellCheckerEntries(wxMenu &menu);

	/// Generate a languages submenu from a list of locales and a current language
	/// @param base_id ID to use for the first menu item
	/// @param curLang Currently selected language
	/// @param lang Full list of languages
	wxMenu *GetLanguagesMenu(int base_id, wxString const& curLang, wxArrayString const& langs);

	void OnUseSuggestion(wxCommandEvent &event);
	void OnSetDicLanguage(wxCommandEvent &event);
	void OnSetThesLanguage(wxCommandEvent &event);

protected:
	/// Backend spellchecker to use
	std::unique_ptr<agi::SpellChecker> spellchecker;

	/// Backend thesaurus to use
	std::unique_ptr<Thesaurus> thesaurus;

	/// Project context, for splitting lines. May be null.
	agi::Context *context;

	/// The word right-clicked on, used for spellchecker replacing
	std::string currentWord;

	/// The beginning of the word right-clicked on, for spellchecker replacing
	std::pair<int, int> currentWordPos;

	/// Spellchecker suggestions for the last right-clicked word
	std::vector<std::string> sugs;

	/// Thesaurus suggestions for the last right-clicked word
	std::vector<std::string> thesSugs;

	/// The last seen line text, used to avoid reparsing the line for syntax
	/// highlighting when possible
	std::string line_text;

	/// Tokenized version of line_text
	std::vector<agi::ass::DialogueToken> tokenized_line;

	SubsEditCtrlBase(agi::Context *context);

	/// Retokenize line_text and split it into words for the spell checker
	void UpdateTokens();

	/// Bind the context menu's handlers. Call at the end of the derived
	/// constructor, as it needs AsWindow().
	void BindContextMenu();

	/// Show the context menu for the word at the given position
	void ShowContextMenu(int pos);

	/// Insert the clipboard at the selection, converting newlines to \N
	void PasteClipboard();

	/// Get the start and length of the word at the given position
	std::pair<int, int> GetBoundsOfWordAtPosition(int pos);

	/// Replace the text as if typed by the user, notifying on_text_modified
	virtual void ReplaceTextUtf8(std::string const& text) = 0;

	/// Reapply syntax highlighting and spell checking
	virtual void UpdateStyle() = 0;

	virtual wxTextEntryBase *AsTextEntry() = 0;

public:
	virtual ~SubsEditCtrlBase();

	/// The control as a window, for sizers and event binding
	virtual wxWindow *AsWindow() = 0;

	virtual void SetTextTo(std::string const& text) = 0;
	virtual std::string GetTextUtf8() const = 0;

	/// Give the platform IME a chance to consume the key event
	virtual bool ProcessImeKeyEvent(wxKeyEvent &) { return false; }

	virtual int GetInsertionPointBytes() const = 0;
	virtual void SetInsertionPointBytes(int pos) = 0;
	virtual int GetSelectionStartBytes() const = 0;
	virtual int GetSelectionEndBytes() const = 0;
	virtual void SetSelectionStartBytes(int pos) = 0;
	virtual void SetSelectionEndBytes(int pos) = 0;

	/// Replace the selection with text as a user edit, leaving the caret after it
	void InsertAtSelection(std::string const& text);

	/// Caret or selection moved. The flag is false when only the caret moved,
	/// in which case the selection collapses to the insertion point.
	std::function<void (bool selection_updated)> on_selection_changed;

	/// Text was edited. The flag marks the start of a new undo action.
	std::function<void (bool start_of_action)> on_text_modified;
};

/// Create the subtitle text editor
SubsEditCtrlBase *CreateSubsEditCtrl(wxWindow *parent, wxSize size, long style, agi::Context *context);
