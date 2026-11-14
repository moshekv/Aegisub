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

#include <libaegisub/util.h>

#include <main.h>

#include <cstdlib>
#include <locale>
#include <typeinfo>

class lagi_util : public libagi { };

namespace agi {
TEST(lagi_util, get_env) {
	EXPECT_FALSE(util::GetEnv("AEGISUB_TEST_UNSET_VARIABLE").has_value());

	// Non-ASCII, to check that the value is converted to UTF-8 on Windows
#ifdef _WIN32
	_wputenv_s(L"AEGISUB_TEST_GET_ENV", L"\u00e9\u65e5");
#else
	setenv("AEGISUB_TEST_GET_ENV", "\xc3\xa9\xe6\x97\xa5", 1);
#endif
	auto value = util::GetEnv("AEGISUB_TEST_GET_ENV");
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ("\xc3\xa9\xe6\x97\xa5", *value);
}

TEST(lagi_util, try_parse_double) {
	double d = 0.0;
	EXPECT_TRUE(util::try_parse("1.0", &d));
	EXPECT_EQ(1.0, d);

	EXPECT_FALSE(util::try_parse("aaa", &d));
	EXPECT_EQ(1.0, d);

	EXPECT_FALSE(util::try_parse("2aaa", &d));
	EXPECT_EQ(1.0, d);
}

TEST(lagi_util, try_parse_int) {
	int i = 0;
	EXPECT_TRUE(util::try_parse("1", &i));
	EXPECT_EQ(1, i);

	EXPECT_FALSE(util::try_parse("2.0", &i));
	EXPECT_EQ(1.0, i);
}

TEST(lagi_util, init_locale_uses_std_numeric_facets) {
	// Boost.Locale's numeric facets crash on older macOS when built with a
	// newer SDK (#733), so InitLocale must not install them.

	// These need to be written into variables first to shut up a compiler warning.
	decltype(auto) get_facet = std::use_facet<std::num_get<char>>(std::locale());
	decltype(auto) put_facet = std::use_facet<std::num_put<char>>(std::locale());

	EXPECT_EQ(typeid(std::num_get<char>), typeid(get_facet));
	EXPECT_EQ(typeid(std::num_put<char>), typeid(put_facet));
}

TEST(lagi_util, is_rtl) {
	// Strong right-to-left first character
	EXPECT_TRUE(util::is_rtl("\u05e9\u05dc\u05d5\u05dd, \u05e2\u05d5\u05dc\u05dd."));
	EXPECT_TRUE(util::is_rtl("\u0645\u0631\u062d\u0628\u0627"));

	// Strong left-to-right first character
	EXPECT_FALSE(util::is_rtl("Hello, world."));
	EXPECT_FALSE(util::is_rtl("Mixed \u05e9\u05dc\u05d5\u05dd English, ok?"));

	// No strong character at all
	EXPECT_FALSE(util::is_rtl(""));
	EXPECT_FALSE(util::is_rtl("1234 -- 5678"));
	EXPECT_FALSE(util::is_rtl("   "));

	// Override blocks are skipped, so the tag letters don't count as LTR
	EXPECT_TRUE(util::is_rtl("{\\i1}\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd{\\i0}"));
	EXPECT_TRUE(util::is_rtl("{\\an8\\pos(10,20)}\u05e9\u05dc\u05d5\u05dd"));
	EXPECT_FALSE(util::is_rtl("{\\an8}Hello"));

	// An unterminated block comments out the rest of the line
	EXPECT_FALSE(util::is_rtl("{\\i1\u05e9\u05dc\u05d5\u05dd"));

	// \N \n \h escapes are skipped rather than read as strong LTR letters
	EXPECT_TRUE(util::is_rtl("\\N\u05e9\u05dc\u05d5\u05dd"));
	EXPECT_TRUE(util::is_rtl("\\h\u05e9\u05dc\u05d5\u05dd"));
	EXPECT_FALSE(util::is_rtl("\\NHello"));

	// Other backslashes are literal, so don't hide the next character
	EXPECT_TRUE(util::is_rtl("\\\u05e9\u05dc\u05d5\u05dd"));
	EXPECT_TRUE(util::is_rtl("\\\u05e9Hello"));

	// Direction comes from the first strong character, not a later one
	EXPECT_TRUE(util::is_rtl("\u05e9\u05dc\u05d5\u05dd Hello"));
	EXPECT_FALSE(util::is_rtl("Hello \u05e9\u05dc\u05d5\u05dd"));
}

#define RLE "\u202b"
#define RLM_PDF "\u200f\u202c"
#define SHALOM "\u05e9\u05dc\u05d5\u05dd"
#define OLAM "\u05e2\u05d5\u05dc\u05dd"

TEST(lagi_util, add_rtl_marks) {
	EXPECT_EQ(RLE SHALOM "!" RLM_PDF, util::add_rtl_marks(SHALOM "!"));

	// Leading override blocks stay first
	EXPECT_EQ("{\\an8}{\\i1}" RLE SHALOM RLM_PDF, util::add_rtl_marks("{\\an8}{\\i1}" SHALOM));

	// Each \N part is marked on its own, and only if it is right-to-left
	EXPECT_EQ(RLE SHALOM "!" RLM_PDF "\\NHello!", util::add_rtl_marks(SHALOM "!\\NHello!"));
	EXPECT_EQ("Hello\\N" RLE SHALOM RLM_PDF, util::add_rtl_marks("Hello\\N" SHALOM));

	// Left unchanged: left-to-right, empty, several style runs, unterminated block
	EXPECT_EQ("Hello!", util::add_rtl_marks("Hello!"));
	EXPECT_EQ("", util::add_rtl_marks(""));
	EXPECT_EQ(SHALOM " {\\i1}" OLAM, util::add_rtl_marks(SHALOM " {\\i1}" OLAM));
	EXPECT_EQ("{\\i1" SHALOM, util::add_rtl_marks("{\\i1" SHALOM));

	// Idempotent
	auto once = util::add_rtl_marks(SHALOM "\\N" OLAM);
	EXPECT_EQ(once, util::add_rtl_marks(once));
}

TEST(lagi_util, remove_rtl_marks) {
	// Undoes add_rtl_marks
	for (const char *text : {SHALOM "!", "{\\an8}- " SHALOM "\\NHello!\\N" OLAM "?", "Hello!", "",
	                         SHALOM " {\\i1}" OLAM, "{\\i1" SHALOM}) {
		EXPECT_EQ(text, util::remove_rtl_marks(util::add_rtl_marks(text)));
	}

	// and leaves marks it didn't add alone
	EXPECT_EQ(RLE "123!" RLM_PDF, util::remove_rtl_marks(RLE "123!" RLM_PDF));
	EXPECT_EQ(RLE SHALOM "\u202c", util::remove_rtl_marks(RLE SHALOM "\u202c"));
	EXPECT_EQ("\u200f" SHALOM "!\u200f", util::remove_rtl_marks("\u200f" SHALOM "!\u200f"));
}

#define YAFE "\u05d9\u05e4\u05d4"

TEST(lagi_util, reorder_rtl_runs) {
	// Karaoke syllables are stored last to first, timed with \kt, with the
	// spaces between the embeddings
	EXPECT_EQ("{\\q2}{\\kt70\\k30}" RLE YAFE "!" RLM_PDF " {\\kt30\\kf40}" RLE OLAM RLM_PDF " {\\kt0\\k30}" RLE SHALOM RLM_PDF,
		util::reorder_rtl_runs("{\\k30}" SHALOM " {\\kf40}" OLAM " {\\k30}" YAFE "!"));

	// Each run gets all the tags in effect for it
	EXPECT_EQ("{\\q2}{\\r\\c&H00FFFF&\\c}" RLE YAFE "!" RLM_PDF " {\\r\\c&H00FFFF&}" RLE OLAM RLM_PDF " {\\r}" RLE SHALOM RLM_PDF,
		util::reorder_rtl_runs(SHALOM " {\\c&H00FFFF&}" OLAM "{\\c} " YAFE "!"));

	// Tags for the whole line stay in front, \t(\...) stays whole
	EXPECT_EQ("{\\an8\\pos(1,2)\\q2}{\\r\\t(\\fs20)\\i1}" RLE OLAM RLM_PDF " {\\r\\t(\\fs20)}" RLE SHALOM RLM_PDF,
		util::reorder_rtl_runs("{\\an8\\pos(1,2)\\t(\\fs20)}" SHALOM " {\\i1}" OLAM));

	// \N parts with one run each are only embedded
	EXPECT_EQ("{\\r\\i1}" RLE SHALOM RLM_PDF "\\N{\\r\\i1\\i0}" RLE OLAM "!" RLM_PDF,
		util::reorder_rtl_runs("{\\i1}" SHALOM "{\\i0}\\N" OLAM "!"));

	// Reordered lines are left alone by all three functions
	auto reordered = util::reorder_rtl_runs("{\\k30}" SHALOM " {\\k40}" OLAM);
	EXPECT_EQ(reordered, util::reorder_rtl_runs(reordered));
	EXPECT_EQ(reordered, util::add_rtl_marks(reordered));
	EXPECT_EQ(reordered, util::remove_rtl_marks(reordered));

	// Spaces at the edges are kept
	EXPECT_EQ("{\\q2} {\\kt40\\k40}" RLE OLAM RLM_PDF " {\\kt10\\k30}" RLE SHALOM RLM_PDF "{\\kt0\\k10} ",
		util::reorder_rtl_runs("{\\k10} {\\k30}" SHALOM " {\\k40}" OLAM " "));

	// Left unchanged: one run, left-to-right, drawings, \fsp, \fe-1 which
	// libass handles, and soft line breaks
	for (const char *text : {SHALOM "\\n{\\i1}" OLAM, SHALOM "!", "Hello {\\i1}world", "{\\p1}m 0 0 l 1 1{\\p0}" SHALOM,
	                         "{\\fsp2}" SHALOM " {\\i1}" OLAM, "{\\fe-1}" SHALOM " {\\i1}" OLAM}) {
		EXPECT_EQ(text, util::reorder_rtl_runs(text));
	}
}

}
