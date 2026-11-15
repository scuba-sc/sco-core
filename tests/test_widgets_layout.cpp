// SPDX-License-Identifier: GPL-3.0-only
// Headless tests of the ImGui adapter's pure layout rules (src/widgets/imgui_layout.h): text
// ellipsis, label column, control width and combo popup limits. No ImGui involved.
#include "../src/widgets/imgui_layout.h"
#include <cstdio>
#include <limits>
#include <string>

using namespace sco::widgets::layout;

static int pass = 0, fail = 0;
#define CHECK(...) do { if (__VA_ARGS__) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)

// Fake font: every code point is 8 px wide, regardless of its UTF-8 length.
static float Measure(const std::string& text) {
    float width = 0;
    for (const char c : text) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) width += 8.0f;
    return width;
}
static bool ValidUtf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!length || i + length > text.size()) return false;
        for (size_t k = 1; k < length; ++k) if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        i += length;
    }
    return true;
}

static void TestEllipsize() {
    bool truncated = true;
    CHECK(Ellipsize("short", 100, Measure, &truncated) == "short" && !truncated);
    CHECK(Ellipsize("exactly8", 64, Measure, &truncated) == "exactly8" && !truncated); // fits to the pixel
    CHECK(Ellipsize("", 0, Measure, &truncated).empty() && !truncated);
    // 10 chars in 64 px: 8 slots, three of them the dots.
    CHECK(Ellipsize("0123456789", 64, Measure, &truncated) == "01234..." && truncated);
    CHECK(Ellipsize("0123456789", 63, Measure, &truncated) == "0123..." && truncated);
    CHECK(Ellipsize("0123456789", 24, Measure, &truncated) == "..." && truncated);   // only the dots fit
    CHECK(Ellipsize("0123456789", 23, Measure, &truncated).empty() && truncated);    // not even those
    CHECK(Ellipsize("0123456789", -5, Measure, &truncated).empty());
    // 300-character option names always end up inside the row, for every width.
    const std::string longName(300, 'x');
    for (float width = 24; width < 2500; width += 7) {
        const std::string out = Ellipsize(longName, width, Measure, &truncated);
        CHECK(Measure(out) <= width);
        if (width >= 2400) CHECK(out == longName && !truncated);
        else CHECK(truncated && out.size() >= 3 && out.compare(out.size() - 3, 3, "...") == 0);
    }
    // Maximal prefix: one more character would not fit.
    for (float width = 24; width < 300; width += 5) {
        const std::string out = Ellipsize(longName, width, Measure);
        CHECK(Measure(out) + 8.0f > width);
    }
    // Never splits a UTF-8 sequence (2-, 3- and 4-byte code points).
    const std::string mixed = "a\xC3\xA9\xE2\x9C\x93\xF0\x9F\x9A\x80z" "a\xC3\xA9\xE2\x9C\x93\xF0\x9F\x9A\x80z";
    for (float width = 24; width < 100; width += 4) {
        const std::string out = Ellipsize(mixed, width, Measure);
        CHECK(ValidUtf8(out) && Measure(out) <= width);
    }
    // Only the first line is shown; a multi-line text counts as truncated even when the line fits.
    CHECK(Ellipsize("one\ntwo", 200, Measure, &truncated) == "one..." && truncated);
    CHECK(Ellipsize("one\r\ntwo", 200, Measure, &truncated) == "one..." && truncated);
    CHECK(Ellipsize("\nbody", 200, Measure, &truncated) == "..." && truncated);
    // Pathological length is capped before measuring.
    const std::string huge(100000, 'y');
    const std::string cut = Ellipsize(huge, 1.0e9f, Measure, &truncated);
    CHECK(truncated && cut.size() == kMaxMeasuredBytes + 3);
    // "##" and "%" stay literal: the function never interprets them.
    CHECK(Ellipsize("a##b%s", 100, Measure) == "a##b%s");
}

static void TestColumnsAndWidths() {
    const float font = 16.0f, big = 32.0f;
    CHECK(LabelColumnWidth(0.0f, font) == 0.0f);   // no labelled sibling: no column
    CHECK(LabelColumnWidth(-3.0f, font) == 0.0f);
    CHECK(LabelColumnWidth(40.0f, font) == 120.0f); // clamped up
    CHECK(LabelColumnWidth(200.0f, font) == 200.0f);
    CHECK(LabelColumnWidth(900.0f, font) == 320.0f); // clamped down
    CHECK(LabelColumnWidth(40.0f, big) == 240.0f && LabelColumnWidth(900.0f, big) == 640.0f); // scales with font
    CHECK(LabelColumnWidth(40.0f, 0.0f) == 120.0f);  // unusable font size falls back to 1x

    CHECK(ControlWidth(1000.0f, 0.0f, 8.0f, font) == 420.0f);       // capped
    CHECK(ControlWidth(300.0f, 0.0f, 8.0f, font) == 300.0f);        // fills the row
    CHECK(ControlWidth(300.0f, 120.0f, 8.0f, font) == 172.0f);      // minus label column and gap
    CHECK(ControlWidth(10.0f, 0.0f, 8.0f, font) == 140.0f);         // floor: auto-resize never collapses
    CHECK(ControlWidth(-50.0f, 120.0f, 8.0f, font) == 140.0f);
    CHECK(ControlWidth(1000.0f, 0.0f, 8.0f, font, kListMax) == 640.0f);
    CHECK(ControlWidth(1000.0f, 0.0f, 8.0f, big) == 840.0f);
    CHECK(ControlWidth(std::numeric_limits<float>::quiet_NaN(), 0.0f, 8.0f, font) == 140.0f);
    // Two aligned controls in a 480 px column: label column up to its cap still leaves a control.
    CHECK(ControlWidth(480.0f, LabelColumnWidth(900.0f, font), 8.0f, font) >= 140.0f);
}

static void TestPopupLimits() {
    const PopupLimit limit = ComboPopupLimit(16.0f, 4.0f, 8.0f);
    CHECK(limit.maxWidth == 640.0f);
    CHECK(limit.maxHeight == 12 * 20.0f + 16.0f);          // a dozen rows plus padding
    const PopupLimit big = ComboPopupLimit(32.0f, 8.0f, 16.0f);
    CHECK(big.maxWidth == 1280.0f && big.maxHeight == 12 * 40.0f + 32.0f);
    // Row width follows the combo, within [280, popup cap - chrome], and never "available width".
    CHECK(ComboPopupRowWidth(420.0f, 16.0f, 8.0f, 14.0f) == 420.0f - 30.0f);
    CHECK(ComboPopupRowWidth(150.0f, 16.0f, 8.0f, 14.0f) == 280.0f);
    CHECK(ComboPopupRowWidth(5000.0f, 16.0f, 8.0f, 14.0f) == 640.0f - 30.0f);
    for (float combo : {0.0f, 50.0f, 300.0f, 700.0f, 1.0e6f}) {
        const float row = ComboPopupRowWidth(combo, 16.0f, 8.0f, 14.0f);
        CHECK(row >= 1.0f);
        CHECK(row + 2 * 8.0f + 14.0f <= limit.maxWidth);
    }
}

static void TestText() {
    CHECK(NeutralizeHashes("plain") == "plain");
    CHECK(NeutralizeHashes("a##b") == "a# #b");
    CHECK(NeutralizeHashes("###") == "# # #");
    CHECK(NeutralizeHashes("a#b#c") == "a#b#c");
    CHECK(NeutralizeHashes("##x").find("##") == std::string::npos);
    CHECK(NeutralizeHashes("").empty());
    CHECK(TooltipText("short") == "short");
    const std::string longText(50000, 'z');
    CHECK(TooltipText(longText).size() == kMaxMeasuredBytes * 2);
    // The cap never lands inside a UTF-8 sequence.
    std::string utf8(kMaxMeasuredBytes * 2 - 1, 'a');
    utf8 += "\xE2\x9C\x93";
    utf8 += "tail";
    CHECK(ValidUtf8(std::string(TooltipText(utf8))) && TooltipText(utf8).size() == kMaxMeasuredBytes * 2 - 1);
}

int main() {
    TestEllipsize();
    TestColumnsAndWidths();
    TestPopupLimits();
    TestText();
    std::printf("widgets layout: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
