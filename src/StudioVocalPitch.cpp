// SPDX-License-Identifier: AGPL-3.0-or-later
// Vocal pitch editor: note blobs over a pitch curve for one Audio Clip. Drag a note up or down
// (semitone steps; Alt for cents), snap notes to the nearest semitone, then Apply renders a new
// source with each note transposed (one Undo through audio.stretchApply).
#include "Studio.h"
#include "Theme.h"
#include <cmath>

namespace vibera
{
juce::Result Studio::analyzeVocalClip(Id clip, vocal::Analysis &out)
{
    const auto found = std::find_if(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
                                    [&](const auto &p) { return p.id == clip; });
    if (found == controller.project.audioPlacements.end())
        return juce::Result::fail("vocalPitch.missingClip: Select an Audio Clip");
    const auto *source = controller.project.channel(found->channel);
    if (!source)
        return juce::Result::fail("vocalPitch.missingClip: The clip has no source");
    return vocal::analyze(juce::File(source->source), found->offset, found->duration, out);
}

juce::Result Studio::applyVocalPitch(Id clip, std::vector<vocal::Shift> shifts, const juce::String &operation)
{
    if (stretchJob)
        return juce::Result::fail("vocalPitch.busy: An audio clip job is already running");
    const auto found = std::find_if(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
                                    [&](const auto &p) { return p.id == clip; });
    if (found == controller.project.audioPlacements.end())
        return juce::Result::fail("vocalPitch.missingClip: Select an Audio Clip");
    const auto *source = controller.project.channel(found->channel);
    if (!source)
        return juce::Result::fail("vocalPitch.missingClip: The clip has no source");
    std::erase_if(shifts, [](const auto &s) { return s.semitones == 0; });
    if (shifts.empty())
        return juce::Result::fail("vocalPitch.noChange: No note was moved");
    for (const auto &s : shifts)
        if (!std::isfinite(s.semitones) || std::abs(s.semitones) > 24 || !(s.end > s.start) || s.start < 0 ||
            s.end > found->duration + 1)
            return juce::Result::fail("vocalPitch.invalidShift: Shifts are -24..24 semitones inside the clip");
    const auto folder = projectFile != juce::File{}
                            ? projectFile.getParentDirectory().getChildFile("Edited")
                            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                  .getChildFile("VibeRa Studio/Edited");
    if (!folder.createDirectory())
        return juce::Result::fail("Cannot create " + folder.getFullPathName());
    auto job = std::make_unique<StretchJob>();
    job->clip = clip;
    job->seconds = found->duration;
    job->project = controller.projectIdentity;
    job->operation = operation;
    job->target = folder.getChildFile(juce::File(source->source).getFileNameWithoutExtension() + " pitch-edit.wav")
                      .getNonexistentSibling(false);
    auto *raw = job.get();
    const juce::File input(source->source);
    const double offset = found->offset, duration = found->duration;
    job->thread = std::thread([raw, input, offset, duration, shifts]
                              {
                                  raw->result = vocal::render(input, offset, duration, shifts, raw->target);
                                  raw->done = true;
                              });
    stretchJob = std::move(job);
    status = "Rendering the edited vocal pitch...";
    return juce::Result::ok();
}

namespace
{
class VocalPitchEditor final : public juce::Component
{
  public:
    VocalPitchEditor(Studio &owner, Id clipId, vocal::Analysis analysis)
        : studio(&owner), clip(clipId), a(std::move(analysis)), shift(a.notes.size(), 0.)
    {
        low = 127, high = 0;
        for (const auto &n : a.notes)
        {
            low = std::min(low, std::floor(n.pitch) - 4);
            high = std::max(high, std::ceil(n.pitch) + 4);
        }
        if (a.notes.empty())
            low = 48, high = 72;
        for (auto *b : {&snap, &snapHalf, &reset, &apply, &cancel})
            addAndMakeVisible(b);
        snap.setTooltip("Move every note (or the selected one) to the nearest semitone");
        snap.onClick = [this] { snapNotes(1); };
        snapHalf.onClick = [this] { snapNotes(.5); };
        reset.onClick = [this]
        {
            std::fill(shift.begin(), shift.end(), 0.);
            repaint();
        };
        apply.onClick = [this]
        {
            std::vector<vocal::Shift> shifts;
            for (size_t i = 0; i < a.notes.size(); ++i)
                if (shift[i] != 0)
                    shifts.push_back({a.notes[i].start, a.notes[i].end, shift[i]});
            if (studio)
            {
                if (auto r = studio->applyVocalPitch(clip, shifts); r.failed())
                    studio->status = r.getErrorMessage();
                studio->repaint();
            }
            close();
        };
        cancel.onClick = [this] { close(); };
        info.setText(juce::String(int(a.notes.size())) + " notes. Drag a note up or down (Alt: fine), double-click "
                                                         "to snap it, then Apply.",
                     juce::dontSendNotification);
        addAndMakeVisible(info);
        setSize(980, 560);
    }

  private:
    juce::Component::SafePointer<Studio> studio;
    Id clip;
    vocal::Analysis a;
    std::vector<double> shift;
    double low = 48, high = 72;
    int selected = -1;
    double dragStart = 0, shiftStart = 0;
    juce::TextButton snap{"Snap to semitones"}, snapHalf{"Snap 50 %"}, reset{"Reset"}, apply{"Apply"},
        cancel{"Cancel"};
    juce::Label info;
    static constexpr int keys = 46;

    juce::Rectangle<int> grid() const
    {
        return getLocalBounds().reduced(10).withTrimmedTop(36).withTrimmedLeft(keys);
    }
    double length() const
    {
        return a.curve.empty() ? 1. : std::max(a.curve.back().seconds + a.hop, .1);
    }
    float xOf(double seconds) const
    {
        const auto g = grid();
        return float(g.getX() + seconds / length() * g.getWidth());
    }
    float yOf(double midi) const
    {
        const auto g = grid();
        return float(g.getBottom() - (midi - low) / (high - low) * g.getHeight());
    }
    float row() const
    {
        return float(grid().getHeight() / (high - low));
    }
    juce::Rectangle<float> blob(size_t i) const
    {
        const auto &n = a.notes[i];
        const float y = yOf(n.pitch + shift[i]);
        return {xOf(n.start), y - row() * .45f, std::max(3.f, xOf(n.end) - xOf(n.start)), row() * .9f};
    }
    void snapNotes(double amount)
    {
        for (size_t i = 0; i < a.notes.size(); ++i)
            if (selected < 0 || int(i) == selected)
            {
                const double current = a.notes[i].pitch + shift[i];
                shift[i] += (std::round(current) - current) * amount;
            }
        repaint();
    }
    void close()
    {
        if (auto *window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(0);
    }
    void resized() override
    {
        auto top = getLocalBounds().reduced(10).removeFromTop(28);
        cancel.setBounds(top.removeFromRight(80));
        top.removeFromRight(6);
        apply.setBounds(top.removeFromRight(80));
        top.removeFromRight(12);
        reset.setBounds(top.removeFromRight(70));
        top.removeFromRight(6);
        snapHalf.setBounds(top.removeFromRight(90));
        top.removeFromRight(6);
        snap.setBounds(top.removeFromRight(140));
        info.setBounds(top);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colour(0xff10191f));
        const auto area = grid();
        for (int m = int(low); m < int(high); ++m)
        {
            const bool black = juce::Array<int>{1, 3, 6, 8, 10}.contains(((m % 12) + 12) % 12);
            const float y = yOf(m + .5);
            g.setColour(black ? juce::Colour(0xff141f26) : juce::Colour(0xff1a272f));
            g.fillRect(float(area.getX()), y, float(area.getWidth()), row());
            g.setColour(black ? juce::Colour(0xff2a3a44) : juce::Colour(0xffd8e2e6));
            g.fillRect(float(area.getX() - keys), y + 1, float(keys - 4), row() - 1);
            if (m % 12 == 0)
            {
                g.setColour(juce::Colour(0xff10191f));
                g.setFont(10.f);
                g.drawText(vocal::noteName(m), juce::Rectangle<float>(float(area.getX() - keys + 2), y, keys - 8.f, row()),
                           juce::Justification::centredRight);
            }
        }
        // Pitch curve, moved with its note.
        juce::Path curve;
        bool open = false;
        for (const auto &f : a.curve)
        {
            if (f.midi == 0)
            {
                open = false;
                continue;
            }
            double moved = f.midi;
            for (size_t i = 0; i < a.notes.size(); ++i)
                if (f.seconds >= a.notes[i].start && f.seconds <= a.notes[i].end)
                    moved += shift[i];
            const juce::Point<float> p{xOf(f.seconds), yOf(moved)};
            if (open)
                curve.lineTo(p);
            else
                curve.startNewSubPath(p);
            open = true;
        }
        for (size_t i = 0; i < a.notes.size(); ++i)
        {
            const auto r = blob(i);
            const bool on = int(i) == selected, moved = shift[i] != 0;
            g.setColour((on ? juce::Colour(0xffeeb76a) : juce::Colour(0xff29d7c5)).withAlpha(moved ? .75f : .45f));
            g.fillRoundedRectangle(r, 5);
            g.setColour(on ? juce::Colour(0xffeeb76a) : juce::Colour(0xff29d7c5));
            g.drawRoundedRectangle(r, 5, 1.2f);
            const double pitch = a.notes[i].pitch + shift[i];
            const int cents = int(std::lround((pitch - std::round(pitch)) * 100));
            // Label above the note so the pitch curve inside it stays readable.
            g.setColour(on ? juce::Colour(0xffeeb76a) : juce::Colour(0xffe4eef2));
            g.setFont(juce::Font(juce::FontOptions(11.f, juce::Font::bold)));
            g.drawText(vocal::noteName(pitch) + (cents ? (cents > 0 ? " +" : " ") + juce::String(cents) + " c" : ""),
                       juce::Rectangle<float>(r.getX(), r.getY() - 15, std::max(60.f, r.getWidth()), 14),
                       juce::Justification::centredLeft, false);
        }
        g.setColour(juce::Colour(0xffe4eef2).withAlpha(.85f));
        g.strokePath(curve, juce::PathStrokeType(1.4f));
        if (a.notes.empty())
        {
            g.setColour(juce::Colour(0xff95a9b4));
            g.drawText("No sung notes were found. The editor works on one voice or instrument at a time.",
                       area, juce::Justification::centred);
        }
    }
    int hit(juce::Point<float> p) const
    {
        for (size_t i = 0; i < a.notes.size(); ++i)
            if (blob(i).expanded(0, 2).contains(p))
                return int(i);
        return -1;
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        selected = hit(e.position);
        if (selected >= 0)
        {
            dragStart = e.position.y;
            shiftStart = shift[size_t(selected)];
        }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (selected < 0)
            return;
        const double moved = (dragStart - e.position.y) / row();
        const double base = a.notes[size_t(selected)].pitch + shiftStart;
        double target = base + moved;
        if (!e.mods.isAltDown())
            target = std::round(target); // Semitone steps land exactly on the grid.
        shift[size_t(selected)] = std::clamp(target - a.notes[size_t(selected)].pitch, -24., 24.);
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent &e) override
    {
        selected = hit(e.position);
        if (selected >= 0)
            snapNotes(1);
    }
};
} // namespace

juce::Result Studio::openVocalPitchEditor(Id clip)
{
    vocal::Analysis analysis;
    if (auto analysed = analyzeVocalClip(clip, analysis); analysed.failed())
        return analysed;
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new VocalPitchEditor(*this, clip, std::move(analysis)));
    options.dialogTitle = "Vocal pitch editor";
    options.dialogBackgroundColour = juce::Colour(0xff10191f);
    options.resizable = true;
    options.useNativeTitleBar = true;
    options.launchAsync();
    return juce::Result::ok();
}
} // namespace vibera
