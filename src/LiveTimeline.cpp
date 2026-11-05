// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PatternChannelLoops.h"
#include "LiveTimeline.h"
#include "PatternAutomation.h"
#include "Tuning.h"
#include "NoteRelease.h"
#include "PatternExtent.h"
#include <tuple>

namespace vibera
{
const LiveNote *LiveChannel::find(Id note, Id placement, std::uint32_t repeatOrdinal) const noexcept
{
    auto key = std::tuple{placement, note, repeatOrdinal};
    auto i = std::lower_bound(notes.begin(), notes.end(), key, [](auto &n, auto k)
                              { return std::tuple{n.placement, n.note, n.repeatOrdinal} < k; });
    return i != notes.end() && i->note == note && i->placement == placement &&
                   i->repeatOrdinal == repeatOrdinal
               ? &*i
               : nullptr;
}
const LiveChannel *LiveDocument::find(Id channel) const noexcept
{
    auto i =
        std::lower_bound(channels.begin(), channels.end(), channel, [](auto &c, Id id) { return c.id < id; });
    return i != channels.end() && i->id == channel ? &*i : nullptr;
}
const LiveDocument::EffectControl *LiveDocument::effect(Id plugin) const noexcept
{
    const auto found = std::lower_bound(effects.begin(), effects.end(), plugin,
                                        [](const auto &control, Id id) { return control.plugin < id; });
    return found == effects.end() || found->plugin != plugin ? nullptr : &*found;
}
const LiveDocument::NativeAutomationRoute *LiveDocument::nativeRoute(Id plugin) const noexcept
{
    const auto found = std::lower_bound(nativeAutomation.begin(), nativeAutomation.end(), plugin,
                                        [](const auto &route, Id id) { return route.plugin < id; });
    return found == nativeAutomation.end() || found->plugin != plugin ? nullptr : &*found;
}
std::unique_ptr<LiveDocument> compileLiveDocument(const Project &p, bool pattern, Id selected,
                                                  std::uint64_t generation)
{
    auto result = std::make_unique<LiveDocument>();
    result->generation = generation;
    result->tempo = p.tempo;
    result->tempoMap = pattern ? TempoMap(p.tempo, {}) : p.songTempo();
    const auto &map = result->tempoMap;
    result->numerator = p.numerator;
    result->denominator = p.denominator;
    if (!pattern)
    {
        result->meters.numerator = p.numerator;
        result->meters.denominator = p.denominator;
        for (const auto &change : p.meterChanges)
            result->meters.changes.push_back({change.beat, double(change.numerator), double(change.denominator)});
    }
    if (pattern)
        if (const auto *part = p.pattern(selected))
        {
            result->numerator = patternNumerator(p, *part);
            result->denominator = patternDenominator(p, *part);
        }
    std::string error;
    // Playlist Automation Clips (muted tracks silence theirs) plus Pattern automation.
    const auto audible = audibleAutomation(p, pattern, selected);
    if (!AutomationProgram::compile(audible.document, audible.songMode, result->automation, error,
                                    p.automationDefaults()))
        return {};
    for (const auto &m : p.mixer)
        result->mixer.push_back({m.index, m.gain, m.muted, m.pan, m.stereoSeparation, m.phaseInverted, m.stereoSwapped, m.eq, m.delayMs});
    std::sort(result->mixer.begin(), result->mixer.end(),
              [](const auto &a, const auto &b) { return a.index < b.index; });
    for (const auto &m : p.mixer)
        for (const auto &route:m.routes) result->mixerSends.push_back({m.index,route.target,route.gain});
    std::sort(result->mixerSends.begin(),result->mixerSends.end(),[](const auto &a,const auto &b)
      {return std::pair{a.source,a.target}<std::pair{b.source,b.target};});
    for (const auto &insert : p.mixer)
        for (const auto &effect : insert.effects)
            result->effects.push_back({effect.id, effect.wet, effect.bypass || insert.effectsBypassed});
    std::sort(result->effects.begin(), result->effects.end(),
              [](const auto &a, const auto &b) { return a.plugin < b.plugin; });
    auto events = sequence(p, pattern, selected);
    for (const auto &note : events)
        if (note.colourGroup < 0 || note.colourGroup > 15)
            return {}; // CONTROL refuses invalid domain before fixed-index audio publication.
    for (const auto &n : events)
        if (const auto *source = p.channel(n.channel);
            source && !source->instrument && !source->audioClip && !n.slide && source->sampler.cut > 0)
            result->cuts[size_t(source->sampler.cut)].push_back(
                {map.secondsAt(n.beat), n.channel, n.note, n.step});
    for (auto &group : result->cuts)
    {
        std::sort(group.begin(), group.end(), [](const auto &a, const auto &b)
                  { return std::tie(a.time, a.sender, a.note) < std::tie(b.time, b.sender, b.note); });
        int lastStep = -1;
        for (size_t i = 0; i < group.size(); ++i)
        {
            if (group[i].step)
                lastStep = int(i);
            group[i].previousStep = lastStep;
            if (i > 0)
                group[i].previousOther =
                    group[i].sender == group[i - 1].sender ? group[i - 1].previousOther : int(i - 1);
        }
    }
    for (auto &c : p.channels)
    {
        LiveChannel channel;
        channel.id = c.id;
        channel.gain = c.audioClip && c.muted ? 0.f : c.gain;
        channel.pan = c.pan;
        channel.sampler = PreparedSamplerSettings(c.sampler, p.tempo);
        channel.sampleInstrument = !c.instrument && !c.audioClip;
        channel.mpe = c.instrument && c.mpe;
        if (channel.sampleInstrument &&
            (!c.scale.empty() || std::any_of(c.tuning.begin(), c.tuning.end(), [](double cents) { return cents != 0; })))
        {
            channel.liveTuned = true;
            for (int key = 0; key < 128; ++key)
            {
                double cents = c.tuning[size_t(key % 12)];
                int sounding = key;
                if (!c.scale.empty())
                {
                    const double deviation = tuning::scaleDeviation(c.scale, c.scaleRoot, key);
                    const int semitones = juce::roundToInt(deviation / 100.);
                    sounding = juce::jlimit(0, 127, key + semitones);
                    cents = deviation - 100. * semitones;
                }
                channel.liveKey[size_t(key)] = std::uint8_t(sounding);
                channel.liveExpression[size_t(key)] = PreparedNoteExpression::prepare(0, juce::jlimit(-100., 100., cents));
            }
        }
        if (!prepareSamplerModulationAutomation(result->automation, c.id, channel.sampleInstrument,
                                                channel.modulationLanes))
            return {};
        // A keyboard-mapped scale moves each key by whole semitones plus a cents remainder.
        auto scaled = [&](const ScheduledNote &n)
        {
            const double deviation =
                channel.sampleInstrument || channel.mpe ? tuning::scaleDeviation(c.scale, c.scaleRoot, n.pitch) : 0.;
            const int semitones = juce::roundToInt(deviation / 100.);
            return std::pair{juce::jlimit(0, 127, n.pitch + semitones), deviation - 100. * semitones};
        };
        for (auto &n : events)
            if (n.channel == c.id)
                channel.notes.push_back(
                    {n.note, n.placement, map.secondsAt(n.beat), map.secondsAt(n.beat + n.length),
                     c.scale.empty() ? n.pitch : scaled(n).first,
                     float(juce::jlimit(n.slide ? 0 : 1, 127, juce::roundToInt(n.velocity * 127))) / 127,
                     n.step,
                     channel.sampleInstrument
                         ? PreparedNoteExpression::prepare(
                               n.pan,
                               juce::jlimit(-100., 100.,
                                            n.fineCents + (c.scale.empty() ? c.tuning[size_t(n.pitch % 12)]
                                                                           : scaled(n).second)),
                               n.modX,
                               n.modY)
                         : PreparedNoteExpression{},
                     float(channel.mpe ? n.fineCents + (c.scale.empty() ? c.tuning[size_t(n.pitch % 12)]
                                                                         : scaled(n).second)
                                       : 0.),
                     n.repeatOrdinal, noteReleaseMidiVelocity(n.releaseVelocity), n.slide, n.portamento,
                     n.colourGroup});
        std::sort(channel.notes.begin(), channel.notes.end(),
                  [](auto &a, auto &b)
                  {
                      return std::tuple{a.placement, a.note, a.repeatOrdinal} <
                             std::tuple{b.placement, b.note, b.repeatOrdinal};
                  });
        for (size_t i = 0; i < channel.notes.size(); ++i)
        {
            auto &n = channel.notes[i];
            channel.events.push_back({n.start, std::uint32_t(i), true});
            channel.events.push_back({n.end, std::uint32_t(i), false});
        }
        std::sort(channel.events.begin(), channel.events.end(), [](auto &a, auto &b)
                  { return std::tie(a.time, a.on, a.index) < std::tie(b.time, b.on, b.index); });
        // Controllers follow the same Pattern windows as notes (Song placements or Pattern).
        // Plugins get every controller; the local sampler reads the pitch bend among them.
        if ((c.instrument || !c.audioClip) && !c.muted)
        {
            auto addControls = [&](const Pattern &source, double start, double length, double offset)
            {
                for (const auto &control : source.controllers)
                {
                    const double b = control.beat - offset;
                    if (control.channel != c.id || b < 0 || b >= length || b >= source.length - offset)
                        continue;
                    LiveChannel::Control live{map.secondsAt(start + b), {}};
                    if (control.type == 1)
                    {
                        const int raw = juce::jlimit(0, 16383, juce::roundToInt(8192 + control.value * 8191));
                        live.bytes = {std::uint8_t(0xe0 | control.midiChannel), std::uint8_t(raw & 127),
                                      std::uint8_t(raw >> 7)};
                    }
                    else if (control.type == 3)
                        live.bytes = {std::uint8_t(0xc0 | control.midiChannel), std::uint8_t(control.number), 0};
                    else if (control.type == 2)
                        live.bytes = {std::uint8_t(0xd0 | control.midiChannel),
                                      std::uint8_t(juce::roundToInt(control.value * 127)), 0};
                    else
                        live.bytes = {std::uint8_t(0xb0 | control.midiChannel), std::uint8_t(control.number),
                                      std::uint8_t(juce::roundToInt(control.value * 127))};
                    channel.controls.push_back(live);
                }
            };
            if (pattern)
            {
                if (const auto *part = p.pattern(selected))
                    addControls(*part, 0, part->length, 0);
            }
            else
                for (const auto &clip : p.placements)
                    if (!clip.muted && !p.laneMuted(clip.lane))
                        if (const auto *part = p.pattern(clip.pattern))
                            forEachClipRepeat(clip.length, clip.offset, part->length,
                                              [&](double position, double length, double offset, std::uint32_t)
                                              { addControls(*part, clip.beat + position, length, offset); });
            if (c.instrument && c.pitchBend != 0)
            {
                // Channel PITCH: the plugin gets the bend with every note start (so it also holds
                // when playback starts mid-song). Pattern pitch-bend events still follow.
                const int raw = juce::jlimit(0, 16383, juce::roundToInt(8192 + c.pitchBend * 8191));
                for (const auto &n : channel.notes)
                    channel.controls.push_back({n.start, {std::uint8_t(0xe0 | (n.colourGroup & 15)),
                                                          std::uint8_t(raw & 127), std::uint8_t(raw >> 7)}});
            }
            std::stable_sort(channel.controls.begin(), channel.controls.end(),
                             [](const auto &a, const auto &b) { return a.time < b.time; });
        }
        result->channels.push_back(std::move(channel));
    }
    std::sort(result->channels.begin(), result->channels.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return result;
}
void bindLiveCutInputs(LiveDocument &document, std::span<const SamplerCutInput> inputs)
{
    for (auto &receiver : document.channels)
    {
        receiver.inputCuts.clear();
        if (!receiver.sampleInstrument || receiver.sampler.values.cutBy == 0)
            continue;
        for (const auto &input : inputs)
            if (const auto *sender = document.find(input.channel);
                sender && sender->sampleInstrument && sender->id != receiver.id &&
                sender->sampler.values.cut == receiver.sampler.values.cutBy && input.signal)
                receiver.inputCuts.push_back({input.channel, input.signal,
                                              input.signal->lastOn.load(std::memory_order_acquire),
                                              input.lane});
    }
}
bool LiveTimeline::publish(std::unique_ptr<LiveDocument> &document)
{
    for (int i = 0; i < 3; ++i)
        if (i != published.load() && slots[size_t(i)].readers.load() == 0)
        {
            // Only the producer destroys retired data, never the audio consumer.
            slots[size_t(i)].document = std::move(document);
            published.store(i);
            // Saturation refuses new structural tickets rather than allowing ABA.
            // Routine publication retains its existing slot/lease behavior.
            if (publicationSerial != std::numeric_limits<std::uint64_t>::max())
                ++publicationSerial;
            return true;
        }
    return false;
}
LiveTimeline::Lease LiveTimeline::acquire() noexcept
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        int i = published.load();
        if (i < 0)
            return {};
        slots[size_t(i)].readers.fetch_add(1);
        if (i == published.load())
            return Lease(*this, i);
        slots[size_t(i)].readers.fetch_sub(1);
    }
    return {};
}
bool LiveSequencer::active(Id note, Id placement, std::uint32_t repeatOrdinal) const noexcept
{
    for (auto &g : gates)
        if (g.active && g.note == note && g.placement == placement && g.repeatOrdinal == repeatOrdinal)
            return true;
    return false;
}
void LiveSequencer::process(const LiveDocument &doc, Id channelId, double time, int frames, double rate,
                            bool playing, VoiceBank &voices, juce::AudioBuffer<float> &buffer, int offset,
                            std::span<VoiceBank *const> extra, bool liveCut) noexcept
{
    auto render = [&](int start, int length)
    {
        voices.render(buffer, start, length);
        for (auto *bank : extra)
            bank->render(buffer, start, length);
    };
    auto select = [&](int frame) noexcept
    {
        if (!voices.selectModulationFrame(frame))
            return false;
        for (auto *bank : extra)
            if (!bank->selectModulationFrame(frame))
                return false;
        return true;
    };
    if (frames > 0 && !select(offset))
        return; // Before jump/generation gate reconciliation and chase.
    auto *channel = doc.find(channelId);
    auto cancelSlide = [&](const SlideControl &control) noexcept
    {
        if (!control.present)
            return;
        const auto &source = control.source;
        voices.cancelSlide(source.note, source.placement, source.start);
        for (auto *bank : extra)
            bank->cancelSlide(source.note, source.placement, source.start);
    };
    auto startSlide = [&](const LiveNote &note, double at, bool retarget = false) noexcept
    {
        auto &control = slides[size_t(note.colourGroup)];
        double reference = control.reference;
        if (!retarget)
        {
            reference = voices.slideReference(note.colourGroup);
            for (auto *bank : extra)
                reference = std::max(reference, bank->slideReference(note.colourGroup));
        }
        const double remaining = note.end - at;
        // An ordinary endpoint edit at/after the last control sample uses the
        // existing5ms dezipper, instead of jumping gain/pan in one sample.
        const double duration = retarget && remaining <= 1. / rate ? .005 : std::max(1. / rate, remaining);
        voices.applySlide(note.note, note.placement, note.start, note.pitch, note.velocity, note.expression,
                          note.colourGroup, reference, duration, retarget);
        for (auto *bank : extra)
            bank->applySlide(note.note, note.placement, note.start, note.pitch, note.velocity,
                             note.expression, note.colourGroup, reference, duration, retarget);
        control = {true, note, reference};
    };
    if (!playing || !channel)
    {
        for (const auto &slide : slides)
            cancelSlide(slide);
        if (wasPlaying)
            voices.reset();
        reset();
        for (auto *bank : extra)
            bank->render(buffer, offset, frames);
        return;
    }
    const auto &cuts = doc.cuts[size_t(channel->sampleInstrument ? channel->sampler.values.cutBy : 0)];
    auto eligibleCut = [&](const auto &event) { return event.sender != channelId || event.step; };
    auto before = std::upper_bound(cuts.begin(), cuts.end(), time,
                                   [](double t, const auto &event) { return t < event.time; });
    double lastCut = -std::numeric_limits<double>::infinity();
    if (before != cuts.begin())
    {
        auto index = int(before - cuts.begin() - 1);
        if (!eligibleCut(cuts[size_t(index)]))
            index = std::max(cuts[size_t(index)].previousOther, cuts[size_t(index)].previousStep);
        if (index >= 0)
            lastCut = cuts[size_t(index)].time;
    }
    const bool jump = !wasPlaying || std::abs(time - previousEnd) > 2.1 / rate;
    const bool changed = doc.generation != generation;
    if (jump)
    {
        for (const auto &slide : slides)
            cancelSlide(slide);
        slides = {};
        for (auto &g : gates)
            if (g.active)
                voices.releaseTagged(g.note, g.placement, true, g.repeatOrdinal);
        gates = {};
        lastInputCutTime = -std::numeric_limits<double>::infinity();
    }
    if (liveCut)
        lastInputCutTime = time;
    lastCut = std::max(lastCut, lastInputCutTime);
    if (changed || jump)
    {
        // Retain the playback position of every unchanged sounding voice.
        voices.visitActive(
            [&](auto &v)
            {
                auto *n = channel->find(v.note, v.placement, v.repeatOrdinal);
                if (!n || n->slide || n->pitch != v.pitch || n->start != v.onset)
                    v.releasing = true;
                else
                    voices.reconcileScore(v, n->velocity, n->expression, n->colourGroup);
            });
        for (auto &g : gates)
            if (g.active)
            {
                auto *n = channel->find(g.note, g.placement, g.repeatOrdinal);
                if (!n || n->slide || n->pitch != g.pitch || n->start != g.start || n->end <= time)
                {
                    voices.releaseTagged(g.note, g.placement, true, g.repeatOrdinal);
                    g.active = false;
                }
                else
                    g.end = n->end;
            }
        // Chase newly inserted/moved notes that already span the playhead.
        for (auto &n : channel->notes)
            if (!n.slide && n.start < time && n.end > time && !active(n.note, n.placement, n.repeatOrdinal))
            {
                for (auto &g : gates)
                    if (!g.active)
                    {
                        // A seek/edit cannot resurrect a note already choked by
                        // the score or live input, even after fixed gate overflow.
                        // Keep its logical gate until source note-off.
                        if (!liveCut && n.start >= lastCut)
                            voices.noteOn(n.pitch, n.velocity, n.note, n.placement, n.start, 0, n.expression,
                                          n.repeatOrdinal, n.portamento, n.colourGroup);
                        g = {true, n.note, n.placement, n.start, n.end, n.pitch, n.repeatOrdinal};
                        break;
                    }
            }
        // Only changed source controls are reconciled. Ordinary publication cannot
        // restart a trajectory or recruit notes born after a Slide was admitted.
        std::array<const LiveNote *, 16> latestControls{};
        for (const auto &note : channel->notes)
            if (note.slide && note.start < time && note.end > time)
            {
                auto &latest = latestControls[size_t(note.colourGroup)];
                if (!latest || std::tie(note.start, note.placement, note.note) >
                                   std::tie(latest->start, latest->placement, latest->note))
                    latest = &note;
            }
        for (size_t group = 0; group < slides.size(); ++group)
        {
            auto &control = slides[group];
            if (control.present)
            {
                const auto &old = control.source;
                const auto *now = channel->find(old.note, old.placement, old.repeatOrdinal);
                if (!now || !now->slide || now->colourGroup != int(group) || now->start != old.start ||
                    now->start > time)
                {
                    cancelSlide(control);
                    // Retain latest admission identity as a supersession barrier.
                    // Deleting B cannot resurrect older A or recruit new voices to A.
                    control.present = false;
                    control.reference = 0;
                }
                else if (now->pitch != old.pitch || now->velocity != old.velocity ||
                         now->expression != old.expression || now->end != old.end)
                    startSlide(*now, time, true);
            }
            const auto *latest = latestControls[group];
            if (latest)
            {
                const auto candidate = std::tie(latest->start, latest->placement, latest->note);
                const auto barrier =
                    std::tie(control.source.start, control.source.placement, control.source.note);
                if (control.source.note == 0 || candidate > barrier ||
                    (!control.present && candidate == barrier))
                    startSlide(*latest, time); // Explicit Undo of B is a fresh B admission.
            }
        }
    }
    generation = doc.generation;
    wasPlaying = true;
    int cursor = 0;
    auto end = time + frames / rate;
    auto i = std::lower_bound(channel->events.begin(), channel->events.end(), time - 0.1 / rate,
                              [](auto &e, double t) { return e.time < t; });
    auto cut = std::lower_bound(cuts.begin(), cuts.end(), time - .1 / rate,
                                [](const auto &event, double t) { return event.time < t; });
    while ((i != channel->events.end() && i->time < end) || (cut != cuts.end() && cut->time < end))
    {
        const auto eventTime =
            std::min(i == channel->events.end() ? end : i->time, cut == cuts.end() ? end : cut->time);
        const int frame = juce::jlimit(cursor, frames, juce::roundToInt((eventTime - time) * rate));
        if (frame == frames)
            break;
        render(offset + cursor, frame - cursor);
        cursor = frame;
        if (!select(offset + cursor))
            return; // Includes same-frame/zero-render event groups.
        auto ownEnd = i;
        while (ownEnd != channel->events.end() && ownEnd->time == eventTime)
            ++ownEnd;
        // Offs, cuts, then new notes: simultaneous notes start together, without
        // dependence on graph track order. Self-cut originates from steps only.
        for (auto event = i; event != ownEnd; ++event)
            if (!event->on)
            {
                const auto &n = channel->notes[event->index];
                if (n.slide)
                    continue;
                voices.releaseTagged(n.note, n.placement, false, n.repeatOrdinal);
                for (auto &gate : gates)
                    if (gate.active && gate.note == n.note && gate.placement == n.placement &&
                        gate.repeatOrdinal == n.repeatOrdinal)
                        gate.active = false;
            }
        while (cut != cuts.end() && cut->time == eventTime)
        {
            if (eligibleCut(*cut))
            {
                voices.releaseConfigured();
                for (auto *bank : extra)
                    bank->releaseConfigured();
            }
            ++cut;
        }
        for (auto event = i; event != ownEnd; ++event)
            if (event->on)
            {
                const auto &n = channel->notes[event->index];
                if (n.slide || active(n.note, n.placement, n.repeatOrdinal))
                    continue;
                Gate *free = nullptr;
                for (auto &g : gates)
                    if (!g.active)
                    {
                        free = &g;
                        break;
                    }
                if (!free)
                {
                    free = &gates[0];
                    voices.releaseTagged(free->note, free->placement, true, free->repeatOrdinal);
                }
                voices.noteOn(n.pitch, n.velocity, n.note, n.placement, n.start, 0, n.expression,
                              n.repeatOrdinal, n.portamento, n.colourGroup);
                *free = {true, n.note, n.placement, n.start, n.end, n.pitch, n.repeatOrdinal};
            }
        // Native Slide controls are silent and run after ordinary same-time ons.
        // Stable source-identity ordering makes the last tied control deterministic.
        for (auto event = i; event != ownEnd; ++event)
            if (event->on)
            {
                const auto &note = channel->notes[event->index];
                if (note.slide)
                    startSlide(note, eventTime);
            }
        i = ownEnd;
    }
    render(offset + cursor, frames - cursor);
    previousEnd = end;
}
} // namespace vibera
