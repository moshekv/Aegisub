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

	// Direction comes from the first strong character, not a later one
	EXPECT_TRUE(util::is_rtl("\u05e9\u05dc\u05d5\u05dd Hello"));
	EXPECT_FALSE(util::is_rtl("Hello \u05e9\u05dc\u05d5\u05dd"));
}

}
