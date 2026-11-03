// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
#include "ScoreTools.h"
#include "Arrangements.h"
#include "ViewportBar.h"
#include "MidiImportChannels.h"
#include "SampleFileBrowser.h"
#include "PlaylistWaveforms.h"
#include "ClipPicker.h"
#include "ClipGroups.h"
#include "StepSequence.h"
#include "PatternChannelLoops.h"
#include "PianoLoopView.h"
#include "PianoNoteModes.h"
#include "ChannelSolo.h"
#include "RackVelocityGraph.h"
#include "ChannelIcons.h"
#include "RackSwingControl.h"
#include "MixerRouting.h"
#include "MixerTargetChoices.h"
#include "MixerControls.h"
#include "MixerEffectSlots.h"
#include "MixerEffectOrder.h"
#include "MixerInsertOrder.h"
#include <algorithm>
#include <cmath>
#include <charconv>
#include <limits>
#include <utility>
namespace vibera
{
inline void reportHistoryResult(Studio &studio, const juce::Result &result)
{
    if (result.failed())
    {
        studio.status = result.getErrorMessage();
        studio.repaint();
    }
}
inline bool finishUiGesture(Studio &studio, const juce::String &label)
{
    const auto result = studio.controller.endGestureChecked(label);
    reportHistoryResult(studio, result);
    return result.wasOk();
}
inline bool selected(const std::vector<Id> &list, Id id)
{
    return std::find(list.begin(), list.end(), id) != list.end();
}
class Studio::Editor final : public juce::Component,
                             public juce::DragAndDropTarget,
                             public juce::FileDragAndDropTarget,
                             public juce::TooltipClient
{
    Studio &s;
    int type;
#include "PianoNoteModesEditor.inc"
    class RackFilterCombo final : public juce::ComboBox
    {
      public:
        std::function<void()> contextMenu;
        void mouseDown(const juce::MouseEvent &event) override
        {
            if (event.mods.isRightButtonDown() && contextMenu)
                contextMenu();
            else
                juce::ComboBox::mouseDown(event);
        }
    } rackFilter;
    juce::TextButton rackMenu{"Menu"};
    ToolbarButton rackGraphButton{"Graph editor"};
    std::unique_ptr<RackVelocityGraph> velocityGraph;
    std::unique_ptr<RackSwingControl> swingControl;
    juce::String rackViewKey, rackGroupNames;
    std::vector<Id> rackRowIds, rackSelectionVisited;
    bool rackSelecting = false, rackSelectAdd = true;
    int rackSelectionLastRow = -1;
    std::unique_ptr<ClipPicker> picker;
    PickerDivider pickerDivider;
    bool pickerShown = true;
    int preferredPickerWidth = 174;
    Id audioPicker = 0;
    juce::String pickerSession;
    int pickerWidth() const
    {
        return type == 2 && pickerShown
                   ? juce::jlimit(96, 280, std::min(preferredPickerWidth, getWidth() / 3))
                   : 0;
    }
    std::unique_ptr<PlaylistWaveforms> waveforms;
    int playlistTool = 0;
    bool slicing = false;
    juce::Point<float> sliceEnd;
    bool sliceAlt = false;
    Id slipTarget = 0;
    std::array<ToolbarButton, 8> playlistTools;
    std::vector<Id> selectionBeforeMarquee;
    bool toggleMarquee = false;
    double playlistLaneScale = 1, savedPlaylistLaneScale = 1;
    int savedLaneOffset = 0;
    bool muting = false, muteEnabled = false;
    std::unordered_set<Id> muteTouched;
    bool noteMuteTool = false, noteSliceTool = false;
    bool stretching = false, stretchCoarse = false;
    int mixerSendSource = -1, mixerSendTarget = 0;
    float mixerSendStart = 0;
    int mixerPanSource = -1;
    float mixerPanStart = 0;
    double stretchReferenceSpan = 1, stretchStartMouseBeat = 0;

    juce::Point<float> lastMutePoint;
    ToolbarButton pianoMute{"Mute notes tool"}, pianoSlice{"Slice notes tool"};
    Id automationDragPoint = 0, automationDragClip = 0, automationPicker = 0;
    double automationDragBeat = 0;
    float automationDragValue = 0;
    juce::String automationGestureProject;
    const AutomationPlacement *automationClip(Id id) const
    {
        for (const auto &clip : s.controller.project.automation.placements)
            if (clip.id == id)
                return &clip;
        return nullptr;
    }
    juce::Rectangle<float> automationArea(Id id) const
    {
        return playlistClipBounds(id).withTrimmedTop(19).reduced(4, 3);
    }
    juce::Point<float> automationPointPosition(Id clipId, const AutomationPoint &point) const
    {
        const auto *clip = automationClip(clipId);
        const auto area = automationArea(clipId);
        if (!clip)
            return {};
        return {area.getX() + float((point.beat - clip->offset) / clip->length) * area.getWidth(),
                area.getBottom() - point.value * area.getHeight()};
    }
    void automationPointMenu(Id clipId, Id pointId)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Delete point");
        menu.addItem(2, "Type in value / time");
        menu.addSeparator();
        menu.addItem(3, "Single curve");
        menu.addItem(4, "Hold");
        menu.addItem(5, "Smooth");
        menu.showMenuAsync(
            {},
            [safe = juce::Component::SafePointer<Editor>(this), clipId, pointId,
             identity = s.controller.projectIdentity](int action)
            {
                if (!safe || !action || safe->s.controller.projectIdentity != identity)
                    return;
                CommandArgs a;
                a.target = pointId;
                if (action == 1)
                    safe->s.command("automation.point.delete", a);
                else if (action >= 3)
                {
                    a.text = action == 3 ? "single" : action == 4 ? "hold" : "smooth";
                    safe->s.command("automation.point.shape", a);
                }
                else if (const auto *clip = safe->automationClip(clipId))
                {
                    const auto *source = safe->s.controller.project.automationSource(clip->source);
                    if (!source)
                        return;
                    for (const auto &point : source->points)
                        if (point.id == pointId)
                        {
                            auto *dialog = new juce::AlertWindow(
                                "Automation point",
                                "Normalized value 0-1; source time in beats; tension -1 to 1",
                                juce::MessageBoxIconType::NoIcon);
                            dialog->addTextEditor("beat", juce::String(point.beat, 9), "Beat");
                            dialog->addTextEditor("value", juce::String(point.value, 6), "Value");
                            dialog->addTextEditor("tension", juce::String(point.tension, 6), "Tension");
                            dialog->addButton("Apply", 1);
                            dialog->addButton("Cancel", 0);
                            const auto shape = point.incoming;
                            const auto identity = safe->s.controller.projectIdentity;
                            const auto revision = safe->s.controller.revision;
                            dialog->enterModalState(
                                true,
                                juce::ModalCallbackFunction::create(
                                    [safe, view = juce::Component::SafePointer<juce::AlertWindow>(dialog), a,
                                     shape, identity, revision](int result) mutable
                                    {
                                        if (!safe || !view || result != 1)
                                            return;
                                        if (safe->s.controller.projectIdentity != identity ||
                                            safe->s.controller.revision != revision)
                                            return;
                                        // Strict numeric input uses the same finite/domain checks as
                                        // serialized automation.
                                        auto numeric = [](const juce::String &text, double &out)
                                        {
                                            const auto utf8 = text.trim().toStdString();
                                            const auto r =
                                                std::from_chars(utf8.data(), utf8.data() + utf8.size(), out);
                                            return r.ec == std::errc{} &&
                                                   r.ptr == utf8.data() + utf8.size() && std::isfinite(out);
                                        };
                                        double tension = 0;
                                        if (!numeric(view->getTextEditorContents("beat"), a.beat) ||
                                            !numeric(view->getTextEditorContents("value"), a.value) ||
                                            !numeric(view->getTextEditorContents("tension"), tension) ||
                                            tension < -1 || tension > 1)
                                        {
                                            safe->s.status = "Enter valid automation numbers";
                                            safe->s.repaint();
                                            return;
                                        }
                                        safe->s.controller.beginGesture();
                                        const auto moved =
                                            safe->s.controller.execute("automation.point.move", a);
                                        if (moved.failed())
                                        {
                                            safe->s.controller.cancelGesture();
                                            safe->s.status = moved.getErrorMessage();
                                            return;
                                        }
                                        a.value = tension;
                                        a.text = shape == AutomationShape::single ? "single"
                                                 : shape == AutomationShape::hold ? "hold"
                                                                                  : "smooth";
                                        safe->s.command("automation.point.shape", a);
                                        finishUiGesture(safe->s, "Edit automation point");
                                    }),
                                true);
                        }
                }
            });
    }
    void automationSourceEditor(Id clipId)
    {
        if (const auto *clip = automationClip(clipId))
            s.automationSourceDialog(clip->source);
    }
    bool automationMouseDown(const juce::MouseEvent &e)
    {
        const auto clipId = hit(e.position);
        const auto *clip = automationClip(clipId);
        if (!clip || e.y < playlistClipBounds(clipId).getY() + 17 || (playlistTool != 0 && playlistTool != 1))
            return false;
        const auto *source = s.controller.project.automationSource(clip->source);
        if (!source)
            return false;
        const auto bounds = playlistClipBounds(clipId);
        if (e.mods.isLeftButtonDown() && (e.x < bounds.getX() + 3 || e.x > bounds.getRight() - 2))
            return false; // Border handles trim the source window; point centres retain point editing.
        s.selectedClips = {clipId};
        automationPicker = clip->source;
        for (const auto &point : source->points)
            if (point.beat >= clip->offset && point.beat <= clip->offset + clip->length &&
                automationPointPosition(clipId, point).getDistanceFrom(e.position) <= 7)
            {
                if (e.mods.isRightButtonDown())
                    automationPointMenu(clipId, point.id);
                else
                {
                    s.controller.beginGesture();
                    automationDragClip = clipId;
                    automationDragPoint = point.id;
                    automationDragBeat = point.beat;
                    automationDragValue = point.value;
                    automationGestureProject = s.controller.projectIdentity;
                }
                repaint();
                return true;
            }
        if (e.mods.isRightButtonDown())
        {
            const auto area = automationArea(clipId);
            if (area.getWidth() <= 0 || area.getHeight() <= 0)
                return true;
            CommandArgs a;
            a.target = source->id;
            a.beat = snap(clip->offset + (e.x - area.getX()) / area.getWidth() * clip->length, e.mods);
            a.beat = juce::jlimit(clip->offset, clip->offset + clip->length, a.beat);
            a.value = e.mods.isShiftDown()
                          ? automationSourceValueAt(*source, a.beat)
                          : juce::jlimit(0., 1., double((area.getBottom() - e.y) / area.getHeight()));
            const auto newPoint = s.controller.project.nextId;
            s.controller.beginGesture();
            s.command("automation.point.add", a);
            if (s.controller.project.nextId != newPoint)
            {
                automationDragPoint = newPoint;
                automationDragClip = clipId;
                automationDragBeat = a.beat;
                automationDragValue = float(a.value);
                automationGestureProject = s.controller.projectIdentity;
            }
            else
                s.controller.cancelGesture();
            return true;
        }
        return false; // Empty left-drag still moves the clip.
    }
    std::vector<Placement> playlistClips() const
    {
        auto clips = s.controller.project.placements;
        for (const auto &audio : s.controller.project.audioPlacements)
            clips.push_back({audio.id, 0, audio.lane, audio.beat,
                             audio.duration * s.controller.project.tempo / 60., 0, audio.muted});
        for (const auto &clip : s.controller.project.automation.placements)
            clips.push_back({clip.id, 0, clip.lane, clip.beat, clip.length, clip.offset, clip.muted});
        return clips;
    }
    const AudioPlacement *audioClip(Id id) const
    {
        for (const auto &clip : s.controller.project.audioPlacements)
            if (clip.id == id)
                return &clip;
        return nullptr;
    }
    double clipOffset(Id id) const
    {
        if (auto *audio = audioClip(id))
            return audio->offset * s.controller.project.tempo / 60.;
        if (const auto *clip = automationClip(id))
            return clip->offset;
        for (auto &clip : s.controller.project.placements)
            if (clip.id == id)
                return clip.offset;
        return 0;
    }
    NoteSliceLine pianoSliceLine(juce::Point<float> start, juce::Point<float> end, bool unsnapped) const
    {
        auto pitch = [&](float y)
        { return juce::jlimit(-.5, 127.5, double(topPitch) + .5 - (y - gridTop()) / height()); };
        const auto top = float(gridTop()), bottom = float(noteBottom());
        start.y = juce::jlimit(top, bottom, start.y);
        end.y = juce::jlimit(top, bottom, end.y);
        NoteSliceLine line{juce::jlimit(0., 1000000., beatAt(start.x)),
                           juce::jlimit(0., 1000000., beatAt(end.x)),
                           pitch(start.y),
                           pitch(end.y),
                           unsnapped ? 0 : s.snap,
                           ghosts && editableGhosts};
        line.loopPreview = pattern() && !pattern()->channelLoops.empty();
        if (s.noteSlicePending() && !s.noteSliceSession->remote && s.noteSliceSession->args.slice)
        {
            // Scrolling/zooming during a drag must not move its musical anchor.
            line.startBeat = s.noteSliceSession->args.slice->startBeat;
            line.startPitch = s.noteSliceSession->args.slice->startPitch;
        }
        return line;
    }
    void commitSlice()
    {
        s.controller.beginGesture();
        const auto clips = playlistClips();
        for (const auto &clip : clips)
        {
            const auto rectangle = cr(clip);
            const auto low = std::min(origin.y, sliceEnd.y), high = std::max(origin.y, sliceEnd.y);
            if (high < rectangle.getY() || low > rectangle.getBottom())
                continue;
            const auto y =
                juce::jlimit(rectangle.getY(), rectangle.getBottom(),
                             (std::max(low, rectangle.getY()) + std::min(high, rectangle.getBottom())) * .5f);
            const auto fraction =
                std::abs(sliceEnd.y - origin.y) > .001f ? (y - origin.y) / (sliceEnd.y - origin.y) : 0.f;
            const auto x = origin.x + fraction * (sliceEnd.x - origin.x);
            const auto beat = snap(beatAt(x), sliceAlt ? juce::ModifierKeys(juce::ModifierKeys::altModifier)
                                                       : juce::ModifierKeys{});
            if (beat > clip.beat && beat < clip.beat + clip.length)
            {
                CommandArgs a;
                a.ids = {clip.id};
                a.beat = beat;
                s.command("clip.slice", a);
            }
        }
        finishUiGesture(s, "Slice clips");
    }
    void muteAt(juce::Point<float> point)
    {
        if (type == 1)
        {
            const juce::Rectangle<float> viewport(float(leftEdge()), float(gridTop()),
                                                  float(std::max(0, getWidth() - leftEdge() - rightInset())),
                                                  float(std::max(0, noteBottom() - gridTop())));
            const juce::Line<float> stroke(lastMutePoint, point);
            CommandArgs args;
            args.pattern = s.selectedPattern;
            args.channel = s.selectedChannel;
            if (pattern())
                for (const auto &appearance : pianoView())
                {
                    const auto &note = appearance.note;
                    if (!canEdit(note) || muteTouched.contains(note.id))
                        continue;
                    const auto bounds = nr(note).getIntersection(viewport);
                    if (bounds.isEmpty())
                        continue;
                    juce::Path shape;
                    shape.addRectangle(bounds);
                    if (bounds.contains(lastMutePoint) || bounds.contains(point) ||
                        shape.intersectsLine(stroke))
                    {
                        muteTouched.insert(note.id);
                        args.ids.push_back(note.id);
                    }
                }
            lastMutePoint = point;
            if (!args.ids.empty())
                s.command(muteEnabled ? "notes.mute" : "notes.unmute", args);
            return;
        }
        auto id = hit(point);
        if (!id || muteTouched.contains(id))
            return;
        muteTouched.insert(id);
        CommandArgs args;
        args.ids = {id};
        args.enabled = muteEnabled;
        s.command("clip.mute", args);
    }
    int topPitch = 72, laneOffset = 0, stepOffset = 0, rackOffset = 0;
    double mixerScroll = 0;
    juce::String mixerOverloadProject;
    std::map<int, std::array<bool, 2>> mixerOverloadDisplay;
    int effectOffset = 0;
    std::unique_ptr<MixerEffectOrderIntent> effectOrder;
    bool effectOrderMoved = false;
    struct EffectWetGesture
    {
        juce::String project, descriptor, owner;
        Id plugin = 0;
        int insert = 0;
        std::uint64_t revision = 0, serial = 0;
        juce::Point<float> lastPoint;
    };
    std::unique_ptr<EffectWetGesture> effectWet;
    struct MixerStereoGesture
    {
        juce::String project, owner;
        int insert = 0;
        std::uint64_t revision = 0, serial = 0;
        juce::Point<float> lastPoint;
    };
    std::unique_ptr<MixerStereoGesture> mixerStereo;
    juce::Point<int> effectOrderPoint;
    double zoom = 1., verticalZoom = 4. / 3.; // 16 px per pitch row at the default zoom.
    bool ghosts = true, editableGhosts = false, previewDragging = false, panning = false, keyZooming = false;
    double panStart = 0, panZoom = 1;
    int panPitch = 72, panAnchor = 60;
    bool regionZooming = false, hasRegionView = false;
    double savedZoom = 1, savedScroll = 0, savedVerticalZoom = 1;
    int savedTopPitch = 72;
    bool timeSelecting = false, timeSelectionAdd = false;
    bool pitchSelecting = false, pitchSelectionAdd = false;
    int pitchAnchor = 60, pitchFirst = 60, pitchLast = 60;
    double timeAnchor = 0, timeStart = 0, timeEnd = 0;
    double timelineBeat(float x) const
    {
        auto beat = beatAt(x);
        return s.snap > 0 ? std::floor(beat / s.snap + .5) * s.snap : beat;
    }
    bool hasTimeSelection() const
    {
        return timeEnd - timeStart > 1.e-8;
    }
    ViewportBar horizontalBar{false}, verticalBar{true};
    juce::TextButton midiFileButton{"Menu"};
    ToolbarButton playlistMenu{"Menu"};
    ToolbarButton playlistGrouping{"Enable Clip grouping"};
    juce::TextButton playlistZoomOut{"-"}, playlistZoomIn{"+"}, playlistFit{"Fit"};
    juce::ComboBox playlistSnap;
    juce::TextButton ghostButton{"Ghosts"}, editGhostButton{"Edit ghosts"};
    std::array<juce::TextButton, 5> viewButtons;
    int navigationHeight() const
    {
        return scrollBarThickness;
    }
    int rightInset() const
    {
        return type == 0 || (type == 3 && mixerStripWidth() == getWidth()) ? 0 : scrollBarThickness;
    }
    int velocityHeight() const
    {
        return type == 1 && getHeight() >= 150 ? juce::jlimit(30, 66, (getHeight() - 68) / 3) : 0;
    }
    int noteBottom() const
    {
        return getHeight() - navigationHeight() - velocityHeight();
    }
    int velocityBase() const
    {
        return getHeight() - navigationHeight() - 8;
    }
    int velocityScale() const
    {
        return std::max(8, velocityHeight() - 23);
    }
    double baseSpacing() const
    {
        auto *p = pattern();
        return type == 2 ? std::max(16., (getWidth() - leftEdge()) /
                                             std::max(32., s.controller.project.endBeat() + 4))
                         : std::max(32., (getWidth() - 80.) / std::max(8., p ? p->length : 8.));
    }
    double viewBeats() const
    {
        return std::max(1, getWidth() - leftEdge() - scrollBarThickness) / spacing();
    }
    double domain() const
    {
        auto *p = pattern();
        return std::max({type == 2 ? 128. : 16.,
                         type == 2 ? s.controller.project.endBeat() + 16
                         : p       ? p->length
                                   : 16.,
                         scroll + viewBeats() + (type == 2 ? 16 : 0)});
    }
    void setZoom(double value, double anchor)
    {
        double beat = beatAt(float(anchor));
        zoom = juce::jlimit(1.e-8, 256., value);
        scroll = std::max(0., beat - (anchor - leftEdge()) / spacing());
        repaint();
        horizontalBar.repaint();
    }
    bool canEdit(const Note &n) const
    {
        return n.channel == s.selectedChannel || (ghosts && editableGhosts);
    }
    Id ghostAt(juce::Point<float> point) const
    {
        if (!ghosts || point.x < leftEdge() || point.y < gridTop() || point.y >= noteBottom())
            return 0;
        if (const auto appearance = pianoHit(point, true))
            return appearance->note.id;
        return 0;
    }
    bool switchGhost(juce::Point<float> point)
    {
        auto id = ghostAt(point);
        if (!id)
            return false;
        if (auto *p = pattern())
            for (auto &n : p->notes)
                if (n.id == id)
                {
                    s.releaseTypingNotes();
                    s.selectedChannel = n.channel;
                    s.selectItems(false, {id});
                    viewedChannel = n.channel;
                    viewedPattern = s.selectedPattern;
                    s.refresh();
                    return true;
                }
        return false;
    }

    Id viewedChannel = 0, viewedPattern = 0;
    juce::String viewedProject;
    mutable std::vector<PianoNoteOccurrence> pianoViewCache;
    mutable juce::String pianoViewProject;
    mutable Id pianoViewPattern = 0;
    mutable std::uint64_t pianoViewRevision = std::numeric_limits<std::uint64_t>::max();
    mutable const Note *pianoViewSource = nullptr;
    mutable size_t pianoViewSourceCount = 0;
    double scroll = 0, lastBeat = 0, lastLength = 0, noteLength = .5;
    int lastPitch = 0, lastLane = 0;
    Id trimmingAudio = 0;
    Id trimmingAutomation = 0;
    double automationTrimBeat = 0, automationTrimOffset = 0, automationTrimLength = 0;
    double trimStartBeat = 0, trimStartOffset = 0, trimStartDuration = 0;
    bool selecting = false, dragging = false, erasing = false, resizing = false, paintMode = false;
    juce::Point<float> origin, lastErasePoint;
    juce::Rectangle<float> rectangle;
    int gridTop() const
    {
        if (type == 1)
            return 50 + 30 * noteModeRows();
        if (type != 2)
            return 50;
        int x = 8, rows = 1;
        for (int width : {54, 54, 54, 54, 54, 54, 54, 54, 55, 112, 26, 26, 36})
        {
            if (x + width > getWidth() - rightInset() && x > 8)
            {
                ++rows;
                x = 8;
            }
            x += width + 4;
        }
        return rows * 30 + 20;
    }
    int laneHeight() const
    {
        return juce::jlimit(16, 240,
                            int(juce::jlimit(40, 58, (getHeight() - gridTop()) / 8) * playlistLaneScale));
    }
    int rackRowHeight() const
    {
        return juce::jlimit(
            32, 38, std::max(1, rackContentBottom() - 44) / std::max(1, int(s.visibleRackChannels().size())));
    }
    int rackGraphHeight() const
    {
        if (type != 0 || !s.rackGraphVisible)
            return 0;
        return std::min(juce::jlimit(88, 148, getHeight() / 2),
                        std::max(0, getHeight() - 44 - 64 - scrollBarThickness));
    }
    int rackContentBottom() const
    {
        return getHeight() - scrollBarThickness - rackGraphHeight();
    }
    int rackLabelWidth() const
    {
        return juce::jlimit(190, 210, int(getWidth() * .34));
    }
    int mixerStripWidth() const
    {
        return getWidth() >= 700 ? getWidth() - 240 : getWidth();
    }
    MixerEffectSlots mixerEffectLayout() const
    {
        return {getWidth(), getHeight(), mixerStripWidth(), scrollBarThickness};
    }
    int mixerEffectCount() const
    {
        const auto *insert = mixerInsert(s.controller.project, s.selectedInsert);
        return insert ? int(insert->effects.size()) : 0;
    }
    juce::Result effectOrderAdmission() const
    {
        juce::String code;
        return s.admitMixerEffectMove(code);
    }
    bool ownsEffectWet() const
    {
        return effectWet && effectWet->project == s.controller.projectIdentity && s.controller.hasGesture() &&
               s.controller.gestureOwner() == effectWet->owner &&
               s.controller.gestureSerial() == effectWet->serial;
    }
    bool validEffectWet() const
    {
        if (!ownsEffectWet() || effectWet->revision != s.controller.revision ||
            effectWet->insert != s.selectedInsert)
            return false;
        const auto *insert = mixerInsert(s.controller.project, effectWet->insert);
        return insert &&
               std::any_of(
                   insert->effects.begin(), insert->effects.end(), [&](const auto &effect)
                   { return effect.id == effectWet->plugin && effect.descriptor == effectWet->descriptor; });
    }
    int effectOrderDestination(juce::Point<int> point) const
    {
        const auto layout = mixerEffectLayout();
        for (int visible = 0; visible < layout.visibleRows(); ++visible)
            if (layout.row(visible).contains(point))
            {
                const int index = effectSlotsOffset() + visible;
                return index <= mixerEffectCount() ? std::min(index, mixerEffectCount() - 1) : -1;
            }
        return -1;
    }
    void mixerEffectMenu(Id id)
    {
        const int insert = s.selectedInsert;
        const auto *m = mixerInsert(s.controller.project, insert);
        if (!m)
            return;
        const auto found =
            std::find_if(m->effects.begin(), m->effects.end(), [id](const auto &fx) { return fx.id == id; });
        if (found == m->effects.end())
            return;
        const auto project = s.controller.projectIdentity;
        const auto descriptor = found->descriptor;
        const auto host = s.audio.preparedPluginHost(s.controller.project, id);
        const auto instance = host ? host->runtimeIdentity() : juce::String{};
        std::shared_ptr<const MixerEffectOrderIntent> order;
        try
        {
            order = MixerEffectOrderIntent::capture(s.controller.project, project, s.controller.revision,
                                                    insert, id);
        }
        catch (...)
        {
            s.status = "Could not prepare effect order intent";
            return;
        }
        const int slot = int(std::distance(m->effects.begin(), found));
        juce::PopupMenu menu;
        menu.addSectionHeader(found->name);
        menu.addItem(1, "Open native editor");
        menu.addItem(2, "Close native editor");
        menu.addItem(3, "Plugin controls...");
        menu.addSeparator();
        menu.addItem(4, "Bypass", true, found->bypass);
        menu.addItem(5, "Remove effect");
        menu.addSeparator();
        menu.addItem(6, "Move up", slot > 0);
        menu.addItem(7, "Move down", slot + 1 < int(m->effects.size()));
        menu.addSeparator();
        menu.addItem(8, "Reset host wet to100%", found->wet != 1);
        menu.addItem(9, "Host dry (0% wet)", found->wet != 0);
        const auto bounds = mixerEffectLayout().row(slot - effectSlotsOffset());
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(
                localAreaToGlobal(bounds)),
            [safe = juce::Component::SafePointer<Editor>(this), project, descriptor, instance, insert, id,
             order](int action)
            {
                if (!safe || !action)
                    return;
                auto &studio = safe->s;
                const auto *current = mixerInsert(studio.controller.project, insert);
                const auto *fx = studio.controller.project.plugin(id);
                if (studio.controller.projectIdentity != project || !current || !fx ||
                    fx->descriptor != descriptor ||
                    std::none_of(current->effects.begin(), current->effects.end(),
                                 [id](const auto &slot) { return slot.id == id; }))
                {
                    studio.status = "Effect changed; reopen its slot menu";
                    return;
                }
                if ((action == 6 || action == 7) && order)
                {
                    const auto result =
                        safe->executeEffectOrder(*order, order->sourceSlot + (action == 6 ? -1 : 1));
                    if (result.failed())
                        studio.status = result.getErrorMessage();
                }
                else if (action == 8 || action == 9)
                {
                    const auto valid =
                        order ? order->validate(studio.controller.project, studio.controller.projectIdentity,
                                                studio.controller.revision, studio.selectedInsert)
                              : juce::Result::fail("Effect mix context changed; reopen the menu");
                    const auto result = valid.failed() ? valid : safe->setEffectWet(id, action == 8 ? 1 : 0);
                    if (result.failed())
                        studio.status = result.getErrorMessage();
                }
                else if (action == 1 || action == 2)
                {
                    const auto now = studio.audio.preparedPluginHost(studio.controller.project, id);
                    if (!now || now->runtimeIdentity() != instance)
                    {
                        studio.status = "Plugin runtime changed; reopen its slot menu";
                        return;
                    }
                    studio.openPluginEditorUI(id, action == 1);
                }
                else if (action == 3)
                    studio.openPluginControlsUI(id);
                else if (action == 4 || action == 5)
                {
                    CommandArgs args;
                    args.target = id;
                    args.enabled = !fx->bypass;
                    studio.command(action == 4 ? "plugin.bypass" : "plugin.remove", args);
                }
                safe->repaint();
            });
    }
    int mixerWidth() const
    {
        // Retain every insert; use existing horizontal scrolling instead of tiny labels/targets.
        return std::max(108, (mixerStripWidth() - 30) / int(s.controller.project.mixer.size()));
    }
    // The Rack add button sits under the last visible Channel while there is room for it.
    bool rackPlusHover = false;
    juce::Rectangle<int> rackPlusBounds() const
    {
        if (type != 0)
            return {};
        const int footer = 44 + (int(s.visibleRackChannels().size()) - rackOffset) * rackRowHeight();
        if (footer < 44 || footer + 30 > rackContentBottom())
            return {};
        return {34, footer + 3, std::max(60, stepStart() - 44), 24};
    }
    int rackRows() const
    {
        return std::max(1, (rackContentBottom() - 44) / rackRowHeight());
    }
    int playlistRows() const
    {
        return std::max(1, (getHeight() - gridTop() - scrollBarThickness) / laneHeight());
    }
    void navigationChanged()
    {
        if (type == 0)
            syncRack();
        repaint();
        horizontalBar.repaint();
        verticalBar.repaint();
    }
    void savePlaylistView()
    {
        if (hasRegionView)
            return;
        savedZoom = zoom;
        savedScroll = scroll;
        savedPlaylistLaneScale = playlistLaneScale;
        savedLaneOffset = laneOffset;
        hasRegionView = true;
    }
    void restorePlaylistView()
    {
        if (!hasRegionView)
            return;
        zoom = savedZoom;
        scroll = savedScroll;
        playlistLaneScale = savedPlaylistLaneScale;
        laneOffset = savedLaneOffset;
        hasRegionView = false;
        navigationChanged();
    }
    void zoomPlaylistRegion(juce::Rectangle<float> region)
    {
        const auto start = beatAt(region.getX());
        const auto span = region.getWidth() / spacing();
        const auto firstLane = laneOffset + int((region.getY() - gridTop()) / laneHeight());
        const auto rows = std::max(1., double(region.getHeight()) / laneHeight());
        savePlaylistView();
        zoom = juce::jlimit(1.e-8, 256.,
                            std::max(1, getWidth() - leftEdge() - scrollBarThickness) /
                                (std::max(.125, span) * baseSpacing()));
        scroll = std::max(0., start);
        const auto base = juce::jlimit(40, 58, (getHeight() - gridTop()) / 8);
        playlistLaneScale = juce::jlimit(.25, 8., (noteBottom() - gridTop()) / (rows * base));
        laneOffset = juce::jlimit(0, std::max(0, visibleLaneCount() - playlistRows()), firstLane);
        navigationChanged();
    }
    int stepStart() const
    {
        return rackLabelWidth() + 8;
    }
    double stepWidth() const
    {
        auto *p = pattern();
        return std::max(14., (getWidth() - stepStart() - 12.) /
                                 std::max(16, p ? std::min(64, int(p->length * 4)) : 16));
    }
    double barBeats() const
    {
        if (type != 2)
            if (const auto *part = pattern())
                return patternBarBeats(s.controller.project, *part);
        return s.controller.project.numerator * 4. / s.controller.project.denominator;
    }
    double spacing() const
    {
        return zoom * baseSpacing();
    }
    int leftEdge() const
    {
        return type == 2 ? 134 + pickerWidth() : 62;
    }
    int visiblePitches() const
    {
        return juce::jlimit(1, 128,
                            int(std::max(1, noteBottom() - gridTop()) / (12. * verticalZoom) + 1.e-7));
    }
    float height() const
    {
        return std::max(1.f, (noteBottom() - gridTop()) / float(visiblePitches()));
    }
    double beatAt(float x) const
    {
        return std::max(0., (x - leftEdge()) / spacing() + scroll);
    }
    double snap(double v, const juce::ModifierKeys &m) const
    {
        return m.isAltDown() || s.snap == 0 ? v : std::round(v / s.snap) * s.snap;
    }
    int pitch(float y) const
    {
        return juce::jlimit(0, 127, topPitch - int((y - gridTop()) / height()));
    }
    Pattern *pattern() const
    {
        return s.controller.project.pattern(s.selectedPattern);
    }
    const std::vector<PianoNoteOccurrence> &pianoView() const
    {
        const auto *source = pattern();
        if (!source)
        {
            pianoViewCache.clear();
            pianoViewPattern = 0;
            return pianoViewCache;
        }
        if (pianoViewProject != s.controller.projectIdentity || pianoViewPattern != source->id ||
            pianoViewRevision != s.controller.revision || pianoViewSource != source->notes.data() ||
            pianoViewSourceCount != source->notes.size())
        {
            pianoViewCache = pianoNoteOccurrences(s.controller.project, *source);
            pianoViewProject = s.controller.projectIdentity;
            pianoViewPattern = source->id;
            pianoViewRevision = s.controller.revision;
            pianoViewSource = source->notes.data();
            pianoViewSourceCount = source->notes.size();
        }
        return pianoViewCache;
    }
    std::optional<PianoNoteOccurrence> pianoHit(juce::Point<float> point, bool otherChannelOnly = false) const
    {
        if (point.x < leftEdge() || point.y < gridTop() || point.y >= noteBottom())
            return {};
        const auto &view = pianoView();
        if (!otherChannelOnly)
            for (auto i = view.rbegin(); i != view.rend(); ++i)
                if (i->note.channel == s.selectedChannel && nr(i->note).contains(point))
                    return *i;
        if (ghosts && (otherChannelOnly || editableGhosts))
            for (auto i = view.rbegin(); i != view.rend(); ++i)
                if (i->note.channel != s.selectedChannel && nr(i->note).contains(point))
                    return *i;
        return {};
    }
    static void tempoDialog(const juce::String &title, double bpm, std::function<void(double, bool)> done)
    {
        auto *box = new juce::AlertWindow(title, "Tempo from this beat on. Ramp glides from the previous tempo.",
                                          juce::MessageBoxIconType::NoIcon);
        box->addTextEditor("bpm", juce::String(bpm, 2), "BPM (10..999)");
        box->addButton("Jump", 1, juce::KeyPress(juce::KeyPress::returnKey));
        box->addButton("Ramp", 2);
        box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        box->enterModalState(true, juce::ModalCallbackFunction::create(
            [box, done](int choice)
            {
                const double value = box->getTextEditorContents("bpm").getDoubleValue();
                if (choice > 0 && value >= 10 && value <= 999)
                    done(value, choice == 2);
            }), true);
    }
    juce::Rectangle<float> nr(const Note &n) const
    {
        return {float(62 + (n.beat - scroll) * spacing()), gridTop() + (topPitch - n.pitch) * height() + .5f,
                float(std::max(4., n.length * spacing() - 1)), std::max(1.f, height() - 1.f)};
    }
    // Playlist rows show the visible lanes (tracks under a collapsed group are hidden).
    int visibleLaneCount() const
    {
        return int(s.controller.project.visiblePlaylistLanes().size());
    }
    int laneAtRow(int row) const
    {
        const auto lanes = s.controller.project.visiblePlaylistLanes();
        if (row < 0 || lanes.empty())
            return 0;
        return row < int(lanes.size()) ? lanes[size_t(row)] : std::min(127, lanes.back() + row - int(lanes.size()) + 1);
    }
    int rowOfLane(int lane) const
    {
        const auto lanes = s.controller.project.visiblePlaylistLanes();
        const auto found = std::lower_bound(lanes.begin(), lanes.end(), lane);
        return found != lanes.end() && *found == lane ? int(found - lanes.begin()) : -1;
    }
    int laneAtY(int y) const
    {
        return juce::jlimit(0, 127, laneAtRow((y - gridTop()) / laneHeight() + laneOffset));
    }
    juce::Rectangle<float> cr(const Placement &p) const
    {
        const int row = type == 2 ? rowOfLane(p.lane) : p.lane;
        if (row < 0)
            return {}; // Hidden in a collapsed group.
        return {float(leftEdge() + (p.beat - scroll) * spacing()),
                float(gridTop() + 2 + (row - laneOffset) * laneHeight()), float(p.length * spacing() - 2),
                float(laneHeight() - 4)};
    }
    Id hit(juce::Point<float> point) const
    {
        if (point.x < leftEdge() || point.y < gridTop() || (type == 1 && point.y >= noteBottom()))
            return 0;
        if (type == 1)
        {
            if (const auto appearance = pianoHit(point))
                return appearance->note.id;
        }
        else
        {
            const auto clips = playlistClips();
            for (auto i = clips.rbegin(); i != clips.rend(); ++i)
                if (cr(*i).contains(point))
                    return i->id;
        }
        return 0;
    }
    void erase(juce::Point<float> point)
    {
        if (auto id = hit(point))
        {
            CommandArgs a;
            a.pattern = s.selectedPattern;
            a.ids = {id};
            s.command(type == 1 ? "notes.delete" : "clip.delete", a);
        }
    }
    void eraseDrag(juce::Point<float> point)
    {
        if (type != 2)
        {
            erase(point);
            return;
        }
        const auto viewport =
            juce::Rectangle<float>(float(leftEdge()), float(gridTop()),
                                   float(std::max(1, getWidth() - leftEdge() - scrollBarThickness)),
                                   float(std::max(1, noteBottom() - gridTop())));
        point = {juce::jlimit(viewport.getX(), viewport.getRight(), point.x),
                 juce::jlimit(viewport.getY(), viewport.getBottom(), point.y)};
        const juce::Line<float> stroke(lastErasePoint, point);
        CommandArgs args;
        for (const auto &clip : playlistClips())
        {
            const auto bounds = cr(clip).getIntersection(viewport);
            if (bounds.isEmpty())
                continue;
            juce::Path outline;
            outline.addRectangle(bounds);
            if (bounds.contains(lastErasePoint) || bounds.contains(point) || outline.intersectsLine(stroke))
                args.ids.push_back(clip.id);
        }
        lastErasePoint = point;
        if (!args.ids.empty())
            s.command("clip.delete", args);
    }
    void step(const juce::MouseEvent &e)
    {
        auto *p = pattern();
        int row = (e.y - 44) / rackRowHeight() + rackOffset;
        auto *channel = s.rackChannelAt(row);
        if (!p || e.y < 44 || row >= rackOffset + rackRows() || !channel || e.x < stepStart())
            return;
        int count = stepSequenceSize(*p);
        double width = stepWidth();
        int index = stepOffset + int((e.x - stepStart()) / width);
        if (index < 0 || index >= count)
            return;
        CommandArgs a;
        a.pattern = p->id;
        a.channel = channel->id;
        a.beat = index * .25;
        a.enabled = !e.mods.isRightButtonDown();
        s.command("steps.set", a);
    }

  public:
    juce::Result verifyMixerTargetMenus(juce::StringArray &passed);
    CommandArgs pickerSelection() const
    {
        CommandArgs args;
        args.text = audioPicker ? "audio" : automationPicker ? "automation" : "pattern";
        const auto active = audioPicker        ? audioPicker
                            : automationPicker ? automationPicker
                                               : s.selectedPattern;
        args.ids = {active};
        if (picker && picker->sourceKind() == args.text)
        {
            auto ids = picker->selectedIds();
            if (std::find(ids.begin(), ids.end(), active) != ids.end())
                args.ids = std::move(ids);
        }
        return args;
    }
    bool placeSources(CommandArgs args)
    {
        const auto before = s.controller.project.nextId;
        s.command("picker.place", args);
        if (s.controller.project.nextId == before)
            return false;
        s.selectedClips.clear();
        for (auto id = before; id < s.controller.project.nextId; ++id)
            s.selectedClips.push_back(id);
        s.refresh();
        return true;
    }
    ClipPicker *clipPicker() const
    {
        return picker.get();
    }
    bool isPickerShown() const
    {
        return pickerShown;
    }
    int actualPickerWidth() const
    {
        return pickerWidth();
    }
    void setPickerWidth(int width)
    {
        preferredPickerWidth = juce::jlimit(120, 280, width);
        resized();
        repaint();
    }
    void setPickerShown(bool visible)
    {
        pickerShown = visible;
        resized();
        repaint();
    }
    juce::Point<int> playlistPosition(double beat, int lane) const
    {
        return {leftEdge() + int((beat - scroll) * spacing()) + 2,
                gridTop() + (std::max(0, rowOfLane(lane)) - laneOffset) * laneHeight() + laneHeight() / 2};
    }
    juce::Rectangle<int> playlistGridBounds() const
    {
        return {leftEdge(), gridTop(), std::max(1, getWidth() - leftEdge() - rightInset()),
                std::max(1, getHeight() - gridTop() - scrollBarThickness)};
    }
    void syncPicker()
    {
        if (pickerSession != s.controller.projectIdentity)
        {
            pickerSession = s.controller.projectIdentity;
            audioPicker = automationPicker = 0;
        }
        if (picker)
            picker->sync();
        if (audioPicker && !s.controller.project.channel(audioPicker))
            audioPicker = 0;
        if (automationPicker && !s.controller.project.automationSource(automationPicker))
            automationPicker = 0;
    }
    bool placePickerSource(const juce::var &description, juce::Point<int> position)
    {
        CommandArgs args;
        if (type != 2 || position.x < leftEdge() || position.x >= getWidth() - rightInset() ||
            position.y < gridTop() || position.y >= getHeight() - scrollBarThickness ||
            !pickerPayload(description, s.controller.projectIdentity, args))
            return false;
        args.beat = snap(beatAt(float(position.x)), juce::ModifierKeys::getCurrentModifiersRealtime());
        args.lane = laneAtY(position.y);
        return placeSources(std::move(args));
    }
    juce::Point<float> automationPointLocation(Id clipId, Id pointId) const
    {
        if (const auto *clip = automationClip(clipId))
            if (const auto *source = s.controller.project.automationSource(clip->source))
                for (const auto &point : source->points)
                    if (point.id == pointId)
                        return automationPointPosition(clipId, point);
        return {};
    }
    juce::Rectangle<float> playlistClipBounds(Id id) const
    {
        for (auto &clip : playlistClips())
            if (clip.id == id)
                return cr(clip);
        return {};
    }
    Editor(Studio &owner, int kind) : s(owner), type(kind)
    {
        setWantsKeyboardFocus(true);
        // Screen readers announce each editor by name with its keyboard essentials.
        const char *titles[]{"Channel Rack", "Piano Roll", "Playlist", "Mixer"};
        const char *help[]{
            "Steps per Channel. Arrow keys move, Space plays, F2 renames, Delete removes the selection.",
            "Notes of the selected Channel. Tab and Shift+Tab step through notes, arrows move the selection, Shift+arrows resize, Delete removes, Ctrl+Z undoes.",
            "Song arrangement. Tab and Shift+Tab step through clips, arrows move clips, Ctrl+B duplicates, Delete removes, Ctrl-drag the ruler selects time.",
            "Mixer inserts. Arrows select inserts, F2 renames, Ctrl+click solos."};
        setTitle(titles[juce::jlimit(0, 3, kind)]);
        setHelpText(help[juce::jlimit(0, 3, kind)]);
        if (type == 0)
        {
            rackFilter.setComponentID("rack.filter");
            rackMenu.setComponentID("rack.menu");
            addAndMakeVisible(rackFilter);
            addAndMakeVisible(rackMenu);
            addAndMakeVisible(rackGraphButton);
            swingControl = std::make_unique<RackSwingControl>(s);
            addAndMakeVisible(*swingControl);
            rackGraphButton.setComponentID("rack.graph");
            rackGraphButton.setIcon(ToolbarButton::Icon::velocity);
            rackGraphButton.setTooltip(
                "Step Graph Editor | Velocity, pitch, local-sampler pan/fine | Ctrl+K");
            rackGraphButton.onClick = [this]
            {
                s.performAction("rack.graph");
                grabKeyboardFocus();
            };
            velocityGraph = std::make_unique<RackVelocityGraph>(s);
            addChildComponent(*velocityGraph);
            velocityGraph->focusRack = [this]
            {
                s.focus = Scope::rack;
                grabKeyboardFocus();
            };
            velocityGraph->wheel = [this](const auto &event, const auto &details)
            { mouseWheelMove(event.getEventRelativeTo(this), details); };
            rackFilter.setTooltip("Channel display filter | PgUp/PgDn | Menu: add, rename or delete groups");
            rackMenu.setTooltip("Channel groups and selection | Group selected: Alt+G");
            rackFilter.onChange = [this]
            {
                s.focus = Scope::rack;
                const auto id = rackFilter.getSelectedId();
                s.setRackFilter(id == 1 ? "all" : id == 2 ? "unsorted" : "group", id > 2 ? Id(id - 3) : 0);
                grabKeyboardFocus();
            };
            rackMenu.onClick = [this] { s.rackGroupMenu(&rackMenu); };
            rackFilter.contextMenu = [this] { s.rackGroupMenu(&rackFilter); };
        }
        if (type != 1)
        {
            addAndMakeVisible(horizontalBar);
            addAndMakeVisible(verticalBar);
            horizontalBar.allowZoom = type == 2;
            verticalBar.allowZoom = false;
            horizontalBar.read = [this]
            {
                if (type == 0)
                    return std::array<double, 3>{
                        double(stepOffset),
                        double(std::max(1, int((getWidth() - stepStart() - 8) / stepWidth()))),
                        double(pattern() ? stepSequenceSize(*pattern()) : 16)};
                if (type == 3)
                    return std::array<double, 3>{
                        mixerScroll, double(mixerStripWidth() - 12),
                        double(mixerWidth() * s.controller.project.mixer.size() + 12)};
                return std::array<double, 3>{scroll, viewBeats(), domain()};
            };
            horizontalBar.change = [this](double start, double span)
            {
                s.focus = Scope(type + 1);
                grabKeyboardFocus();
                if (type == 0)
                    setRackViewport(rackOffset, int(start));
                else if (type == 3)
                    mixerScroll = start;
                else
                {
                    zoom = juce::jlimit(1.e-8, 256.,
                                        std::max(1, getWidth() - leftEdge() - scrollBarThickness) /
                                            (span * baseSpacing()));
                    scroll = start;
                }
                navigationChanged();
            };
            verticalBar.read = [this]
            {
                if (type == 3)
                {
                    size_t count = 0;
                    for (auto &insert : s.controller.project.mixer)
                        if (insert.index == s.selectedInsert)
                            count = insert.effects.size();
                    const auto layout = mixerEffectLayout();
                    return std::array<double, 3>{double(layout.clampedOffset(effectOffset, int(count))),
                                                 double(std::max(1, layout.visibleRows())),
                                                 double(count + 1)};
                }
                if (type == 0)
                    return std::array<double, 3>{double(rackOffset), double(rackRows()),
                                                 double(s.visibleRackChannels().size())};
                return std::array<double, 3>{double(laneOffset), double(playlistRows()), double(visibleLaneCount())};
            };
            verticalBar.change = [this](double start, double)
            {
                s.focus = Scope(type + 1);
                grabKeyboardFocus();
                if (type == 0)
                    rackOffset = juce::jlimit(
                        0, std::max(0, int(s.visibleRackChannels().size()) - rackRows()), int(start));
                else if (type == 3)
                {
                    auto values = verticalBar.read();
                    setEffectSlotsOffset(
                        juce::jlimit(0, std::max(0, int(values[2] - values[1])), int(start)));
                    return;
                }
                else
                    laneOffset = juce::jlimit(0, std::max(0, visibleLaneCount() - playlistRows()), int(start));
                navigationChanged();
            };
            horizontalBar.setTooltip(type == 2 ? "Scroll timeline; drag thumb edges to zoom"
                                               : "Scroll to hidden steps or Mixer inserts");
            verticalBar.setTooltip(type == 3 ? "Scroll ordered effect slots"
                                             : "Scroll to hidden Channels or Playlist tracks");
            verticalBar.setVisible(type != 0 && (type != 3 || mixerStripWidth() < getWidth()));
        }
        if (type == 2)
        {
            for (auto *control :
                 std::initializer_list<juce::Component *>{&playlistMenu, &playlistGrouping, &playlistSnap,
                                                          &playlistZoomOut, &playlistZoomIn, &playlistFit})
                addAndMakeVisible(control);
            playlistSnap.addItemList({"Snap: None", "Snap: 1/4", "Snap: 1/2", "Snap: Beat", "Snap: Bar"}, 1);
            playlistSnap.setSelectedId(2, juce::dontSendNotification);
            playlistSnap.onChange = [this]
            {
                const double values[]{0, .25, .5, 1, barBeats()};
                s.snap = values[juce::jlimit(0, 4, playlistSnap.getSelectedId() - 1)];
                s.refresh();
            };
            playlistZoomOut.onClick = [this] { viewAction("playlist.zoomOut"); };
            playlistZoomIn.onClick = [this] { viewAction("playlist.zoomIn"); };
            playlistFit.onClick = [this] { viewAction("playlist.fit"); };
            playlistGrouping.setIcon(ToolbarButton::Icon::group);
            playlistGrouping.setComponentID("playlist.grouping");
            playlistGrouping.setTooltip("Enable Clip grouping. Group selected: Shift+G. Ungroup: Alt+G. "
                                        "Membership stays when disabled.");
            playlistGrouping.onClick = [this]
            {
                s.focus = Scope::playlist;
                grabKeyboardFocus();
                s.performAction("playlist.grouping");
            };
            playlistMenu.onClick = [this]
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Undo");
                menu.addItem(2, "Redo");
                menu.addSeparator();
                menu.addItem(3, "Duplicate selected clips", !s.selectedClips.empty());
                menu.addItem(4, "Make Unique", !s.selectedClips.empty());
                menu.addItem(5, "Delete selected clips", !s.selectedClips.empty());
                menu.addSeparator();
                menu.addItem(8, "Group Clips (Shift+G)", s.effectiveClipSelection().size() > 1);
                menu.addItem(9, "Ungroup Clips (Alt+G)", !s.selectedClips.empty());
                menu.addItem(10, "Enable Clip grouping", true, s.controller.clipGroupingEnabled);
                menu.addSeparator();
                {
                    juce::PopupMenu arrangementsMenu;
                    const auto &project = s.controller.project;
                    arrangementsMenu.addItem(-1, arrangements::currentName(project), true, true);
                    for (size_t i = 0; i < project.arrangements.size(); ++i)
                        arrangementsMenu.addItem(400 + int(i), project.arrangements[i].name);
                    arrangementsMenu.addSeparator();
                    arrangementsMenu.addItem(390, "New arrangement");
                    arrangementsMenu.addItem(391, "Clone arrangement");
                    arrangementsMenu.addItem(392, "Rename arrangement...");
                    arrangementsMenu.addItem(393, "Delete arrangement", !project.arrangements.empty());
                    menu.addSubMenu("Arrangements (" + arrangements::currentName(project) + ")", arrangementsMenu);
                }
                menu.addItem(11, "Add marker at playhead");
                const bool markers = !s.controller.project.markers.empty();
                menu.addItem(13, "Jump to next marker", markers);
                menu.addItem(14, "Jump to previous marker", markers);
                menu.addItem(15, "Select (loop) section between markers", markers);
                menu.addItem(12, "Add tempo change at playhead...");
                juce::PopupMenu curves;
                curves.addItem(2001, "Linear");
                curves.addItem(2002, "Ease in and out");
                curves.addItem(2003, "Ease in (starts slowly)");
                curves.addItem(2004, "Ease out (ends slowly)");
                menu.addSubMenu("Tempo curve over the time selection", curves, s.playlistRange.has_value());
                juce::PopupMenu meters;
                for (const auto &[n, d] : std::vector<std::pair<int, int>>{{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}, {9, 8}, {12, 8}})
                    meters.addItem(1000 + n * 100 + d, juce::String(n) + "/" + juce::String(d));
                const auto atBar = s.controller.project.barPosition(std::max(0., s.audio.positionBeat()));
                const bool changeHere = std::any_of(s.controller.project.meterChanges.begin(),
                                                    s.controller.project.meterChanges.end(), [&](const auto &c)
                                                    { return std::abs(c.beat - (std::max(0., s.audio.positionBeat()) - atBar.beatInBar)) < 1.e-6; });
                meters.addSeparator();
                meters.addItem(1999, "Remove the change at this bar", changeHere);
                menu.addSubMenu("Time signature from the next bar (now " + juce::String(atBar.numerator) + "/" +
                                    juce::String(atBar.denominator) + ")",
                                meters);
                menu.addSeparator();
                menu.addItem(7, "Picker Panel (Alt+P)", true, pickerShown);
                menu.addItem(6, "Draw selected Pattern", true, automationPicker == 0);
                std::vector<Id> sources;
                juce::PopupMenu automation;
                for (const auto &source : s.controller.project.automation.sources)
                {
                    sources.push_back(source.id);
                    automation.addItem(100 + int(sources.size()), juce::String::fromUTF8(source.name.c_str()),
                                       true, automationPicker == source.id);
                }
                if (!sources.empty())
                    menu.addSubMenu("Draw automation source", automation);
                menu.showMenuAsync(
                    juce::PopupMenu::Options().withTargetComponent(&playlistMenu),
                    [safe = juce::Component::SafePointer<Editor>(this), sources](int result)
                    {
                        if (!safe || !result)
                            return;
                        if (result >= 13 && result <= 15)
                        {
                            safe->s.performAction(result == 13   ? "marker.next"
                                                  : result == 14 ? "marker.previous"
                                                                 : "marker.selectSection");
                            return;
                        }
                        if (result == 8 || result == 9 || result == 10)
                        {
                            safe->s.focus = Scope::playlist;
                            safe->s.performAction(result == 8   ? "clip.group"
                                                  : result == 9 ? "clip.ungroup"
                                                                : "playlist.grouping");
                            return;
                        }
                        if (result == 7)
                        {
                            safe->viewAction("playlist.picker");
                            return;
                        }
                        if (result >= 390 && result < 400 + 256)
                        {
                            CommandArgs a;
                            if (result >= 400)
                            {
                                a.target = safe->s.controller.project.arrangements[size_t(result - 400)].id;
                                safe->s.command("arrangement.select", a);
                            }
                            else if (result == 390)
                                safe->s.command("arrangement.add", a);
                            else if (result == 391)
                                safe->s.command("arrangement.clone", a);
                            else if (result == 393)
                                safe->s.command("arrangement.delete", a);
                            else
                            {
                                auto *box = new juce::AlertWindow("Rename arrangement", {}, juce::MessageBoxIconType::NoIcon);
                                box->addTextEditor("name", arrangements::currentName(safe->s.controller.project));
                                box->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
                                box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                                box->enterModalState(true, juce::ModalCallbackFunction::create(
                                    [safe, box](int ok)
                                    {
                                        if (!safe || ok != 1)
                                            return;
                                        CommandArgs rename;
                                        rename.text = box->getTextEditorContents("name").substring(0, 128);
                                        safe->s.command("arrangement.rename", rename);
                                    }), true);
                            }
                            return;
                        }
                        if (result >= 1000 && result <= 1999)
                        {
                            // A time-signature change starts on the next bar line (or removes this bar's).
                            const auto &project = safe->s.controller.project;
                            const double now = std::max(0., safe->s.audio.positionBeat());
                            CommandArgs a;
                            if (result == 1999)
                            {
                                const double barStart = now - project.barPosition(now).beatInBar;
                                for (const auto &c : project.meterChanges)
                                    if (std::abs(c.beat - barStart) < 1.e-6)
                                        a.target = c.id;
                                safe->s.command("meter.change.delete", a);
                                return;
                            }
                            a.beat = std::max(project.nextBarLine(now), project.barPosition(now).barLength);
                            a.numerator = (result - 1000) / 100;
                            a.denominator = (result - 1000) % 100;
                            safe->s.command("meter.change.add", a);
                            return;
                        }
                        if (result >= 2001 && result <= 2004 && safe->s.playlistRange)
                        {
                            const auto range = *safe->s.playlistRange;
                            const char *shapes[]{"linear", "ease", "in", "out"};
                            const juce::String shape = shapes[result - 2001];
                            tempoDialog("Tempo at the end of the selection",
                                        safe->s.controller.project.songTempo().bpmAt(range.second),
                                        [safe, range, shape](double value, bool)
                                        {
                                            if (!safe)
                                                return;
                                            CommandArgs a;
                                            a.beat = range.first;
                                            a.length = range.second - range.first;
                                            a.value = value;
                                            a.text = shape;
                                            safe->s.command("tempo.curve", a);
                                        });
                            return;
                        }
                        if (result == 12)
                        {
                            const double at = std::max(0., safe->s.audio.positionBeat());
                            if (at <= 0)
                            {
                                safe->s.status = "Move the playhead past beat 0: the project tempo covers the start";
                                return;
                            }
                            tempoDialog("Add tempo change", safe->s.controller.project.songTempo().bpmAt(at),
                                        [safe, at](double value, bool ramp)
                                        {
                                            if (!safe)
                                                return;
                                            CommandArgs a;
                                            a.beat = at;
                                            a.value = value;
                                            a.step = ramp;
                                            safe->s.command("tempo.change.add", a);
                                        });
                            return;
                        }
                        if (result == 11)
                        {
                            CommandArgs a;
                            a.beat = std::max(0., safe->s.audio.positionBeat());
                            safe->s.command("marker.add", a);
                            return;
                        }
                        safe->s.focus = Scope::playlist;
                        if (result == 6 || result > 100)
                        {
                            safe->automationPicker = result == 6 ? 0 : sources.at(size_t(result - 101));
                            safe->audioPicker = 0;
                            safe->s.status = result == 6 ? "Draw Pattern" : "Draw linked automation clips";
                            safe->s.refresh();
                            return;
                        }
                        const char *actions[]{"edit.undo", "edit.redo", "clip.duplicate", "clip.unique",
                                              "clip.delete"};
                        if (result == 1)
                            reportHistoryResult(safe->s, safe->s.controller.undoEditChecked());
                        else if (result == 2)
                            reportHistoryResult(safe->s, safe->s.controller.redoEditChecked());
                        else
                        {
                            CommandArgs args;
                            args.ids = safe->s.effectiveClipSelection();
                            if (args.ids.empty())
                                return;
                            args.target = args.ids.front();
                            double first = 1.e9, last = 0;
                            for (auto &clip : safe->playlistClips())
                                if (selected(args.ids, clip.id))
                                {
                                    first = std::min(first, clip.beat);
                                    last = std::max(last, clip.beat + clip.length);
                                }
                            args.beat = std::ceil((last - first) / safe->barBeats()) * safe->barBeats();
                            safe->s.command(actions[result - 1], args);
                        }
                        safe->s.refresh();
                    });
            };
            const std::array<juce::String, 8> names{"Draw", "Paint",  "Slip",   "Slice",
                                                    "Mute", "Select", "Delete", "Zoom"};
            for (int index = 0; index < int(playlistTools.size()); ++index)
            {
                auto &button = playlistTools[size_t(index)];
                addAndMakeVisible(button);
                button.setButtonText(names[size_t(index)]);
                button.setIcon(std::array<ToolbarButton::Icon, 8>{
                    ToolbarButton::Icon::draw, ToolbarButton::Icon::paint, ToolbarButton::Icon::slip,
                    ToolbarButton::Icon::slice, ToolbarButton::Icon::mute, ToolbarButton::Icon::select,
                    ToolbarButton::Icon::erase, ToolbarButton::Icon::zoom}[size_t(index)]);
                button.setTooltip(names[size_t(index)] + " - Playlist tool");
                button.onClick = [this, index]
                {
                    s.focus = Scope::playlist;
                    grabKeyboardFocus();
                    setPlaylistTool(index);
                };
            }
            playlistMenu.setIcon(ToolbarButton::Icon::menu);
            playlistMenu.setTooltip("Playlist menu: undo, redo, duplicate, Make Unique and delete");
            playlistTools[0].setTooltip("Draw [P]: place and edit clips");
            playlistTools[1].setTooltip("Paint [B]: paint repeated clips");
            playlistTools[2].setTooltip("Slip [S]: drag source content; clip boundaries stay fixed");
            playlistTools[3].setTooltip(
                "Slice [C]: drag vertically; Shift-click cuts immediately; Alt bypasses snap");
            playlistTools[4].setTooltip(
                "Mute [T]: click/drag to mute clips independently of Channel and lane");
            playlistTools[5].setTooltip("Select [E]: click or drag a region; Shift adds/removes selection");
            playlistTools[6].setTooltip(
                "Delete [D]: click/drag to erase; Shift-drag selects; Escape cancels");
            playlistTools[7].setTooltip("Zoom [Shift+Z]: drag a region; right-click restores previous view");
            setPlaylistTool(0);
            picker = std::make_unique<ClipPicker>(s);
            picker->choose = [this](int kind, Id id)
            {
                automationPicker = kind == 2 ? id : 0;
                audioPicker = kind == 1 ? id : 0;
            };
            addAndMakeVisible(*picker);
            addAndMakeVisible(pickerDivider);
            pickerDivider.move = [this](int width) { setPickerWidth(width); };
        }
        if (type == 1)
        {
            setupNoteModesToolbar();
            addAndMakeVisible(pianoSlice);
            pianoSlice.setIcon(ToolbarButton::Icon::slice);
            pianoSlice.setComponentID("piano.toolSlice");
            pianoSlice.setTooltip("Slice notes [C]. Drag a cut line, release to split; Shift click cuts the "
                                  "pointed row; Alt bypasses snap. Ctrl selects. Escape cancels.");
            pianoSlice.onClick = [this]
            {
                grabKeyboardFocus();
                s.focus = Scope::piano;
                s.performAction("piano.toolSlice");
            };
            addAndMakeVisible(pianoMute);
            pianoMute.setIcon(ToolbarButton::Icon::mute);
            pianoMute.setComponentID("piano.toolMute");
            pianoMute.setTooltip("Mute notes [T]. Click a muted note to unmute; drag paints the first note's "
                                 "state. P returns to Draw. Right-click deletes; Ctrl selects.");
            pianoMute.onClick = [this]
            {
                s.focus = Scope::piano;
                grabKeyboardFocus();
                s.performAction("piano.toolMute");
            };
            for (auto *c : std::initializer_list<juce::Component *>{
                     &horizontalBar, &verticalBar, &ghostButton, &editGhostButton, &midiFileButton})
                addAndMakeVisible(c);
            horizontalBar.read = [this] { return std::array<double, 3>{scroll, viewBeats(), domain()}; };
            horizontalBar.change = [this](double start, double span)
            {
                s.focus = Scope::piano;
                zoom = juce::jlimit(1.e-8, 256.,
                                    std::max(1, getWidth() - leftEdge() - scrollBarThickness) /
                                        (span * baseSpacing()));
                scroll = start;
                repaint();
            };
            verticalBar.read = [this]
            { return std::array<double, 3>{double(127 - topPitch), double(visiblePitches()), 128.}; };
            verticalBar.change = [this](double start, double span)
            {
                s.focus = Scope::piano;
                verticalZoom = juce::jlimit(.1, 8., std::max(1, noteBottom() - gridTop()) / (span * 12.));
                topPitch = juce::jlimit(visiblePitches() - 1, 127, 127 - int(start));
                repaint();
            };
            midiFileButton.onClick = [this]
            {
                s.focus = Scope::piano;
                s.midiFileMenu();
            };
            midiFileButton.setTooltip("Piano Roll tools, MIDI file import and export");
            ghostButton.onClick = [this] { viewAction("piano.ghosts"); };
            editGhostButton.onClick = [this] { viewAction("piano.editGhosts"); };
            const char *names[]{"H-", "H+", "V-", "V+", "Fit"};
            for (int i = 0; i < 5; ++i)
            {
                auto &b = viewButtons[size_t(i)];
                b.setButtonText(names[i]);
                addAndMakeVisible(b);
                b.onClick = [this, i]
                {
                    s.focus = Scope::piano;
                    if (i < 2)
                        viewAction(i ? "piano.zoomIn" : "piano.zoomOut");
                    else if (i == 4)
                        viewAction("piano.fit");
                    else
                    {
                        verticalZoom = juce::jlimit(.1, 8., verticalZoom * (i == 3 ? 1.25 : .8));
                        topPitch = juce::jlimit(visiblePitches() - 1, 127, topPitch);
                        repaint();
                    }
                };
            }
            horizontalBar.setTooltip("Drag thumb to scroll; drag its edges to zoom horizontally");
            verticalBar.setTooltip("Drag thumb to change pitch range; drag edges to zoom vertically");
        }
    }
    // Current state in words for assistive technology, refreshed with the editor.
    juce::String accessibleSummary() const
    {
        const auto &project = s.controller.project;
        if (type == 1)
        {
            const auto *part = project.pattern(s.selectedPattern);
            const auto *channel = project.channel(s.selectedChannel);
            int notes = 0;
            if (part)
                for (const auto &note : part->notes)
                    notes += note.channel == s.selectedChannel;
            return (channel ? channel->name : juce::String("No Channel")) + ", " + juce::String(notes) + " notes, " +
                   juce::String(int(s.selectedNotes.size())) + " selected";
        }
        if (type == 2)
            return juce::String(int(project.placements.size() + project.audioPlacements.size())) + " clips, " +
                   juce::String(int(s.selectedClips.size())) + " selected, " +
                   juce::String(int(project.markers.size())) + " markers";
        if (type == 3)
            return juce::String(int(project.mixer.size())) + " inserts, insert " + juce::String(s.selectedInsert) +
                   " selected";
        return juce::String(int(project.channels.size())) + " Channels";
    }
    juce::Result verifyRackGroups(const juce::File &);
    RackSwingControl *rackSwingControl() const
    {
        return swingControl.get();
    }
    juce::Result verifyStepFill(const juce::File &);
    juce::Result verifyChannelSolo(const juce::File &);
    juce::var rackViewport() const
    {
        const int count = pattern() ? stepSequenceSize(*pattern()) : 16;
        const int page = std::max(1, int((getWidth() - stepStart() - 8) / stepWidth()));
        auto *state = new juce::DynamicObject;
        state->setProperty("firstRow", rackOffset);
        state->setProperty("rowsPerPage", rackRows());
        state->setProperty("maximumFirstRow", std::max(0, int(s.visibleRackChannels().size()) - rackRows()));
        state->setProperty("firstStep", stepOffset);
        state->setProperty("totalSteps", count);
        state->setProperty("stepsPerPage", page);
        state->setProperty("maximumFirstStep", std::max(0, count - page));
        return juce::var(state);
    }
    void setRackViewport(int row, int step)
    {
        const auto state = rackViewport();
        const int nextRow = juce::jlimit(0, int(state["maximumFirstRow"]), row);
        const int nextStep = juce::jlimit(0, int(state["maximumFirstStep"]), step);
        if (nextRow != rackOffset || nextStep != stepOffset)
            finishRackGesture();
        rackOffset = nextRow;
        stepOffset = nextStep;
        navigationChanged();
    }
    void finishRackGesture()
    {
        if (type != 0)
            return;
        finishVelocityGesture(true);
        if (dragging)
        {
            dragging = false;
            finishUiGesture(s, "Edit steps");
        }
        rackSelecting = false;
        rackSelectionVisited.clear();
    }
    juce::Result finishVelocityGesture(bool commit)
    {
        if (velocityGraph)
            return velocityGraph->finish(commit);
        return juce::Result::ok();
    }
    bool velocityGestureActive() const
    {
        return velocityGraph && velocityGraph->ownsGesture();
    }
    RackVelocityGraph *rackVelocityGraph() const
    {
        return velocityGraph.get();
    }
    void syncRack()
    {
        if (type != 0)
            return;
        const auto key = s.controller.projectIdentity + ":" + s.rackFilter + ":" + juce::String(s.rackGroup);
        const auto rows = s.visibleRackChannels();
        const bool reset = key != rackViewKey;
        const bool changed = reset || rows != rackRowIds;
        rackViewKey = key;
        rackRowIds = rows;
        if (changed)
            finishRackGesture();
        if (reset)
            rackOffset = 0;
        rackOffset = juce::jlimit(0, std::max(0, int(rows.size()) - rackRows()), rackOffset);
        juce::String names;
        for (const auto &group : s.controller.project.channelGroups)
            names += juce::String(group.id) + ":" + group.name + "\n";
        if (names != rackGroupNames || rackFilter.getNumItems() == 0)
        {
            rackGroupNames = names;
            rackFilter.clear(juce::dontSendNotification);
            rackFilter.addItem("All", 1);
            rackFilter.addItem("Unsorted", 2);
            for (const auto &group : s.controller.project.channelGroups)
                rackFilter.addItem(group.name, int(group.id) + 3);
        }
        rackFilter.setSelectedId(s.rackFilter == "all"        ? 1
                                 : s.rackFilter == "unsorted" ? 2
                                                              : int(s.rackGroup) + 3,
                                 juce::dontSendNotification);
        const int width = std::min(120, std::max(80, getWidth() - 190));
        rackFilter.setBounds(10, 4, width, 24);
        rackMenu.setBounds(width + 16, 4, 46, 24);
        rackGraphButton.setBounds(width + 68, 4, 28, 24);
        rackGraphButton.setToggleState(s.rackGraphVisible, juce::dontSendNotification);
        if (swingControl)
        {
            swingControl->setVisible(getWidth() >= 420);
            swingControl->setBounds(getWidth() - 150, 3, 78, 25);
            swingControl->sync();
        }
        if (velocityGraph)
        {
            velocityGraph->left = stepStart();
            velocityGraph->offset = stepOffset;
            velocityGraph->width = stepWidth();
            velocityGraph->setBounds(0, rackContentBottom(), getWidth(), rackGraphHeight());
            velocityGraph->setVisible(s.rackGraphVisible);
            velocityGraph->sync();
        }
    }
    void viewAction(const juce::String &action)
    {
        if (type == 2)
        {
            s.focus = Scope::playlist;
            if (action == "playlist.picker")
            {
                setPickerShown(!pickerShown);
                return;
            }
            if (action == "playlist.zoomIn" || action == "playlist.zoomOut")
                setZoom(zoom * (action == "playlist.zoomIn" ? 1.25 : .8),
                        juce::jlimit(leftEdge(), getWidth() - rightInset(), getMouseXYRelative().x));
            else if (action == "playlist.fit")
            {
                if (hasRegionView)
                    restorePlaylistView();
                else
                {
                    savePlaylistView();
                    scroll = 0;
                    zoom = std::max(1, getWidth() - leftEdge() - scrollBarThickness) /
                           (std::max(16., s.controller.project.endBeat() + 4) * baseSpacing());
                    laneOffset = 0;
                    playlistLaneScale = 1;
                }
            }
            else if (action == "playlist.zoomSelection")
            {
                juce::Rectangle<float> region;
                const auto effective = s.effectiveClipSelection();
                for (auto &clip : playlistClips())
                    if (selected(effective, clip.id))
                        region = region.getUnion(cr(clip));
                if (!region.isEmpty())
                    zoomPlaylistRegion(region.expanded(4.f));
            }
            else if (action == "playlist.zoom1" || action == "playlist.zoom2" || action == "playlist.zoom3")
            {
                savePlaylistView();
                const auto span = action == "playlist.zoom1" ? 16. : action == "playlist.zoom2" ? 32. : 64.;
                zoom = std::max(1, getWidth() - leftEdge() - scrollBarThickness) / (span * baseSpacing());
            }
            navigationChanged();
            return;
        }
        if (type != 1)
            return;
        s.focus = Scope::piano;
        if (action == "piano.ghosts")
        {
            ghosts = !ghosts;
            if (!ghosts)
                editableGhosts = false;
        }
        else if (action == "piano.editGhosts")
        {
            editableGhosts = !editableGhosts;
            if (editableGhosts)
                ghosts = true;
        }
        else if (action == "piano.fit")
        {
            auto *p = pattern();
            zoom = std::max(1, getWidth() - leftEdge() - scrollBarThickness) /
                   (std::max(4., p ? p->length : 4.) * baseSpacing());
            scroll = 0;
            revealChannel(true);
        }
        else if (action == "piano.selectionZoom")
        {
            if (!hasTimeSelection())
            {
                s.status = "No Piano Roll timeline selection: Ctrl-drag the ruler first";
                return;
            }
            zoom = juce::jlimit(1.e-8, 256.,
                                std::max(1, getWidth() - leftEdge() - scrollBarThickness) /
                                    ((timeEnd - timeStart) * baseSpacing()));
            scroll = timeStart;
        }
        else if (action.startsWith("piano.preset"))
        {
            zoom = action.endsWith("1") ? .5 : action.endsWith("2") ? 1. : 2.;
            scroll = 0;
        }
        else
            setZoom(zoom * (action == "piano.zoomIn" ? 1.25 : .8),
                    juce::jlimit(double(leftEdge()), double(getWidth() - rightInset()),
                                 double(getMouseXYRelative().x)));
        if (!editableGhosts)
            if (auto *p = pattern())
                std::erase_if(s.selectedNotes,
                              [&](Id id)
                              {
                                  for (auto &n : p->notes)
                                      if (n.id == id)
                                          return n.channel != s.selectedChannel;
                                  return false;
                              });
        ghostButton.setToggleState(ghosts, juce::dontSendNotification);
        editGhostButton.setToggleState(editableGhosts, juce::dontSendNotification);
        repaint();
        horizontalBar.repaint();
        verticalBar.repaint();
    }
    juce::Rectangle<float> noteBounds(const Note &note) const
    {
        return nr(note);
    }
    juce::Rectangle<int> pianoNoteViewport() const
    {
        return {leftEdge(), gridTop(), std::max(0, getWidth() - leftEdge() - scrollBarThickness),
                std::max(0, noteBottom() - gridTop())};
    }
    float keyY(int note) const
    {
        return gridTop() + (topPitch - note + .5f) * height();
    }
    double beatUnder(float x) const
    {
        return beatAt(x);
    }
    double visibleBeatCount() const
    {
        return viewBeats();
    }
    double repeatInterval() const
    {
        return hasTimeSelection() ? timeEnd - timeStart : 0.;
    }
    juce::Rectangle<int> stretchHandleBounds() const
    {
        if (type != 1 || noteSliceTool || noteMuteTool || !pattern())
            return {};
        juce::Rectangle<float> bounds;
        auto include = [&](const Note &n)
        {
            if (canEdit(n) && selected(s.selectedNotes, n.id))
                bounds = bounds.isEmpty() ? nr(n) : bounds.getUnion(nr(n));
        };
        if (s.noteStretchPending() && s.noteStretchSession->args.pattern == s.selectedPattern)
            for (const auto &note : s.noteStretchSession->plan.notes)
                include(note);
        else
            for (const auto &note : pattern()->notes)
                include(note);
        if (bounds.isEmpty() || getWidth() <= leftEdge() + scrollBarThickness + 24 ||
            noteBottom() <= gridTop() + 20)
            return {};
        return {juce::jlimit(leftEdge(), getWidth() - scrollBarThickness - 10,
                             int(std::round(bounds.getRight())) + 4),
                juce::jlimit(gridTop(), noteBottom() - 19, int(std::round(bounds.getCentreY())) - 9), 7, 18};
    }
    juce::Rectangle<int> mixerRouteButtonBounds(int insert) const
    {
        for (size_t i = 0; i < s.controller.project.mixer.size(); ++i)
            if (s.controller.project.mixer[i].index == insert)
                return {14 + int(i) * mixerWidth() - int(mixerScroll), getHeight() - 42, 26, 27};
        return {};
    }
    juce::Rectangle<int> mixerSendLevelBounds(int insert) const
    {
        auto button = mixerRouteButtonBounds(insert);
        return button.isEmpty()
                   ? button
                   : juce::Rectangle<int>{button.getX() + mixerWidth() - 43, button.getY(), 28, 27};
    }
    bool mixerSendLevelHit(int insert, juce::Point<float> point) const
    {
        return type == 3 && point.x >= 0 && point.x < mixerStripWidth() && point.y >= 0 &&
               point.y < getHeight() - scrollBarThickness &&
               mixerSendLevelBounds(insert).contains(point.toInt());
    }
    bool mixerSendGestureActive() const
    {
        return bool(mixerStereo) || bool(effectWet) || bool(effectOrder) || mixerSendSource >= 0 ||
               mixerPanSource >= 0 || (type == 3 && dragging);
    }
    juce::Rectangle<int> mixerNameBounds(int insert) const
    {
        auto b = mixerRouteButtonBounds(insert);
        return {b.getX() - 6, 34, mixerWidth() - 9, 24};
    }
    int mixerFaderTravel() const
    {
        // Logical coordinates: tall/maximised windows must not require huge hand travel.
        return juce::jlimit(1, 280, getHeight() - 172);
    }
    juce::Rectangle<int> mixerFaderBounds(int insert) const
    {
        auto b = mixerNameBounds(insert);
        return {b.getX() + mixerWidth() - 25, 100, 6, mixerFaderTravel()};
    }
    juce::Rectangle<int> mixerGainReadoutBounds(int insert) const
    {
        const auto name = mixerNameBounds(insert);
        return {name.getX() + 6, 110 + mixerFaderTravel(), mixerWidth() - 16, 20};
    }
    juce::Rectangle<float> mixerFaderThumbBounds(int insert, float gain) const
    {
        const auto rail = mixerFaderBounds(insert);
        return {float(rail.getCentreX() - 16), mixerGainY(gain) - 7.f, 32.f, 16.f};
    }
    float mixerGainY(float gain) const
    {
        return 100.f + (1.f - juce::jlimit(0.f, 1.f, gain / 2.f)) * mixerFaderTravel();
    }
    double mixerGainAtY(float y) const
    {
        return juce::jlimit(0., 2., (1. - (y - 100.) / mixerFaderTravel()) * 2.);
    }
    float mixerMeterY(float db) const
    {
        // The scale describes actual signal amplitude, not the linear fader position.
        return 100.f + (1.f - juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f)) * mixerFaderTravel();
    }
    juce::Rectangle<int> mixerMeterHeaderBounds(int insert) const
    {
        const auto meter = mixerMeterBounds(insert);
        return meter.isEmpty() ? juce::Rectangle<int>{} : meter.withHeight(11);
    }
    juce::Rectangle<int> mixerMeterTickLabelBounds(int insert, int db) const
    {
        const auto header = mixerMeterHeaderBounds(insert);
        if (header.isEmpty())
            return {};
        const auto height = mixerFaderTravel();
        // A compact rail may be shorter than two labels. Keep the identified
        // zero endpoint; never paint different dB values on the same pixels.
        if (height < 27 && db != 0)
            return {};
        const auto name = mixerNameBounds(insert);
        // Reserve actual text separation: the header ends at100 and the
        // zero endpoint used to start at100, crowding adjacent glyphs.
        // Keep the signal rail/tick positions and reset hit target unchanged.
        const int top = header.getBottom() + 3;
        const auto y =
            juce::jlimit(top, std::max(top, 100 + height - 12), juce::roundToInt(mixerMeterY(float(db))) - 6);
        return {name.getX() + 24, y, 24, 12};
    }
    juce::Rectangle<int> mixerMeterBounds(int insert) const
    {
        const auto b = mixerNameBounds(insert);
        return getHeight() < 180 ? juce::Rectangle<int>{}
                                 : juce::Rectangle<int>{b.getX() + 4, 89, 44, mixerFaderTravel() + 11};
    }
    juce::Rectangle<int> mixerMuteBounds(int insert) const
    {
        auto b = mixerNameBounds(insert);
        return getHeight() < 180 ? juce::Rectangle<int>{} : juce::Rectangle<int>{b.getX() + 2, 63, 26, 26};
    }
    juce::Rectangle<int> mixerPanBounds(int insert) const
    {
        auto b = mixerNameBounds(insert);
        return getHeight() < 180 ? juce::Rectangle<int>{}
                                 : juce::Rectangle<int>{b.getX() + 34, 63, b.getWidth() - 36, 26};
    }
    juce::Rectangle<int> mixerStereoSeparationBounds(int insert) const
    {
        const auto name = mixerNameBounds(insert);
        const int y = 286 + mixerFaderTravel(); // Eight px below the last existing RMS readout.
        if (type != 3 || y + 68 > getHeight() - 48)
            return {}; // Context-menu controls remain available in compact strips.
        return {name.getX() + 4, y, mixerWidth() - 17, 36};
    }
    juce::Rectangle<int> mixerPhaseInvertBounds(int insert) const
    {
        const auto separation = mixerStereoSeparationBounds(insert);
        return separation.isEmpty() ? juce::Rectangle<int>{}
                                    : juce::Rectangle<int>{separation.getX(), separation.getBottom() + 4,
                                                           (separation.getWidth() - 6) / 2, 28};
    }
    juce::Rectangle<int> mixerStereoSwapBounds(int insert) const
    {
        const auto separation = mixerStereoSeparationBounds(insert);
        const auto phase = mixerPhaseInvertBounds(insert);
        return phase.isEmpty()
                   ? juce::Rectangle<int>{}
                   : juce::Rectangle<int>{phase.getRight() + 6, phase.getY(),
                                          separation.getRight() - phase.getRight() - 6, phase.getHeight()};
    }
    juce::String mixerStereoTooltipAt(juce::Point<float> point) const
    {
        if (type == 3)
            for (const auto &insert : s.controller.project.mixer)
            {
                if (mixerControlHit(mixerStereoSeparationBounds(insert.index), point))
                    return "Stereo separation: -100% subtract opposite, 0% original, +100% mono. Drag "
                           "vertically; Shift for fine; wheel; double-click original; right-click exact "
                           "value.";
                if (mixerControlHit(mixerPhaseInvertBounds(insert.index), point))
                    return "Invert polarity of both stereo channels. Click to toggle; highlighted means "
                           "enabled.";
                if (mixerControlHit(mixerStereoSwapBounds(insert.index), point))
                    return "Swap left and right stereo channels. Click to toggle; highlighted means enabled.";
            }
        return {};
    }
    juce::String getTooltip() override
    {
        if (type == 1)
            if (const auto row = pianoHit(getMouseXYRelative().toFloat()))
                if (row->repeated() || row->dormant)
                    return row->repeated()
                               ? "Loop preview: edits change the original source note and all its repetitions"
                               : "Dormant source note: retained for editing; outside the current playback "
                                 "window";
        return mixerStereoTooltipAt(getMouseXYRelative().toFloat());
    }
    bool ownsMixerStereo() const
    {
        return mixerStereo && mixerStereo->project == s.controller.projectIdentity &&
               s.controller.hasGesture() && s.controller.gestureOwner() == mixerStereo->owner &&
               s.controller.gestureSerial() == mixerStereo->serial;
    }
    bool validMixerStereo() const
    {
        return ownsMixerStereo() && mixerStereo->revision == s.controller.revision &&
               mixerStereo->insert == s.selectedInsert &&
               mixerInsert(s.controller.project, mixerStereo->insert) != nullptr;
    }
    juce::Result setMixerStereo(int insert, double value, const juce::String &owner = {})
    {
        juce::String code;
        if (auto result = s.admitMixerChainControl(code, owner); result.failed())
            return result;
        CommandArgs args;
        args.mixer = insert;
        args.value = value;
        s.command("mixer.stereoSeparation", args, owner);
        return s.status == "Ready" ? juce::Result::ok() : juce::Result::fail(s.status);
    }
    juce::Result beginMixerStereo(int insert, juce::Point<float> point)
    {
        juce::String code;
        if (auto result = s.admitMixerChainControl(code); result.failed())
            return result;
        if (!mixerInsert(s.controller.project, insert))
            return juce::Result::fail("Mixer insert is unavailable");
        try
        {
            auto pending = std::make_unique<MixerStereoGesture>();
            pending->project = s.controller.projectIdentity;
            pending->insert = insert;
            pending->owner = "ui.mixer.stereo." + juce::Uuid().toString();
            pending->revision = s.controller.revision;
            pending->lastPoint = point;
            if (!s.controller.beginGesture(pending->owner))
                return juce::Result::fail("Another edit owns the stereo separation gesture");
            pending->serial = s.controller.gestureSerial();
            mixerStereo = std::move(pending);
            return juce::Result::ok();
        }
        catch (...)
        {
            return juce::Result::fail("Could not prepare stereo gesture; document unchanged");
        }
    }
    juce::Result finishMixerStereo(bool commit)
    {
        if (!mixerStereo)
            return juce::Result::ok();
        const bool owned = ownsMixerStereo(), valid = validMixerStereo();
        auto pending = std::move(mixerStereo); // Clear intrinsic owner before checked observers can reenter.
        if (!owned)
            return juce::Result::fail("Stereo context changed; unrelated edit owner preserved");
        const auto result = commit && valid
                                ? s.controller.endGestureChecked("Mixer stereo separation", pending->owner)
                                : s.controller.cancelGestureChecked(pending->owner);
        if (result.failed() && !mixerStereo && s.controller.projectIdentity == pending->project &&
            s.controller.gestureOwner() == pending->owner && s.controller.gestureSerial() == pending->serial)
            mixerStereo =
                std::move(pending); // Keep refused history/cancel recoverable, never silently abandon it.
        repaint();
        if (result.failed())
            return result;
        return commit && !valid ? juce::Result::fail("Stereo context changed; gesture cancelled") : result;
    }

    bool mixerControlHit(juce::Rectangle<int> bounds, juce::Point<float> p) const
    {
        return p.x >= 0 && p.x < mixerStripWidth() - 12 && p.y >= 30 &&
               p.y < getHeight() - scrollBarThickness && bounds.contains(p.toInt());
    }
    void resetMixerEffectView()
    {
        if (mixerStereo)
            reportHistoryResult(s, finishMixerStereo(false));
        if (effectWet)
            finishEffectWet(false);
        effectOrder.reset();
        effectOrderMoved = false;
        effectOffset = 0;
        navigationChanged();
    }
    juce::Result setEffectWet(Id plugin, double value, const juce::String &owner = {})
    {
        juce::String code;
        if (auto result = s.admitMixerChainControl(code, owner); result.failed())
            return result;
        const auto *insert = mixerInsert(s.controller.project, s.selectedInsert);
        if (!insert || std::none_of(insert->effects.begin(), insert->effects.end(),
                                    [plugin](const auto &effect) { return effect.id == plugin; }))
            return juce::Result::fail("Effect mix target is unavailable in this insert");
        CommandArgs args;
        args.target = plugin;
        args.value = value;
        s.command("plugin.effect.mix", args, owner);
        return s.status == "Ready" ? juce::Result::ok() : juce::Result::fail(s.status);
    }
    juce::Result beginEffectWet(Id plugin, juce::Point<float> point)
    {
        juce::String code;
        if (auto result = s.admitMixerChainControl(code); result.failed())
            return result;
        const auto *insert = mixerInsert(s.controller.project, s.selectedInsert);
        const auto *effect = s.controller.project.plugin(plugin);
        if (!insert || !effect ||
            std::none_of(insert->effects.begin(), insert->effects.end(),
                         [plugin](const auto &slot) { return slot.id == plugin; }))
            return juce::Result::fail("Effect mix target is unavailable");
        try
        {
            auto pending = std::make_unique<EffectWetGesture>();
            pending->project = s.controller.projectIdentity;
            pending->descriptor = effect->descriptor;
            pending->plugin = plugin;
            pending->insert = s.selectedInsert;
            pending->revision = s.controller.revision;
            pending->owner = "ui.mixer.wet." + juce::Uuid().toString();
            pending->lastPoint = point;
            if (!s.controller.beginGesture(pending->owner))
                return juce::Result::fail("Another edit owns the effect mix gesture");
            pending->serial = s.controller.gestureSerial();
            effectWet = std::move(pending);
            repaint();
            return juce::Result::ok();
        }
        catch (...)
        {
            return juce::Result::fail("Could not prepare effect mix gesture; document unchanged");
        }
    }
    juce::Result finishEffectWet(bool commit)
    {
        if (!effectWet)
            return juce::Result::ok();
        const bool owned = ownsEffectWet(), valid = validEffectWet();
        auto pending = std::move(effectWet); // Clear UI ownership before checked observers can reenter.
        if (!owned)
        {
            repaint();
            return juce::Result::fail("Effect mix context changed; unrelated owner preserved");
        }
        const auto result = commit && valid
                                ? s.controller.endGestureChecked("Effect host wet/dry", pending->owner)
                                : s.controller.cancelGestureChecked(pending->owner);
        if (result.failed() && !effectWet && s.controller.projectIdentity == pending->project &&
            s.controller.gestureOwner() == pending->owner && s.controller.gestureSerial() == pending->serial)
            effectWet = std::move(pending); // Keep a refused history/cancel owner recoverable.
        repaint();
        if (result.failed())
            return result;
        return commit && !valid ? juce::Result::fail("Effect mix context changed; gesture cancelled")
                                : result;
    }
    juce::Result executeEffectOrder(const MixerEffectOrderIntent &intent, int destination)
    {
        if (auto result = effectOrderAdmission(); result.failed())
            return result;
        if (auto result = intent.validate(s.controller.project, s.controller.projectIdentity,
                                          s.controller.revision, s.selectedInsert);
            result.failed())
            return result;
        CommandArgs args;
        args.target = intent.source;
        args.lane = destination;
        s.command("plugin.effect.move", args); // Same complete canonical value/history as MCP.
        return s.status == "Ready" ? juce::Result::ok() : juce::Result::fail(s.status);
    }
    juce::Result beginEffectOrder(Id source)
    {
        if (auto result = effectOrderAdmission(); result.failed())
            return result;
        try
        {
            auto prepared =
                MixerEffectOrderIntent::capture(s.controller.project, s.controller.projectIdentity,
                                                s.controller.revision, s.selectedInsert, source);
            if (!prepared)
                return juce::Result::fail("Mixer effect is unavailable");
            effectOrder = std::move(prepared);
            effectOrderMoved = false;
            repaint();
            return juce::Result::ok();
        }
        catch (...)
        {
            return juce::Result::fail("Could not prepare effect order intent");
        }
    }
    MixerEffectSlots effectSlotsLayout() const
    {
        return mixerEffectLayout();
    }
    int effectSlotsOffset() const
    {
        return mixerEffectLayout().clampedOffset(effectOffset, mixerEffectCount());
    }
    int maximumEffectSlotsOffset() const
    {
        return std::max(0, mixerEffectCount() + 1 - mixerEffectLayout().visibleRows());
    }
    juce::Result setEffectSlotsOffset(int offset)
    {
        if (offset < 0 || offset > maximumEffectSlotsOffset())
            return juce::Result::fail("Effect slot offset is outside the visible insert range");
        effectOffset = offset;
        if (effectOrder)
            effectOrder->destination = effectOrderDestination(effectOrderPoint);
        navigationChanged();
        return juce::Result::ok();
    }
    juce::var effectSlotsState() const
    {
        const auto layout = mixerEffectLayout();
        auto *out = new juce::DynamicObject;
        out->setProperty("visible", !layout.pane.isEmpty());
        out->setProperty("insert", s.selectedInsert);
        out->setProperty("offset", effectSlotsOffset());
        out->setProperty("maximumOffset", maximumEffectSlotsOffset());
        out->setProperty("visibleRows", layout.visibleRows());
        out->setProperty("effectCount", mixerEffectCount());
        juce::var reorder;
        if (effectOrder)
        {
            auto *intent = new juce::DynamicObject;
            intent->setProperty("completionState", "pending");
            intent->setProperty("plugin", juce::int64(effectOrder->source));
            intent->setProperty("insert", effectOrder->insert);
            intent->setProperty("projectIdentity", effectOrder->project);
            intent->setProperty("sourceRevision", juce::int64(effectOrder->revision));
            intent->setProperty("sourceSlot", effectOrder->sourceSlot);
            intent->setProperty("destinationSlot", effectOrder->destination >= 0
                                                       ? juce::var(effectOrder->destination)
                                                       : juce::var{});
            intent->setProperty("moved", effectOrderMoved);
            intent->setProperty("valid", effectOrder
                                             ->validate(s.controller.project, s.controller.projectIdentity,
                                                        s.controller.revision, s.selectedInsert)
                                             .wasOk());
            reorder = juce::var(intent);
        }
        out->setProperty("reorder", reorder);
        juce::var wet;
        if (effectWet)
        {
            auto *pending = new juce::DynamicObject;
            pending->setProperty("completionState", "pending");
            pending->setProperty("plugin", juce::int64(effectWet->plugin));
            pending->setProperty("insert", effectWet->insert);
            pending->setProperty("projectIdentity", effectWet->project);
            pending->setProperty("revision", juce::int64(effectWet->revision));
            pending->setProperty("valid", validEffectWet());
            wet = juce::var(pending);
        }
        out->setProperty("wetGesture", wet);
        out->setProperty("rowUnits", "zero-based ordered slots; last row is Add, not a plugin ID");
        return juce::var(out);
    }
    bool pointerGestureActive() const
    {
        return bool(mixerStereo) || bool(effectWet) || bool(effectOrder) || velocityGestureActive() ||
               (swingControl && swingControl->gestureActive()) || selecting || dragging || stretching ||
               erasing || previewDragging || panning || keyZooming || regionZooming || timeSelecting ||
               pitchSelecting || slicing || muting || automationDragPoint != 0 ||
               (type == 1 && (s.noteSlicePending() || s.noteStretchPending()));
    }
    void cancelPointerGesture()
    {
        if (mixerStereo)
        {
            const auto result = finishMixerStereo(false);
            s.status = result.wasOk() ? "Stereo separation cancelled" : result.getErrorMessage();
            repaint();
            return;
        }
        if (effectWet)
        {
            const auto result = finishEffectWet(false);
            s.status = result.wasOk() ? "Effect mix cancelled" : result.getErrorMessage();
            repaint();
            return;
        }
        if (effectOrder)
        {
            effectOrder.reset();
            effectOrderMoved = false;
            s.status = "Effect reorder cancelled";
            repaint();
            return;
        }
        if (type == 1 && s.noteStretchPending())
            s.finishNoteStretch(false);
        stretching = false;
        mixerSendSource = -1;
        mixerPanSource = -1;
        if (type == 1 && s.noteSlicePending())
            s.finishNoteSlice(false, "Local Slice preview cancelled");
        finishVelocityGesture(false);
        s.previewNote(-1);
        s.controller.cancelGesture();
        if (type == 2 && selecting)
            s.selectedClips = selectionBeforeMarquee;
        selectionBeforeMarquee.clear();
        toggleMarquee = false;
        selecting = dragging = erasing = resizing = previewDragging = panning = keyZooming = regionZooming =
            timeSelecting = pitchSelecting = false;
        rectangle = {};
        slicing = false;
        automationDragPoint = automationDragClip = 0;
        trimmingAutomation = 0;
        automationGestureProject.clear();
        slipTarget = 0;
        muting = false;
        muteTouched.clear();
        repaint();
    }
    Id noteAt(juce::Point<float> point) const
    {
        return hit(point);
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        return s.keyPressed(key);
    }
    bool keyStateChanged(bool down) override
    {
        return s.keyStateChanged(down);
    }
    bool painting() const
    {
        return paintMode;
    }
    void setPaintMode(bool value)
    {
        if (type == 1 && pointerGestureActive())
            cancelPointerGesture();
        noteMuteTool = noteSliceTool = false;
        pianoSlice.setToggleState(false, juce::dontSendNotification);
        paintMode = value;
        pianoMute.setToggleState(false, juce::dontSendNotification);
        if (type == 2)
            setPlaylistTool(value ? 1 : 0);
    }
    bool noteMuteToolActive() const
    {
        return type == 1 && noteMuteTool;
    }
    void setNoteMuteTool()
    {
        if (type != 1)
            return;
        if (pointerGestureActive())
            cancelPointerGesture();
        noteMuteTool = true;
        noteSliceTool = false;
        pianoSlice.setToggleState(false, juce::dontSendNotification);
        paintMode = false;
        pianoMute.setToggleState(true, juce::dontSendNotification);
        repaint();
    }
    bool noteSliceToolActive() const
    {
        return type == 1 && noteSliceTool;
    }
    void setNoteSliceTool()
    {
        if (type != 1)
            return;
        if (pointerGestureActive())
            cancelPointerGesture();
        noteSliceTool = true;
        noteMuteTool = false;
        paintMode = false;
        pianoMute.setToggleState(false, juce::dontSendNotification);
        pianoSlice.setToggleState(true, juce::dontSendNotification);
        repaint();
    }
    void setPlaylistTool(int value)
    {
        if (pointerGestureActive())
            cancelPointerGesture();
        playlistTool = juce::jlimit(0, 7, value);
        paintMode = value == 1;
        for (int index = 0; index < int(playlistTools.size()); ++index)
            playlistTools[size_t(index)].setToggleState(index == value, juce::dontSendNotification);
        repaint();
    }
    void resized() override
    {
        finishVelocityGesture(true);
        syncRack();
        if (picker)
        {
            picker->setVisible(pickerShown);
            pickerDivider.setVisible(pickerShown);
            const int top = gridTop() - 20;
            picker->setBounds(0, top, std::max(1, pickerWidth() - 4), std::max(1, getHeight() - top));
            pickerDivider.setBounds(pickerWidth() - 4, top, 4, std::max(1, getHeight() - top));
        }
        if (type != 1)
        {
            horizontalBar.setBounds(type == 0   ? stepStart()
                                    : type == 2 ? leftEdge()
                                                : 6,
                                    getHeight() - scrollBarThickness,
                                    std::max(1, (type == 3 ? mixerStripWidth() : getWidth()) -
                                                    (type == 0   ? stepStart()
                                                     : type == 2 ? leftEdge()
                                                                 : 6) -
                                                    (type == 0 ? 0 : scrollBarThickness)),
                                    scrollBarThickness);
            verticalBar.setBounds(
                getWidth() - rightInset(), type == 0 ? 44 : gridTop(), scrollBarThickness,
                std::max(1, getHeight() - (type == 0 ? 44 : gridTop()) - scrollBarThickness));
            verticalBar.setVisible(type != 0 && (type != 3 || mixerStripWidth() < getWidth()));
            auto range = horizontalBar.read();
            if (type == 3)
                mixerScroll = juce::jlimit(0., std::max(0., range[2] - range[1]), mixerScroll);
            if (type == 0)
                rackOffset = juce::jlimit(0, std::max(0, int(s.visibleRackChannels().size()) - rackRows()),
                                          rackOffset);
        }
        if (type == 2)
        {
            int x = 8, y = 3;
            auto place = [&](juce::Component &control, int width)
            {
                if (x + width > getWidth() - rightInset() && x > 8)
                {
                    x = 8;
                    y += 30;
                }
                control.setBounds(x, y, width, 26);
                control.setVisible(true);
                x += width + 4;
            };
            for (auto &tool : playlistTools)
                place(tool, 54);
            place(playlistMenu, 55);
            place(playlistGrouping, 32);
            place(playlistSnap, 112);
            place(playlistZoomOut, 26);
            place(playlistZoomIn, 26);
            place(playlistFit, 36);
        }
        if (type == 1)
        {
            horizontalBar.setBounds(62, getHeight() - scrollBarThickness,
                                    std::max(1, getWidth() - 62 - scrollBarThickness), scrollBarThickness);
            verticalBar.setBounds(getWidth() - rightInset(), gridTop(), scrollBarThickness,
                                  std::max(1, noteBottom() - gridTop()));
            layoutNoteModesToolbar();
            midiFileButton.setBounds(4, 2, 70, 24);
            ghostButton.setBounds(82, 2, 54, 24);
            editGhostButton.setBounds(142, 2, 88, 24);
            pianoMute.setBounds(238, 2, 26, 24);
            pianoMute.setVisible(getWidth() > 440);
            pianoSlice.setBounds(268, 2, 26, 24);
            pianoSlice.setVisible(getWidth() > 480);
            editGhostButton.setVisible(getWidth() > 410);
            for (int i = 0; i < 5; ++i)
                viewButtons[size_t(i)].setBounds(getWidth() - 154 + i * 30, 2, i == 4 ? 34 : 28, 24);
            ghostButton.setToggleState(ghosts, juce::dontSendNotification);
            editGhostButton.setToggleState(editableGhosts, juce::dontSendNotification);
        }
        revealChannel(true);
    }
    void revealChannel(bool force = false)
    {
        if (type == 2 && viewedProject != s.controller.projectIdentity)
        {
            scroll = 0;
            laneOffset = 0;
            playlistLaneScale = 1;
            hasRegionView = false;
            viewedProject = s.controller.projectIdentity;
        }
        if (type == 2)
        {
            playlistGrouping.setToggleState(s.controller.clipGroupingEnabled, juce::dontSendNotification);
            playlistGrouping.setEnabled(!s.controller.hasGesture());
        }
        if (type == 2)
            playlistSnap.setSelectedId(s.snap == 0     ? 1
                                       : s.snap == .25 ? 2
                                       : s.snap == .5  ? 3
                                       : s.snap == 1   ? 4
                                                       : 5,
                                       juce::dontSendNotification);
        if (type == 0)
        {
            int count = pattern() ? stepSequenceSize(*pattern()) : 16;
            int visible = std::max(1, int((getWidth() - stepStart() - 8) / stepWidth()));
            stepOffset = juce::jlimit(0, std::max(0, count - visible), stepOffset);
            return;
        }
        if (type != 1 || (!force && viewedProject == s.controller.projectIdentity &&
                          viewedChannel == s.selectedChannel && viewedPattern == s.selectedPattern))
            return;
        if (viewedProject != s.controller.projectIdentity || viewedPattern != s.selectedPattern)
        {
            timeStart = timeEnd = 0;
            timeSelecting = false;
            pitchSelecting = false;
            hasRegionView = false;
        }
        viewedProject = s.controller.projectIdentity;
        viewedChannel = s.selectedChannel;
        viewedPattern = s.selectedPattern;
        int low = 128, high = -1;
        if (auto *p = pattern())
            for (auto &n : p->notes)
                if (n.channel == viewedChannel)
                {
                    low = std::min(low, n.pitch);
                    high = std::max(high, n.pitch);
                }
        topPitch = high >= 0 ? juce::jlimit(visiblePitches() - 1, 127,
                                            high + std::max(0, (visiblePitches() - (high - low + 1)) / 2))
                             : 72;
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel);
        juce::Graphics::ScopedSaveState contentClip(g);
        // Scrollbars own reserved gutters; no waveform, note, step, label or
        // selection overlay may paint beneath their separate child components.
        g.reduceClipRegion(0, 0, std::max(0, getWidth() - rightInset()),
                           std::max(0, getHeight() - scrollBarThickness));
        auto &project = s.controller.project;
        const auto effective = type == 2 ? s.effectiveClipSelection() : std::vector<Id>{};
        const std::unordered_set<Id> effectiveSet(effective.begin(), effective.end());
        std::unordered_set<Id> groupedClips;
        if (type == 2)
            for (const auto &group : project.clipGroups)
                groupedClips.insert(group.members.begin(), group.members.end());
        if (type == 0)
        {
            g.reduceClipRegion(0, 0, getWidth(), std::max(0, rackContentBottom()));
            auto *p = pattern();
            if (!p)
                return;
            int count = stepSequenceSize(*p);
            if (getWidth() >= 516)
                label(g, p->name + " / " + juce::String(int(s.visibleRackChannels().size())) + " Channels",
                      {224, 3, std::max(0, getWidth() - 426), 25}, muted, 11);
            if (getWidth() >= 420)
                label(g, "Swing", {getWidth() - 191, 3, 40, 25}, muted, 10);
            if (getWidth() >= 310)
                label(g, "<    >", {getWidth() - 72, 3, 64, 30}, accent, 16);
            float width = float(stepWidth());
            int visible = std::max(1, int((getWidth() - stepStart() - 8) / width));
            label(g,
                  "Steps " + juce::String(stepOffset + 1) + "-" +
                      juce::String(std::min(count, stepOffset + visible)) +
                      (p->autoLength ? " | Auto " : " | Manual ") +
                      juce::String(patternNumerator(project, *p)) + "/" +
                      juce::String(patternDenominator(project, *p)),
                  {stepStart(), 27, getWidth() - stepStart() - 10, 17}, muted, 10);
            label(g, "Name: instrument  PR: Piano roll", {12, 27, stepStart() - 20, 17}, muted, 10);
            const auto visibleChannels = s.visibleRackChannels();
            const auto loopPreview = rawPatternOccurrences(project, *p);
            for (size_t i = 0; i < visibleChannels.size(); ++i)
            {
                auto &c = *project.channel(visibleChannels[i]);
                int y = 44 + (int(i) - rackOffset) * rackRowHeight();
                if (y + 32 > rackContentBottom())
                    break;
                if (int(i) < rackOffset)
                    continue;
                auto colour = c.colour ? juce::Colour(c.colour) : channelColour(int(c.id - 1));
                g.setColour(selected(s.selectedChannels, c.id) ? grid : bg.brighter(.02f));
                g.fillRoundedRectangle(8.f, float(y), float(getWidth() - 16), 32.f, 3);
                g.setColour(selected(s.selectedChannels, c.id) ? colour : colour.darker(.65f));
                g.fillRoundedRectangle(9.f, float(y + 3), 4.f, 26.f, 2);
                if (c.id == s.selectedChannel)
                {
                    g.setColour(ink);
                    g.fillEllipse(8.f, float(y + 14), 6.f, 6.f);
                }
                g.setColour(c.muted ? muted.darker(.6f) : accent);
                g.fillEllipse(21.f, float(y + 12), 8.f, 8.f);
                if (channelIsSoloed(project, c.id))
                {
                    g.setColour(orange);
                    g.drawEllipse(18.f, float(y + 9), 14.f, 14.f, 1.5f);
                }
                if (c.muteLocked)
                {
                    g.setColour(ink);
                    g.drawRoundedRectangle(18.5f, float(y + 10), 13.f, 12.f, 2.f, 1.f);
                    g.fillRect(24.f, float(y + 14), 2.f, 4.f);
                }
                g.setColour(grid.brighter(.05f));
                g.fillRoundedRectangle(38.f, float(y + 2), float(rackLabelWidth() - 112), 28.f, 3);
                const bool hasIcon = c.icon != "none";
                if (hasIcon)
                    channel_icons::draw(g, {43.f, float(y + 7), 18.f, 18.f}, c.icon, colour);
                label(g, c.name, {hasIcon ? 65 : 44, y, rackLabelWidth() - (hasIcon ? 143 : 122), 32},
                      c.muted ? muted : ink, 12);
                label(g, c.audioClip ? "--" : "PR", {rackLabelWidth() - 70, y, 30, 32},
                      c.audioClip ? muted : accent, 11);
                label(g, c.mixer == 0 ? "M" : juce::String(c.mixer), {rackLabelWidth() - 36, y, 32, 32},
                      muted, 12);
                if (c.audioClip)
                {
                    label(g, "Audio Clip / Playlist", {stepStart(), y, getWidth() - stepStart() - 8, 32},
                          muted, 12);
                    continue;
                }
                bool melodic = stepSequenceIsMelodic(*p, c.id);
                bool recording = s.liveTakePattern() == p->id &&
                                 std::any_of(s.liveTakeNotes().begin(), s.liveTakeNotes().end(),
                                             [&](const auto &note) { return note.channel == c.id; });
                double displayLength = p->length;
                if (recording)
                    for (auto &note : s.liveTakeNotes())
                        displayLength = std::max(displayLength, note.beat + note.length);
                melodic = melodic || recording;
                if (melodic)
                {
                    if (recording)
                        for (auto &note : s.liveTakeNotes())
                            if (note.channel == c.id)
                            {
                                g.setColour(orange);
                                float x = stepStart() +
                                          float(note.beat / displayLength * (getWidth() - stepStart() - 12));
                                float length = float(std::max(
                                    3., note.length / displayLength * (getWidth() - stepStart() - 12) - 2));
                                g.fillRoundedRectangle(x, float(y + 23 - note.pitch % 12), length, 3.f, 1.f);
                            }
                    for (const auto &occurrence : loopPreview)
                    {
                        const auto &n = occurrence.note;
                        if (n.channel == c.id && n.beat < p->length)
                        {
                            g.setColour(n.muted ? colour.withAlpha(.3f)
                                                : colour.withAlpha(occurrence.iteration ? .45f : 1.f));
                            float x =
                                stepStart() + float(n.beat / displayLength * (getWidth() - stepStart() - 12));
                            g.fillRoundedRectangle(
                                x, float(y + 23 - n.pitch % 12),
                                float(std::max(3., std::min(n.length, p->length - n.beat) / displayLength *
                                                           (getWidth() - stepStart() - 12) -
                                                       2)),
                                3.f, 1.f);
                        }
                    }
                }
                else
                {
                    const double period = channelLoopLength(project, *p, c.id);
                    for (int j = stepOffset; j < std::min(count, stepOffset + visible); ++j)
                    {
                        const double beat = period > 0 ? std::fmod(j * .25, period) : j * .25;
                        const bool repeated = period > 0 && j * .25 >= period;
                        bool on = std::any_of(
                            p->notes.begin(), p->notes.end(), [&](auto &n)
                            { return n.channel == c.id && n.step && std::abs(n.beat - beat) < .0001; });
                        const bool audible = std::any_of(p->notes.begin(), p->notes.end(),
                                                         [&](const auto &n)
                                                         {
                                                             return n.channel == c.id && n.step && !n.muted &&
                                                                    std::abs(n.beat - beat) < .0001;
                                                         });
                        const auto stepColour = on && !audible ? colour.darker(.65f)
                                                : repeated     ? colour.darker(.4f)
                                                               : colour;
                        auto r = juce::Rectangle<float>(stepStart() + (j - stepOffset) * width, float(y + 6),
                                                        width - 3, 20.f);
                        // Off steps alternate per beat group with clear contrast (the odd group
                        // carries a hint of the Channel colour) so beats read at a glance.
                        const bool oddGroup = (j / (16 / patternDenominator(project, *p))) % 2 != 0;
                        g.setColour(on ? stepColour
                                       : oddGroup ? grid.interpolatedWith(colour, .14f).brighter(.05f)
                                                  : bg.brighter(.1f));
                        g.fillRoundedRectangle(r, 3.5f);
                        if (on)
                        {
                            g.setColour(stepColour.brighter(.32f));
                            g.fillRoundedRectangle(r.withHeight(2.f).translated(0, 2), 1);
                        }
                        else
                        {
                            g.setColour(juce::Colours::black.withAlpha(.18f));
                            g.fillRoundedRectangle(r.withTrimmedTop(r.getHeight() - 3), 1.5f);
                        }
                        if (period > 0 && std::abs(std::fmod(j * .25, period)) < .0001)
                        {
                            g.setColour(accent);
                            g.fillRect(r.getX() - 2, float(y + 3), 1.f, 26.f);
                        }
                    }
                }
            }
            const int footer = 44 + (int(visibleChannels.size()) - rackOffset) * rackRowHeight();
            if (const auto plus = rackPlusBounds(); !plus.isEmpty())
            {
                // FL-style add button under the Channels: opens the instrument picker.
                g.setColour(panel.brighter(rackPlusHover ? .14f : .06f));
                g.fillRoundedRectangle(plus.toFloat(), 4.f);
                g.setColour(accent.withAlpha(rackPlusHover ? 1.f : .7f));
                g.drawRoundedRectangle(plus.toFloat().reduced(.5f), 4.f, 1.f);
                g.setFont(juce::Font(vibera::uiFont, 18.f, juce::Font::bold));
                g.drawText("+", plus, juce::Justification::centred);
            }
            if (rackContentBottom() > footer + 28)
                label(g,
                      visibleChannels.empty() ? "No Channels in this group. Add a sample or instrument."
                      : !selected(visibleChannels, s.selectedChannel)
                          ? "MIDI: " + project.channel(s.selectedChannel)->name + " (outside filter)"
                          : juce::String(int(s.selectedChannels.size())) +
                                " selected | Selector: right-click/drag, double-click all",
                      {rackPlusBounds().isEmpty() ? 12 : rackPlusBounds().getRight() + 12, footer,
                       getWidth() - 24, 28}, muted, 10);
        }
        else if (type == 1 || type == 2)
        {
            if (type == 1)
                if (auto *channel = project.channel(s.selectedChannel); channel && channel->audioClip)
                {
                    label(g, "Audio Clip triggering is not implemented yet. Edit this clip in Playlist.",
                          {18, 60, getWidth() - 36, 60}, muted, 12);
                    return;
                }
            const int left = leftEdge(), top = gridTop();
            if (type == 1)
            {
                syncNoteModesToolbar();
                midiFileButton.setTooltip(noteSliceTool  ? "Slice [C] | Piano Roll menu"
                                          : noteMuteTool ? "Mute [T] | Piano Roll menu"
                                          : paintMode    ? "Paint [B] | Piano Roll menu"
                                                         : "Draw [P] | Piano Roll menu");
                if (hasTimeSelection())
                {
                    auto first = float(leftEdge() + (timeStart - scroll) * spacing());
                    auto last = float(leftEdge() + (timeEnd - scroll) * spacing());
                    auto region = juce::Rectangle<float>(first, 28, last - first, float(gridTop() - 28))
                                      .getIntersection({float(leftEdge()), 28,
                                                        float(getWidth() - leftEdge() - scrollBarThickness),
                                                        float(gridTop() - 28)});
                    g.setColour(accent.withAlpha(.3f));
                    g.fillRect(region);
                }
                // Grid rows: white-key rows lighter than black-key rows, a firmer line under each C.
                std::array<int, 12> guide{};
                if (s.pianoGuide.name.isNotEmpty())
                    guide = score::guideMarks(s.pianoGuide.name, s.pianoGuide.root, s.pianoGuide.maqam);
                for (int p = topPitch; p > topPitch - visiblePitches(); --p)
                {
                    const float y = top + (topPitch - p) * height();
                    const bool black = juce::MidiMessage::isMidiNoteBlack(p);
                    g.setColour(black ? bg.darker(.08f) : panel.brighter(.035f));
                    g.fillRect(float(left), y, float(getWidth() - left), height());
                    if (guide[size_t(p % 12)] != 0)
                    {
                        // Scale/maqam guide: degrees tinted, the tonic stronger.
                        g.setColour(accent.withAlpha((guide[size_t(p % 12)] & 3) == 2 ? .16f : .07f));
                        g.fillRect(float(left), y, float(getWidth() - left), height());
                    }
                    g.setColour(p % 12 == 0 ? grid.brighter(.25f) : grid.withAlpha(.4f));
                    g.fillRect(float(left), y + height() - 1.f, float(getWidth() - left), 1.f);
                }
                // Keyboard: every white key spans to the middle of a neighbouring black key (and
                // meets the next white key directly at E/F and B/C); black keys sit on top.
                {
                    juce::Graphics::ScopedSaveState keys(g);
                    g.reduceClipRegion(0, top, 60, noteBottom() - top);
                    const float h = height(), keyWidth = 59.f, blackWidth = 36.f;
                    const auto white = themeColour(0xffeef2f4), keyLine = themeColour(0xff9aa6ae);
                    auto rowTop = [&](int p) { return float(top) + float(topPitch - p) * h; };
                    const int lowest = topPitch - visiblePitches();
                    for (int p = topPitch + 1; p >= lowest; --p)
                    {
                        if (p < 0 || p > 127 || juce::MidiMessage::isMidiNoteBlack(p))
                            continue;
                        const float upper = juce::MidiMessage::isMidiNoteBlack(p + 1) ? rowTop(p) - h * .5f : rowTop(p);
                        const float lower =
                            juce::MidiMessage::isMidiNoteBlack(p - 1) ? rowTop(p) + h * 1.5f : rowTop(p) + h;
                        g.setGradientFill(juce::ColourGradient(white.darker(.06f), 0.f, 0.f, white, keyWidth, 0.f, false));
                        g.fillRect(0.f, upper, keyWidth, lower - upper);
                        g.setColour(keyLine);
                        g.fillRect(0.f, lower - 1.f, keyWidth, 1.f);
                        if (p % 12 == 0 && h >= 7.f)
                        {
                            g.setColour(themeColour(0xff56626b));
                            g.setFont(juce::Font(vibera::uiFont, juce::jlimit(8.f, 11.f, h - 1.f), juce::Font::bold));
                            g.drawText(juce::MidiMessage::getMidiNoteName(p, true, true, 5),
                                       juce::Rectangle<float>(0.f, lower - h - 1.f, keyWidth - 4.f, h + 1.f),
                                       juce::Justification::centredRight, false);
                        }
                    }
                    for (int p = topPitch + 1; p >= lowest; --p)
                    {
                        if (p < 0 || p > 127 || !juce::MidiMessage::isMidiNoteBlack(p))
                            continue;
                        const auto key = juce::Rectangle<float>(0.f, rowTop(p) + .5f, blackWidth, h - 1.f);
                        g.setColour(juce::Colours::black.withAlpha(.3f));
                        g.fillRoundedRectangle(key.translated(1.5f, 1.5f), 2.f);
                        g.setGradientFill(juce::ColourGradient(themeColour(0xff11161a), 0.f, 0.f,
                                                               themeColour(0xff323b42), blackWidth, 0.f, false));
                        g.fillRoundedRectangle(key.withX(-3.f).withWidth(blackWidth + 3.f), 2.f);
                        g.setColour(juce::Colours::white.withAlpha(.07f));
                        g.fillRect(key.getRight() - 4.f, key.getY() + 1.f, 2.f, key.getHeight() - 2.f);
                    }
                    // Guide marks on the keys: a dot per degree, a half-flat sign for quarter tones.
                    for (int p = topPitch + 1; p >= lowest && s.pianoGuide.name.isNotEmpty(); --p)
                    {
                        if (p < 0 || p > 127 || guide[size_t(p % 12)] == 0)
                            continue;
                        const auto row = juce::Rectangle<float>(38.f, rowTop(p), 20.f, h);
                        g.setColour((guide[size_t(p % 12)] & 3) == 2 ? orange : accent);
                        if (guide[size_t(p % 12)] & 4)
                        {
                            g.setFont(juce::Font(vibera::uiFont, juce::jlimit(8.f, 12.f, h), juce::Font::bold));
                            g.drawText(juce::String::fromUTF8("\xe2\x99\xad") + "-", row, juce::Justification::centred, false);
                        }
                        else
                            g.fillEllipse(row.withSizeKeepingCentre(std::min(5.f, h - 1.f), std::min(5.f, h - 1.f)));
                    }
                    if (pitchSelecting)
                        for (int p = pitchFirst; p <= pitchLast; ++p)
                        {
                            g.setColour(accent.withAlpha(.4f));
                            g.fillRect(0.f, rowTop(p), keyWidth, h);
                        }
                    g.setColour(grid);
                    g.fillRect(59.f, float(top), 1.f, float(noteBottom() - top));
                }
                g.setColour(bg);
                g.fillRect(0, noteBottom(), getWidth(), velocityHeight());
                g.setColour(grid);
                g.drawHorizontalLine(noteBottom(), 0, float(getWidth()));
                const bool compactVelocity = velocityHeight() < 54;
                label(g, compactVelocity ? "Vel" : "Velocity",
                      {5, noteBottom() + 2, compactVelocity ? 24 : 55, 14}, muted, 10);
                if (velocityHeight() >= 42)
                    label(g, "127", {30, velocityBase() - 27, 28, 12}, muted, 10);
                label(g, "0", {40, std::max(noteBottom() + 16, velocityBase() - 9), 18, 12}, muted, 10);
            }
            else
            {
                for (int row = 0; row < std::min(visibleLaneCount() - laneOffset,
                                                 (getHeight() - top - scrollBarThickness + laneHeight() - 1) /
                                                     laneHeight());
                     ++row)
                {
                    int y = top + row * laneHeight();
                    g.setColour(row % 2 ? bg.brighter(.025f) : bg);
                    g.fillRect(left, y, getWidth() - left, laneHeight());
                    // Track header card: full-height colour tab, name, and a quiet lane number.
                    const auto header = juce::Rectangle<float>(float(pickerWidth() + 3), float(y + 2),
                                                               float(left - pickerWidth() - 7), float(laneHeight() - 4));
                    g.setColour(panel.brighter(.04f));
                    g.fillRoundedRectangle(header, 4.f);
                    const int rowLane = laneAtRow(row + laneOffset);
                    const auto *trackInfo = s.controller.project.track(rowLane);
                    g.setColour(trackInfo && trackInfo->colour ? juce::Colour(trackInfo->colour)
                                                               : channelColour(rowLane));
                    g.fillRoundedRectangle(header.withWidth(4.f).reduced(0, 3), 2.f);
                    // Track switch LED: lit while the track plays.
                    g.setColour(trackInfo && trackInfo->muted ? muted.withAlpha(.5f) : juce::Colour(0xff6fcf7f));
                    g.fillEllipse(header.getX() + 10.f, header.getCentreY() - 3.5f, 7.f, 7.f);
                    if (trackInfo && trackInfo->muted)
                    {
                        // A muted track dims its lane; its clips stay visible but silent.
                        g.setColour(juce::Colours::black.withAlpha(.28f));
                        g.fillRect(juce::Rectangle<int>(left, y, getWidth() - left, laneHeight()));
                    }
                    if (row == 0)
                        for (const auto &change : s.controller.project.tempoChanges)
                        {
                            // Tempo changes sit on the lower half of the ruler.
                            const float x = float(left + (change.beat - scroll) * spacing());
                            if (x < left || x > getWidth() - scrollBarThickness)
                                continue;
                            const juce::String text = juce::String::fromUTF8(change.ramp ? "\xe2\x86\x97 " : "\xe2\x99\xa9 ") +
                                                      juce::String(change.bpm, change.bpm == std::floor(change.bpm) ? 0 : 1);
                            g.setColour(accent);
                            g.fillRect(x, float(gridTop() - 11), 1.f, 11.f);
                            g.setFont(juce::Font(vibera::uiFont, 10.5f, juce::Font::bold));
                            g.drawText(text, juce::Rectangle<float>(x + 3, float(gridTop() - 11), 70, 11),
                                       juce::Justification::centredLeft, false);
                        }
                    if (row == 0)
                        for (const auto &change : s.controller.project.meterChanges)
                        {
                            const float x = float(left + (change.beat - scroll) * spacing());
                            if (x < left || x > getWidth() - scrollBarThickness)
                                continue;
                            g.setColour(orange);
                            g.setFont(juce::Font(vibera::uiFont, 10.5f, juce::Font::bold));
                            g.drawText(juce::String(change.numerator) + "/" + juce::String(change.denominator),
                                       juce::Rectangle<float>(x + 3, float(gridTop() - 22), 40, 11),
                                       juce::Justification::centredLeft, false);
                        }
                    if (row == 0)
                        for (const auto &marker : s.controller.project.markers)
                        {
                            const float x = float(left + (marker.beat - scroll) * spacing());
                            if (x < left || x > getWidth() - scrollBarThickness)
                                continue;
                            g.setColour(juce::Colour(0xffe8a33d).withAlpha(.35f));
                            g.drawVerticalLine(int(x), float(gridTop()), float(getHeight() - scrollBarThickness));
                            juce::Path flag;
                            flag.addTriangle(x, float(gridTop() - 22), x + 7, float(gridTop() - 18), x,
                                             float(gridTop() - 14));
                            g.setColour(juce::Colour(0xffe8a33d));
                            g.fillPath(flag);
                            g.fillRect(x, float(gridTop() - 22), 1.f, 22.f);
                            g.setFont(juce::Font(vibera::uiFont, 11.f, juce::Font::bold));
                            g.drawText(marker.name, juce::Rectangle<float>(x + 9, float(gridTop() - 22), 140, 12),
                                       juce::Justification::centredLeft, true);
                        }
                    if (row == 0 && s.playlistRange)
                    {
                        // Time selection (also the audio punch range) on the ruler and tracks.
                        const float first = float(left + (s.playlistRange->first - scroll) * spacing());
                        const float last = float(left + (s.playlistRange->second - scroll) * spacing());
                        const auto visible = juce::Rectangle<float>(float(left), float(gridTop() - 22),
                                                                    float(getWidth() - left - scrollBarThickness),
                                                                    float(getHeight() - gridTop() + 22));
                        const auto band = juce::Rectangle<float>(first, float(gridTop() - 22), last - first,
                                                                 float(getHeight() - gridTop() + 22))
                                              .getIntersection(visible);
                        g.setColour(juce::Colour(0xffef6576).withAlpha(.07f));
                        g.fillRect(band);
                        g.setColour(juce::Colour(0xffef6576).withAlpha(.55f));
                        g.fillRect(band.withHeight(20.f));
                    }
                    g.setColour(ink);
                    g.setFont(juce::Font(vibera::uiFont, 12.5f, juce::Font::bold));
                    g.setColour(trackInfo && trackInfo->muted ? muted : ink);
                    // Groups: the first track shows an open/closed triangle, grouped tracks are indented.
                    const auto *nextInfo = s.controller.project.track(rowLane + 1);
                    const juce::String groupMark =
                        trackInfo && trackInfo->groupedAbove ? juce::String("    ")
                        : nextInfo && nextInfo->groupedAbove
                            ? juce::String::charToString(juce::juce_wchar(trackInfo && trackInfo->collapsed ? 0x25b8 : 0x25be)) + " "
                            : juce::String();
                    g.drawText(groupMark + (trackInfo && trackInfo->name.isNotEmpty() ? trackInfo->name
                                                                           : "Track " + juce::String(rowLane + 1)) +
                                   (trackInfo && trackInfo->muted ? " (muted)" : ""),
                               header.withTrimmedLeft(22).withTrimmedRight(26), juce::Justification::centredLeft, true);
                    g.setColour(muted.withAlpha(.7f));
                    g.setFont(juce::Font(vibera::uiFont, 11.f, juce::Font::plain));
                    g.drawText(juce::String(rowLane + 1), header.withTrimmedRight(8),
                               juce::Justification::centredRight, false);
                    g.setColour(grid.withAlpha(.5f));
                    g.drawHorizontalLine(y, float(left), float(getWidth()));
                }
            }
            double division = type == 1 ? .25 : 1.;
            while (division * spacing() < 8.)
                division *= 2.;
            for (double b = std::floor(scroll / division) * division;
                 b < (getWidth() - left) / spacing() + scroll; b += division)
            {
                int x = int(left + (b - scroll) * spacing());
                if (x < left)
                    continue;
                // The Playlist follows Song time-signature changes; editors use their own meter.
                const auto songBar = s.controller.project.barPosition(b);
                const bool songMeter = type == 2 && !s.controller.project.meterChanges.empty();
                bool bar = songMeter ? songBar.beatInBar < .0001 || songBar.barLength - songBar.beatInBar < .0001
                                     : std::abs(b / barBeats() - std::round(b / barBeats())) < .0001;
                bool beat = std::abs(b - std::round(b)) < .0001;
                const float bottom = float(type == 1 ? noteBottom() : getHeight());
                // Every other bar gets a faint band so long phrases read by bar at a glance.
                if (bar && !songMeter && int(std::round(b / barBeats())) % 2 == 1)
                {
                    g.setColour(ink.withAlpha(.022f));
                    g.fillRect(float(x), float(top), float(barBeats() * spacing()), bottom - float(top));
                }
                g.setColour(bar ? grid.brighter(.5f) : grid.withAlpha(beat ? .8f : .35f));
                g.drawVerticalLine(x, float(top), bottom);
                if (bar)
                {
                    const int number = songMeter ? s.controller.project.barPosition(b + .0002).bar + 1
                                                 : int(std::round(b / barBeats())) + 1;
                    label(g, juce::String(number), {x + 6, type == 2 ? top - 20 : 28, 32, 20}, muted, 11);
                }
            }
            if (type == 1 && s.quantizerGuide && s.quantizerGuidePattern == s.selectedPattern)
            {
                g.setColour(orange.withAlpha(.6f));
                for (auto beat :
                     grooveGuidePositions(*s.quantizerGuide, std::max(0., scroll), scroll + viewBeats()))
                {
                    const int x = int(left + (beat - scroll) * spacing());
                    if (x >= left && x < getWidth() - scrollBarThickness)
                        g.drawVerticalLine(x, float(top), float(noteBottom()));
                }
            }
            {
                juce::Graphics::ScopedSaveState clip(g);
                g.reduceClipRegion(left, top, getWidth() - left, getHeight() - top - (type == 1 ? 66 : 0));
                if (type == 1)
                {
                    if (pattern())
                        for (const auto &appearance : pianoView())
                        {
                            const auto &n = appearance.note;
                            if (n.pitch > topPitch || n.pitch <= topPitch - visiblePitches())
                                continue;
                            bool active = n.channel == s.selectedChannel;
                            if (!active && !ghosts)
                                continue;
                            auto r = nr(n);
                            auto colour = active ? (selected(s.selectedNotes, n.id)
                                                        ? orange
                                                        : accent.withRotatedHue(float(n.colourGroup) / 16.f))
                                                 : (selected(s.selectedNotes, n.id) ? orange.withAlpha(.7f)
                                                                                    : grid.brighter(.3f));
                            if (n.muted)
                                colour = colour.withMultipliedAlpha(.3f);
                            if (appearance.repeated() || appearance.dormant)
                                colour = colour.withMultipliedAlpha(appearance.dormant ? .28f : .52f);
                            // Velocity shades the body; an outline keeps neighbours apart.
                            const auto body = active ? colour.interpolatedWith(colour.darker(.45f), 1.f - n.velocity)
                                                     : colour;
                            g.setColour(body);
                            g.fillRoundedRectangle(r, 2.5f);
                            g.setColour(colour.darker(.55f).withMultipliedAlpha(.9f));
                            g.drawRoundedRectangle(r.reduced(.5f), 2.5f, 1.f);
                            if (active && r.getHeight() >= 8.f && r.getWidth() >= 20.f && !n.muted)
                            {
                                auto name = juce::MidiMessage::getMidiNoteName(n.pitch, true, true, 5);
                                if (std::abs(n.fineCents) > .5)
                                    name << (n.fineCents > 0 ? " +" : " ") << juce::roundToInt(n.fineCents);
                                g.setColour(themeColour(0xff0e1418).withAlpha(.85f));
                                g.setFont(juce::Font(vibera::uiFont, juce::jlimit(8.f, 11.5f, r.getHeight() - 2.f),
                                                     juce::Font::bold));
                                g.drawText(name, r.withTrimmedLeft(4.f).withTrimmedRight(2.f),
                                           juce::Justification::centredLeft, true);
                            }
                            if (appearance.repeated() || appearance.dormant)
                            {
                                g.setColour(colour.brighter(.35f));
                                g.drawRoundedRectangle(r, 2, 1.f);
                            }
                            if (n.slide)
                            {
                                juce::Path triangle;
                                triangle.addTriangle(r.getX() + 3, r.getBottom() - 2, r.getX() + 10,
                                                     r.getBottom() - 2, r.getX() + 10, r.getY() + 2);
                                g.setColour(ink.withAlpha(.9f));
                                g.fillPath(triangle);
                            }
                            if (n.portamento && r.getWidth() > 16)
                            {
                                g.setColour(ink.withAlpha(.9f));
                                g.drawEllipse(r.getRight() - 10, r.getCentreY() - 3, 6, 6, 1.f);
                            }
                            if (n.muted)
                            {
                                g.setColour(ink.withAlpha(.45f));
                                g.drawHorizontalLine(juce::roundToInt(r.getCentreY()), r.getX(),
                                                     r.getRight());
                            }
                            if (active)
                            {
                                g.setColour(colour.brighter(.45f).withMultipliedAlpha(.8f));
                                g.fillRect(r.reduced(1.5f, 0.f).withHeight(1.f).translated(0.f, 1.f));
                            }
                        }
                }
                else
                    for (auto &c : playlistClips())
                    {
                        auto r = cr(c);
                        auto *p = project.pattern(c.pattern);
                        const auto *audio = audioClip(c.id);
                        const auto *source = audio ? project.channel(audio->channel) : nullptr;
                        const auto *automated = automationClip(c.id);
                        const auto *curve = automated ? project.automationSource(automated->source) : nullptr;
                        auto colour = p && p->colour ? juce::Colour(p->colour)
                                                     : channelColour(source ? int(source->id % 8)
                                                                     : p    ? int(p->id % 8)
                                                                            : 0);
                        if (curve)
                            colour = curve->colour ? juce::Colour(curve->colour) : accent;
                        if (effectiveSet.contains(c.id))
                            colour = orange;
                        if (c.muted)
                            colour = colour.darker(.6f);
                        g.setColour(colour.darker(.65f));
                        g.fillRoundedRectangle(r, 3);
                        g.setColour(colour.withAlpha(.7f));
                        g.fillRect(r.withHeight(16));
                        if (groupedClips.contains(c.id))
                        {
                            g.setColour(s.controller.clipGroupingEnabled ? ink : muted);
                            const auto x = r.getX() + 3, y = r.getY() + 5;
                            g.drawRoundedRectangle(x, y, 6, 5, 1.5f, 1);
                            g.drawRoundedRectangle(x + 4, y + 2, 6, 5, 1.5f, 1);
                        }
                        else
                            label(g, c.muted ? "x" : "v", {int(r.getX()) + 3, int(r.getY()), 12, 16}, ink,
                                  10);
                        label(g,
                              curve    ? juce::String::fromUTF8(curve->name.c_str())
                              : source ? source->name
                              : p      ? p->name
                                       : "Missing",
                              {int(r.getX()) + 18, int(r.getY()), int(r.getWidth()) - 22, 16}, ink, 11);
                        if (audio && source && r.getWidth() > 4)
                        {
                            if (!waveforms)
                                waveforms = std::make_unique<PlaylistWaveforms>(*this);
                            auto thumbnail = waveforms->get(source->source);
                            auto area = r.withTrimmedTop(18).reduced(2).toNearestInt();
                            g.setColour(ink.withAlpha(.8f));
                            thumbnail.drawChannels(g, area, audio->offset, audio->offset + audio->duration,
                                                   1.f);
                            // Fades: shaded corner and curve over the faded span.
                            const float perSecond = float(r.getWidth() / audio->duration);
                            for (int side = 0; side < 2; ++side)
                            {
                                const float span =
                                    float(side == 0 ? audio->fadeIn : audio->fadeOut) * perSecond;
                                if (span < 1.f)
                                    continue;
                                const float x0 = side == 0 ? r.getX() : r.getRight() - span;
                                juce::Path fade;
                                fade.startNewSubPath(x0, side == 0 ? r.getBottom() : r.getY() + 1);
                                fade.quadraticTo(side == 0 ? x0 : x0 + span, r.getY() + 1, x0 + span,
                                                 side == 0 ? r.getY() + 1 : r.getBottom());
                                juce::Path shade(fade);
                                shade.lineTo(side == 0 ? x0 : x0 + span, r.getY() + 1);
                                shade.closeSubPath();
                                g.setColour(bg.withAlpha(.45f));
                                g.fillPath(shade);
                                g.setColour(ink.withAlpha(.85f));
                                g.strokePath(fade, juce::PathStrokeType(1.2f));
                            }
                        }
                        if (curve)
                        {
                            const auto area = automationArea(c.id);
                            if (area.getWidth() > 0 && area.getHeight() > 0)
                            {
                                juce::Graphics::ScopedSaveState clipped(g);
                                g.reduceClipRegion(r.toNearestInt());
                                juce::Path line;
                                const int steps = std::clamp(int(area.getWidth()), 2, 512);
                                for (int step = 0; step <= steps; ++step)
                                {
                                    const float x = area.getX() + area.getWidth() * step / steps;
                                    const float value =
                                        automationSourceValueAt(*curve, c.offset + c.length * step / steps);
                                    const float y = area.getBottom() - value * area.getHeight();
                                    if (step == 0)
                                        line.startNewSubPath(x, y);
                                    else
                                        line.lineTo(x, y);
                                }
                                g.setColour(ink);
                                g.strokePath(line, juce::PathStrokeType(1.4f));
                                for (const auto &point : curve->points)
                                    if (point.beat >= c.offset && point.beat <= c.offset + c.length)
                                    {
                                        const auto location = automationPointPosition(c.id, point);
                                        g.setColour(point.id == automationDragPoint ? orange : ink);
                                        g.fillEllipse(location.x - 3, location.y - 3, 6, 6);
                                    }
                            }
                        }
                        // Miniature notes come from the actual multi-channel pattern and visible source
                        // window.
                        if (p)
                            for (auto &n : p->notes)
                                if (n.beat >= c.offset && n.beat < c.offset + c.length)
                                {
                                    g.setColour(channelColour(int(n.channel - 1)));
                                    float x = r.getX() + float((n.beat - c.offset) * spacing());
                                    float y =
                                        r.getY() + 19 + float((72 - n.pitch) / 48.) * (r.getHeight() - 22);
                                    g.fillRect(x, juce::jlimit(r.getY() + 19, r.getBottom() - 3, y),
                                               float(std::max(2., n.length * spacing() - 1)), 2.f);
                                }
                        g.setColour(colour);
                        g.drawRoundedRectangle(r, 3, 1);
                    }
                if (s.liveTakePattern() == s.selectedPattern)
                {
                    if (type == 1)
                    {
                        for (auto &note : s.liveTakeNotes())
                            if (note.channel == s.selectedChannel && note.pitch <= topPitch &&
                                note.pitch > topPitch - visiblePitches())
                            {
                                auto r = nr(note);
                                g.setColour(orange.withAlpha(.45f));
                                g.fillRoundedRectangle(r, 2);
                                g.setColour(orange);
                                g.drawRoundedRectangle(r, 2, 1);
                            }
                    }
                    else if (s.liveTakeInSong() && !s.liveTakeNotes().empty())
                    {
                        double end = 0;
                        for (auto &note : s.liveTakeNotes())
                            end = std::max(end, note.beat + note.length);
                        Placement preview{0,
                                          s.liveTakePattern(),
                                          0,
                                          s.liveTakeSongStart(),
                                          std::max(.25, end - s.liveTakeSourceOffset()),
                                          s.liveTakeSourceOffset()};
                        auto r = cr(preview);
                        g.setColour(orange.withAlpha(.25f));
                        g.fillRoundedRectangle(r, 3);
                        g.setColour(orange);
                        g.drawRoundedRectangle(r, 3, 1.5f);
                        label(g, r.getWidth() >= 70 ? "Recording" : "R",
                              {int(r.getX()) + 2, int(r.getY()), std::max(1, int(r.getWidth()) - 4), 16}, ink,
                              11);
                    }
                }
            }
            if (type == 1)
                if (pattern())
                    for (const auto &appearance : pianoView())
                        if (const auto &n = appearance.note; n.channel == s.selectedChannel)
                        {
                            float x = float(left + (n.beat - scroll) * spacing());
                            if (x < left || x >= getWidth())
                                continue;
                            g.setColour((selected(s.selectedNotes, n.id) ? orange : accent)
                                            .withMultipliedAlpha(n.muted ? .3f : 1.f)
                                            .withMultipliedAlpha(appearance.repeated() ? .52f
                                                                 : appearance.dormant  ? .28f
                                                                                       : 1.f));
                            g.fillRect(x, velocityBase() - n.velocity * velocityScale(), 3.f,
                                       n.velocity * velocityScale());
                        }
            // Recorded controllers of the Channel as dots in the velocity lane (bend centred).
            if (type == 1 && velocityHeight() > 0)
                if (const auto *part = pattern())
                    for (const auto &control : part->controllers)
                        if (control.channel == s.selectedChannel)
                        {
                            const float x = float(left + (control.beat - scroll) * spacing());
                            if (x < left || x >= getWidth())
                                continue;
                            const double level = control.type == 1   ? (control.value + 1) * .5
                                                 : control.type == 3 ? control.number / 127.
                                                                     : control.value;
                            g.setColour(juce::Colour(control.type == 1   ? 0xffb889f4
                                                     : control.type == 2 ? 0xfff4a259
                                                     : control.type == 3 ? 0xff9ece6a
                                                                         : 0xff7aa2f7)
                                            .withAlpha(.85f));
                            g.fillEllipse(x - 2.f, float(velocityBase() - level * velocityScale()) - 2.f, 4.f, 4.f);
                        }
            if (type == 1 && s.liveTakePattern() == s.selectedPattern)
                for (auto &note : s.liveTakeNotes())
                    if (note.channel == s.selectedChannel)
                    {
                        float x = float(left + (note.beat - scroll) * spacing());
                        if (x >= left && x < getWidth())
                        {
                            g.setColour(orange);
                            g.fillRect(x, velocityBase() - note.velocity * velocityScale(), 3.f,
                                       note.velocity * velocityScale());
                        }
                    }
            if (s.audio.playing())
            {
                int x = int(left + (s.audio.positionBeat() - scroll) * spacing());
                if (x >= left && x < getWidth())
                {
                    g.setColour(accent);
                    g.drawVerticalLine(x, 28.f, float(getHeight()));
                }
            }
        }
        else
        {
            label(g,
                  "Name: F2 / middle-click | Move: Alt arrows / Shift-wheel name | Enable: mute / Ctrl solo "
                  "| Pan: "
                  "double-click centre",
                  {12, 0, getWidth() - 24, 27}, muted.withAlpha(.5f), 11); // Quiet hint, not a heading.
            int width = mixerWidth();
            int position = 0;
            g.saveState();
            g.reduceClipRegion(0, 30, mixerStripWidth() - 12, getHeight() - 40);
            for (auto &m : project.mixer)
            {
                int x = 6 + position++ * width - int(mixerScroll);
                if (x + width < 0 || x >= mixerStripWidth() - 12)
                    continue;
                auto colour = m.colour != 0  ? juce::Colour(m.colour)
                              : m.index == 0 ? accent
                                             : channelColour(m.index - 1);
                const bool activeInsert = m.index == s.selectedInsert;
                g.setColour(activeInsert                         ? bg.interpolatedWith(accent, .19f)
                            : s.mixerSelection.contains(m.index) ? bg.interpolatedWith(accent, .08f)
                                                                 : bg.brighter(.03f));
                g.fillRoundedRectangle(float(x), 32.f, float(width - 5), float(getHeight() - 44), 3);
                if (activeInsert)
                {
                    g.setColour(accent.withAlpha(.85f));
                    g.drawRoundedRectangle(float(x) + .75f, 32.75f, float(width) - 6.5f,
                                           float(getHeight()) - 45.5f, 3.f, 1.5f);
                }
                // Header: a dark tint of the insert colour with a solid colour cap, instead of a
                // saturated block, so a row of strips stays calm and the names stay readable.
                g.setColour(bg.interpolatedWith(colour, .28f));
                g.fillRoundedRectangle(float(x + 2), 34.f, float(width - 9), 24.f, 2);
                g.setColour(colour);
                g.fillRoundedRectangle(float(x + 2), 34.f, float(width - 9), 3.f, 1.5f);
                g.setColour(ink);
                g.setFont(juce::Font(vibera::uiFont, 13, juce::Font::bold));
                g.drawFittedText(m.name, {x + 9, 34, width - 20, 24}, juce::Justification::centredLeft, 1, .9f);
                if (const auto *group = mixerGroup(project, mixerGroupFor(project, m.index)))
                {
                    g.setColour(group->colour ? juce::Colour(group->colour) : accent);
                    g.fillRect(x + 3, 57, width - 11, 2); // Separate marker; no new target/overlap.
                }
                auto enable = mixerMuteBounds(m.index), pan = mixerPanBounds(m.index);
                if (!enable.isEmpty())
                {
                    const bool solo = project.mixerSolo && project.mixerSolo->insert == m.index;
                    g.setColour(m.muted ? muted.withAlpha(.35f) : solo ? orange : accent);
                    g.fillEllipse(enable.withSizeKeepingCentre(18, 18).toFloat());
                    if (solo)
                    {
                        // Keep the solo indication inside its enable light;
                        // the former below-light label overlapped the dBFS header.
                        g.setColour(bg);
                        g.setFont(juce::Font(vibera::uiFont, 11, juce::Font::bold));
                        g.drawText("S", enable, juce::Justification::centred, false);
                    }
                    // Pan as a centre-out bar: the filled span shows direction and amount at a glance.
                    g.setColour(grid);
                    g.fillRoundedRectangle(pan.toFloat(), 4);
                    const float half = pan.getWidth() / 2.f - 3.f, centreX = float(pan.getCentreX());
                    const float panX = centreX + m.pan * half;
                    g.setColour(colour);
                    g.fillRoundedRectangle(juce::Rectangle<float>(std::min(centreX, panX), float(pan.getBottom() - 5),
                                                                  std::max(2.f, std::abs(panX - centreX)), 3.f),
                                           1.5f);
                    const auto text = std::abs(m.pan) <= .005
                                          ? juce::String("C")
                                          : juce::String(m.pan < 0 ? "L" : "R") +
                                                juce::String(juce::roundToInt(std::abs(m.pan) * 100));
                    g.setColour(ink.withAlpha(.9f));
                    g.setFont(juce::Font(vibera::uiFont, 11.5f, juce::Font::plain));
                    g.drawText(text, pan.withTrimmedBottom(4), juce::Justification::centred, false);
                }
                const int h = mixerFaderTravel();
                const auto fader = mixerFaderBounds(m.index);
                g.setColour(grid);
                g.fillRoundedRectangle(fader.toFloat(), 3);
                const float displayGain =
                    s.audio.playing() ? s.audio.mixerDisplayGain(m.index).value_or(m.gain) : m.gain;
                const float y = mixerGainY(displayGain);
                // Neutral fader cap with a coloured centre line, like a hardware console.
                const auto thumb = mixerFaderThumbBounds(m.index, displayGain);
                g.setGradientFill(juce::ColourGradient(panel.brighter(m.muted ? .12f : .42f), thumb.getX(), thumb.getY(),
                                                       panel.brighter(m.muted ? .04f : .2f), thumb.getX(),
                                                       thumb.getBottom(), false));
                g.fillRoundedRectangle(thumb, 3);
                g.setColour(juce::Colours::black.withAlpha(.35f));
                g.drawRoundedRectangle(thumb.reduced(.5f), 3, 1.f);
                g.setColour(m.muted ? muted : colour);
                g.fillRect(thumb.getX() + 3.f, float(y) - 1.f, thumb.getWidth() - 6.f, 2.f);
                const auto meter = s.audio.mixerMeter(m.index);
                if (mixerOverloadProject != s.controller.projectIdentity)
                {
                    mixerOverloadDisplay.clear();
                    mixerOverloadProject = s.controller.projectIdentity;
                }
                auto &heldOverload = mixerOverloadDisplay[m.index];
                if (meter.available)
                    heldOverload = {meter.overload[0], meter.overload[1]};
                if (getHeight() >= 180)
                {
                    const bool overload = heldOverload[0] || heldOverload[1];
                    label(g, overload ? "CLIP" : "dB", mixerMeterHeaderBounds(m.index),
                          overload ? juce::Colour(0xffef6678) : muted.withAlpha(.8f), 11.f);
                    // Compact strips retain legible endpoints and a midpoint; tall ones add12dB steps.
                    const int step = h < 72 ? 60 : h < 180 ? 30 : 12;
                    for (int db = 0; db >= -60; db -= step)
                    {
                        if (h < 24 && db < 0)
                            continue;
                        const int tickY = juce::roundToInt(mixerMeterY(float(db)));
                        g.setColour(muted.withAlpha(.5f));
                        g.drawHorizontalLine(tickY, float(x + 20), float(x + 23));
                        const auto textBounds = mixerMeterTickLabelBounds(m.index, db);
                        if (!textBounds.isEmpty())
                            label(g, juce::String(db), textBounds, muted, 11.f);
                    }
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const int mx = x + 7 + ch * 7;
                        g.setColour(grid);
                        g.fillRect(mx, 100, 5, h);
                        const auto db = juce::Decibels::gainToDecibels(meter.display[ch], -60.f);
                        const int top = juce::roundToInt(mixerMeterY(db));
                        g.setColour(heldOverload[size_t(ch)] ? juce::Colour(0xffef6678)
                                    : db > -6                ? orange
                                                             : accent);
                        g.fillRect(mx, top, 5, 100 + h - top);
                    }
                }
                label(g,
                      displayGain > 0
                          ? juce::String(juce::Decibels::gainToDecibels(displayGain, -100.f), 1) + " dB"
                          : juce::String("-inf dB"),
                      mixerGainReadoutBounds(m.index), ink, 13);
                // Read-only actual signal values occupy spare height; no invented effect controls.
                // These are current-block peak/RMS, not a retained clip-hold or loudness measurement.
                if (100 + h + 184 <= getHeight() - 44)
                {
                    const int infoY = 132 + h;
                    auto signalText = [](float value)
                    {
                        return value > 0 ? juce::String(juce::Decibels::gainToDecibels(value, -100.f), 1)
                                         : juce::String("-inf");
                    };
                    const auto signalColour = meter.stale || !meter.available ? muted : ink;
                    label(g,
                          !meter.available ? "Unavailable"
                          : meter.stale    ? "Stale"
                                           : "Signal dBFS",
                          {x + 6, infoY, width - 12, 20}, signalColour, 13);
                    if (meter.available)
                    {
                        label(g, "Block peak", {x + 6, infoY + 25, width - 12, 18}, muted, 13);
                        label(g, "L " + signalText(meter.peak[0]), {x + 6, infoY + 44, width - 12, 18},
                              signalColour, 14);
                        label(g, "R " + signalText(meter.peak[1]), {x + 6, infoY + 64, width - 12, 18},
                              signalColour, 14);
                        label(g, "RMS", {x + 6, infoY + 88, width - 12, 18}, muted, 13);
                        label(g, "L " + signalText(meter.rms[0]), {x + 6, infoY + 108, width - 12, 18},
                              signalColour, 14);
                        label(g, "R " + signalText(meter.rms[1]), {x + 6, infoY + 128, width - 12, 18},
                              signalColour, 14);
                    }
                }
                const auto stereo = mixerStereoSeparationBounds(m.index);
                if (!stereo.isEmpty())
                {
                    const auto knob = stereo.withSizeKeepingCentre(28, 28).withX(stereo.getX());
                    const auto centre = knob.getCentre().toFloat();
                    constexpr float startAngle = 3.9269908f, sweep = 4.7123890f;
                    const float angle = startAngle + (m.stereoSeparation + 1.f) * .5f * sweep;
                    g.setColour(grid);
                    g.fillEllipse(knob.toFloat());
                    juce::Path ring;
                    ring.addCentredArc(centre.x, centre.y, 11, 11, 0, startAngle, startAngle + sweep, true);
                    g.setColour(muted.withAlpha(.65f));
                    g.strokePath(ring, juce::PathStrokeType(1.5f));
                    g.setColour(colour);
                    g.drawLine(centre.x, centre.y, centre.x + std::sin(angle) * 8,
                               centre.y - std::cos(angle) * 8, 2);
                    const auto text = stereo.withTrimmedLeft(34);
                    label(g, "Stereo", text.withHeight(16), muted, 12.5f);
                    const int percent = juce::roundToInt(m.stereoSeparation * 100);
                    label(g, (percent > 0 ? "+" : "") + juce::String(percent) + "%", text.withTrimmedTop(16),
                          ink, 14);
                    const auto phase = mixerPhaseInvertBounds(m.index), swap = mixerStereoSwapBounds(m.index);
                    for (int icon = 0; icon < 2; ++icon)
                    {
                        const auto box = icon == 0 ? phase : swap;
                        const bool active = icon == 0 ? m.phaseInverted : m.stereoSwapped;
                        g.setColour(active ? colour.withAlpha(.30f) : grid);
                        g.fillRoundedRectangle(box.toFloat(), 4);
                        g.setColour(active ? colour : ink.withAlpha(.8f));
                        const auto c = box.getCentre().toFloat();
                        juce::Path shape;
                        if (icon == 0)
                        {
                            shape.addEllipse(c.x - 6, c.y - 6, 12, 12);
                            shape.startNewSubPath(c.x - 9, c.y + 8);
                            shape.lineTo(c.x + 9, c.y - 8);
                        }
                        else
                        {
                            shape.startNewSubPath(c.x - 10, c.y - 5);
                            shape.lineTo(c.x - 5, c.y - 5);
                            shape.lineTo(c.x + 5, c.y + 5);
                            shape.lineTo(c.x + 10, c.y + 5);
                            shape.startNewSubPath(c.x + 7, c.y + 2);
                            shape.lineTo(c.x + 10, c.y + 5);
                            shape.lineTo(c.x + 7, c.y + 8);
                            shape.startNewSubPath(c.x - 10, c.y + 5);
                            shape.lineTo(c.x - 5, c.y + 5);
                            shape.lineTo(c.x + 5, c.y - 5);
                            shape.lineTo(c.x + 10, c.y - 5);
                            shape.startNewSubPath(c.x + 7, c.y - 8);
                            shape.lineTo(c.x + 10, c.y - 5);
                            shape.lineTo(c.x + 7, c.y - 2);
                        }
                        g.strokePath(shape, juce::PathStrokeType(1.6f));
                        if (active)
                            g.fillEllipse(float(box.getRight() - 6), float(box.getY() + 3), 3, 3);
                    }
                }

                const auto *route = mixerRoute(project, s.selectedInsert, m.index);
                const bool source = m.index == s.selectedInsert;
                const bool available = canMixerRoute(project, s.selectedInsert, m.index).wasOk();
                auto button = mixerRouteButtonBounds(m.index);
                g.setColour(route ? accent.withAlpha(.28f) : bg);
                g.fillRoundedRectangle(button.toFloat(), 4);
                g.setColour(source      ? orange
                            : route     ? accent
                            : available ? ink.withAlpha(.65f)
                                        : muted.withAlpha(.28f));
                juce::Path arrow;
                const float cx = button.getCentreX(), cy = button.getCentreY();
                if (source)
                    arrow.addTriangle(cx - 6, cy - 4, cx + 6, cy - 4, cx, cy + 5);
                else
                    arrow.addTriangle(cx - 6, cy + 4, cx + 6, cy + 4, cx, cy - 5);
                g.fillPath(arrow);
                if (route)
                {
                    const auto level = mixerSendLevelBounds(m.index);
                    g.setColour(accent.withAlpha(.45f));
                    g.drawEllipse(level.toFloat().reduced(1), 1.5f);
                    label(g, juce::String(juce::roundToInt(route->gain * 100)), level, ink, 14);
                }
            }
            g.restoreState();
            const auto layout = mixerEffectLayout();
            if (!layout.pane.isEmpty())
            {
                g.setColour(bg);
                g.fillRect(layout.pane);
                g.setColour(grid);
                g.drawVerticalLine(layout.pane.getX(), float(layout.pane.getY()),
                                   float(layout.pane.getBottom()));
                const auto *insert = mixerInsert(project, s.selectedInsert);
                label(g, "EFFECTS", {layout.pane.getX() + 12, 32, layout.pane.getWidth() - 84, 28}, accent,
                      12);
                g.setColour(grid);
                g.fillRoundedRectangle(layout.chainBypass.toFloat(), 4.f);
                const auto enabled = layout.chainBypass.getCentre().toFloat();
                g.setColour(insert && insert->effectsBypassed ? orange : accent);
                g.drawEllipse(enabled.x - 6, enabled.y - 6, 12, 12, 1.5f);
                g.drawLine(enabled.x, enabled.y - 8, enabled.x, enabled.y - 1, 1.5f);
                g.setColour(grid);
                g.fillRoundedRectangle(layout.add.toFloat(), 4.f);
                const auto plus = layout.add.getCentre().toFloat();
                g.setColour(ink);
                g.drawLine(plus.x - 5, plus.y, plus.x + 5, plus.y, 1.5f);
                g.drawLine(plus.x, plus.y - 5, plus.x, plus.y + 5, 1.5f);
                if (insert)
                {
                    const int offset = layout.clampedOffset(effectOffset, int(insert->effects.size()));
                    g.saveState();
                    g.reduceClipRegion(layout.rows);
                    for (int visible = 0; visible < layout.visibleRows(); ++visible)
                    {
                        const int index = offset + visible;
                        if (index > int(insert->effects.size()))
                            break;
                        const auto row = layout.row(visible);
                        g.setColour(panel);
                        g.fillRoundedRectangle(row.toFloat(), 4.f);
                        if (index == int(insert->effects.size()))
                        {
                            label(g, "+  Add effect", row.reduced(10, 0), muted, 12);
                            break;
                        }
                        const auto &fx = insert->effects[size_t(index)];
                        const auto power = layout.bypass(visible).getCentre().toFloat();
                        const bool bypassed = insert->effectsBypassed || fx.bypass;
                        g.setColour(bypassed ? muted : accent);
                        g.drawEllipse(power.x - 5, power.y - 5, 10, 10, 1.5f);
                        g.drawLine(power.x, power.y - 7, power.x, power.y - 1, 1.5f);
                        label(g, juce::String(index + 1) + "  " + fx.name, layout.name(visible),
                              bypassed ? muted : ink, 12);
                        const auto wet = layout.wet(visible).toFloat().reduced(4);
                        const auto centre = wet.getCentre();
                        constexpr float start = -juce::MathConstants<float>::pi * .75f,
                                        end = juce::MathConstants<float>::pi * .75f;
                        juce::Path arc;
                        arc.addCentredArc(centre.x, centre.y, wet.getWidth() * .5f, wet.getHeight() * .5f, 0,
                                          start, end, true);
                        g.setColour(grid);
                        g.strokePath(arc, juce::PathStrokeType(2.f));
                        juce::Path amount;
                        amount.addCentredArc(centre.x, centre.y, wet.getWidth() * .5f, wet.getHeight() * .5f,
                                             0, start, start + (end - start) * fx.wet, true);
                        g.setColour(bypassed ? muted : accent);
                        g.strokePath(amount, juce::PathStrokeType(2.f));
                        const float angle = start + (end - start) * fx.wet;
                        g.drawLine(centre.x, centre.y, centre.x + std::sin(angle) * 6,
                                   centre.y - std::cos(angle) * 6, 1.5f);
                        label(g,
                              "Wet " + juce::String(juce::roundToInt(fx.wet * 100)) + "%" +
                                  (bypassed ? "  Bypassed" : ""),
                              layout.wetLabel(visible), bypassed ? muted : ink, 11);
                        const auto grip = layout.grip(visible).getCentre().toFloat();
                        g.setColour(muted);
                        for (int x = -1; x <= 1; x += 2)
                            for (int y = -1; y <= 1; ++y)
                                g.fillEllipse(grip.x + float(x * 2) - 1, grip.y + float(y * 4) - 1, 2, 2);
                        if (effectOrder && effectOrderMoved && effectOrder->destination == index)
                        {
                            g.setColour(accent);
                            g.drawRoundedRectangle(row.toFloat().reduced(.5f), 4.f, 1.f);
                        }
                        const auto dots = layout.menu(visible).getCentre().toFloat();
                        g.setColour(muted);
                        for (int dot = -1; dot <= 1; ++dot)
                            g.fillEllipse(dots.x + float(dot * 4) - 1, dots.y - 1, 2, 2);
                    }
                    g.restoreState();
                }
                label(g, "Wet: drag/Shift fine; wheel; dbl-click100%",
                      {layout.pane.getX() + 8, layout.pane.getBottom() - 25, layout.pane.getWidth() - 16, 22},
                      muted, 10);
            }
        }
        if (slicing && type == 2)
        {
            g.setColour(orange);
            g.drawLine({origin, sliceEnd}, 2.f);
        }
        if (type == 1)
        {
            auto handle = stretchHandleBounds();
            if (!handle.isEmpty())
            {
                // Stretch grip for the selection: a slim pill with two grip lines.
                const auto grip = handle.toFloat().reduced(1.f, 0.f);
                g.setColour(orange.withAlpha(.85f));
                g.fillRoundedRectangle(grip, 2.5f);
                g.setColour(bg.withAlpha(.8f));
                g.fillRect(grip.getCentreX() - 1.5f, grip.getY() + 5.f, 1.f, grip.getHeight() - 10.f);
                g.fillRect(grip.getCentreX() + .5f, grip.getY() + 5.f, 1.f, grip.getHeight() - 10.f);
            }
            if (s.noteStretchPending() && s.noteStretchSession->args.pattern == s.selectedPattern &&
                s.noteStretchSession->revision == s.controller.revision)
            {
                juce::Graphics::ScopedSaveState save(g);
                g.reduceClipRegion(leftEdge(), gridTop(), getWidth() - leftEdge() - scrollBarThickness,
                                   noteBottom() - gridTop());
                for (const auto &note : s.noteStretchSession->plan.notes)
                {
                    auto r = nr(note).toFloat();
                    g.setColour(orange.withAlpha(note.muted ? .08f : .18f));
                    g.fillRoundedRectangle(r, 2.f);
                    g.setColour(orange.withAlpha(note.muted ? .5f : 1.f));
                    g.drawRoundedRectangle(r, 2.f, 1.5f);
                }
            }
        }
        if (type == 1 && s.noteSlicePending() && s.noteSliceSession->args.slice &&
            s.noteSliceSession->revision == s.controller.revision &&
            s.noteSliceSession->project == s.controller.projectIdentity)
        {
            const auto &line = *s.noteSliceSession->args.slice;
            auto pixel = [&](double beat, double pitch)
            {
                return juce::Point<float>{float(leftEdge() + (beat - scroll) * spacing()),
                                          float(gridTop() + (topPitch - pitch + .5) * height())};
            };
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(juce::Rectangle<int>(leftEdge(), gridTop(),
                                                    std::max(0, getWidth() - leftEdge() - rightInset()),
                                                    std::max(0, noteBottom() - gridTop())));
            g.setColour(accent.brighter(.5f));
            g.drawLine({pixel(line.startBeat, line.startPitch), pixel(line.endBeat, line.endPitch)}, 2.f);
            if (auto *p = pattern())
                for (const auto &cut : s.noteSliceSession->cuts)
                {
                    const auto &source = p->notes[cut.index];
                    auto marker = [&](double beat)
                    {
                        const auto at = pixel(beat, source.pitch);
                        g.drawLine(at.x, at.y - height() * .4f, at.x, at.y + height() * .4f, 2.f);
                    };
                    if (line.loopPreview)
                    {
                        for (const auto &row : pianoView())
                            if (row.note.id == source.id)
                            {
                                const auto beat = cut.beat + row.note.beat - row.sourceBeat;
                                if (beat > row.note.beat && beat < row.note.beat + row.note.length)
                                    marker(beat);
                            }
                    }
                    else
                        marker(cut.beat);
                }
        }
        if (selecting || regionZooming)
        {
            g.setColour(accent.withAlpha(.15f));
            g.fillRect(rectangle);
            g.setColour(accent);
            g.drawRect(rectangle, 1);
        }
    }
    // Playlist track header: the LED toggles mute (FL's track switch), right-click edits.
    void trackMenu(int lane, const juce::ModifierKeys &mods)
    {
        const auto *info = s.controller.project.track(lane);
        auto run = [this, lane](const char *id, auto &&fill)
        {
            CommandArgs a;
            a.lane = lane;
            fill(a);
            s.command(id, a);
            repaint();
        };
        if (!mods.isPopupMenu() && mods.isCtrlDown())
            return run("track.solo", [](CommandArgs &) {}); // Ctrl+click the LED: solo (again: unsolo).
        if (!mods.isPopupMenu())
            return run("track.mute", [&](CommandArgs &a) { a.enabled = !(info && info->muted); });
        juce::PopupMenu menu, colours;
        menu.addItem(1, "Mute track", true, info && info->muted);
        menu.addItem(7, "Solo track (Ctrl+click the LED)");
        int head = lane; // First track of this lane's group.
        while (head > 0 && s.controller.project.track(head) && s.controller.project.track(head)->groupedAbove)
            --head;
        const auto *below = s.controller.project.track(head + 1);
        const bool inGroup = below && below->groupedAbove;
        const auto *headInfo = s.controller.project.track(head);
        menu.addItem(8, "Group with track above", lane > 0, info && info->groupedAbove);
        menu.addItem(9, headInfo && headInfo->collapsed ? "Expand group" : "Collapse group", inGroup);
        menu.addItem(2, "Rename...");
        const std::array<juce::uint32, 8> palette{0xffef6576, 0xffe8a33d, 0xffe6d35a, 0xff6fcf7f,
                                                   0xff4fb3d9, 0xff7d8cf0, 0xffc77df0, 0xff9aa3ad};
        colours.addItem(20, "Theme colour", true, !info || info->colour == 0);
        for (size_t i = 0; i < palette.size(); ++i)
        {
            juce::Image swatch(juce::Image::ARGB, 14, 14, true);
            juce::Graphics(swatch).fillAll(juce::Colour(palette[i]));
            colours.addItem(21 + int(i), "Colour " + juce::String(int(i) + 1), true, info && info->colour == palette[i],
                            swatch);
        }
        menu.addSubMenu("Colour", colours);
        menu.addSeparator();
        menu.addItem(3, "Insert track above");
        menu.addItem(4, "Delete track (and its clips)");
        menu.addItem(5, "Move track up", lane > 0);
        menu.addItem(6, "Move track down", lane < 127);
        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [safe = juce::Component::SafePointer<Editor>(this), lane, run, palette,
                            name = info ? info->name : juce::String()](int chosen)
                           {
                               if (!safe || chosen <= 0)
                                   return;
                               if (chosen == 7)
                                   run("track.solo", [](CommandArgs &) {});
                               else if (chosen == 8)
                                   run("track.group", [&](CommandArgs &a)
                                       {
                                           const auto *t = safe->s.controller.project.track(lane);
                                           a.enabled = !(t && t->groupedAbove);
                                       });
                               else if (chosen == 9)
                                   run("track.collapse", [&](CommandArgs &a)
                                       {
                                           int first = lane;
                                           const auto &project = safe->s.controller.project;
                                           while (first > 0 && project.track(first) && project.track(first)->groupedAbove)
                                               --first;
                                           a.enabled = !(project.track(first) && project.track(first)->collapsed);
                                       });
                               else if (chosen == 1)
                                   run("track.mute", [&](CommandArgs &a) { a.enabled = !safe->s.controller.project.laneMuted(lane); });
                               else if (chosen == 2)
                               {
                                   auto *box = new juce::AlertWindow("Rename track", "Track " + juce::String(lane + 1),
                                                                     juce::MessageBoxIconType::NoIcon);
                                   box->addTextEditor("name", name);
                                   box->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                                   box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                                   box->enterModalState(true, juce::ModalCallbackFunction::create(
                                                                  [box, run](int result)
                                                                  {
                                                                      if (result == 1)
                                                                          run("track.rename", [&](CommandArgs &a)
                                                                              { a.text = box->getTextEditorContents("name"); });
                                                                  }),
                                                        true);
                               }
                               else if (chosen == 3)
                                   run("track.insert", [](CommandArgs &) {});
                               else if (chosen == 4)
                                   run("track.delete", [](CommandArgs &) {});
                               else if (chosen == 5 || chosen == 6)
                                   run("track.move", [&](CommandArgs &a) { a.value = chosen == 5 ? -1 : 1; });
                               else if (chosen >= 20)
                                   run("track.colour", [&](CommandArgs &a)
                                       { a.value = chosen == 20 ? 0. : double(palette[size_t(chosen - 21)]); });
                           });
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (e.y >= getHeight() - scrollBarThickness || (type != 3 && e.x >= getWidth() - rightInset()))
            return;
        if (type == 1)
            if (auto *channel = s.controller.project.channel(s.selectedChannel);
                channel && channel->audioClip)
            {
                s.status = "Audio Clip Piano roll triggering is not implemented yet; edit it in Playlist";
                s.refresh();
                return;
            }

        grabKeyboardFocus();
        s.focus = Scope(type + 1);
        origin = e.position;
        lastErasePoint = origin;
        lastBeat = lastLength = 0;
        lastPitch = lastLane = 0;
        auto safe = juce::Component::SafePointer<Editor>(this);
        slipTarget = 0;
        if (type == 2 && e.x >= leftEdge() && e.y >= gridTop())
        {
            if (playlistTool == 7 || (paintMode && e.mods.isCtrlDown() && e.mods.isRightButtonDown()))
            {
                if (e.mods.isRightButtonDown() && !e.mods.isCtrlDown())
                    restorePlaylistView();
                else
                {
                    regionZooming = true;
                    rectangle = {e.position.x, e.position.y, 0, 0};
                }
                return;
            }
            if (e.mods.isLeftButtonDown() &&
                (playlistTool == 5 || e.mods.isCtrlDown() || (playlistTool == 6 && e.mods.isShiftDown())))
            {
                const auto id = hit(e.position);
                if (id && playlistTool == 5 && !e.mods.isCtrlDown())
                {
                    selectionBeforeMarquee = s.selectedClips;
                    toggleMarquee = e.mods.isShiftDown();
                    selecting = true;
                    rectangle = {e.position.x, e.position.y, 0, 0};
                    if (e.mods.isShiftDown())
                    {
                        s.selectItems(true, {id}, "toggle");
                    }
                    else
                        s.selectItems(true, {id});
                }
                else
                {
                    selectionBeforeMarquee = s.selectedClips;
                    toggleMarquee = e.mods.isShiftDown() && playlistTool == 5;
                    selecting = true;
                    rectangle = {e.position.x, e.position.y, 0, 0};
                    if (!e.mods.isShiftDown())
                        s.selectedClips.clear();
                }
                repaint();
                return;
            }
            if (playlistTool == 6 && e.mods.isLeftButtonDown())
            {
                s.controller.beginGesture();
                erasing = true;
                erase(e.position);
                return;
            }
        }
        if (type == 2 && e.x >= leftEdge() && e.y >= gridTop() && e.mods.isLeftButtonDown() &&
            !e.mods.isCtrlDown())
        {
            if (playlistTool == 4)
            {
                const auto id = hit(e.position);
                if (id)
                {
                    for (const auto &clip : playlistClips())
                        if (clip.id == id)
                            muteEnabled = !clip.muted;
                    muting = true;
                    muteTouched.clear();
                    s.controller.beginGesture();
                    muteAt(e.position);
                }
                return;
            }
            if (playlistTool == 3)
            {
                sliceEnd = origin;
                sliceAlt = e.mods.isAltDown();
                slicing = true;
                if (e.mods.isShiftDown())
                {
                    commitSlice();
                    slicing = false;
                }
                repaint();
                return;
            }
            if (playlistTool == 2)
            {
                if (auto id = hit(e.position))
                {
                    if (!selected(s.selectedClips, id))
                        s.selectedClips = {id};
                    slipTarget = id;
                    dragging = true;
                    s.controller.beginGesture();
                }
                return;
            }
        }
        if (type == 0)
        {
            if (e.y < 35)
            {
                if (e.x >= getWidth() - 72)
                {
                    int page = std::max(1, int((getWidth() - stepStart() - 8) / stepWidth()));
                    setRackViewport(rackOffset, stepOffset + (e.x < getWidth() - 36 ? -page : page));
                    return;
                }
                juce::PopupMenu m;
                m.addItem(200, "Auto: last occupied bar", true, pattern() && pattern()->autoLength);
                m.addSeparator();
                m.addItem(1, "1 bar");
                m.addItem(2, "2 bars");
                m.addItem(4, "4 bars");
                m.addItem(8, "8 bars");
                m.addItem(16, "16 bars");
                m.addItem(100, "Custom length...");
                juce::PopupMenu steps;
                for (const int count : {1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 128, 256, 512})
                    steps.addItem(1000 + count, juce::String(count) + " steps");
                m.addSubMenu("Manual steps", steps);
                m.addSeparator();
                m.addItem(201, "Pattern time signature...");
                const auto identity = s.controller.projectIdentity;
                const auto targetPattern = s.selectedPattern;
                const auto beatsPerBar = barBeats();
                const auto expectedRevision = s.controller.revision;
                m.showMenuAsync({},
                                [safe, identity, targetPattern, beatsPerBar, expectedRevision](int n)
                                {
                                    if (safe && n && safe->s.controller.projectIdentity == identity &&
                                        safe->s.selectedPattern == targetPattern &&
                                        safe->s.controller.revision == expectedRevision)
                                    {
                                        if (n == 100)
                                        {
                                            safe->s.performAction("pattern.length");
                                            return;
                                        }
                                        if (n == 201)
                                        {
                                            safe->s.performAction("pattern.meter");
                                            return;
                                        }
                                        CommandArgs a;
                                        a.pattern = targetPattern;
                                        a.text = n == 200 ? "auto" : "manual";
                                        a.length = n >= 1000 ? (n - 1000) * .25 : n * beatsPerBar;
                                        safe->s.command("pattern.extent", a);
                                    }
                                });
                return;
            }
            if (rackPlusBounds().contains(e.getPosition()))
            {
                s.showInstrumentMenu();
                return;
            }
            int row = (e.y - 44) / rackRowHeight() + rackOffset;
            auto *targetChannel = s.rackChannelAt(row);
            if (e.y >= 44 && !targetChannel && e.mods.isRightButtonDown() &&
                !(s.rackGraphVisible && e.y >= rackContentBottom()))
            {
                s.showInstrumentMenu(); // Right-click on an empty Rack area adds an instrument.
                return;
            }
            if (e.y < 44 || row >= rackOffset + rackRows() || !targetChannel)
                return;
            const auto channelId = targetChannel->id;
            if (e.x >= 4 && e.x < 18)
            {
                if (e.mods.isRightButtonDown())
                {
                    rackSelecting = true;
                    rackSelectionLastRow = row;
                    rackSelectionVisited = {channelId};
                    rackSelectAdd = !selected(s.selectedChannels, channelId);
                    s.selectRackChannels({channelId}, rackSelectAdd ? "add" : "remove");
                }
                else
                    s.selectRackChannels({channelId}, "replace", channelId);
                return;
            }
            if (e.x >= 18 && e.x < 34)
            {
                if (e.mods.isRightButtonDown())
                {
                    juce::PopupMenu menu;
                    s.addChannelMuteItems(menu, channelId);
                    menu.showMenuAsync({});
                }
                else
                {
                    CommandArgs args;
                    args.channel = channelId;
                    // Explicit precedence; combined FL modifiers are unobserved.
                    // Shift only changes the lock, Alt scopes solo to the group,
                    // and ordinary Ctrl keeps the existing global solo default.
                    if (e.mods.isShiftDown())
                    {
                        args.enabled = !targetChannel->muteLocked;
                        s.command("channel.muteLock", args);
                    }
                    else if (e.mods.isAltDown() || e.mods.isCtrlDown())
                    {
                        const bool grouped = e.mods.isAltDown();
                        args.soloScope = grouped ? "group" : "global";
                        args.enabled = !channelIsSoloedInScope(s.controller.project, channelId, grouped);
                        s.command("channel.solo", args);
                    }
                    else
                    {
                        args.enabled = !targetChannel->muted;
                        s.command("channel.mute", args);
                    }
                }
                return;
            }
            s.selectRackChannels({channelId}, "replace", channelId);
            targetChannel = s.controller.project.channel(channelId);
            if (!targetChannel)
                return;
            auto &c = *targetChannel;
            if (e.x < stepStart())
            {
                if (e.mods.isRightButtonDown())
                {
                    juce::PopupMenu m;
                    m.addItem(1, "Piano roll", !c.audioClip);
                    m.addItem(2, "Open instrument");
                    m.addItem(3, "Browse native / VST3 instruments...", !c.audioClip);
                    {
                        juce::PopupMenu insert, replaceWith;
                        s.addInstrumentItems(insert);
                        s.addInstrumentItems(replaceWith, c.id);
                        m.addSubMenu("Insert", insert);
                        m.addSubMenu("Replace", replaceWith, !c.audioClip);
                    }
                    m.addItem(4, "Cut itself", !c.audioClip && !c.instrument);
                    m.addItem(5, "Name / colour / icon...    F2 / middle-click");
                    m.addSeparator();
                    s.addStepFillItems(m, {c.id}, s.selectedPattern);
                    s.addChannelLoopItems(m, c.id, s.selectedPattern);
                    m.addSeparator();
                    s.addChannelManagementItems(m, {c.id});
                    s.addChannelPresetItems(m, c.id);
                    s.addChannelTuningItems(m, c.id);
                    m.addSeparator();
                    s.addRackGraphItem(m, c.id);
                    m.addSeparator();
                    s.addSwingItems(m, {c.id});
                    Id id = c.id;
                    const auto identity = s.controller.projectIdentity;
                    m.showMenuAsync({},
                                    [safe, id, identity](int action)
                                    {
                                        if (!safe || safe->s.controller.projectIdentity != identity ||
                                            !safe->s.controller.project.channel(id))
                                            return;
                                        safe->s.selectedChannel = id;
                                        safe->s.refresh();
                                        if (action == 1)
                                            safe->s.showEditor(1);
                                        if (action == 2)
                                            safe->s.openInstrument(id);
                                        if (action == 3)
                                            safe->s.pluginMenu(id);
                                        if (action == 5)
                                            safe->s.channelPropertiesDialog({id});
                                        if (action == 4)
                                        {
                                            CommandArgs args;
                                            args.channel = id;
                                            args.enabled = true;
                                            safe->s.command("channel.sampler.cutItself", args);
                                        }
                                    });
                }
                else if (e.mods.isMiddleButtonDown() && e.x >= 38 && e.x < rackLabelWidth() - 74)
                    s.channelPropertiesDialog({c.id});
                else if (e.x >= rackLabelWidth() - 36 && e.x < rackLabelWidth())
                {
                    juce::PopupMenu m;
                    const auto targets = std::make_shared<const MixerTargetChoices>(
                        s.controller.project, s.controller.projectIdentity, c.id);
                    for (const auto &choice : targets->items())
                        m.addItem(choice.ordinal, choice.name);
                    m.showMenuAsync({},
                                    [safe, targets](int ordinal)
                                    {
                                        if (!safe || ordinal <= 0)
                                            return;
                                        CommandArgs args;
                                        const auto valid = targets->resolve(
                                            safe->s.controller.project, safe->s.controller.projectIdentity,
                                            ordinal, args);
                                        if (valid.failed())
                                        {
                                            reportHistoryResult(safe->s, valid);
                                            return;
                                        }
                                        safe->s.command("channel.mixer", args);
                                    });
                }
                else if (e.x >= rackLabelWidth() - 70 && e.x < rackLabelWidth() - 40)
                {
                    if (!c.audioClip)
                        s.showEditor(1);
                }
                else if (e.x >= 38 && e.x < rackLabelWidth() - 74)
                    s.openInstrument(c.id);
            }
            else
            {
                if (c.audioClip)
                {
                    s.status = "Audio Clip step/Piano roll triggering is not implemented yet";
                    s.refresh();
                    return;
                }
                bool melodic = false;
                if (s.liveTakePattern() == s.selectedPattern)
                    melodic = std::any_of(s.liveTakeNotes().begin(), s.liveTakeNotes().end(),
                                          [&](const auto &note) { return note.channel == c.id; });
                if (auto *p = pattern())
                    melodic = melodic || stepSequenceIsMelodic(*p, c.id);
                if (melodic)
                    s.showEditor(1);
                else
                {
                    s.controller.beginGesture();
                    dragging = true;
                    step(e);
                }
            }
            s.refresh();
            return;
        }
        if (type == 1)
        {
            if (e.mods.isBackButtonDown())
            {
                switchGhost(e.position);
                return;
            }
            if (e.y >= 28 && e.y < gridTop() && e.x >= leftEdge() && e.x < getWidth() - rightInset() &&
                e.mods.isLeftButtonDown() && (e.mods.isCtrlDown() || e.getNumberOfClicks() >= 2))
            {
                timeSelecting = true;
                timeSelectionAdd = e.mods.isShiftDown();
                timeAnchor = timeStart = timeEnd = timelineBeat(float(e.x));
                repaint();
                return;
            }
            if (e.mods.isCtrlDown() && e.mods.isRightButtonDown() && e.x >= leftEdge() &&
                e.x < getWidth() - rightInset() && e.y >= gridTop() && e.y < noteBottom())
            {
                regionZooming = true;
                rectangle = {e.position.x, e.position.y, 0, 0};
                return;
            }
            if (e.mods.isMiddleButtonDown())
            {
                panning = e.x >= leftEdge();
                keyZooming = !panning;
                panStart = scroll;
                panPitch = topPitch;
                panZoom = verticalZoom;
                panAnchor = pitch(e.y);
                return;
            }
            if (e.x < leftEdge() && e.y >= gridTop() && e.y < noteBottom() && e.mods.isLeftButtonDown())
            {
                if (e.mods.isCtrlDown())
                {
                    pitchSelecting = true;
                    pitchSelectionAdd = e.mods.isShiftDown();
                    pitchAnchor = pitchFirst = pitchLast = pitch(e.y);
                    repaint();
                    return;
                }
                previewDragging = true;
                s.previewNote(pitch(e.y));
                repaint();
                return;
            }
            if (e.y >= getHeight() - navigationHeight() || e.x >= getWidth() - rightInset())
                return;
            if (e.mods.isRightButtonDown() && !editableGhosts && ghostAt(e.position))
                return;
        }
        if (type == 3)
        {
            if (e.y >= getHeight() - scrollBarThickness)
                return;
            if (e.x >= mixerStripWidth())
            {
                if (effectWet)
                {
                    if (auto result = finishEffectWet(false); result.failed())
                    {
                        s.status = result.getErrorMessage();
                        return;
                    }
                }
                effectOrder.reset();
                effectOrderMoved = false;
                const auto layout = mixerEffectLayout();
                const auto hit = layout.hit(e.position.toInt(), effectOffset, mixerEffectCount());
                if (hit.action == MixerEffectSlots::Action::chainBypass && e.mods.isLeftButtonDown())
                {
                    juce::String code;
                    if (auto result = s.admitMixerChainControl(code); result.failed())
                        s.status = result.getErrorMessage();
                    else if (const auto *insert = mixerInsert(s.controller.project, s.selectedInsert))
                    {
                        CommandArgs args;
                        args.mixer = insert->index;
                        args.enabled = !insert->effectsBypassed;
                        s.command("mixer.effects.bypass", args);
                    }
                }
                else if (hit.action == MixerEffectSlots::Action::add && e.mods.isLeftButtonDown())
                {
                    juce::String code;
                    s.browseMixerEffects(s.selectedInsert, code);
                }
                else if (const auto *insert = mixerInsert(s.controller.project, s.selectedInsert);
                         insert && hit.slot >= 0 && size_t(hit.slot) < insert->effects.size())
                {
                    // Resolve the stable ID from current model order at dispatch,
                    // never retain a slot number across asynchronous operations.
                    const auto &fx = insert->effects[size_t(hit.slot)];
                    if (e.mods.isRightButtonDown() ||
                        (e.mods.isLeftButtonDown() && hit.action == MixerEffectSlots::Action::menu))
                        mixerEffectMenu(fx.id);
                    else if (e.mods.isLeftButtonDown() && hit.action == MixerEffectSlots::Action::wet)
                    {
                        if (e.getNumberOfClicks() == 1)
                            if (auto result = beginEffectWet(fx.id, e.position); result.failed())
                                s.status = result.getErrorMessage();
                    }
                    else if (e.mods.isLeftButtonDown() && hit.action == MixerEffectSlots::Action::reorder)
                    {
                        if (auto result = beginEffectOrder(fx.id); result.failed())
                            s.status = result.getErrorMessage();
                        else
                        {
                            effectOrderPoint = e.position.toInt();
                            effectOrder->destination = hit.slot;
                        }
                    }
                    else if (e.mods.isLeftButtonDown() && hit.action == MixerEffectSlots::Action::open)
                        s.openPluginEditorUI(fx.id);
                    else if (e.mods.isLeftButtonDown() && hit.action == MixerEffectSlots::Action::bypass)
                    {
                        CommandArgs args;
                        args.target = fx.id;
                        args.enabled = !fx.bypass;
                        s.command("plugin.bypass", args);
                    }
                }
                else if (e.mods.isRightButtonDown() && layout.pane.contains(e.position.toInt()))
                    s.effectMenu(s.selectedInsert);
                repaint();
                return;
            }
            int width = mixerWidth();
            auto position = juce::jlimit(0, int(s.controller.project.mixer.size()) - 1,
                                         (e.x - 6 + int(mixerScroll)) / width);
            const int destination = s.controller.project.mixer[size_t(position)].index;
            if (e.y >= getHeight() - 44)
            {
                const int source = s.selectedInsert;
                const auto *route = mixerRoute(s.controller.project, source, destination);
                if (e.mods.isRightButtonDown())
                {
                    if (route && mixerSendLevelHit(destination, e.position))
                    {
                        s.mixerAutomationMenu(source, MixerAutomationControl::send, destination);
                        return;
                    }
                    s.mixerRouteMenu(source, destination);
                    return;
                }
                if (mixerSendLevelHit(destination, e.position) && route)
                {
                    if (s.controller.hasGesture() || !s.controller.beginGesture())
                        return;
                    mixerSendSource = source;
                    mixerSendTarget = destination;
                    mixerSendStart = route->gain;
                    origin = e.position;
                    dragging = true;
                    return;
                }
                if (mixerRouteButtonBounds(destination).contains(e.position.toInt()) && source != destination)
                {
                    CommandArgs a;
                    a.mixer = source;
                    a.target = destination;
                    a.enabled = !route;
                    s.command("mixer.route", a);
                }
                return;
            }
            const bool nameTarget = mixerControlHit(mixerNameBounds(destination), e.position);
            if (e.mods.isCtrlDown() && e.mods.isRightButtonDown() &&
                mixerControlHit(mixerMuteBounds(destination), e.position))
            {
                s.command("mixer.unmuteAll", {});
                return;
            }
            if (nameTarget && !e.mods.isRightButtonDown() && !e.mods.isMiddleButtonDown() &&
                (e.mods.isCtrlDown() || e.mods.isShiftDown()))
            {
                auto result = s.selectMixerMany({destination}, e.mods.isShiftDown() ? "range" : "toggle");
                if (result.failed())
                    s.status = result.getErrorMessage();
                return;
            }
            const bool retainSelection = nameTarget && s.mixerSelection.contains(destination) &&
                                         (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown());
            if (!retainSelection)
                if (auto result = s.selectMixerInsert(destination); result.failed())
                {
                    s.status = result.getErrorMessage();
                    return;
                }
            if (nameTarget)
            {
                if (e.mods.isMiddleButtonDown())
                    s.mixerNameDialog(destination);
                else if (e.mods.isRightButtonDown())
                    s.mixerControlMenu(destination);
                return;
            }
            if (mixerControlHit(mixerMuteBounds(destination), e.position))
            {
                if (e.mods.isRightButtonDown())
                {
                    s.mixerControlMenu(destination);
                    return;
                }
                CommandArgs a;
                a.mixer = destination;
                const bool solo = e.mods.isCtrlDown() || e.mods.isAltDown();
                a.enabled = solo ? !(s.controller.project.mixerSolo &&
                                     s.controller.project.mixerSolo->insert == destination &&
                                     s.controller.project.mixerSolo->connected == e.mods.isAltDown())
                                 : !mixerInsert(s.controller.project, destination)->muted;
                s.command(solo ? (e.mods.isAltDown() ? "mixer.soloConnected" : "mixer.solo") : "mixer.mute",
                          a);
                return;
            }
            if (mixerControlHit(mixerMeterBounds(destination), e.position))
            {
                if (e.mods.isRightButtonDown())
                    s.mixerControlMenu(destination);
                else if (e.mods.isLeftButtonDown())
                {
                    std::uint64_t fence = 0;
                    s.resetMixerOverload(destination, fence);
                }
                return;
            }
            if (mixerControlHit(mixerStereoSeparationBounds(destination), e.position))
            {
                if (e.mods.isRightButtonDown())
                    s.mixerAutomationMenu(destination, MixerAutomationControl::stereoSeparation);
                else if (e.mods.isLeftButtonDown())
                    if (auto result = beginMixerStereo(destination, e.position); result.failed())
                        s.status = result.getErrorMessage();
                return;
            }
            if (mixerControlHit(mixerPhaseInvertBounds(destination), e.position) ||
                mixerControlHit(mixerStereoSwapBounds(destination), e.position))
            {
                if (e.mods.isRightButtonDown())
                    s.mixerControlMenu(destination);
                else if (e.mods.isLeftButtonDown())
                {
                    juce::String code;
                    if (auto result = s.admitMixerChainControl(code); result.failed())
                    {
                        s.status = result.getErrorMessage();
                        return;
                    }
                    const bool phase = mixerControlHit(mixerPhaseInvertBounds(destination), e.position);
                    const auto *insert = mixerInsert(s.controller.project, destination);
                    CommandArgs args;
                    args.mixer = destination;
                    args.enabled = phase ? !insert->phaseInverted : !insert->stereoSwapped;
                    s.command(phase ? "mixer.phaseInvert" : "mixer.stereoSwap", args);
                }
                return;
            }
            if (mixerControlHit(mixerPanBounds(destination), e.position))
            {
                if (e.mods.isRightButtonDown())
                {
                    s.mixerAutomationMenu(destination, MixerAutomationControl::pan);
                    return;
                }
                if (e.mods.isLeftButtonDown() && s.controller.beginGesture())
                {
                    mixerPanSource = destination;
                    mixerPanStart = mixerInsert(s.controller.project, destination)->pan;
                    origin = e.position;
                    dragging = true;
                }
                return;
            }
            if (e.mods.isRightButtonDown())
            {
                if (e.y >= 100)
                {
                    s.mixerAutomationMenu(destination, MixerAutomationControl::gain);
                    return;
                }
                s.effectMenu(s.selectedInsert);
                return;
            }
            if (e.y < 100 || e.y > 100 + mixerFaderTravel() + 6 || getHeight() < 180)
            {
                repaint();
                return;
            }
            s.controller.beginGesture();
            dragging = true;
            mouseDrag(e);
            return;
        }
        if (type == 2 && e.y < gridTop() && e.y >= gridTop() - 11 && e.x >= leftEdge())
            for (const auto &change : s.controller.project.tempoChanges)
            {
                const float x = float(leftEdge() + (change.beat - scroll) * spacing());
                if (e.x < x - 4 || e.x > x + 50)
                    continue;
                const auto id = change.id;
                const double beat = change.beat;
                if (e.mods.isRightButtonDown())
                {
                    juce::PopupMenu menu;
                    menu.addItem(1, "Edit tempo change...");
                    menu.addItem(2, "Delete tempo change");
                    menu.showMenuAsync({}, [safe, id, beat, bpm = change.bpm](int chosen)
                    {
                        if (!safe || chosen <= 0)
                            return;
                        CommandArgs a;
                        a.target = id;
                        if (chosen == 2)
                            return safe->s.command("tempo.change.delete", a);
                        tempoDialog("Edit tempo change", bpm, [safe, id, beat](double value, bool ramp)
                        {
                            if (!safe)
                                return;
                            CommandArgs edit;
                            edit.target = id;
                            edit.beat = beat;
                            edit.value = value;
                            edit.step = ramp;
                            safe->s.command("tempo.change.set", edit);
                        });
                    });
                }
                else
                    s.audio.seekBeat(beat);
                repaint();
                return;
            }
        if (type == 2 && e.y < gridTop() && e.y >= gridTop() - 22 && e.x >= leftEdge())
            for (const auto &marker : s.controller.project.markers)
            {
                const float x = float(leftEdge() + (marker.beat - scroll) * spacing());
                if (std::abs(e.x - x) > 7 && !(e.x >= x && e.x <= x + 60 && e.y < gridTop() - 10))
                    continue;
                const auto id = marker.id;
                if (e.mods.isRightButtonDown())
                {
                    juce::PopupMenu menu;
                    menu.addItem(1, "Rename marker...");
                    menu.addItem(2, "Move marker to playhead");
                    menu.addItem(3, "Delete marker");
                    menu.showMenuAsync({}, [safe, id, name = marker.name](int chosen)
                    {
                        if (!safe || chosen <= 0)
                            return;
                        CommandArgs a;
                        a.target = id;
                        if (chosen == 2)
                        {
                            a.beat = std::max(0., safe->s.audio.positionBeat());
                            safe->s.command("marker.move", a);
                        }
                        else if (chosen == 3)
                            safe->s.command("marker.delete", a);
                        else
                        {
                            auto *box = new juce::AlertWindow("Rename marker", {}, juce::MessageBoxIconType::NoIcon);
                            box->addTextEditor("name", name);
                            box->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
                            box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                            box->enterModalState(true, juce::ModalCallbackFunction::create(
                                [safe, id, box](int ok)
                                {
                                    if (!safe || ok != 1)
                                        return;
                                    CommandArgs rename;
                                    rename.target = id;
                                    rename.text = box->getTextEditorContents("name").substring(0, 128);
                                    safe->s.command("marker.rename", rename);
                                }), true);
                        }
                    });
                }
                else
                    s.audio.seekBeat(marker.beat);
                repaint();
                return;
            }
        if (type == 2 && e.y < gridTop() && e.y >= gridTop() - 22 && e.x >= leftEdge() &&
            e.mods.isLeftButtonDown() && (e.mods.isCtrlDown() || e.getNumberOfClicks() >= 2))
        {
            timeSelecting = true;
            timeAnchor = timeStart = timeEnd = timelineBeat(float(e.x));
            s.playlistRange.reset();
            repaint();
            return;
        }
        if (e.y < gridTop())
        {
            if (type == 1 && e.y < 28 && e.x < 78)
            {
                juce::PopupMenu m;
                m.addItem(1, "None");
                m.addItem(2, "1/4 beat");
                m.addItem(3, "1/2 beat");
                m.addItem(4, "1 beat");
                m.showMenuAsync({},
                                [safe](int n)
                                {
                                    if (safe && n)
                                    {
                                        safe->s.snap = n == 1 ? 0 : n == 2 ? .25 : n == 3 ? .5 : 1.;
                                        safe->s.refresh();
                                    }
                                });
            }
            else
            {
                if (auto result = s.finishMidiTake(); result.failed())
                {
                    s.status = result.getErrorMessage();
                    return;
                }
                s.audio.seekBeat(beatAt(e.x));
                if (type == 2 && s.playlistRange &&
                    (beatAt(e.x) < s.playlistRange->first || beatAt(e.x) > s.playlistRange->second))
                {
                    s.playlistRange.reset(); // A ruler click outside the time selection clears it.
                    repaint();
                }
            }
            return;
        }
        if (type == 2 && e.x < leftEdge() && e.x >= pickerWidth() && e.y >= gridTop())
        {
            // The LED at the start of the header mutes; right-click opens the track menu.
            if (e.mods.isPopupMenu() || e.x < pickerWidth() + 26)
                trackMenu(laneAtY(e.y), e.mods);
            return;
        }
        if (e.x < leftEdge())
            return;
        if (type == 1 && noteSliceTool && e.y < noteBottom() && e.mods.isLeftButtonDown() &&
            !e.mods.isCtrlDown())
        {
            const auto result = s.beginNoteSlice(false, 120000);
            if (result.failed())
            {
                s.status = result.getErrorMessage();
                return;
            }
            sliceEnd = origin;
            sliceAlt = e.mods.isAltDown();
            const auto line = pianoSliceLine(origin, origin, sliceAlt);
            if (auto r = s.updateNoteSlice(line); r.failed())
            {
                s.finishNoteSlice(false, r.getErrorMessage());
                s.status = r.getErrorMessage();
                return;
            }
            if (e.mods.isShiftDown())
            {
                const auto r = s.finishNoteSlice(true);
                if (r.failed())
                    s.status = r.getErrorMessage();
            }
            else
                slicing = true;
            repaint();
            return;
        }
        if (type == 1 && noteMuteTool && e.y < noteBottom() && e.mods.isLeftButtonDown() &&
            !e.mods.isCtrlDown())
        {
            muteEnabled = true;
            if (const auto id = hit(e.position))
                if (auto *source = pattern())
                    for (const auto &note : source->notes)
                        if (note.id == id)
                            muteEnabled = !note.muted;
            muteTouched.clear();
            lastMutePoint = e.position;
            if (s.controller.beginGesture())
            {
                muting = true;
                muteAt(e.position);
            }
            return;
        }
        if (type == 2 && automationMouseDown(e))
            return;
        auto &list = type == 1 ? s.selectedNotes : s.selectedClips;
        if (e.mods.isCtrlDown())
        {
            selecting = true;
            rectangle = {e.position.x, e.position.y, 0, 0};
            if (!e.mods.isShiftDown())
                list.clear();
            return;
        }
        Id id = hit(e.position);
        const auto pressedOccurrence =
            type == 1 ? pianoHit(e.position) : std::optional<PianoNoteOccurrence>{};
        bool menuHit = false;
        if (type == 2 && id)
            for (auto &c : playlistClips())
                if (c.id == id)
                    menuHit = e.x < cr(c).getX() + 14 && e.y < cr(c).getY() + 14;
        if (e.mods.isRightButtonDown() || menuHit)
        {
            if (type == 2 && id && !e.mods.isRightButtonDown())
            {
                list = {id};
                juce::PopupMenu m;
                m.addItem(1, "Make Unique");
                m.addItem(2, "Delete");
                m.addItem(3, automationClip(id) ? "Automation source / targets"
                             : audioClip(id)    ? "Channel settings"
                                                : "Edit Pattern");
                if (automationClip(id))
                    m.addItem(50, "Use as tempo curve...");
                if (audioClip(id))
                {
                    // Comping: offered for audio takes grouped with other audio takes.
                    const auto &project = s.controller.project;
                    const bool take = std::any_of(
                        project.clipGroups.begin(), project.clipGroups.end(),
                        [&](const auto &group)
                        {
                            return std::find(group.members.begin(), group.members.end(), id) != group.members.end() &&
                                   std::any_of(group.members.begin(), group.members.end(),
                                               [&](Id member) { return member != id && audioClip(member); });
                        });
                    m.addSeparator();
                    m.addItem(4, "Use this take here (comp)", take);
                    m.addItem(5, "Use this take for the time selection (swipe comp)", take && s.playlistRange.has_value());
                    m.addItem(6, "Flatten comp (remove unused takes)", take);
                    juce::PopupMenu fades;
                    fades.addItem(10, "No fades");
                    fades.addItem(11, "10 ms in/out");
                    fades.addItem(12, "50 ms in/out");
                    fades.addItem(13, "250 ms in/out");
                    fades.addItem(14, "1 s fade in");
                    fades.addItem(15, "1 s fade out");
                    m.addSubMenu("Fades", fades);
                    juce::PopupMenu stretch;
                    stretch.addItem(20, "x0.5 (twice as fast)");
                    stretch.addItem(21, "x0.75");
                    stretch.addItem(22, "x1.25");
                    stretch.addItem(23, "x1.5");
                    stretch.addItem(24, "x2 (half speed)");
                    stretch.addSeparator();
                    stretch.addItem(25, "Fit to 1 bar");
                    stretch.addItem(26, "Fit to 2 bars");
                    stretch.addItem(27, "Fit to 4 bars");
                    stretch.addSeparator();
                    stretch.addItem(28, "Warp to Song tempo (played at the base tempo)");
                    stretch.addItem(29, "Warp to Song tempo (played at the clip start tempo)");
                    m.addSubMenu("Stretch (keep pitch)", stretch);
                    juce::PopupMenu editAudio;
                    editAudio.addItem(30, "Reverse");
                    editAudio.addItem(31, "Normalize to -0.3 dBFS");
                    editAudio.addItem(32, "Gain +3 dB");
                    editAudio.addItem(33, "Gain -3 dB");
                    editAudio.addItem(34, "Consolidate (bake window and fades)");
                    m.addItem(40, "Align first transient to the grid (1/16)");
                    juce::PopupMenu tempo;
                    tempo.addItem(41, "Detect tempo");
                    tempo.addItem(42, "Fit to tempo (detect, keep pitch)");
                    tempo.addItem(43, "Set source tempo and fit...");
                    m.addSubMenu("Tempo", tempo);
                    m.addSubMenu("Edit audio", editAudio);
                }
                m.showMenuAsync({},
                                [safe, id](int n)
                                {
                                    if (!safe)
                                        return;
                                    CommandArgs a;
                                    a.target = id;
                                    a.ids = {id};
                                    if (n == 1)
                                        safe->s.command("clip.unique", a);
                                    if (n == 2)
                                        safe->s.command("clip.delete", a);
                                    if (n == 4)
                                        safe->s.command("audio.comp", a);
                                    if (n == 6)
                                        safe->s.command("audio.flattenComp", a);
                                    if (n == 5 && safe->s.playlistRange)
                                    {
                                        a.compRange = std::array<double, 2>{safe->s.playlistRange->first,
                                                                            safe->s.playlistRange->second};
                                        safe->s.command("audio.comp", a);
                                    }
                                    if (n == 40)
                                        if (auto r = safe->s.alignClipToGrid(id); r.failed())
                                            safe->s.status = r.getErrorMessage();
                                    if (n >= 41 && n <= 43)
                                    {
                                        const auto *clip = safe->audioClip(id);
                                        const auto *source = clip ? safe->s.controller.project.channel(clip->channel) : nullptr;
                                        if (!source)
                                            return;
                                        juce::String how;
                                        const double bpm = safe->s.detectFileTempo(juce::File(source->source), how);
                                        if (n == 41)
                                            safe->s.status = bpm > 0 ? "Detected " + juce::String(bpm, 1) + " BPM (" + how + ")"
                                                                     : juce::String("Could not detect the tempo of this clip");
                                        else if (n == 42)
                                        {
                                            if (auto r = safe->s.fitAudioClipToTempo(id, bpm); r.failed())
                                                safe->s.status = r.getErrorMessage();
                                        }
                                        else
                                        {
                                            auto *box = new juce::AlertWindow("Source tempo", "The tempo this clip was played at. It is fitted to the Song tempo with its pitch kept.",
                                                                              juce::MessageBoxIconType::NoIcon);
                                            box->addTextEditor("bpm", bpm > 0 ? juce::String(bpm, 2) : juce::String("120"), "BPM");
                                            box->addButton("Fit", 1, juce::KeyPress(juce::KeyPress::returnKey));
                                            box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                                            box->enterModalState(true, juce::ModalCallbackFunction::create(
                                                [box, safe, id](int choice)
                                                {
                                                    const double value = box->getTextEditorContents("bpm").getDoubleValue();
                                                    if (choice == 1 && safe)
                                                        if (auto r = safe->s.fitAudioClipToTempo(id, value); r.failed())
                                                            safe->s.status = r.getErrorMessage();
                                                }), true);
                                        }
                                        safe->s.repaint();
                                        return;
                                    }
                                    if (n >= 30 && n <= 34)
                                    {
                                        const char *edits[] = {"reverse", "normalize", "gain", "gain", "consolidate"};
                                        const double values[] = {0, -.3, 3, -3, 0};
                                        if (auto r = safe->s.editAudioClip(id, edits[n - 30], values[n - 30]); r.failed())
                                            safe->s.status = r.getErrorMessage();
                                        return;
                                    }
                                    if (n == 28 || n == 29)
                                    {
                                        const auto *clip = safe->audioClip(id);
                                        if (!clip)
                                            return;
                                        const auto &project = safe->s.controller.project;
                                        const double bpm = n == 28 ? project.tempo : project.songTempo().bpmAt(clip->beat);
                                        if (auto r = safe->s.warpAudioClip(id, bpm); r.failed())
                                            safe->s.status = r.getErrorMessage();
                                        return;
                                    }
                                    if (n >= 20 && n <= 27)
                                    {
                                        const auto *clip = safe->audioClip(id);
                                        if (!clip)
                                            return;
                                        constexpr double ratios[] = {.5, .75, 1.25, 1.5, 2.};
                                        double ratio = n < 25 ? ratios[n - 20] : 1.;
                                        if (n >= 25)
                                        {
                                            const auto &project = safe->s.controller.project;
                                            const double beats = (n == 25 ? 1 : n == 26 ? 2 : 4) *
                                                                 project.numerator * 4. / project.denominator;
                                            ratio = (project.songSeconds(clip->beat + beats) -
                                                     project.songSeconds(clip->beat)) /
                                                    clip->duration;
                                        }
                                        if (auto r = safe->s.stretchAudioClip(id, ratio); r.failed())
                                            safe->s.status = r.getErrorMessage();
                                        return;
                                    }
                                    if (n >= 10 && n <= 15)
                                    {
                                        constexpr double both[] = {0., .01, .05, .25};
                                        if (n <= 13)
                                            a.fadeIn = a.fadeOut = both[n - 10];
                                        else if (n == 14)
                                            a.fadeIn = 1.;
                                        else
                                            a.fadeOut = 1.;
                                        safe->s.command("audio.fade", a);
                                    }
                                    if (n == 50)
                                    {
                                        auto *box = new juce::AlertWindow(
                                            "Use as tempo curve",
                                            "The drawn curve becomes the Song tempo over this clip: bottom = low BPM, top = high BPM.",
                                            juce::MessageBoxIconType::NoIcon);
                                        box->addTextEditor("low", "80", "Low BPM (bottom)");
                                        box->addTextEditor("high", "160", "High BPM (top)");
                                        box->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
                                        box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                                        box->enterModalState(true, juce::ModalCallbackFunction::create(
                                            [box, safe, id](int choice)
                                            {
                                                if (choice == 0 || !safe)
                                                    return;
                                                CommandArgs args;
                                                args.target = id;
                                                args.offset = box->getTextEditorContents("low").getDoubleValue();
                                                args.value = box->getTextEditorContents("high").getDoubleValue();
                                                safe->s.command("tempo.fromAutomation", args);
                                            }), true);
                                        return;
                                    }
                                    if (n == 3)
                                    {
                                        if (const auto *clip = safe->automationClip(id))
                                        {
                                            safe->automationPicker = clip->source;
                                            safe->automationSourceEditor(id);
                                            return;
                                        }
                                        if (const auto *audio = safe->audioClip(id))
                                        {
                                            safe->s.openInstrument(audio->channel);
                                            return;
                                        }
                                        for (auto &c : safe->playlistClips())
                                            if (c.id == id)
                                                safe->s.selectedPattern = c.pattern;
                                        safe->s.showEditor(1);
                                        safe->s.refresh();
                                    }
                                });
                return;
            }
            s.controller.beginGesture();
            erasing = true;
            erase(e.position);
            return;
        }
        if (type == 1 && velocityHeight() > 0 && e.y >= noteBottom() && e.mods.isAltDown())
        {
            // Alt+click a controller point: set its value at that height; Alt+right-click deletes it.
            if (auto *p = pattern())
            {
                const ControllerEvent *nearest = nullptr;
                float best = 8.f;
                for (const auto &c : p->controllers)
                    if (c.channel == s.selectedChannel)
                        if (const float dx = std::abs(float(leftEdge() + (c.beat - scroll) * spacing()) - e.position.x);
                            dx < best)
                        {
                            best = dx;
                            nearest = &c;
                        }
                if (nearest)
                {
                    CommandArgs a;
                    a.pattern = p->id;
                    a.target = nearest->id;
                    const double level = juce::jlimit(0., 1., (velocityBase() - e.y) / double(velocityScale()));
                    a.value = nearest->type == 1 ? level * 2 - 1 : level;
                    s.command(e.mods.isPopupMenu() ? "notes.controller.delete" : "notes.controller.set", a);
                }
                else
                    s.status = "Alt+click a controller point (CC, bend, pressure, program) to edit it";
            }
            return;
        }
        if (type == 1 && velocityHeight() > 0 && e.y >= noteBottom())
        {
            if (auto *p = pattern())
            {
                std::optional<Note> nearest;
                double d = .3;
                // Reverse visits raw originals first, preserving source precedence.
                const auto &view = pianoView();
                for (auto i = view.rbegin(); i != view.rend(); ++i)
                    if (const auto &n = i->note; canEdit(n) && std::abs(n.beat - beatAt(e.x)) < d)
                    {
                        nearest = n;
                        d = std::abs(n.beat - beatAt(e.x));
                    }
                if (nearest)
                {
                    CommandArgs a;
                    a.pattern = p->id;
                    a.ids = selected(list, nearest->id) ? list : std::vector<Id>{nearest->id};
                    a.value = juce::jlimit(.01, 1., (velocityBase() - e.y) / double(velocityScale()));
                    s.command("notes.velocity", a);
                }
            }
            return;
        }
        if (type == 1 && e.mods.isLeftButtonDown() && !noteSliceTool && !noteMuteTool)
        {
            bool edge = false;
            if (pianoRightShiftDown() && id && selected(list, id) && pressedOccurrence &&
                !pressedOccurrence->clippedEnd)
            {
                const auto bounds = nr(pressedOccurrence->note);
                edge = e.x >= bounds.getRight() - (bounds.getWidth() > 10 ? 6 : 2);
            }
            if (stretchHandleBounds().contains(e.getPosition()) || edge)
            {
                std::vector<Id> targets;
                for (const auto &n : pattern()->notes)
                    if (canEdit(n) && selected(list, n.id))
                        targets.push_back(n.id);
                auto r = s.beginNoteStretch(targets, false, 120000);
                if (r.failed())
                {
                    s.status = r.getErrorMessage();
                    return;
                }
                stretching = true;
                stretchCoarse = edge;
                origin = e.position;
                auto end = s.noteStretchSession->plan.end;
                if (edge)
                    for (const auto &n : s.noteStretchSession->plan.notes)
                        if (n.id == id)
                            end = n.beat + n.length;
                stretchReferenceSpan = end - s.noteStretchSession->args.beat;
                stretchStartMouseBeat = beatAt(e.position.x);
                s.status = "Stretch selection | Alt: " + juce::String(edge ? "25% ratios" : "no snap") +
                           " | Escape: cancel";
                repaint();
                return;
            }
        }
        s.controller.beginGesture();
        dragging = true;
        resizing = false;
        trimmingAudio = 0;
        trimmingAutomation = 0;
        if (id)
        {
            if (!selected(type == 2 ? s.effectiveClipSelection() : list, id))
                list = {id};
            if (e.mods.isShiftDown())
            {
                CommandArgs a;
                a.pattern = s.selectedPattern;
                a.ids = type == 2 ? s.effectiveClipSelection() : list;
                const auto previousNextId = s.controller.project.nextId;
                s.command(type == 1 ? "notes.duplicate" : "clip.duplicate", a);
                list.clear();
                if (type == 1)
                {
                    auto *p = pattern();
                    for (auto i = p->notes.rbegin(); i != p->notes.rend() && list.size() < a.ids.size(); ++i)
                        list.push_back(i->id);
                }
                else
                {
                    const auto clips = playlistClips();
                    for (auto &clip : clips)
                        if (clip.id >= previousNextId)
                            list.push_back(clip.id);
                    std::sort(list.begin(), list.end(), std::greater<Id>());
                }
            }
            if (type == 1)
            {
                if (auto *p = pattern())
                    for (auto &n : p->notes)
                        if (n.id == id)
                        {
                            const auto bounds = pressedOccurrence ? nr(pressedOccurrence->note) : nr(n);
                            resizing = (!pressedOccurrence || !pressedOccurrence->clippedEnd) &&
                                       e.x >= bounds.getRight() - (bounds.getWidth() > 10 ? 6 : 2);
                            noteLength = n.length;
                        }
            }
            else
                for (auto &c : playlistClips())
                    if (c.id == id)
                    {
                        resizing = e.x >= cr(c).getRight() - 7;
                        if (!e.mods.isShiftDown() && e.x <= cr(c).getX() + 7)
                        {
                            if (const auto *clip = automationClip(id))
                            {
                                trimmingAutomation = id;
                                automationTrimBeat = clip->beat;
                                automationTrimOffset = clip->offset;
                                automationTrimLength = clip->length;
                                automationGestureProject = s.controller.projectIdentity;
                                list = {id};
                            }
                            if (auto *audio = audioClip(id))
                            {
                                trimmingAudio = id;
                                trimStartBeat = audio->beat;
                                trimStartOffset = audio->offset;
                                trimStartDuration = audio->duration;
                                list = {id};
                            }
                        }
                    }
        }
        else
        {
            CommandArgs a;
            a.pattern = s.selectedPattern;
            a.channel = s.selectedChannel;
            a.beat = snap(beatAt(e.x), e.mods);
            if (type == 1)
            {
                a.pitch = pitch(e.y);
                a.length = noteLength;
                a.value = .8;
                fillNoteInsertion(a);
                const auto previousId = s.controller.project.nextId;
                s.command("notes.add", a);
                if (s.controller.project.nextId != previousId)
                    if (auto *p = pattern(); p && !p->notes.empty())
                        list = {p->notes.back().id};
            }
            else
            {
                a.lane = laneAtY(e.y);
                auto sources = pickerSelection();
                sources.lane = a.lane;
                sources.beat = a.beat;
                placeSources(std::move(sources));
            }
        }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (mixerStereo)
        {
            if (!validMixerStereo())
            {
                const auto result = finishMixerStereo(false);
                s.status = result.failed() ? result.getErrorMessage() : "Stereo context changed; cancelled";
                return;
            }
            const auto insert = mixerStereo->insert;
            const auto owner = mixerStereo->owner;
            const auto current = mixerInsert(s.controller.project, insert)->stereoSeparation;
            const auto delta =
                (mixerStereo->lastPoint.y - e.position.y) / 100.f * (e.mods.isShiftDown() ? .1f : 1.f);
            mixerStereo->lastPoint = e.position;
            if (delta != 0)
            {
                const auto result =
                    setMixerStereo(insert, juce::jlimit(-1., 1., double(current) + delta), owner);
                if (mixerStereo && mixerStereo->owner == owner && ownsMixerStereo())
                    mixerStereo->revision = s.controller.revision;
                if (result.failed())
                    s.status = result.getErrorMessage();
            }
            repaint();
            return;
        }
        if (effectWet)
        {
            if (!validEffectWet())
            {
                const auto result = finishEffectWet(false);
                s.status =
                    result.failed() ? result.getErrorMessage() : "Effect mix context changed; cancelled";
                return;
            }
            const auto plugin = effectWet->plugin;
            const auto owner = effectWet->owner;
            const auto current = s.controller.project.plugin(plugin)->wet;
            const auto delta =
                (effectWet->lastPoint.y - e.position.y) / 120.f * (e.mods.isShiftDown() ? .1f : 1.f);
            effectWet->lastPoint = e.position;
            if (delta != 0)
            {
                const auto result =
                    setEffectWet(plugin, juce::jlimit(0., 1., double(current) + delta), owner);
                if (effectWet && effectWet->owner == owner && ownsEffectWet())
                    effectWet->revision = s.controller.revision;
                if (result.failed())
                    s.status = result.getErrorMessage();
            }
            repaint();
            return;
        }
        if (effectOrder)
        {
            if (auto result = effectOrder->validate(s.controller.project, s.controller.projectIdentity,
                                                    s.controller.revision, s.selectedInsert);
                result.failed())
            {
                s.status = result.getErrorMessage();
                effectOrder.reset();
                effectOrderMoved = false;
                repaint();
                return;
            }
            effectOrderMoved = effectOrderMoved || e.getDistanceFromDragStart() >= 4;
            effectOrderPoint = e.position.toInt();
            effectOrder->destination = effectOrderDestination(effectOrderPoint);
            repaint();
            return;
        }
        if (stretching)
        {
            if (!s.noteStretchPending())
            {
                stretching = false;
                return;
            }
            const auto &session = *s.noteStretchSession;
            const auto anchor = session.args.beat;
            const auto end = anchor + stretchReferenceSpan + beatAt(e.position.x) - stretchStartMouseBeat;
            auto ratio = (end - anchor) / stretchReferenceSpan;
            if (stretchCoarse && e.mods.isAltDown())
                ratio = std::round(ratio * 4) / 4;
            else if (!e.mods.isAltDown())
                ratio = (snap(end, e.mods) - anchor) / stretchReferenceSpan;
            ratio = juce::jlimit(session.plan.minimumRatio, session.plan.maximumRatio, ratio);
            if (auto r = s.updateNoteStretch(ratio); r.failed())
                s.status = r.getErrorMessage();
            repaint();
            return;
        }

        if (type == 0 && rackSelecting)
        {
            const int row = (int(e.y) - 44) / rackRowHeight() + rackOffset;
            if (e.y >= 44 && row < rackOffset + rackRows() && s.rackChannelAt(row))
            {
                std::vector<Id> ids;
                for (int i = std::min(row, rackSelectionLastRow); i <= std::max(row, rackSelectionLastRow);
                     ++i)
                    if (auto *channel = s.rackChannelAt(i);
                        channel && !selected(rackSelectionVisited, channel->id))
                    {
                        rackSelectionVisited.push_back(channel->id);
                        ids.push_back(channel->id);
                    }
                rackSelectionLastRow = row;
                if (!ids.empty())
                    s.selectRackChannels(ids, rackSelectAdd ? "add" : "remove");
            }
            return;
        }
        if (automationDragPoint)
        {
            if (automationGestureProject != s.controller.projectIdentity)
            {
                // Project replacement already cancels the old canonical gesture. Drop stale UI IDs.
                automationDragPoint = automationDragClip = 0;
                repaint();
                return;
            }
            const auto *clip = automationClip(automationDragClip);
            if (!clip)
                return;
            const auto area = automationArea(automationDragClip);
            if (area.getWidth() <= 0 || area.getHeight() <= 0)
                return;
            CommandArgs a;
            a.target = automationDragPoint;
            a.beat =
                e.mods.isCtrlDown()
                    ? automationDragBeat
                    : snap(automationDragBeat + (e.x - origin.x) / area.getWidth() * clip->length, e.mods);
            a.value =
                e.mods.isShiftDown()
                    ? automationDragValue
                    : juce::jlimit(0., 1., double(automationDragValue - (e.y - origin.y) / area.getHeight()));
            s.command("automation.point.move", a);
            return;
        }
        if (muting)
        {
            muteAt(e.position);
            return;
        }
        if (slicing && type == 1)
        {
            if (!s.noteSlicePending())
            {
                slicing = false;
                repaint();
                return;
            }
            sliceEnd = e.position;
            sliceAlt = e.mods.isAltDown();
            const auto r = s.updateNoteSlice(pianoSliceLine(origin, sliceEnd, sliceAlt));
            if (r.failed())
                s.status = r.getErrorMessage();
            repaint();
            return;
        }
        if (slicing)
        {
            sliceEnd = {e.mods.isShiftDown() ? e.position.x : origin.x, e.position.y};
            sliceAlt = e.mods.isAltDown();
            repaint();
            return;
        }
        if (type == 2 && slipTarget && dragging)
        {
            const auto before = clipOffset(slipTarget);
            const auto delta = -snap((e.x - origin.x) / spacing(), e.mods);
            CommandArgs a;
            a.ids = s.selectedClips;
            a.beat = delta - lastBeat;
            s.command("clip.slip", a);
            lastBeat += clipOffset(slipTarget) - before;
            return;
        }

        if (pitchSelecting)
        {
            auto note = pitch(juce::jlimit(gridTop(), noteBottom() - 1, e.y));
            pitchFirst = std::min(pitchAnchor, note);
            pitchLast = std::max(pitchAnchor, note);
            repaint();
            return;
        }
        if (timeSelecting)
        {
            auto beat =
                timelineBeat(juce::jlimit(float(leftEdge()), float(getWidth() - rightInset()), e.position.x));
            timeStart = std::min(timeAnchor, beat);
            timeEnd = std::max(timeAnchor, beat);
            if (type == 2)
            {
                if (timeEnd - timeStart > 1.e-8)
                    s.playlistRange = std::pair{timeStart, timeEnd};
                else
                    s.playlistRange.reset();
            }
            repaint();
            return;
        }
        if (regionZooming)
        {
            auto point = juce::Point<float>{
                juce::jlimit(float(leftEdge()), float(getWidth() - rightInset()), e.position.x),
                juce::jlimit(float(gridTop()), float(noteBottom()), e.position.y)};
            rectangle = juce::Rectangle<float>(origin, point);
            repaint();
            return;
        }
        if (type == 1 && previewDragging)
        {
            s.previewNote(e.x < leftEdge() && e.y >= gridTop() && e.y < noteBottom() ? pitch(e.y) : -1);
            repaint();
            return;
        }
        if (type == 1 && (panning || keyZooming))
        {
            if (panning)
            {
                scroll = std::max(0., panStart - (e.x - origin.x) / spacing());
                topPitch =
                    juce::jlimit(visiblePitches() - 1, 127, panPitch + int((e.y - origin.y) / height()));
            }
            else
            {
                verticalZoom = juce::jlimit(.1, 8., panZoom * std::pow(1.01, origin.y - e.y));
                topPitch = juce::jlimit(visiblePitches() - 1, 127,
                                        panAnchor + int((origin.y - gridTop()) / height()));
            }
            repaint();
            horizontalBar.repaint();
            verticalBar.repaint();
            return;
        }

        if (selecting)
        {
            rectangle = juce::Rectangle<float>(origin, e.position);
            repaint();
            return;
        }
        if (erasing)
        {
            eraseDrag(e.position);
            return;
        }
        if (!dragging)
            return;
        if (type == 0)
        {
            step(e);
            return;
        }
        if (type == 3)
        {
            CommandArgs a;
            if (mixerPanSource >= 0)
            {
                if (!s.controller.hasGesture())
                {
                    mixerPanSource = -1;
                    dragging = false;
                    return;
                }
                a.mixer = mixerPanSource;
                a.value = juce::jlimit(-1., 1.,
                                       double(mixerPanStart) + (e.position.x - origin.x) / 100. *
                                                                   (e.mods.isShiftDown() ? .1 : 1.));
                s.command("mixer.pan", a);
                return;
            }
            if (mixerSendSource >= 0)
            {
                if (!s.controller.hasGesture())
                {
                    mixerSendSource = -1;
                    dragging = false;
                    return;
                }
                a.mixer = mixerSendSource;
                a.target = mixerSendTarget;
                a.value = juce::jlimit(0., 2.,
                                       double(mixerSendStart) - (e.position.y - origin.y) / 100. *
                                                                    (e.mods.isShiftDown() ? .1 : 1.));
                s.command("mixer.send", a);
                return;
            }
            a.mixer = s.selectedInsert;
            a.value = mixerGainAtY(e.position.y);
            s.command("mixer.gain", a);
            return;
        }
        if (paintMode && !resizing)
        {
            CommandArgs a;
            a.pattern = s.selectedPattern;
            a.channel = s.selectedChannel;
            a.beat = snap(beatAt(e.x), e.mods);
            if (type == 1)
            {
                a.pitch = pitch(e.y);
                a.length = noteLength;
                a.value = .8;
                fillNoteInsertion(a);
                bool exists = false;
                if (auto *p = pattern())
                    for (auto &n : p->notes)
                        if (n.channel == a.channel && n.pitch == a.pitch && n.slide == a.slide &&
                            n.portamento == a.portamento && n.colourGroup == a.colourGroup &&
                            std::abs(n.beat - a.beat) < .0001)
                            exists = true;
                if (!exists)
                    s.command("notes.add", a);
            }
            else
            {
                auto sources = pickerSelection();
                const auto first = sources.ids.front();
                double length = 0;
                if (sources.text == "audio")
                {
                    if (const auto *c = s.controller.project.channel(first))
                        length = c->audioDuration * s.controller.project.tempo / 60.;
                }
                else if (sources.text == "automation")
                {
                    if (const auto *source = s.controller.project.automationSource(first))
                        length = source->length;
                }
                else if (const auto *p = s.controller.project.pattern(first))
                    length = p->length;
                if (length <= 0)
                    return;
                sources.lane = laneAtY(e.y);
                if (size_t(sources.lane) + sources.ids.size() > 128)
                    return;
                sources.beat = std::floor(beatAt(e.x) / length) * length;
                for (size_t i = 0; i < sources.ids.size(); ++i)
                {
                    bool exists = false;
                    for (const auto &clip : playlistClips())
                        if (clip.lane == sources.lane + int(i) && std::abs(clip.beat - sources.beat) < .0001)
                        {
                            if (sources.text == "pattern")
                                exists = exists || clip.pattern == sources.ids[i];
                            else if (sources.text == "audio")
                            {
                                const auto *audio = audioClip(clip.id);
                                exists = exists || (audio && audio->channel == sources.ids[i]);
                            }
                            else
                            {
                                const auto *curve = automationClip(clip.id);
                                exists = exists || (curve && curve->source == sources.ids[i]);
                            }
                        }
                    if (!exists)
                    {
                        auto single = sources;
                        single.ids = {sources.ids[i]};
                        single.lane += int(i);
                        placeSources(std::move(single));
                    }
                }
            }
            return;
        }
        if (type == 2 && trimmingAutomation)
        {
            if (automationGestureProject != s.controller.projectIdentity)
            {
                trimmingAutomation = 0;
                dragging = false;
                return;
            }
            if (const auto *clip = automationClip(trimmingAutomation))
            {
                const auto delta =
                    juce::jlimit(-std::min(automationTrimBeat, automationTrimOffset),
                                 automationTrimLength - .000001, snap((e.x - origin.x) / spacing(), e.mods));
                CommandArgs a;
                a.target = trimmingAutomation;
                a.beat = automationTrimBeat + delta;
                a.value = automationTrimOffset + delta;
                a.length = automationTrimLength - delta;
                a.lane = clip->lane;
                s.command("automation.move", a);
            }
            return;
        }
        if (type == 2 && trimmingAudio)
        {
            if (auto *audio = audioClip(trimmingAudio))
            {
                const auto secondsPerBeat = 60. / s.controller.project.tempo;
                const auto delta = juce::jlimit(-std::min(trimStartBeat, trimStartOffset / secondsPerBeat),
                                                std::max(0., (trimStartDuration - .001) / secondsPerBeat),
                                                snap((e.x - origin.x) / spacing(), e.mods));
                const auto currentBeat = audio->beat;
                CommandArgs trim;
                trim.target = trimmingAudio;
                trim.offset = trimStartOffset + delta * secondsPerBeat;
                trim.length = trimStartDuration - delta * secondsPerBeat;
                s.command("audio.trim", trim);
                CommandArgs move;
                move.ids = {trimmingAudio};
                move.grouping =
                    false; // Left-edge trim changes this source window, never translates its group.
                move.length = 0;
                move.beat = trimStartBeat + delta - currentBeat;
                s.command("clip.move", move);
            }
            return;
        }
        CommandArgs a;
        a.pattern = s.selectedPattern;
        a.ids = type == 1 ? s.selectedNotes : s.selectedClips;
        a.pitch = 0;
        a.length = 0;
        std::optional<Placement> anchor;
        if (type == 2 && !a.ids.empty())
            for (const auto &clip : playlistClips())
                if (clip.id == a.ids.front())
                {
                    anchor = clip;
                    break;
                }
        const auto previousBeat = lastBeat, previousLength = lastLength;
        const auto previousLane = lastLane;
        double d = snap((e.x - origin.x) / spacing(), e.mods);
        if (resizing)
        {
            a.length = d - lastLength;
            lastLength = d;
        }
        else
        {
            a.beat = d - lastBeat;
            lastBeat = d;
            if (type == 1)
            {
                int p = pitch(e.y) - pitch(origin.y);
                a.pitch = p - lastPitch;
                lastPitch = p;
            }
            else
            {
                // Across a collapsed group, move by visible rows: lanes differ from rows there.
                int lane = visibleLaneCount() == 128
                               ? int((e.y - origin.y) / laneHeight())
                               : laneAtY(int(e.y)) - laneAtY(int(origin.y));
                a.lane = lane - lastLane;
                lastLane = lane;
            }
        }
        s.command(type == 1 ? "notes.move" : "clip.move", a);
        if (anchor)
            for (const auto &clip : playlistClips())
                if (clip.id == anchor->id)
                {
                    lastBeat = previousBeat + clip.beat - anchor->beat;
                    lastLength = previousLength + clip.length - anchor->length;
                    lastLane = previousLane + clip.lane - anchor->lane;
                    break;
                }
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (mixerStereo)
        {
            const auto result = finishMixerStereo(true);
            s.status = result.wasOk() ? "Ready" : result.getErrorMessage();
            repaint();
            return;
        }
        if (effectWet)
        {
            const auto result = finishEffectWet(true);
            s.status = result.wasOk() ? "Ready" : result.getErrorMessage();
            repaint();
            return;
        }
        if (effectOrder)
        {
            // Clear intrinsic UI ownership before any canonical observer can re-enter.
            auto intent = std::move(effectOrder);
            const bool moved = std::exchange(effectOrderMoved, false);
            const int destination = effectOrderDestination(e.position.toInt());
            const auto valid = intent->validate(s.controller.project, s.controller.projectIdentity,
                                                s.controller.revision, s.selectedInsert);
            if (valid.failed())
                s.status = valid.getErrorMessage();
            else if (moved && destination >= 0)
            {
                if (auto result = executeEffectOrder(*intent, destination); result.failed())
                    s.status = result.getErrorMessage();
            }
            else
                s.status = "Effect reorder cancelled";
            repaint();
            return;
        }
        if (type == 3 && mixerPanSource >= 0)
        {
            CommandArgs a;
            a.mixer = mixerPanSource;
            const auto *insert = mixerInsert(s.controller.project, mixerPanSource);
            a.value = insert ? insert->pan : 0;
            mixerPanSource = -1;
            dragging = false;
            const bool committed = finishUiGesture(s, "Mixer pan");
            if (committed && insert && s.recordingNotes)
                s.command("mixer.pan", a);
            repaint();
            return;
        }
        if (type == 3 && mixerSendSource >= 0)
        {
            CommandArgs a;
            a.mixer = mixerSendSource;
            a.target = mixerSendTarget;
            const auto *route = mixerRoute(s.controller.project, mixerSendSource, mixerSendTarget);
            a.value = route ? route->gain : 0;
            mixerSendSource = -1;
            dragging = false;
            const bool committed = finishUiGesture(s, "Mixer send level");
            if (committed && route && s.recordingNotes)
                s.command("mixer.send", a);
            repaint();
            return;
        }
        if (type == 3 && dragging)
        {
            CommandArgs a;
            a.mixer = s.selectedInsert;
            const auto *insert = mixerInsert(s.controller.project, s.selectedInsert);
            a.value = insert ? insert->gain : 0;
            dragging = false;
            const bool committed = finishUiGesture(s, "Mixer volume");
            if (committed && insert && s.recordingNotes)
                s.command("mixer.gain", a);
            repaint();
            return;
        }
        if (stretching)
        {
            stretching = false;
            if (s.noteStretchPending())
                if (auto r = s.finishNoteStretch(true); r.failed())
                    s.status = r.getErrorMessage();
            repaint();
            return;
        }

        if (type == 0 && rackSelecting)
        {
            rackSelecting = false;
            rackSelectionVisited.clear();
            return;
        }
        trimmingAutomation = 0;
        if (automationDragPoint)
        {
            automationDragPoint = automationDragClip = 0;
            finishUiGesture(s, "Move automation point");
            repaint();
            return;
        }
        if (muting)
        {
            muting = false;
            muteTouched.clear();
            finishUiGesture(s, type == 1 ? "Mute notes" : "Mute clips");
            return;
        }
        if (slicing && type == 1)
        {
            slicing = false;
            if (s.noteSlicePending())
            {
                if (origin.getDistanceFrom(e.position) < 2)
                    s.finishNoteSlice(false, "No cut-line drag; hold Shift for a single-click cut");
                else
                {
                    const auto r = s.updateNoteSlice(pianoSliceLine(origin, e.position, e.mods.isAltDown()));
                    if (r.wasOk())
                    {
                        const auto done = s.finishNoteSlice(true);
                        if (done.failed())
                            s.status = done.getErrorMessage();
                    }
                    else
                    {
                        s.status = r.getErrorMessage();
                        s.finishNoteSlice(false, r.getErrorMessage());
                    }
                }
            }
            repaint();
            return;
        }
        if (slicing)
        {
            commitSlice();
            slicing = false;
            repaint();
            return;
        }
        slipTarget = 0;

        if (pitchSelecting)
        {
            pitchSelecting = false;
            std::vector<Id> selection;
            if (auto *p = pattern())
                for (auto &note : p->notes)
                    if (canEdit(note) && note.pitch >= pitchFirst && note.pitch <= pitchLast &&
                        !selected(selection, note.id))
                        selection.push_back(note.id);
            s.selectItems(false, selection, pitchSelectionAdd ? "add" : "replace");
            return;
        }
        if (timeSelecting)
        {
            timeSelecting = false;
            std::vector<Id> selection;
            if (hasTimeSelection())
            {
                std::set<Id> channels;
                for (const auto &channel : s.controller.project.channels)
                    if (channel.id == s.selectedChannel || (ghosts && editableGhosts))
                        channels.insert(channel.id);
                selection = pianoOccurrenceRange(pianoView(), timeStart, timeEnd, 0, 127, channels, true);
            }
            s.selectItems(false, selection, timeSelectionAdd ? "add" : "replace");
            return;
        }
        if (regionZooming)
        {
            regionZooming = false;
            if (type == 2)
            {
                if (rectangle.getWidth() >= 4 && rectangle.getHeight() >= 4)
                    zoomPlaylistRegion(rectangle);
                else
                    restorePlaylistView();
                rectangle = {};
                navigationChanged();
                return;
            }
            if (rectangle.getWidth() >= 4 && rectangle.getHeight() >= 4)
            {
                auto start = beatAt(rectangle.getX());
                auto span = rectangle.getWidth() / spacing();
                auto top = pitch(int(rectangle.getY()));
                auto rows = std::max(1, top - pitch(int(rectangle.getBottom())) + 1);
                if (!hasRegionView)
                {
                    savedZoom = zoom;
                    savedScroll = scroll;
                    savedVerticalZoom = verticalZoom;
                    savedTopPitch = topPitch;
                    hasRegionView = true;
                }
                zoom = juce::jlimit(1.e-8, 256.,
                                    std::max(1, getWidth() - leftEdge() - scrollBarThickness) /
                                        (span * baseSpacing()));
                scroll = std::max(0., start);
                verticalZoom = juce::jlimit(.1, 8., std::max(1, noteBottom() - gridTop()) / (rows * 12.));
                topPitch = juce::jlimit(visiblePitches() - 1, 127, top);
            }
            else if (hasRegionView)
            {
                zoom = savedZoom;
                scroll = savedScroll;
                verticalZoom = savedVerticalZoom;
                topPitch = savedTopPitch;
                hasRegionView = false;
            }
            rectangle = {};
            repaint();
            horizontalBar.repaint();
            verticalBar.repaint();
            return;
        }
        if (previewDragging || panning || keyZooming)
        {
            s.previewNote(-1);
            previewDragging = panning = keyZooming = false;
            repaint();
            return;
        }
        if (selecting)
        {
            auto &list = type == 1 ? s.selectedNotes : s.selectedClips;
            if (type == 1)
            {
                std::set<Id> channels;
                for (const auto &channel : s.controller.project.channels)
                    if (channel.id == s.selectedChannel || (ghosts && editableGhosts))
                        channels.insert(channel.id);
                const auto ids =
                    pianoOccurrenceRange(pianoView(), beatAt(rectangle.getX()), beatAt(rectangle.getRight()),
                                         pitch(rectangle.getBottom()), pitch(rectangle.getY()), channels);
                for (auto id : ids)
                    if (!selected(list, id))
                        list.push_back(id);
            }
            else
                for (auto &c : playlistClips())
                    if (rectangle.intersects(cr(c)))
                    {
                        if (toggleMarquee && selected(selectionBeforeMarquee, c.id))
                            list.erase(std::remove(list.begin(), list.end(), c.id), list.end());
                        else if (!selected(list, c.id))
                            list.push_back(c.id);
                    }
            selectionBeforeMarquee.clear();
            toggleMarquee = false;
            selecting = false;
            s.selectItems(type == 2, list);
        }
        if (dragging || erasing)
            finishUiGesture(s, type == 0 ? "Edit steps" : type == 3 ? "Mixer volume" : "Edit selection");
        dragging = erasing = false;
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent &e) override
    {
        if (type == 1 && e.mods.isLeftButtonDown())
        {
            if (const auto occurrence = pianoHit(e.position); occurrence && canEdit(occurrence->note))
            {
                cancelPointerGesture();
                noteProperties(occurrence->note.id);
                return;
            }
        }
        if (type == 3 && e.mods.isLeftButtonDown())
        {
            const auto hit = mixerEffectLayout().hit(e.position.toInt(), effectOffset, mixerEffectCount());
            if (hit.action == MixerEffectSlots::Action::wet)
            {
                const auto *insert = mixerInsert(s.controller.project, s.selectedInsert);
                if (insert && hit.slot >= 0 && size_t(hit.slot) < insert->effects.size())
                {
                    const auto id = insert->effects[size_t(hit.slot)].id;
                    const auto owner =
                        effectWet && effectWet->plugin == id ? effectWet->owner : juce::String{};
                    const auto result = setEffectWet(id, 1, owner);
                    if (effectWet && effectWet->owner == owner && ownsEffectWet())
                        effectWet->revision = s.controller.revision;
                    if (result.failed())
                        s.status = result.getErrorMessage();
                    else if (effectWet)
                        reportHistoryResult(s, finishEffectWet(true));
                }
                repaint();
                return;
            }
            for (const auto &m : s.controller.project.mixer)
                if (mixerControlHit(mixerStereoSeparationBounds(m.index), e.position))
                {
                    const auto insert = m.index;
                    const auto owner =
                        mixerStereo && mixerStereo->insert == insert ? mixerStereo->owner : juce::String{};
                    if (mixerStereo && !validMixerStereo())
                    {
                        reportHistoryResult(s, finishMixerStereo(false));
                        return;
                    }
                    const auto result = setMixerStereo(insert, 0, owner);
                    if (mixerStereo && mixerStereo->owner == owner && ownsMixerStereo())
                        mixerStereo->revision = s.controller.revision;
                    if (result.failed())
                        s.status = result.getErrorMessage();
                    else if (mixerStereo)
                        reportHistoryResult(s, finishMixerStereo(true));
                    repaint();
                    return;
                }
            for (const auto &m : s.controller.project.mixer)
                if (mixerControlHit(mixerPanBounds(m.index), e.position))
                {
                    CommandArgs a;
                    a.mixer = m.index;
                    a.value = 0;
                    s.command("mixer.pan", a);
                    return;
                }
            for (const auto &m : s.controller.project.mixer)
                if (mixerSendLevelHit(m.index, e.position) &&
                    mixerRoute(s.controller.project, s.selectedInsert, m.index))
                {
                    CommandArgs a;
                    a.mixer = s.selectedInsert;
                    a.target = m.index;
                    a.value = 1;
                    s.command("mixer.send", a);
                    return;
                }
        }
        if (type == 1 && e.mods.isRightButtonDown() && switchGhost(e.position))
            return;
        if (type == 0)
        {
            if (e.x >= 4 && e.x < 18 && e.y >= 44)
                s.selectRackChannels(s.visibleRackChannels());
            return;
        }
        if (type == 2)
        {
            auto id = hit(e.position);
            for (auto &c : playlistClips())
                if (c.id == id)
                {
                    if (const auto *audio = audioClip(c.id))
                        s.openInstrument(audio->channel);
                    else if (const auto *clip = automationClip(c.id))
                    {
                        automationPicker = clip->source;
                        automationSourceEditor(id);
                    }
                    else
                    {
                        s.selectedPattern = c.pattern;
                        s.showEditor(1);
                    }
                    s.refresh();
                }
        }
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel) override
    {
        if (type == 1 && e.mods.isAltDown())
        {
            Id id = hit(e.position);
            if (auto *p = pattern())
                for (auto &n : p->notes)
                    if (n.id == id)
                    {
                        CommandArgs a;
                        a.pattern = p->id;
                        a.ids = selected(s.selectedNotes, id) ? s.selectedNotes : std::vector<Id>{id};
                        a.value = juce::jlimit(.01, 1., n.velocity + wheel.deltaY * .3);
                        s.command("notes.velocity", a);
                        return;
                    }
        }
        if (type == 3)
        {
            // A name-row intent, never pan/send fine adjustment or the FX pane.
            // One physical wheel event is one canonical discrete order/history action.
            if (e.mods.isShiftDown() && !e.mods.isCtrlDown() && !e.mods.isAltDown() &&
                std::abs(wheel.deltaY) > 0)
                for (const auto &insert : s.controller.project.mixer)
                    if (mixerControlHit(mixerNameBounds(insert.index), e.position))
                    {
                        CommandArgs args;
                        args.mixer = insert.index;
                        s.command(wheel.deltaY > 0 ? "mixer.moveLeft" : "mixer.moveRight", args);
                        return;
                    }
            if (mixerEffectLayout().pane.contains(e.position.toInt()))
            {
                const auto layout = mixerEffectLayout();
                const auto hit = layout.hit(e.position.toInt(), effectOffset, mixerEffectCount());
                const auto amount = wheel.deltaY + wheel.deltaX;
                if (hit.action == MixerEffectSlots::Action::wet)
                {
                    if (effectWet)
                    {
                        s.status = "Finish the wet drag before wheel editing";
                        return;
                    }
                    if (const auto *insert = mixerInsert(s.controller.project, s.selectedInsert);
                        insert && hit.slot >= 0 && size_t(hit.slot) < insert->effects.size())
                    {
                        const auto &effect = insert->effects[size_t(hit.slot)];
                        const auto result = setEffectWet(
                            effect.id,
                            juce::jlimit(0., 1.,
                                         double(effect.wet) + amount * (e.mods.isShiftDown() ? .01 : .1)));
                        if (result.failed())
                            s.status = result.getErrorMessage();
                    }
                    repaint();
                    return;
                }
                if (amount != 0)
                    setEffectSlotsOffset(layout.clampedOffset(effectSlotsOffset() + (amount > 0 ? -1 : 1),
                                                              mixerEffectCount()));
                return;
            }
            for (const auto &m : s.controller.project.mixer)
                if (mixerControlHit(mixerStereoSeparationBounds(m.index), e.position))
                {
                    if (mixerStereo)
                    {
                        s.status = "Finish the active stereo drag before wheel editing";
                        return;
                    }
                    const auto result =
                        setMixerStereo(m.index, juce::jlimit(-1., 1.,
                                                             double(m.stereoSeparation) +
                                                                 (wheel.deltaY + wheel.deltaX) *
                                                                     (e.mods.isShiftDown() ? .02 : .2)));
                    if (result.failed())
                        s.status = result.getErrorMessage();
                    repaint();
                    return;
                }
            for (const auto &m : s.controller.project.mixer)
                if (mixerControlHit(mixerPanBounds(m.index), e.position))
                {
                    CommandArgs a;
                    a.mixer = m.index;
                    a.value = juce::jlimit(-1., 1.,
                                           double(m.pan) + (wheel.deltaY + wheel.deltaX) *
                                                               (e.mods.isShiftDown() ? .02 : .2));
                    s.command("mixer.pan", a);
                    return;
                }
            for (const auto &m : s.controller.project.mixer)
                if (mixerSendLevelHit(m.index, e.position))
                    if (const auto *route = mixerRoute(s.controller.project, s.selectedInsert, m.index))
                    {
                        CommandArgs a;
                        a.mixer = s.selectedInsert;
                        a.target = m.index;
                        a.value = juce::jlimit(0., 2.,
                                               double(route->gain) + (wheel.deltaY + wheel.deltaX) *
                                                                         (e.mods.isShiftDown() ? .02 : .2));
                        s.command("mixer.send", a);
                        return;
                    }
            auto values = horizontalBar.read();
            mixerScroll = juce::jlimit(0., std::max(0., values[2] - values[1]),
                                       mixerScroll - (wheel.deltaY + wheel.deltaX) * 180);
            navigationChanged();
            return;
        }
        if (type == 0)
        {
            if (e.mods.isShiftDown() && !e.mods.isCtrlDown() && !e.mods.isAltDown() && e.x >= 34 &&
                e.x < rackLabelWidth() - 36 && e.y >= 44 && e.y < rackContentBottom() &&
                std::abs(wheel.deltaY) > 0)
            {
                if (auto *channel = s.rackChannelAt((e.y - 44) / rackRowHeight() + rackOffset))
                {
                    CommandArgs args;
                    args.ids = {channel->id};
                    s.command(wheel.deltaY > 0 ? "channel.moveUp" : "channel.moveDown", args);
                }
                return;
            }
            const bool horizontal = e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
            const auto delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
            if (delta == 0)
                return;
            if (!horizontal)
            {
                setRackViewport(rackOffset + (delta < 0 ? 1 : -1), stepOffset);
                return;
            }
            int page = std::max(1, int((getWidth() - stepStart() - 8) / stepWidth()));
            setRackViewport(rackOffset, stepOffset + (delta < 0 ? page : -page));
            return;
        }
        if (type == 1 || type == 2)
        {
            if (e.mods.isCtrlDown())
            {
                setZoom(zoom * (wheel.deltaY > 0 ? 1.1 : .9), e.x);
                repaint();
                return;
            }
            if (e.mods.isShiftDown())
            {
                if (type == 1)
                    topPitch =
                        juce::jlimit(visiblePitches() - 1, 127, topPitch + (wheel.deltaY > 0 ? 3 : -3));
                else
                    laneOffset = juce::jlimit(0, std::max(0, visibleLaneCount() - playlistRows()),
                                              laneOffset + (wheel.deltaY > 0 ? -1 : 1));
                repaint();
                return;
            }
            scroll = std::max(0., scroll - (wheel.deltaX + wheel.deltaY) * 8);
            repaint();
        }
    }
    bool isInterestedInDragSource(const SourceDetails &details) override
    {
        if (details.description.isObject())
        {
            CommandArgs args;
            return type == 2 && pickerPayload(details.description, s.controller.projectIdentity, args);
        }
        const auto source = details.description.toString();
        return type == 0 ||
               (type == 2 && !source.startsWith("builtin:") &&
                juce::File(source).hasFileExtension("wav;aif;aiff;flac")) ||
               (type == 1 && !source.startsWith("builtin:") &&
                juce::File(source).hasFileExtension("mid;midi"));
    }
    // Drop feedback: an accent outline while an accepted item hovers this editor.
    bool dropHover = false;
    void itemDragEnter(const SourceDetails &) override
    {
        dropHover = true;
        repaint();
    }
    void itemDragExit(const SourceDetails &) override
    {
        dropHover = false;
        repaint();
    }
    void fileDragEnter(const juce::StringArray &, int, int) override
    {
        dropHover = true;
        repaint();
    }
    void fileDragExit(const juce::StringArray &) override
    {
        dropHover = false;
        repaint();
    }
    void paintOverChildren(juce::Graphics &g) override
    {
        if (!dropHover)
            return;
        g.setColour(accent.withAlpha(.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.f), 6.f);
        g.setColour(accent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.f), 6.f, 2.f);
    }
    void itemDropped(const SourceDetails &d) override
    {
        dropHover = false;
        repaint();
        // Over the Rack's velocity graph the drop adds a new Channel (the graph is never an
        // obscured replacement target, but the drop must not be lost).
        if (d.description.isObject())
        {
            placePickerSource(d.description, d.localPosition);
            return;
        }
        if (type == 2)
        {
            filesDroppedWithModifiers({d.description.toString()}, int(d.localPosition.x),
                                      int(d.localPosition.y),
                                      juce::ModifierKeys::getCurrentModifiersRealtime());
            return;
        }
        if (!d.description.toString().startsWith("builtin:") &&
            juce::File(d.description.toString()).hasFileExtension("mid;midi"))
        {
            filesDroppedWithModifiers({d.description.toString()}, int(d.localPosition.x),
                                      int(d.localPosition.y),
                                      juce::ModifierKeys::getCurrentModifiersRealtime());
            return;
        }
        int row = (d.localPosition.y - 44) / rackRowHeight() + rackOffset;
        const bool overGraph = s.rackGraphVisible && d.localPosition.y >= rackContentBottom();
        bool replace = !overGraph && d.localPosition.y >= 44 && row >= 0 && s.rackChannelAt(row) != nullptr &&
                       d.localPosition.x < stepStart();
        if (replace)
            s.selectedChannel = s.rackChannelAt(row)->id;
        s.loadSample(d.description.toString(), replace);
    }
    bool isInterestedInFileDrag(const juce::StringArray &files) override
    {
        return files.size() == 1 &&
               (type == 0 || (type == 2 && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac")) ||
                (type == 1 && juce::File(files[0]).hasFileExtension("mid;midi")));
    }
    void filesDropped(const juce::StringArray &files, int x, int y) override
    {
        filesDroppedWithModifiers(files, x, y, juce::ModifierKeys::getCurrentModifiersRealtime());
    }
    void filesDroppedWithModifiers(const juce::StringArray &files, int x, int y, juce::ModifierKeys modifiers)
    {
        dropHover = false;
        repaint();
        if (files.size() != 1)
            return;
        if (type == 2)
        {
            // Anywhere over the Playlist places the clip; above the grid it lands on the top lane.
            s.importAudioClip(juce::File(files[0]), snap(beatAt(std::max(x, leftEdge())), modifiers),
                              laneAtY(std::max(y, gridTop())));
            return;
        }
        if (juce::File(files[0]).hasFileExtension("mid;midi"))
        {
            Id target = s.selectedChannel;
            const int row = (y - 44) / rackRowHeight() + rackOffset;
            if (type == 0 && y >= 44 && s.rackChannelAt(row))
                target = s.rackChannelAt(row)->id;
            s.importMidiFile(juce::File(files[0]), s.selectedPattern, target,
                             midiDropShowsOptions(modifiers));
            return;
        }
        if (type != 0)
            return;
        int row = (y - 44) / rackRowHeight() + rackOffset;
        const bool overGraph = s.rackGraphVisible && y >= rackContentBottom();
        bool replace = !overGraph && y >= 44 && s.rackChannelAt(row) != nullptr && x < stepStart();
        if (replace)
            s.selectedChannel = s.rackChannelAt(row)->id;
        s.loadSample(files[0], replace);
    }
};
class WorkspacePanelBounds final : public juce::ComponentBoundsConstrainer
{
    void checkBounds(juce::Rectangle<int> &bounds, const juce::Rectangle<int> &previous,
                     const juce::Rectangle<int> &limits, bool top, bool left, bool bottom,
                     bool right) override
    {
        const auto area = juce::Rectangle<int>(4, 84, std::max(300, limits.getWidth() - 8),
                                               std::max(160, limits.getHeight() - 113));
        juce::ComponentBoundsConstrainer::checkBounds(bounds, previous, limits, top, left, bottom, right);
        bounds.setSize(std::min(bounds.getWidth(), area.getWidth()),
                       std::min(bounds.getHeight(), area.getHeight()));
        bounds.setPosition(juce::jlimit(area.getX(), area.getRight() - bounds.getWidth(), bounds.getX()),
                           juce::jlimit(area.getY(), area.getBottom() - bounds.getHeight(), bounds.getY()));
    }
};
class Studio::EditorWindow final : public juce::Component
{
    Studio &s;
    int type;
    juce::String name;
    Editor editor;
    RoundedPanelEffect rounded;
    ToolbarButton close{"Close"}, maximise{"Maximise"}, dock{"Dock"}, detach{"Detach"};
    bool nativeHosted = false;
    juce::Rectangle<int> dragBounds;
    bool movingHeader = false;
    WorkspacePanelBounds constraints;
    juce::ResizableBorderComponent edges{this, &constraints};

  public:
    EditorWindow(Studio &owner, const juce::String &title, int kind)
        : s(owner), type(kind), name(title), editor(owner, kind)
    {
        setBroughtToFrontOnMouseClick(true);
        setComponentEffect(&rounded);
        addAndMakeVisible(editor);
        addAndMakeVisible(close);
        addAndMakeVisible(maximise);
        addAndMakeVisible(dock);
        addAndMakeVisible(detach);
        close.setIcon(ToolbarButton::Icon::close);
        maximise.setIcon(ToolbarButton::Icon::maximise);
        dock.setIcon(ToolbarButton::Icon::dock);
        detach.setIcon(ToolbarButton::Icon::detach);
        detach.setTooltip("Detach editor into a desktop window; drag it to any monitor");
        detach.onClick = [this] { s.detachWorkspaceWindow(type); };
        addAndMakeVisible(edges);
        constraints.setMinimumSize(300, type == 3 ? 240 : 160);
        constraints.setMinimumOnscreenAmounts(30, 100, 30, 100);
        dock.setTooltip("Return this window to the docked workspace");
        dock.onClick = [this] { s.dockWorkspaceWindow(type); };
        close.setTooltip("Hide editor (use its familiar F-key to restore)");
        maximise.setTooltip("Maximise / restore editor");
        close.onClick = [this] { s.toggleEditor(type); };
        maximise.onClick = [this] { s.maximiseEditor(type); };
    }
    juce::Component *getContentComponent()
    {
        return &editor;
    }
    void setNativeHosted(bool native)
    {
        nativeHosted = native;
        setComponentEffect(native ? nullptr : &rounded);
        resized();
        repaint();
    }
    ~EditorWindow() override
    {
        setComponentEffect(nullptr);
    }
    bool hitTest(int x, int y) override
    {
        return nativeHosted || RoundedPanelEffect::contains(getWidth(), getHeight(), x, y);
    }
    void resized() override
    {
        close.setVisible(!nativeHosted);
        maximise.setVisible(!nativeHosted);
        detach.setVisible(!nativeHosted);
        if (nativeHosted)
        {
            dock.setVisible(false);
            edges.setVisible(false);
            editor.setBounds(getLocalBounds());
            return;
        }
        editor.setBounds(1, 31, std::max(0, getWidth() - 2), std::max(0, getHeight() - 32));
        close.setBounds(getWidth() - 30, 3, 26, 24);
        maximise.setBounds(getWidth() - 60, 3, 26, 24);
        dock.setBounds(getWidth() - 112, 3, 48, 24);
        detach.setBounds(getWidth() - (s.floatingWindows[size_t(type)] ? 144 : 92), 3, 28, 24);
        dock.setVisible(s.floatingWindows[size_t(type)]);
        edges.setBounds(getLocalBounds());
        edges.setBorderThickness(juce::BorderSize<int>(4));
        edges.setVisible(s.floatingWindows[size_t(type)] && s.maximisedEditor != type);
        if (s.floatingWindows[size_t(type)] && s.maximisedEditor < 0 && getWidth() > 0)
            s.floatingBounds[size_t(type)] = getBounds();
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel);
        if (nativeHosted)
            return;
        g.setGradientFill(juce::ColourGradient(panel.brighter(.06f), 0, 0, panel, 0, 30, false));
        g.fillRect(0, 0, getWidth(), 30);
        g.setColour(grid);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 8.f, 1.f);
        g.drawHorizontalLine(30, 0, float(getWidth()));
        juce::String title = name;
        if (type == 1)
            if (auto *c = s.controller.project.channel(s.selectedChannel))
                title += " - " + c->name;
        if (type == 2)
            title += " - " + s.controller.project.name;
        if (type == 3 && getHeight() <= 32)
            title += " - maximise to edit";
        label(g, title, {12, 0, getWidth() - (s.floatingWindows[size_t(type)] ? 160 : 108), 30}, ink, 13);
    }
    void mouseDown(const juce::MouseEvent &event) override
    {
        s.focus = Scope(type + 1);
        editor.grabKeyboardFocus();
        toFront(false);
        movingHeader = event.y < 30 && event.mods.isLeftButtonDown();
        dragBounds = s.maximisedEditor == type && s.floatingWindows[size_t(type)]
                         ? s.floatingBounds[size_t(type)]
                         : getBounds();
    }
    void mouseDrag(const juce::MouseEvent &event) override
    {
        if (movingHeader && event.getDistanceFromDragStart() > 4)
            s.moveWorkspaceWindow(type, dragBounds.translated(event.getDistanceFromDragStartX(),
                                                              event.getDistanceFromDragStartY()));
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        movingHeader = false;
    }
    void mouseDoubleClick(const juce::MouseEvent &) override
    {
        s.maximiseEditor(type);
    }
};
class Studio::Splitter final : public juce::Component
{
    Studio &s;
    int kind;
    float initial = 0;

  public:
    Splitter(Studio &owner, int type) : s(owner), kind(type)
    {
        setMouseCursor(kind == 0 || kind == 2 || kind == 4 ? juce::MouseCursor::LeftRightResizeCursor
                                                           : juce::MouseCursor::UpDownResizeCursor);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(bg);
        g.setColour(isMouseOverOrDragging() ? accent.withAlpha(.65f) : grid.withAlpha(.6f));
        if (kind == 0 || kind == 2 || kind == 4)
            g.fillRect(getWidth() / 2, getHeight() / 2 - 18, 1, 36);
        else
            g.fillRect(getWidth() / 2 - 18, getHeight() / 2, 36, 1);
    }
    void mouseDown(const juce::MouseEvent &) override
    {
        initial = kind == 0   ? s.browserWidth
                  : kind == 1 ? s.playlistRatio
                  : kind == 2 ? s.rackWidth
                  : kind == 4 ? s.coProducerWidth
                              : s.mixerHeight;
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (kind == 0)
            s.browserWidth = juce::jlimit(180.f, 360.f, initial + e.getDistanceFromDragStartX());
        if (kind == 1)
            s.playlistRatio =
                juce::jlimit(.25f, .75f,
                             initial + e.getDistanceFromDragStartY() /
                                           float(std::max(1, s.workspace.getHeight() - int(s.mixerHeight))));
        if (kind == 2)
            s.rackWidth = juce::jlimit(330.f, 700.f, initial - e.getDistanceFromDragStartX());
        if (kind == 3)
            s.mixerHeight = juce::jlimit(168.f, 360.f, initial - e.getDistanceFromDragStartY());
        if (kind == 4) // CoProducer dock on the right: dragging left widens it.
            s.coProducerWidth = juce::jlimit(340.f, 760.f, initial - e.getDistanceFromDragStartX());
        s.resized();
        s.repaint();
    }
};
class Studio::Browser final : public juce::Component, public juce::FileDragAndDropTarget
{
    Studio &s;
    bool nativeHosted = false;
    int pressed = -1;
    bool soundsCollapsed = false, wasCompact = false;
    int soundTop() const
    {
        return (getHeight() < 700 ? 68 : 76) - (nativeHosted ? 30 : 0);
    }
    int soundStride() const
    {
        return getHeight() < 700 ? 28 : 38;
    }
    static constexpr int recentRows = 5;
    int sampleHeading() const
    {
        return soundsCollapsed ? 72 - (nativeHosted ? 30 : 0) : soundTop() + recentRows * soundStride() + 6;
    }
    // The producer's learned favourites (best of each role first); built-in sounds fill the
    // remaining rows until enough has been learned.
    std::vector<juce::String> recent() const
    {
        std::vector<juce::String> list;
        for (const auto &entry : s.recentSources.favourites(recentRows))
            list.push_back(entry.source);
        for (const auto &builtin : sources)
            if (int(list.size()) < recentRows && std::find(list.begin(), list.end(), builtin) == list.end())
                list.push_back(builtin);
        return list;
    }
    juce::String pressedSource;
    bool started = false;
    bool movingHeader = false;
    juce::Rectangle<int> dragBounds;
    ToolbarButton dock{"Dock"}, detach{"Detach"};
    WorkspacePanelBounds constraints;
    juce::ResizableBorderComponent edges{this, &constraints};
    const juce::StringArray sources{"builtin:kick", "builtin:snare", "builtin:hat", "builtin:bass"};
    juce::TextButton import{"Load sample..."};
    juce::TextButton plugins{"Native / VST3 instruments..."};
    SampleFileBrowser files;
    RoundedPanelEffect rounded;

  public:
    Browser(Studio &owner)
        : s(owner), files(owner, owner.persistentTyping ? owner.profileFile().getSiblingFile("browser.json")
                                                        : juce::File{})
    {
        setComponentEffect(&rounded);
        setBroughtToFrontOnMouseClick(true);
        addAndMakeVisible(dock);
        addAndMakeVisible(detach);
        dock.setIcon(ToolbarButton::Icon::dock);
        detach.setIcon(ToolbarButton::Icon::detach);
        detach.setTooltip("Detach Browser into a desktop window; drag it to any monitor");
        detach.onClick = [this] { s.detachWorkspaceWindow(4); };
        addAndMakeVisible(edges);
        constraints.setMinimumSize(190, 250);
        constraints.setMinimumOnscreenAmounts(30, 100, 30, 100);
        dock.onClick = [this] { s.dockWorkspaceWindow(4); };
        dock.setTooltip("Return Browser to its docked position");
        addAndMakeVisible(import);
        addAndMakeVisible(plugins);
        addAndMakeVisible(files);
        files.rootChanged = [this]
        {
            if (getHeight() < 700)
                soundsCollapsed = true;
            resized();
            repaint();
        };
        plugins.onClick = [this] { s.showPlugins(); };
        import.onClick = [this]
        {
            s.chooser =
                std::make_unique<juce::FileChooser>("Load sample", juce::File{}, "*.wav;*.aif;*.flac");
            auto safe = juce::Component::SafePointer<Studio>(&s);
            s.chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                       juce::FileBrowserComponent::canSelectFiles,
                                   [safe](const juce::FileChooser &fc)
                                   {
                                       if (safe && fc.getResult().existsAsFile())
                                           safe->loadSample(fc.getResult().getFullPathName(), false);
                                   });
        };
    }
    ~Browser() override
    {
        setComponentEffect(nullptr);
    }
    void setNativeHosted(bool native)
    {
        nativeHosted = native;
        setComponentEffect(native ? nullptr : &rounded);
        resized();
        repaint();
    }
    bool hitTest(int x, int y) override
    {
        return nativeHosted || RoundedPanelEffect::contains(getWidth(), getHeight(), x, y);
    }
    void resized() override
    {
        dock.setBounds(getWidth() - 54, 3, 48, 24);
        dock.setVisible(!nativeHosted && s.floatingWindows[4]);
        detach.setBounds(getWidth() - (s.floatingWindows[4] ? 88 : 36), 3, 28, 24);
        detach.setVisible(!nativeHosted);
        edges.setBounds(getLocalBounds());
        edges.setBorderThickness(juce::BorderSize<int>(4));
        edges.setVisible(!nativeHosted && s.floatingWindows[4]);
        if (!nativeHosted && s.floatingWindows[4] && getWidth() > 0)
            s.floatingBounds[4] = getBounds();
        const bool compact = getHeight() < 700;
        if (compact && getHeight() > 0 && !wasCompact)
            soundsCollapsed = true;
        wasCompact = compact && getHeight() > 0;
        const int importY = sampleHeading() + 32;
        import.setBounds(12, importY, getWidth() - 24, 30);
        const bool compactFiles = compact;
        plugins.setBounds(12, importY + (compactFiles ? 36 : 70), getWidth() - 24, 30);
        const int filesY = importY + (compactFiles ? 78 : 114);
        const int filesHeight = getHeight() - filesY - 110;
        files.setVisible(filesHeight >= 80);
        files.setBounds(10, filesY, getWidth() - 20, std::max(0, filesHeight));
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel.darker(.15f));
        g.setColour(grid);
        if (!nativeHosted)
        {
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 8.f, 1.f);
            g.drawHorizontalLine(30, 0, float(getWidth()));
            label(g, "Browser", {12, 0, getWidth() - (s.floatingWindows[4] ? 100 : 48), 30}, ink, 13);
        }
        label(g, soundsCollapsed ? ">  RECENT SAMPLES / INSTRUMENTS" : "v  RECENT SAMPLES / INSTRUMENTS",
              {12, nativeHosted ? 10 : 40, getWidth() - 24, 24}, muted, 11);
        const auto list = recent();
        static const juce::StringArray roleOrder{"Kick", "Snare", "Clap", "Hat", "808", "Cymbal", "Perc", "Bass", "Vocal", "FX", "Sample"};
        for (int i = 0; i < (soundsCollapsed ? 0 : int(list.size())); ++i)
        {
            const int y = soundTop() + i * soundStride();
            const auto &source = list[size_t(i)];
            const auto role = RecentSources::role(source);
            const auto colour = channelColour(std::max(0, roleOrder.indexOf(role)));
            g.setColour(i == pressed ? grid : panel);
            g.fillRoundedRectangle(8.f, float(y), float(getWidth() - 16), float(soundStride() - 4), 3);
            g.setColour(colour);
            g.fillRoundedRectangle(14.f, float(y + 6), 4.f, float(soundStride() - 16), 2);
            const bool builtin = source.startsWith("builtin:");
            const auto plugin = source.startsWith("plugin:")
                                    ? s.instrumentByIdentifier(source.fromFirstOccurrenceOf(":", false, false))
                                    : std::nullopt;
            label(g,
                  builtin                       ? sourceName(source).toUpperCase()
                  : source.startsWith("plugin:") ? (plugin ? plugin->name : juce::String("Missing instrument"))
                                                 : juce::File(source).getFileNameWithoutExtension(),
                  {26, y, getWidth() - 96, soundStride() - 4}, ink, 12);
            if (!builtin) // A built-in's name already says its role.
                label(g, role.toUpperCase(), {getWidth() - 68, y, 54, soundStride() - 4}, colour.withAlpha(.85f), 9);
        }
        label(g, "SAMPLES", {12, sampleHeading(), getWidth() - 24, 24}, muted, 11);
        if (getHeight() >= 700)
            label(g, "WAV / AIFF / FLAC", {12, sampleHeading() + 71, getWidth() - 24, 22}, muted, 11);
        if (getHeight() > 540)
        {
            g.setColour(grid);
            g.drawHorizontalLine(float(files.isVisible() ? getHeight() - 106 : 386), 12.f,
                                 float(getWidth() - 12));
            const int projectY = files.isVisible() ? getHeight() - 100 : 397;
            label(g, "PROJECT", {12, projectY, getWidth() - 24, 20}, muted, 11);
            label(g, s.controller.project.name, {12, projectY + 20, getWidth() - 24, 22}, ink, 13);
            label(g,
                  juce::String(s.controller.project.channels.size()) + " channels  /  " +
                      juce::String(s.controller.project.patterns.size()) + " patterns",
                  {12, projectY + 42, getWidth() - 24, 20}, muted, 11);
            if (!files.isVisible())
            {
                label(g, "Click to audition", {12, getHeight() - 76, getWidth() - 24, 22}, muted, 12);
                label(g, "Drag to Channel Rack", {12, getHeight() - 52, getWidth() - 24, 22}, muted, 12);
            }
        }
        label(g, "VibeRa Studio " + juce::JUCEApplication::getInstance()->getApplicationVersion(),
              {12, getHeight() - 27, getWidth() - 24, 20}, muted, 10);
    }
    bool isInterestedInFileDrag(const juce::StringArray &names) override
    {
        return files.isInterestedInFileDrag(names);
    }
    void filesDropped(const juce::StringArray &names, int x, int y) override
    {
        files.filesDropped(names, x, y);
    }
    bool viewAction(const juce::String &action)
    {
        return files.viewAction(action);
    }
    SampleFileBrowser &filesForTest()
    {
        return files;
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        movingHeader = !nativeHosted && e.y < 30 && e.mods.isLeftButtonDown();
        dragBounds = getBounds();
        if (movingHeader)
        {
            toFront(false);
            return;
        }
        if (e.y >= (nativeHosted ? 10 : 40) && e.y < (nativeHosted ? 34 : 64))
        {
            soundsCollapsed = !soundsCollapsed;
            pressed = -1;
            resized();
            repaint();
            return;
        }
        const auto list = recent();
        pressed = (!soundsCollapsed && e.y >= soundTop() && e.y < soundTop() + int(list.size()) * soundStride())
                      ? juce::jlimit(0, int(list.size()) - 1, (e.y - soundTop()) / soundStride())
                      : -1;
        started = false;
        pressedSource = pressed >= 0 ? list[size_t(pressed)] : juce::String{};
        if (pressed >= 0 && e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Load into a new Channel");
            menu.addItem(2, "Forget this sound", !pressedSource.startsWith("builtin:"));
            auto safe = juce::Component::SafePointer<Browser>(this);
            const auto source = pressedSource;
            menu.showMenuAsync({}, [safe, source](int choice)
                               {
                                   if (!safe)
                                       return;
                                   if (choice == 1)
                                       safe->s.loadSample(source, false);
                                   if (choice == 2)
                                       safe->s.recentSources.forget(source);
                                   safe->repaint();
                               });
            return;
        }
        if (pressed >= 0)
            s.audio.requestAudition(pressedSource);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (movingHeader && e.getDistanceFromDragStart() > 4)
        {
            s.moveWorkspaceWindow(
                4, dragBounds.translated(e.getDistanceFromDragStartX(), e.getDistanceFromDragStartY()));
            return;
        }
        if (pressed >= 0 && !started && e.getDistanceFromDragStart() > 5)
        {
            started = true;
            // A small labelled chip follows the mouse (an empty image would make JUCE drag a
            // snapshot of the whole Browser over the editors).
            const auto name = pressedSource.startsWith("builtin:") ? sourceName(pressedSource).toUpperCase()
                              : pressedSource.startsWith("plugin:")
                                  ? (s.instrumentByIdentifier(pressedSource.fromFirstOccurrenceOf(":", false, false))
                                         ? s.instrumentByIdentifier(pressedSource.fromFirstOccurrenceOf(":", false, false))->name
                                         : juce::String("Instrument"))
                                  : juce::File(pressedSource).getFileNameWithoutExtension();
            juce::Image chip(juce::Image::ARGB, 180, 26, true);
            {
                juce::Graphics g(chip);
                g.setColour(panel.brighter(.12f).withAlpha(.92f));
                g.fillRoundedRectangle(0.f, 0.f, 180.f, 26.f, 6.f);
                g.setColour(accent);
                g.drawRoundedRectangle(.5f, .5f, 179.f, 25.f, 6.f, 1.f);
                g.setColour(ink);
                g.setFont(juce::Font(vibera::uiFont, 12.f, juce::Font::bold));
                g.drawText(name, 10, 0, 164, 26, juce::Justification::centredLeft, true);
            }
            const juce::Point<int> offset(-12, -13);
            s.startDragging(pressedSource, this, juce::ScaledImage(chip), true, &offset);
        }
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        movingHeader = false;
    }
    void mouseDoubleClick(const juce::MouseEvent &event) override
    {
        if (!nativeHosted && event.y < 30)
        {
            s.dockWorkspaceWindow(4);
            return;
        }
        if (pressed >= 0)
            s.loadSample(pressedSource, false);
    }
};
} // namespace vibera
