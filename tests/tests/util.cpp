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

class lagi_util : public libagi { };

namespace agi {
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
