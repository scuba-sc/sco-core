// SPDX-License-Identifier: GPL-3.0-only
// Internal to the ImGui adapter: the pure sizing and text-fitting rules of the renderer. Nothing here
// touches ImGui, so the rules run in headless tests with a fake measure function.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sco::widgets::layout {

// Pixel limits below are written for this font size and scale linearly with the live one.
constexpr float kReferenceFontSize = 16.0f;
constexpr float kLabelColumnMin = 120.0f, kLabelColumnMax = 320.0f;
constexpr float kControlMin = 140.0f, kControlMax = 420.0f;
constexpr float kListMax = 640.0f;
constexpr float kComboPopupMaxWidth = 640.0f, kComboPopupMinRowWidth = 280.0f;
constexpr int kComboPopupMaxRows = 12;
constexpr float kTooltipWrap = 480.0f;
// Option and label text is cut here before it is measured, however long the plugin string is.
constexpr size_t kMaxMeasuredBytes = 4096;

inline float Scale(float fontSize) {
    return fontSize > 0.0f && std::isfinite(fontSize) ? fontSize / kReferenceFontSize : 1.0f;
}

// One consistent label column per container, from the widest sibling label. Zero when no sibling
// has a label, so controls without labels take the whole row.
inline float LabelColumnWidth(float widestLabel, float fontSize) {
    if (!(widestLabel > 0.0f)) return 0.0f;
    const float scale = Scale(fontSize);
    return std::clamp(widestLabel, kLabelColumnMin * scale, kLabelColumnMax * scale);
}

// Width of a control that fills the rest of its row up to a cap. Never below a floor: layouts that
// derive their size from the available width inside auto-resizing windows would otherwise shrink
// it to nothing (see the BUTTON note in the adapter).
inline float ControlWidth(float available, float labelColumn, float spacing, float fontSize,
                          float maximum = kControlMax) {
    const float scale = Scale(fontSize);
    float width = std::isfinite(available) ? available : 0.0f;
    if (labelColumn > 0.0f) width -= labelColumn + spacing;
    return std::clamp(width, kControlMin * scale, std::max(maximum * scale, kControlMin * scale));
}

struct PopupLimit {
    float maxWidth, maxHeight;
};
// Combo popups never grow past a fixed width or about a dozen rows, whatever the options hold.
inline PopupLimit ComboPopupLimit(float fontSize, float itemSpacingY, float windowPaddingY) {
    const float row = (fontSize > 0.0f ? fontSize : kReferenceFontSize) + std::max(itemSpacingY, 0.0f);
    return {kComboPopupMaxWidth * Scale(fontSize),
            row * static_cast<float>(kComboPopupMaxRows) + 2.0f * std::max(windowPaddingY, 0.0f)};
}

// Fixed width of the selectable rows in a combo popup: the combo's own width, at least a readable
// minimum, never more than the popup cap. Independent of any popup auto-size.
inline float ComboPopupRowWidth(float comboWidth, float fontSize, float windowPaddingX, float scrollbar) {
    const float scale = Scale(fontSize);
    const float chrome = 2.0f * std::max(windowPaddingX, 0.0f) + std::max(scrollbar, 0.0f);
    const float cap = std::max(kComboPopupMaxWidth * scale - chrome, 1.0f);
    return std::min(std::max(comboWidth - chrome, kComboPopupMinRowWidth * scale), cap);
}

namespace detail {
inline bool Continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
} // namespace detail

// "..." keeps the longest prefix, cut on a UTF-8 code point boundary, that fits maxWidth with the
// ellipsis appended. measure(const std::string&) returns the pixel width of a string. Only the first
// line of the text is considered. Text that fits is returned unchanged and *truncated stays false.
// When not even the ellipsis fits the result is empty.
template <class Measure>
std::string Ellipsize(std::string_view text, float maxWidth, Measure&& measure, bool* truncated = nullptr) {
    if (truncated) *truncated = false;
    size_t length = text.find_first_of("\r\n");
    bool cut = length != std::string_view::npos;
    if (!cut) length = text.size();
    if (length > kMaxMeasuredBytes) {
        length = kMaxMeasuredBytes;
        while (length > 0 && detail::Continuation(text[length])) --length;
        cut = true;
    }
    const std::string line(text.substr(0, length));
    if (!cut && measure(line) <= maxWidth) return line;
    if (truncated) *truncated = true;
    // Candidate cut points: the start of every code point, so a prefix never splits a sequence. When
    // the line was complete (no newline, no cap) the full-length prefix needs no ellipsis, so it is
    // excluded; otherwise it is allowed.
    std::vector<size_t> cuts;
    for (size_t i = 0; i < length; ++i)
        if (!detail::Continuation(line[i])) cuts.push_back(i);
    if (cut) cuts.push_back(length);
    const std::string dots = "...";
    size_t low = 0, high = cuts.empty() ? 0 : cuts.size() - 1; // cuts[0] == 0 whenever non-empty
    if (cuts.empty() || !(measure(dots) <= maxWidth)) return std::string();
    while (low < high) {
        const size_t mid = (low + high + 1) / 2;
        if (measure(line.substr(0, cuts[mid]) + dots) <= maxWidth) low = mid;
        else high = mid - 1;
    }
    return line.substr(0, cuts[low]) + dots;
}

// Full text for a tooltip: capped (on a code point boundary) so a pathological string cannot build a
// huge tooltip window.
inline std::string_view TooltipText(std::string_view text) {
    size_t length = std::min(text.size(), kMaxMeasuredBytes * 2);
    while (length > 0 && length < text.size() && detail::Continuation(text[length])) --length;
    return text.substr(0, length);
}

// ImGui hides everything after "##" in the strings it renders as labels. Where a plugin string has
// to go through such a call, keep it readable by never leaving two '#' adjacent.
inline std::string NeutralizeHashes(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '#' && !out.empty() && out.back() == '#') out.push_back(' ');
        out.push_back(c);
    }
    return out;
}

} // namespace sco::widgets::layout
