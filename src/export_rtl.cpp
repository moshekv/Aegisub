// Copyright (c) 2026, Aegisub contributors
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#include "export_rtl.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"

#include <libaegisub/split.h>
#include <libaegisub/util.h>

#include <wx/intl.h>

namespace {
std::string filter_name() {
	return from_wx(_("Right-to-Left Word Order"));
}
}

AssRtlOrderFilter::AssRtlOrderFilter()
: AssExportFilter(filter_name(), from_wx(_("Stores the parts of right-to-left lines with tags in the middle, such as karaoke syllables, last to first, so that VSFilter and libass show them in the right order. Such lines are not wrapped automatically. Only applied when exporting to ASS or SSA.")), -6000)
{
}

void AssRtlOrderFilter::ProcessSubs(AssFile *subs) {
	for (auto& diag : subs->Events) {
		// libass lays out lines of styles with encoding -1 right-to-left itself,
		// and VSFilter draws lines with letter spacing one letter at a time
		auto style = subs->GetStyle(diag.Style);
		if (!style || (style->encoding != -1 && style->spacing == 0))
			diag.Text = agi::util::reorder_rtl_runs(diag.Text);
	}
}

bool AssRtlOrderFilter::IsSelected(AssFile const& subs) {
	auto const name = filter_name();
	for (auto token : agi::Split(subs.Properties.export_filters, '|')) {
		if (name == token) return true;
	}
	return false;
}
