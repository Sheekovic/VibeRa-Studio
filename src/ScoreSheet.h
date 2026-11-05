// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

// "Export as score sheet": a plain vector PDF (A4) of one Channel part on a grand staff.
// Notes are placed by pitch (sharps for black keys, ledger lines), their value is shown by the
// head (open for half/whole), stem and flags (eighth, sixteenth); four bars per system. It is a
// readable lead sheet, not engraved notation: no beams, rests, ties or key signatures.
namespace vibera::score_sheet
{
struct NoteIn
{
    double beat = 0, length = .25;
    int pitch = 60;
};

class Pdf
{
    std::vector<juce::String> pages;
    juce::String page;

  public:
    void line(double x1, double y1, double x2, double y2, double width = .7)
    {
        page << juce::String(width, 2) << " w " << juce::String(x1, 2) << " " << juce::String(y1, 2) << " m "
             << juce::String(x2, 2) << " " << juce::String(y2, 2) << " l S\n";
    }
    void ellipse(double cx, double cy, double rx, double ry, bool filled)
    {
        const double k = .5523;
        page << juce::String(cx + rx, 2) << " " << juce::String(cy, 2) << " m\n";
        auto curve = [&](double x1, double y1, double x2, double y2, double x3, double y3)
        {
            page << juce::String(x1, 2) << " " << juce::String(y1, 2) << " " << juce::String(x2, 2) << " "
                 << juce::String(y2, 2) << " " << juce::String(x3, 2) << " " << juce::String(y3, 2) << " c\n";
        };
        curve(cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry);
        curve(cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy);
        curve(cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry);
        curve(cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy);
        page << (filled ? "f\n" : "1 w S\n");
    }
    void text(double x, double y, double size, const juce::String &value)
    {
        juce::String safe;
        for (auto c : value)
            if (c >= 32 && c < 127)
                safe << ((c == '(' || c == ')' || c == '\\') ? juce::String("\\") + juce::String::charToString(c)
                                                              : juce::String::charToString(c));
        page << "BT /F1 " << juce::String(size, 1) << " Tf " << juce::String(x, 2) << " " << juce::String(y, 2)
             << " Td (" << safe << ") Tj ET\n";
    }
    void newPage()
    {
        if (page.isNotEmpty())
            pages.push_back(page);
        page = {};
    }
    int pageCount() const { return int(pages.size()) + (page.isNotEmpty() ? 1 : 0); }
    juce::MemoryBlock finish()
    {
        newPage();
        juce::MemoryOutputStream out;
        std::vector<juce::int64> offsets;
        auto object = [&](const juce::String &body)
        {
            offsets.push_back(out.getPosition());
            out << juce::String(int(offsets.size())) << " 0 obj\n" << body << "\nendobj\n";
        };
        out << "%PDF-1.4\n";
        const int count = int(pages.size());
        juce::String kids;
        for (int i = 0; i < count; ++i)
            kids << juce::String(4 + i * 2) << " 0 R ";
        object("<< /Type /Catalog /Pages 2 0 R >>");
        object("<< /Type /Pages /Kids [ " + kids + "] /Count " + juce::String(count) + " >>");
        object("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        for (int i = 0; i < count; ++i)
        {
            object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> "
                   "/Contents " + juce::String(5 + i * 2) + " 0 R >>");
            const auto &content = pages[size_t(i)];
            object("<< /Length " + juce::String(int(content.getNumBytesAsUTF8())) + " >>\nstream\n" + content +
                   "endstream");
        }
        const auto xref = out.getPosition();
        out << "xref\n0 " << juce::String(int(offsets.size()) + 1) << "\n0000000000 65535 f \n";
        for (auto offset : offsets)
            out << juce::String(offset).paddedLeft('0', 10) << " 00000 n \n";
        out << "trailer\n<< /Size " << juce::String(int(offsets.size()) + 1) << " /Root 1 0 R >>\nstartxref\n"
            << juce::String(xref) << "\n%%EOF\n";
        return out.getMemoryBlock();
    }
};

// Diatonic step (C0 = 0) and whether the pitch is sharp.
inline std::pair<int, bool> diatonic(int pitch)
{
    static constexpr int letters[]{0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    static constexpr bool sharps[]{false, true, false, true, false, false, true, false, true, false, true, false};
    return {(pitch / 12) * 7 + letters[pitch % 12], sharps[pitch % 12]};
}

inline juce::MemoryBlock render(std::vector<NoteIn> notes, int numerator, int denominator, double bpm,
                                const juce::String &title, int &pagesOut)
{
    Pdf pdf;
    const double barBeats = numerator * 4. / std::max(1, denominator);
    double end = barBeats;
    for (const auto &n : notes)
        end = std::max(end, n.beat + n.length);
    const int bars = std::max(1, int(std::ceil(end / barBeats - 1.e-9)));
    constexpr double left = 60, right = 545, space = 6, barsPerSystem = 4, systemHeight = 150;
    const double barWidth = (right - left - 40) / barsPerSystem;
    const int systemsPerPage = 5;
    const int systems = int(std::ceil(bars / barsPerSystem));
    std::sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) { return a.beat < b.beat; });
    const int trebleBase = diatonic(64).first, bassBase = diatonic(43).first; // Bottom lines E4 / G2.
    for (int system = 0; system < systems; ++system)
    {
        const int onPage = system % systemsPerPage;
        if (onPage == 0)
        {
            pdf.newPage();
            if (system == 0)
            {
                pdf.text(left, 800, 18, title);
                pdf.text(left, 782, 10, juce::String(juce::roundToInt(bpm)) + " BPM   " + juce::String(numerator) + "/" +
                                            juce::String(denominator));
            }
        }
        const double top = 740 - onPage * systemHeight;
        const double trebleBottom = top - 4 * space, bassBottom = trebleBottom - 60;
        for (int staff = 0; staff < 2; ++staff)
        {
            const double bottom = staff == 0 ? trebleBottom : bassBottom;
            for (int lineIndex = 0; lineIndex < 5; ++lineIndex)
                pdf.line(left, bottom + lineIndex * space, right, bottom + lineIndex * space, .6);
            pdf.text(left + 2, bottom + space * 1.4, 9, staff == 0 ? "Treble" : "Bass");
        }
        pdf.line(left, bassBottom, left, trebleBottom + 4 * space, 1.2);
        const double firstBarX = left + 40;
        for (int b = 0; b <= barsPerSystem; ++b)
        {
            const double x = firstBarX + b * barWidth;
            pdf.line(x, bassBottom, x, trebleBottom + 4 * space, b == barsPerSystem ? 1.2 : .7);
            if (b < barsPerSystem && system * int(barsPerSystem) + b < bars)
                pdf.text(x + 2, trebleBottom + 4 * space + 6, 7, juce::String(system * int(barsPerSystem) + b + 1));
        }
        const double systemStart = system * barsPerSystem * barBeats, systemEnd = systemStart + barsPerSystem * barBeats;
        for (const auto &n : notes)
        {
            if (n.beat < systemStart - 1.e-9 || n.beat >= systemEnd - 1.e-9)
                continue;
            const double x = firstBarX + 10 + (n.beat - systemStart) / barBeats * barWidth;
            const auto [step, sharp] = diatonic(std::clamp(n.pitch, 21, 108));
            const bool treble = n.pitch >= 60;
            const double bottom = treble ? trebleBottom : bassBottom;
            const int position = step - (treble ? trebleBase : bassBase); // 0 = bottom line, 2 per line.
            const double y = bottom + position * space / 2;
            for (int ledger = position < 0 ? -2 : 10; position < 0 ? ledger >= position : ledger <= position;
                 ledger += position < 0 ? -2 : 2)
                pdf.line(x - 6, bottom + ledger * space / 2, x + 6, bottom + ledger * space / 2, .7);
            // Value from the length: 4 whole, 2 half, 1 quarter, .5 eighth, .25 sixteenth.
            static constexpr double values[]{4, 2, 1, .5, .25};
            double value = values[4];
            for (double candidate : values)
                if (n.length >= candidate * .75)
                {
                    value = candidate;
                    break;
                }
            pdf.ellipse(x, y, 3.6, 2.6, value < 2);
            if (sharp)
                pdf.text(x - 12, y - 3, 9, "#");
            if (value < 4)
            {
                const bool up = position < 4;
                const double stemX = up ? x + 3.4 : x - 3.4, stemEnd = up ? y + 20 : y - 20;
                pdf.line(stemX, y, stemX, stemEnd, .8);
                const int flags = value == .5 ? 1 : value == .25 ? 2 : 0;
                for (int f = 0; f < flags; ++f)
                {
                    const double fy = stemEnd + (up ? -f * 5 : f * 5);
                    pdf.line(stemX, fy, stemX + 6, fy + (up ? -7 : 7), 1);
                }
            }
        }
    }
    pagesOut = pdf.pageCount();
    return pdf.finish();
}
} // namespace vibera::score_sheet
