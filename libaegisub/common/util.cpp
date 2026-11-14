// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "libaegisub/util.h"
#include "libaegisub/util_osx.h"

#include "libaegisub/exception.h"

#include <boost/locale/boundary.hpp>
#include <boost/locale/conversion.hpp>
#include <boost/locale/util.hpp>
#include <boost/range/distance.hpp>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <thread>
#include <unicode/edits.h>
#include <unicode/casemap.h>
#include <unicode/ucasemap.h>
#include <unicode/bytestream.h>
#include <unicode/locid.h>

#include <unicode/uchar.h>
#include <unicode/utf8.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {
const size_t bad_pos = (size_t)-1;
const std::pair bad_match(bad_pos, bad_pos);

std::pair<size_t, size_t> find_range(std::string const& haystack, std::string const& needle, size_t start = 0) {
	const size_t match_start = haystack.find(needle, start);
	if (match_start == std::string::npos)
		return bad_match;
	return {match_start, match_start + needle.size()};
}

void parse_blocks(std::vector<std::pair<size_t, size_t>>& blocks, std::string const& str) {
	blocks.clear();

	size_t ovr_start = bad_pos;
	size_t i = 0;
	for (auto const& c : str) {
		if (c == '{' && ovr_start == bad_pos)
			ovr_start = i;
		else if (c == '}' && ovr_start != bad_pos) {
			blocks.emplace_back(ovr_start, i + 1);
			ovr_start = bad_pos;
		}
		++i;
	}
}

} // anonymous namespace

namespace agi { namespace util {

std::string strftime(const char *fmt, const tm *tmptr) {
	if (!tmptr) {
		time_t t = time(nullptr);
		tmptr = localtime(&t);
	}

	char buff[65536];
	::strftime(buff, sizeof buff, fmt, tmptr);
	return buff;
}

std::string fold_case(std::string_view str, icu::Edits *edits) {
	if (str.size() > std::numeric_limits<int32_t>::max())
		throw InvalidInputException("String is too long for case folding");
	auto size = static_cast<int32_t>(str.size());
	UErrorCode err = U_ZERO_ERROR;
	std::string ret;
	icu::StringByteSink sink(&ret, size);
	icu::CaseMap::utf8Fold(0, icu::StringPiece(str.data(), size), sink, edits, err);
	if (U_FAILURE(err)) throw InvalidInputException(u_errorName(err));
	return ret;
}

std::pair<size_t, size_t> ifind(std::string const& haystack, std::string const& needle) {
	icu::Edits edits;
	const auto folded_hs = fold_case(haystack, &edits);
	const auto folded_n = fold_case(needle, nullptr);
	auto it = edits.getFineIterator();
	size_t pos = 0;
	while (true) {
		auto match = find_range(folded_hs, folded_n, pos);
		if (match == bad_match || !edits.hasChanges())
			return match;
		UErrorCode err = U_ZERO_ERROR;

		int32_t first_raw = it.sourceIndexFromDestinationIndex(static_cast<int32_t>(match.first), err);
		int32_t second_raw = it.sourceIndexFromDestinationIndex(static_cast<int32_t>(match.second), err);

		bool good_match = it.destinationIndexFromSourceIndex(first_raw, err) == static_cast<int32_t>(match.first) && it.destinationIndexFromSourceIndex(second_raw, err) == static_cast<int32_t>(match.second);

		if (U_FAILURE(err)) throw InvalidInputException(u_errorName(err));

		if (good_match)
			return {first_raw, second_raw};

		pos = match.first + 1;
	}
}

std::string tagless_find_helper::strip_tags(std::string const& str, size_t s) {
	parse_blocks(blocks, str);

	std::string out;

	size_t last = s;
	for (auto const& block : blocks) {
		if (block.second <= s) continue;
		if (block.first > last)
			out.append(str.begin() + last, str.begin() + block.first);
		last = block.second;
	}

	if (last < str.size())
		out.append(str.begin() + last, str.end());

	start = s;
	return out;
}

void tagless_find_helper::map_range(size_t &s, size_t &e) {
	s += start;
	e += start;

	// Shift the start and end of the match to be relative to the unstripped
	// match
	for (auto const& block : blocks) {
		// Any blocks before start are irrelevant as they're included in `start`
		if (block.second <= start) continue;
		// Skip over blocks at the very beginning of the match
		// < should only happen if the cursor was within an override block
		// when the user started a search
		if (block.first <= s) {
			size_t len = block.second - std::max(block.first, start);
			s += len;
			e += len;
			continue;
		}

		assert(block.first > s);
		// Blocks after the match are irrelevant
		if (block.first >= e) break;

		// Extend the match to include blocks within the match
		// Note that blocks cannot be partially within the match
		e += block.second - block.first;
	}
}

std::string ThreadId() {
	auto impl = [&]() {
		auto id = std::this_thread::get_id();

		std::ostringstream stream;

#ifdef _WIN32
		stream << GetCurrentProcessId();
		stream << "-";
#elif defined(__linux__) || defined(__APPLE__)
		stream << getpid();
		stream << "-";
#endif
		stream << id;	// For some reason thread ids can only be serialized like this
		return stream.str();
	};

	thread_local std::string result = impl();
	return result;
}

void InitLocale() {
	auto id = boost::locale::util::get_system_locale(true);
	UErrorCode err = U_ZERO_ERROR;
	icu::Locale::setDefault(icu::Locale::createCanonical(id.c_str()), err);
	if (U_FAILURE(err)) throw InternalError(u_errorName(err));

	// Try to get the UTF-8 version of the current locale
	auto locale = boost::locale::generator().generate("");

	// Check if we actually got a UTF-8 locale
	using codecvt = std::codecvt<wchar_t, char, std::mbstate_t>;
	int result = std::codecvt_base::error;
	if (std::has_facet<codecvt>(locale)) {
		wchar_t test[] = L"\xFFFE";
		char buff[8];
		auto mb = std::mbstate_t();
		const wchar_t* from_next;
		char* to_next;
		result = std::use_facet<codecvt>(locale).out(mb,
			test, std::end(test), from_next,
			buff, std::end(buff), to_next);
	}

	// If we didn't get a UTF-8 locale, force it to a known one
	// FIXME: Should the ICU locale also be forced to en_US here?
	if (result != std::codecvt_base::ok)
		locale = boost::locale::generator().generate("en_US.UTF-8");

	// Use the standard library's number parsing and formatting rather than
	// Boost.Locale's, which nothing relies on. Boost.Locale's facets fall back
	// to std::num_get code inlined from the SDK's libc++ headers, and with
	// newer SDKs that code overflows a stack buffer when it calls into an
	// older system libc++ (e.g. on macOS 13).
	locale = std::locale(locale, std::locale::classic(), std::locale::numeric);
	std::locale::global(locale);
}

bool is_rtl(std::string const& str) {
	const char *data = str.c_str();
	const int32_t len = static_cast<int32_t>(str.size());

	for (int32_t i = 0; i < len; ) {
		// Skip override blocks; an unterminated '{' comments out the rest of the line
		if (data[i] == '{') {
			int32_t close = i + 1;
			while (close < len && data[close] != '}') ++close;
			if (close == len) return false;
			i = close + 1;
			continue;
		}

		UChar32 c;
		U8_NEXT(data, i, len, c);
		if (c < 0) continue;

		// Skip \N \n \h so that the escape letter isn't taken for a strong
		// left-to-right character. Other backslashes are just backslashes.
		if (c == '\\' && i < len && (data[i] == 'N' || data[i] == 'n' || data[i] == 'h')) {
			++i;
			continue;
		}

		switch (u_charDirection(c)) {
		case U_LEFT_TO_RIGHT:
			return false;
		case U_RIGHT_TO_LEFT:
		case U_RIGHT_TO_LEFT_ARABIC:
			return true;
		default:
			break;
		}
	}

	return false;
}

namespace {
const char rle[] = "\xE2\x80\xAB";                 // U+202B RIGHT-TO-LEFT EMBEDDING
const char rlm_pdf[] = "\xE2\x80\x8F\xE2\x80\xAC"; // U+200F RIGHT-TO-LEFT MARK, U+202C POP DIRECTIONAL FORMATTING
constexpr auto npos = std::string::npos;

std::string embed_if_rtl(std::string const& text) {
	return is_rtl(text) ? rle + text + rlm_pdf : text;
}

/// Where the text after the override blocks at the start of a line begins,
/// or npos if one of them is unterminated
size_t text_start(std::string const& text) {
	size_t start = 0;
	while (start < text.size() && text[start] == '{') {
		size_t close = text.find('}', start);
		if (close == npos) return npos;
		start = close + 1;
	}
	return start;
}

/// Apply f to each \N part of the text after start
template<typename Func>
std::string map_parts(std::string const& text, size_t start, Func f) {
	std::string out = text.substr(0, start);
	for (;;) {
		size_t end = text.find("\\N", start);
		out += f(text.substr(start, end == npos ? npos : end - start));
		if (end == npos) return out;
		out += "\\N";
		start = end + 2;
	}
}

/// Is tag (without its backslash) the override tag name, followed by its value?
bool tag_is(std::string_view tag, std::string_view name) {
	if (tag.substr(0, name.size()) != name) return false;
	if (tag.size() == name.size()) return true;
	char c = tag[name.size()];
	return (c >= '0' && c <= '9') || c == '-' || c == '(';
}

int tag_value(std::string_view tag, std::string_view name) {
	return std::atoi(std::string(tag.substr(name.size())).c_str());
}

/// Split the inside of an override block into tags, keeping \t(\...) whole
std::vector<std::string_view> split_tags(std::string_view block) {
	std::vector<std::string_view> tags;
	size_t begin = npos;
	int depth = 0;
	for (size_t i = 0; i < block.size(); ++i) {
		if (block[i] == '(') ++depth;
		else if (block[i] == ')' && depth) --depth;
		else if (block[i] == '\\' && !depth) {
			if (begin != npos && i > begin) tags.push_back(block.substr(begin, i - begin));
			begin = i + 1;
		}
	}
	if (begin != npos && begin < block.size()) tags.push_back(block.substr(begin));
	return tags;
}

struct Run {
	std::string style; ///< Every style override tag in effect, in order
	std::string kara;  ///< \kt and \k tags of the karaoke syllable the run is in
	std::string text;
};

/// Store the style runs of right-to-left lines last to first, as renderers
/// place runs left-to-right whatever the marks inside them say. Karaoke
/// syllables get their start time with \kt so that they still play in order.
std::string reorder_runs(std::string const& text) {
	std::string globals, style, syllable;
	bool restyled = false, karaoke = false;
	int ktime = 0;
	std::vector<std::vector<Run>> parts(1);

	for (size_t i = 0; i < text.size(); ) {
		if (text[i] == '{') {
			size_t close = text.find('}', i);
			if (close == npos) return text;
			for (auto tag : split_tags(std::string_view(text).substr(i + 1, close - i - 1))) {
				// Drawings and per-letter spacing can't be helped, and \fe-1
				// already makes libass lay the line out right-to-left
				if ((tag_is(tag, "p") && tag_value(tag, "p")) || (tag_is(tag, "fsp") && tag_value(tag, "fsp"))
					|| (tag_is(tag, "fe") && tag_value(tag, "fe") == -1))
					return text;

				std::string_view kname;
				for (std::string_view name : {"kf", "ko", "kt", "k", "K"}) {
					if (tag_is(tag, name)) { kname = name; break; }
				}
				if (kname == "kt")
					ktime = tag_value(tag, kname);
				else if (!kname.empty()) {
					int duration = tag_value(tag, kname);
					syllable = "\\kt" + std::to_string(ktime) + "\\" + std::string(kname) + std::to_string(duration);
					ktime += duration;
					karaoke = true;
				}
				else if (tag_is(tag, "pos") || tag_is(tag, "move") || tag_is(tag, "org") || tag_is(tag, "clip")
					|| tag_is(tag, "iclip") || tag_is(tag, "fad") || tag_is(tag, "fade") || tag_is(tag, "an")
					|| tag_is(tag, "a") || tag_is(tag, "q"))
					globals += "\\" + std::string(tag);
				else {
					style += "\\" + std::string(tag);
					restyled = true;
				}
			}
			i = close + 1;
			continue;
		}

		size_t end = std::min(text.find('{', i), text.size());
		for (;;) {
			size_t brk = std::min(text.find("\\N", i), end);
			parts.back().push_back({style, syllable, text.substr(i, brk - i)});
			if (brk == end) break;
			parts.emplace_back();
			i = brk + 2;
		}
		i = end;
	}

	auto block = [&](Run const& run) {
		std::string tags;
		if (restyled) tags = "\\r" + run.style;
		// Text before the first syllable must not join the syllable before it
		if (karaoke) tags += run.kara.empty() ? "\\kt0\\k0" : run.kara;
		return tags.empty() ? tags : "{" + tags + "}";
	};
	auto lead_end = [](std::string const& s) { return std::min(s.find_first_not_of(' '), s.size()); };
	auto trail_begin = [](std::string const& s) { size_t e = s.find_last_not_of(' '); return e == npos ? s.size() : e + 1; };

	std::string body;
	bool any_rtl = false, reversed = false;
	for (auto& runs : parts) {
		if (&runs != &parts.front()) body += "\\N";
		runs.erase(std::remove_if(runs.begin(), runs.end(), [](Run const& r) { return r.text.empty(); }), runs.end());
		std::string plain;
		for (auto const& run : runs) plain += run.text;
		bool rtl = is_rtl(plain);
		any_rtl |= rtl;

		if (runs.size() < 2 || !rtl || !(restyled || karaoke)) {
			if (runs.size() == 1 || !(restyled || karaoke)) {
				if (!runs.empty()) body += block(runs[0]) + embed_if_rtl(plain);
			}
			else {
				for (auto const& run : runs) body += block(run) + run.text;
			}
			continue;
		}

		// Reverse the runs, embedding each, with the spaces between them
		// outside the embeddings so that they stay between the runs
		reversed = true;
		body += runs.back().text.substr(trail_begin(runs.back().text));
		for (size_t j = runs.size(); j-- > 0; ) {
			auto const& s = runs[j].text;
			size_t b = lead_end(s), e = std::max(trail_begin(s), b);
			body += block(runs[j]);
			if (e > b) body += rle + s.substr(b, e - b) + rlm_pdf;
			if (j > 0) body += s.substr(0, b) + runs[j - 1].text.substr(trail_begin(runs[j - 1].text));
		}
		body += runs.front().text.substr(0, lead_end(runs.front().text));
	}

	if (!any_rtl) return text;
	// Wrapping would break reversed lines in the wrong place
	if (reversed) globals += "\\q2";
	return (globals.empty() ? "" : "{" + globals + "}") + body;
}
}

std::string add_rtl_marks(std::string const& text) {
	size_t start = text_start(text);
	if (start == npos || text.find(rle) != npos || text.find('{', start) != npos) return text;
	return map_parts(text, start, embed_if_rtl);
}

std::string reorder_rtl_runs(std::string const& text) {
	size_t start = text_start(text);
	// \q2 would turn soft line breaks into hard ones
	if (start == npos || text.find(rle) != npos || text.find('{', start) == npos || text.find("\\n") != npos)
		return text;
	return reorder_runs(text);
}

std::string remove_rtl_marks(std::string const& text) {
	size_t start = text_start(text);
	if (start == npos || text.find(rle) == npos || text.find('{', start) != npos) return text;
	return map_parts(text, start, [](std::string part) {
		constexpr size_t rle_len = sizeof(rle) - 1, rlm_pdf_len = sizeof(rlm_pdf) - 1;
		if (part.size() >= rle_len + rlm_pdf_len && part.starts_with(rle) && part.ends_with(rlm_pdf)) {
			// Only marks that add_rtl_marks would add back
			auto inner = part.substr(rle_len, part.size() - rle_len - rlm_pdf_len);
			if (is_rtl(inner)) return inner;
		}
		return part;
	});
}
} // namespace util

#ifndef __APPLE__
namespace osx {
AppNapDisabler::AppNapDisabler(std::string) { }
AppNapDisabler::~AppNapDisabler() { }
}
#endif
} // namespace agi
