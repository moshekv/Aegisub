// Copyright (c) 2026, Aegisub contributors
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#pragma once

#include "ass_export_filter.h"

/// @class AssRtlOrderFilter
/// @brief Stores the style runs of right-to-left lines last to first
///
/// VSFilter, and libass unless the style's encoding is -1, place the style
/// runs between override tags left-to-right, which puts the karaoke syllables
/// and differently styled words of right-to-left lines in reverse order.
class AssRtlOrderFilter final : public AssExportFilter {
public:
	AssRtlOrderFilter();
	static void ProcessSubs(AssFile *subs);
	void ProcessSubs(AssFile *subs, wxWindow *) override { ProcessSubs(subs); }
	/// Players that lay out lines right-to-left properly, as is usual for
	/// other formats, would show reordered lines backwards
	bool AssOnly() const override { return true; }

	/// Is this filter selected in the script's export settings?
	static bool IsSelected(AssFile const& subs);
};
