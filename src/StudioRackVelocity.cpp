// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "Settings.h"
#include "MixerRouting.h"
#include "StepExpression.h"
#include "StepShift.h"
#include "StepRepeat.h"
#include "StepRelease.h"
#include <limits>
#include "../tests/SyntheticDevice.h"
namespace vibera
{
juce::Result Studio::stepVelocityAdmission() const
{
    if (afterPluginState || pendingExport || audio.rendering() || customization.running)
        return juce::Result::fail("Finish the active state capture, export or macro before editing step properties");
    return juce::Result::ok();
}
juce::Result Studio::finishRackVelocity(bool commit)
{
    if (windows[0])
        return static_cast<Editor *>(windows[0]->getContentComponent())->finishVelocityGesture(commit);
    return juce::Result::ok();
}
void Studio::setRackGraphVisible(bool visible)
{
    if (finishRackVelocity().failed())
        return;
    rackGraphVisible = visible;
    if (visible)
        showEditor(0);
    refresh();
}
juce::Result Studio::setRackGraphProperty(const juce::String &property)
{
    if (property != "velocity" && property != "pitch" && property != "pan" && property != "fine" &&
        property != "shift" && property != "repeat" && property != "release" && property != "modX" &&
        property != "modY")
        return juce::Result::fail("stepGraph.invalidProperty: Expected velocity, pitch, pan, fine, shift, "
                                  "repeat, release, modX or modY");
    if (rackGraphProperty == property)
        return juce::Result::ok();
    if (auto finished = finishRackVelocity(); finished.failed())
        return finished;
    if (controller.hasGesture())
        return juce::Result::fail(
            "stepGraph.busy: Finish the owned document preview before changing property");
    rackGraphProperty = property;
    refresh();
    return juce::Result::ok();
}
void Studio::addRackGraphItem(juce::PopupMenu &menu, Id channel)
{
    juce::PopupMenu::Item item("Graph editor");
    item.itemID = 700;
    const auto *target = controller.project.channel(channel);
    item.isEnabled = target && !target->audioClip;
    item.action =
        [safe = juce::Component::SafePointer<Studio>(this), channel, session = controller.projectIdentity]
    {
        if (!safe || safe->controller.projectIdentity != session)
            return;
        const auto *target = safe->controller.project.channel(channel);
        if (!target || target->audioClip)
            return;
        safe->selectRackChannels({channel}, "replace", channel);
        safe->setRackGraphVisible(true);
    };
    menu.addItem(item);
}
juce::var Studio::rackGraphState() const
{
    auto *object = new juce::DynamicObject;
    auto set = [&](const char *key, juce::var value) { object->setProperty(key, value); };
    set("visible", rackGraphVisible);
    const bool pitch = rackGraphProperty == "pitch";
    const auto *expression = rackGraphProperty == "pan"    ? stepExpressionInfo(StepExpression::pan)
                             : rackGraphProperty == "fine" ? stepExpressionInfo(StepExpression::fine)
                             : rackGraphProperty == "modX" ? stepExpressionInfo(StepExpression::modX)
                             : rackGraphProperty == "modY" ? stepExpressionInfo(StepExpression::modY)
                                                           : nullptr;
    const auto *shift = rackGraphProperty == "shift" ? stepShiftInfo() : nullptr;
    const auto *repeat = rackGraphProperty == "repeat" ? stepRepeatInfo() : nullptr;
    const auto *release = rackGraphProperty == "release" ? stepReleaseInfo() : nullptr;
    const bool modulation = rackGraphProperty == "modX" || rackGraphProperty == "modY";
    set("property", rackGraphProperty);
    set("units", release      ? release->units
                 : repeat     ? repeat->units
                 : shift      ? shift->units
                 : expression ? expression->units
                 : pitch      ? "MIDI note number 0-127"
                              : "normalized 0-1");
    set("minimum", release      ? release->minimum
                   : repeat     ? repeat->minimum
                   : shift      ? shift->minimum
                   : expression ? expression->minimum
                                : 0.);
    set("maximum", release      ? release->maximum
                   : repeat     ? repeat->maximum
                   : shift      ? shift->maximum
                   : expression ? expression->maximum
                   : pitch      ? 127.
                                : 1.);
    set("defaultValue", release      ? release->defaultValue
                        : repeat     ? repeat->defaultValue
                        : shift      ? shift->defaultValue
                        : expression ? expression->defaultValue
                        : pitch      ? 60.
                                     : .8);
    set("centered", expression != nullptr);
    set("stepBeats", .25);
    set("pattern", juce::int64(selectedPattern));
    set("channel", juce::int64(selectedChannel));
    const bool uiGesture =
        windows[0] && static_cast<Editor *>(windows[0]->getContentComponent())->velocityGestureActive();
    const bool semanticGesture =
        mcpGesture && (mcpGesture->domain == "stepVelocity" || mcpGesture->domain == "stepGraph") &&
        mcpGesture->state == "pending" && controller.gestureOwner() == mcpGesture->id;
    set("gestureActive", uiGesture || semanticGesture);
    set("uiGestureActive", uiGesture);
    set("semanticGestureActive", semanticGesture);
    if (windows[0])
        if (const auto *graph = static_cast<Editor *>(windows[0]->getContentComponent())->rackVelocityGraph())
        {
            auto rectangle = [](juce::Rectangle<int> area)
            { return juce::Array<juce::var>{area.getX(), area.getY(), area.getWidth(), area.getHeight()}; };
            set("boundsInStudio", rectangle(getLocalArea(graph, graph->getLocalBounds())));
            set("chartBoundsInStudio", rectangle(getLocalArea(graph, graph->chart().toNearestInt())));
            set("columnWidth", graph->width);
            set("firstStep", graph->offset);
            set("rendered", graph->isShowing() && graph->getHeight() > 0);
        }
    set("completionState", "completed");
    const auto *selected = controller.project.channel(selectedChannel);
    const bool localSamplerTarget = selected && !selected->instrument && !selected->audioClip;
    const bool supported = selected && !selected->audioClip && (!expression || localSamplerTarget);
    set("localSamplerTarget", localSamplerTarget);
    juce::Array<juce::var> supportedProperties;
    if (selected && !selected->audioClip)
    {
        supportedProperties.add("velocity");
        supportedProperties.add("pitch");
        if (localSamplerTarget)
        {
            supportedProperties.add("pan");
            supportedProperties.add("fine");
        }
        supportedProperties.add("shift");
        supportedProperties.add("repeat");
        supportedProperties.add("release");
        if (localSamplerTarget)
        {
            supportedProperties.add("modX");
            supportedProperties.add("modY");
        }
    }
    set("supportedProperties", supportedProperties);
    set("supported", supported);
    set("storedEditingEligible", supported);
    set("noteModulationSupported", localSamplerTarget);
    set("noteModulationScope",
        localSamplerTarget
            ? "Original local sampler composition: clamp(global + 2*(note-0.5),0,1). Mod X offsets cutoff; "
              "Mod Y offsets resonance. Filter Off is a no-op. Stored source state, not a voice/DSP ACK."
            : "Mod X/Y stored only; native/VST3 and Audio Clip per-note sampler modulation unsupported.");
    set("noteModulationAcknowledged", juce::var{});
    const juce::String releaseResponse = selected ? noteReleaseAudioResponse(*selected) : "unavailable";
    const bool hostedMidiEligible = supported && selected && bool(selected->instrument);
    set("releaseVelocityAudioResponse", releaseResponse);
    set("appliedEligible", release ? hostedMidiEligible && releaseResponse != "ignored" : supported);
    if (release)
    {
        set("midiExportEligible", supported);
        set("hostedMidiEligible", hostedMidiEligible);
        set("midiDeliveryAcknowledged", juce::var{});
    }
    // Eligibility is not an acknowledgement that the prepared DSP document adopted this edit.
    set("applicationAcknowledged", juce::var{});
    set("applicationScope",
        release
            ? (releaseResponse == "unavailable"
                   ? "MIDI note-off velocity is unavailable for an Audio Clip or missing score Channel."
               : releaseResponse == "ignored"
                   ? "Normalized MIDI note-off velocity / MIDI score export. Stored/export-only for local "
                     "sampler/current native Synth audio, which ignore it. Not ADSR seconds; no delivery or "
                     "DSP ACK."
                   : "Normalized MIDI note-off velocity / MIDI score export. Hosted instrument response is "
                     "instrument dependent/patch defined, not ADSR seconds; no delivery or DSP ACK.")
        : repeat ? "Stored Repeat rate index expands each score-note occurrence; local/native score Channels "
                   "supported, Audio Clips unsupported. Source cells/IDs/extent unchanged; prepared document "
                   "adoption "
                   "is not acknowledged by this readback."
        : shift  ? "Stored positive per-note Shift is applied once after Swing to each score occurrence; "
                   "local/native score Channels supported, Audio Clips unsupported. Raw cells/source extent "
                   "unchanged; "
                   "prepared document adoption is not acknowledged by this readback."
        : modulation ? "Original local sampler cutoff/resonance offsets: clamp(global + 2*(note-0.5),0,1). "
                       "Filter Off is a no-op. Stored source state; no voice or prepared DSP adoption ACK. "
                       "Native/VST3 and Audio Clips unsupported."
        : expression ? "Stored per-note local sampler expression; native/VST3 and Audio Clips unsupported. "
                       "Prepared document adoption is not acknowledged by this readback."
                     : "Existing note velocity/pitch path; no prepared document adoption acknowledgement.");
    set("modes", juce::Array<juce::var>{"set", "scale", "ramp", "reset"});
    const auto *pattern = controller.project.pattern(selectedPattern);
    const auto *channel = controller.project.channel(selectedChannel);
    const auto viewport =
        windows[0] ? static_cast<Editor *>(windows[0]->getContentComponent())->rackViewport() : juce::var{};
    const int first = int(viewport["firstStep"]), page = std::max(1, int(viewport["stepsPerPage"]));
    int eligible = 0;
    juce::Array<juce::var> notes;
    if (pattern && channel && !channel->audioClip)
        for (const auto &note : pattern->notes)
            if (graphStep(note, channel->id, pattern->length))
            {
                ++eligible;
                const int column = int(std::llround(note.beat * 4));
                if (column < first || column >= first + page)
                    continue;
                auto *item = new juce::DynamicObject;
                item->setProperty("id", juce::int64(note.id));
                item->setProperty("column", column);
                item->setProperty("beat", note.beat);
                item->setProperty("velocity", note.velocity);
                item->setProperty("pitch", note.pitch);
                item->setProperty("muted", note.muted);
                item->setProperty("pan", note.pan);
                item->setProperty("fineCents", note.fineCents);
                item->setProperty("modX", note.modX);
                item->setProperty("modY", note.modY);
                item->setProperty("shiftPercent", note.shiftPercent);
                item->setProperty("shiftDelayBeats", noteShiftDelay(note.shiftPercent));
                item->setProperty("releaseVelocity", note.releaseVelocity);
                item->setProperty("releaseMidiVelocity", int(noteReleaseMidiVelocity(note.releaseVelocity)));
                item->setProperty("repeatMode", note.repeatMode);
                const auto *repeatRate = noteRepeatRate(note.repeatMode);
                item->setProperty("repeatRate", repeatRate ? repeatRate->label : "invalid");
                item->setProperty("repeatIntervalBeats", noteRepeatInterval(note.repeatMode));
                item->setProperty("value", release      ? note.releaseVelocity
                                           : repeat     ? double(note.repeatMode)
                                           : shift      ? note.shiftPercent
                                           : expression ? stepExpressionValue(note, expression->property)
                                           : pitch      ? double(note.pitch)
                                                        : double(note.velocity));
                item->setProperty(
                    "midiVelocity",
                    note.velocity <= 0 ? 0 : juce::jlimit(1, 127, juce::roundToInt(note.velocity * 127)));
                notes.add(juce::var(item));
            }
    set("eligibleNoteCount", eligible);
    set("editable", eligible > 0 && supported && stepVelocityAdmission().wasOk());
    if (expression && !supported)
        set("editRefusal", "stepExpression.unsupportedTarget");
    set("notes", notes);
    set("readbackScope", "Visible columns only; vibera_inspect notes gives complete canonical Pattern data");
    set("removedStepsRetained", customization.muteRemovedSteps);
    set("inactivePropertyEditing", true);
    int retained = 0;
    if (pattern)
        for (const auto &note : pattern->notes)
            if (note.channel == selectedChannel && note.step && note.muted)
                ++retained;
    set("retainedInactiveSourceNotes", retained);
    set("scaleZeroAnchor", repeat       ? "additive baseline index translation, clamped0-14"
                           : shift      ? "additive lift; positive anchor uses baseline ratio, clamped0-100"
                           : expression ? "additive baseline offset, clamped to property range"
                           : pitch      ? "baseline semitone offset, clamped0-127"
                                        : "additive lift; positive anchor uses baseline ratio, clamped0-1");
    return juce::var(object);
}
juce::Result Studio::verifyRackVelocity(const juce::File &folder, const juce::File &fixture)
{
    bool observeExpressionCallbacks = false, expressionTimingOpen = false;
    int expressionCallbacks = 0, expressionOverruns = 0, expressionNonfinite = 0, expressionSilent = 0;
    int expressionInvalidSpans = 0, expressionStarts = 0, expressionStops = 0;
    int expressionStartDelta = 0, expressionStopDelta = 0;
    double expressionMaximumMs = 0;
    SyntheticCallbackSpan expressionWorst;
    juce::String expressionTimingMode;
    constexpr double expressionBudgetMs = 1000. * 256. / 48000.;
    auto writeExpressionTiming = [&](bool passed)
    {
        auto *metrics = new juce::DynamicObject;
        auto put = [&](const char *key, juce::var value) { metrics->setProperty(key, value); };
        put("passed", passed);
        put("scope",
            "Warm prepared sampler pan/fine/Mod X/Y and local/native score Shift/Repeat/Release edits and "
            "Undo/Redo; outer virtual "
            "SyntheticDevice::process boundary, not physical hardware");
        put("mode", expressionTimingMode);
        put("frames", 256);
        put("sampleRate", 48000.);
        put("budgetMs", expressionBudgetMs);
        put("callbacks", expressionCallbacks);
        put("overruns", expressionOverruns);
        put("maximumMs", expressionMaximumMs);
        put("nonfiniteBlocks", expressionNonfinite);
        put("silentBlocks", expressionSilent);
        put("invalidSpans", expressionInvalidSpans);
        put("deviceStarts", expressionStartDelta);
        put("deviceStops", expressionStopDelta);
        // CallbackAllocationProbe defines global operators in the separate test
        // executable, not this Studio application. Never invent a zero reading.
        put("callbackAllocations", juce::var{});
        put("callbackDestructions", juce::var{});
        put("heapObservation",
            "Unavailable in this application target; test-executable-only probe not installed");
        put("worstSpan", expressionWorst.timing.describe(expressionWorst.began, expressionWorst.ended));
        put("nativeOsInput", false);
        put("hardware", false);
        put("listening", false);
        return folder.getChildFile("step-expression-" + expressionTimingMode + "-callbacks.json")
            .replaceWithText(juce::JSON::toString(juce::var(metrics), true) + "\n");
    };
    auto fail = [&](const juce::String &message)
    {
        if (expressionTimingOpen && expressionCallbacks > 0)
        {
            const bool retained = writeExpressionTiming(false);
            return juce::Result::fail(
                "Velocity graph: " + message + " callbacks=" + juce::String(expressionCallbacks) + " maxMs=" +
                juce::String(expressionMaximumMs, 6) + " overruns=" + juce::String(expressionOverruns) +
                " nonfinite=" + juce::String(expressionNonfinite) + " silent=" +
                juce::String(expressionSilent) + " metricsRetained=" + juce::String(retained ? 1 : 0));
        }
        return juce::Result::fail("Velocity graph: " + message);
    };
    folder.createDirectory();
    MacroPage shiftMacro(*this);
    if (auto checked = shiftMacro.verifyStepShiftBuilder(); checked.failed())
        return fail(checked.getErrorMessage());
    if (auto checked = shiftMacro.verifyStepRepeatBuilder(); checked.failed())
        return fail(checked.getErrorMessage());
    if (auto checked = shiftMacro.verifyStepReleaseBuilder(); checked.failed())
        return fail(checked.getErrorMessage());
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    customization = Customization::familiarDefaults();
    auto seed = Project::demo();
    auto target = seed.channels.front().id;
    if (fixture != juce::File{})
    {
        Project hosted;
        if (loadProject(fixture, hosted).failed())
            return fail("provider fixture load");
        const auto instrument = std::find_if(hosted.channels.begin(), hosted.channels.end(),
                                             [](const auto &c) { return bool(c.instrument); });
        if (instrument == hosted.channels.end())
            return fail("actual installed instrument missing");
        seed.channels.front().instrument = instrument->instrument;
        seed.channels.front().instrument->id = seed.allocate();
    }
    auto &part = seed.patterns.front();
    part.notes.clear();
    part.length = 20;
    part.autoLength = false; // This held-take editor scenario deliberately keeps its window fixed.
    for (int i = 0; i < 4; ++i)
        part.notes.push_back({seed.allocate(), target, i * .25, .25, 60, (i + 1) * .2f, true});
    part.notes.push_back({seed.allocate(), target, 17, .25, 60, .5f, true});
    part.notes.push_back({seed.allocate(), seed.channels.back().id, 0, 4, 48, .6f, false});
    seed.placements = {{seed.allocate(), part.id, 0, 0, 20, 0}};
    controller.replace(seed);
    selectedPattern = part.id;
    selectedChannel = target;
    selectedChannels = {target};
    selectedNotes.clear();
    selectedClips.clear();
    focus = Scope::rack;
    maximisedEditor = -1;
    setSize(1920, 1040);
    refresh();
    resized();
    auto *editor = static_cast<Editor *>(windows[0]->getContentComponent());
    auto *graph = editor->rackVelocityGraph();
    if (!graph || !keyPressed(juce::KeyPress('K', juce::ModifierKeys::ctrlModifier, 0)) || !rackGraphVisible)
        return fail("actual Ctrl+K toggle");
    setRackGraphVisible(false);
    juce::PopupMenu menu;
    addRackGraphItem(menu, target);
    std::function<void()> openGraph;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().itemID == 700)
            openGraph = it.getItem().action;
    if (!openGraph)
        return fail("actual Graph editor context item");
    openGraph();
    if (!rackGraphVisible || selectedChannel != target)
        return fail("Graph context action");
    auto &manager = audio.engine.getDeviceManager();
    if (!dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice()))
    {
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
    }
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device)
        return fail("no synthetic device");
    juce::AudioBuffer<float> block(2, 256);
    auto tick = [&]
    {
        timerCallback();
        if (!observeExpressionCallbacks)
            device->process(block);
        else
        {
            SyntheticCallbackSpan span;
            span.began = juce::Time::getHighResolutionTicks();
            device->process(block);
            span.ended = juce::Time::getHighResolutionTicks();
            span.timing = device->lastTiming();
            const auto elapsedMs = SyntheticCallbackTiming::milliseconds(span.began, span.ended);
            ++expressionCallbacks;
            expressionOverruns += elapsedMs > expressionBudgetMs;
            expressionInvalidSpans += !span.timing.containedBy(span.began, span.ended);
            if (expressionCallbacks == 1 || elapsedMs > expressionMaximumMs)
            {
                expressionMaximumMs = elapsedMs;
                expressionWorst = span;
            }
            bool nonfinite = false;
            float peak = 0;
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int frame = 0; frame < block.getNumSamples(); ++frame)
                {
                    const auto value = block.getSample(channel, frame);
                    if (!std::isfinite(value))
                        nonfinite = true;
                    else
                        peak = std::max(peak, std::abs(value));
                }
            expressionNonfinite += nonfinite;
            expressionSilent += peak <= .000001f;
            expressionStartDelta = device->starts - expressionStarts;
            expressionStopDelta = device->stops - expressionStops;
        }
        drainMidiTake();
        dispatchMessages(2);
    };
    auto prepare = [&]
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 20000;
        while (needsRebuild && juce::Time::getMillisecondCounter() < deadline)
            tick();
        return !needsRebuild && projectionError.isEmpty();
    };
    if (!prepare())
        return fail("initial projection: " + status);
    auto point = [&](int column, float value)
    {
        auto area = graph->chart();
        return juce::Point<float>{float(graph->left + (column - graph->offset + .5) * graph->width),
                                  area.getBottom() - value * area.getHeight()};
    };
    auto event = [&](juce::Point<float> position, int flags, bool moved)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                                juce::ModifierKeys(flags), 1, 0, 0, 0, 0, graph, graph,
                                juce::Time::getCurrentTime(), position, juce::Time::getCurrentTime(), 1,
                                moved);
    };
    auto down = [&](int column, float value, int flags)
    { graph->mouseDown(event(point(column, value), flags, false)); };
    auto drag = [&](int column, float value, int flags)
    { graph->mouseDrag(event(point(column, value), flags, true)); };
    auto up = [&] { graph->mouseUp(event(point(0, .5f), 0, true)); };
    auto velocity = [&](int index)
    { return controller.project.pattern(selectedPattern)->notes[size_t(index)].velocity; };
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto original = controller.project.state();
    down(0, .3f, left);
    drag(3, .9f, left);
    up();
    if (std::abs(velocity(0) - .3f) > .001 || std::abs(velocity(1) - .5f) > .001 ||
        std::abs(velocity(3) - .9f) > .001)
        return fail("left paint does not fill skipped active columns");
    const auto painted = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("paint drag one Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(painted))
        return fail("paint Redo");
    down(0, .15f, left | juce::ModifierKeys::ctrlModifier);
    drag(0, .3f, left | juce::ModifierKeys::ctrlModifier);
    if (std::abs(velocity(3) - .9f) > .001)
        return fail("UI scale preview compounded");
    keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!controller.project.state().isEquivalentTo(painted) || controller.hasGesture())
        return fail("Escape exact cancel");
    down(0, .2f, juce::ModifierKeys::rightButtonModifier);
    drag(3, .8f, juce::ModifierKeys::rightButtonModifier);
    drag(1, .8f, juce::ModifierKeys::rightButtonModifier);
    up();
    if (std::abs(velocity(1) - .8f) > .001 || std::abs(velocity(3) - .9f) > .001)
        return fail("UI shrinking ramp baseline");
    controller.undoEdit();
    down(0, .5f, left | juce::ModifierKeys::altModifier);
    drag(3, .5f, left | juce::ModifierKeys::altModifier);
    up();
    if (velocity(0) != .8f || velocity(3) != .8f)
        return fail("Alt reset actual default");
    controller.undoEdit();
    const auto noOp = controller.project.state();
    down(6, .7f, left);
    up();
    if (!controller.project.state().isEquivalentTo(noOp))
        return fail("empty column created data");
    auto *propertyPicker = dynamic_cast<juce::ComboBox *>(graph->findChildWithID("rack.graphProperty"));
    if (!propertyPicker)
        return fail("actual graph property selector missing");
    propertyPicker->setSelectedId(2, juce::sendNotificationSync);
    if (rackGraphProperty != "pitch" || rackGraphState()["property"].toString() != "pitch" ||
        rackGraphState()["defaultValue"] != juce::var(60.) ||
        !controller.project.state().isEquivalentTo(noOp))
        return fail("actual Pitch selector/readback changed score or failed");
    auto pitch = [&](int index)
    { return controller.project.pattern(selectedPattern)->notes[size_t(index)].pitch; };
    down(0, 60.f / 127, left);
    drag(3, 72.f / 127, left);
    up();
    if (pitch(0) != 60 || pitch(1) != 64 || pitch(2) != 68 || pitch(3) != 72)
        return fail("actual Pitch paint did not produce literal semitone interpolation");
    const auto pitchPainted = controller.project.state();
    if (controller.undoEditChecked().failed() || !controller.project.state().isEquivalentTo(noOp) ||
        controller.redoEditChecked().failed() || !controller.project.state().isEquivalentTo(pitchPainted))
        return fail("Pitch paint was not one exact Undo/Redo");
    down(0, 64.f / 127, left | juce::ModifierKeys::ctrlModifier);
    drag(0, 67.f / 127, left | juce::ModifierKeys::ctrlModifier);
    if (pitch(0) != 67 || pitch(1) != 71 || pitch(3) != 79 || pitch(4) != 67)
        return fail("Pitch Ctrl transposition compounded or lost offscreen source");
    keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!controller.project.state().isEquivalentTo(pitchPainted) || controller.hasGesture())
        return fail("Pitch Escape did not cancel exactly");
    down(0, 60.f / 127, juce::ModifierKeys::rightButtonModifier);
    drag(3, 72.f / 127, juce::ModifierKeys::rightButtonModifier);
    drag(1, 70.f / 127, juce::ModifierKeys::rightButtonModifier);
    up();
    if (pitch(1) != 70 || pitch(3) != 72 || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(pitchPainted))
        return fail("Pitch shrinking ramp/history failed");
    down(0, .5f, left | juce::ModifierKeys::altModifier);
    drag(3, .5f, left | juce::ModifierKeys::altModifier);
    up();
    if (pitch(0) != 60 || pitch(1) != 60 || pitch(3) != 60 || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(pitchPainted) || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(noOp))
        return fail("Pitch Alt reset/history did not preserve original source");
    const auto fineHistory = controller.undo.getUndoDescriptions();
    const auto fineStart = point(0, .5f);
    const int fineFlags = left | juce::ModifierKeys::shiftModifier;
    graph->mouseDown(event(fineStart, fineFlags, false));
    graph->mouseDrag(
        event(fineStart.translated(0, -graph->chart().getHeight() * 10.f / 127), fineFlags, true));
    up();
    if (pitch(0) != 61 || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(noOp) ||
        controller.undo.getUndoDescriptions() != fineHistory)
        return fail("Shift-fine Pitch must adjust one semitone without a click jump and undo exactly");
    propertyPicker->setSelectedId(1, juce::sendNotificationSync);
    if (rackGraphProperty != "velocity")
        return fail("Return to Velocity selector");
    editor->setRackViewport(0, 64);
    if (graph->offset != int(editor->rackViewport()["firstStep"]))
        return fail("graph horizontal scroll mismatch");
    down(68, .4f, left);
    up();
    if (std::abs(velocity(4) - .4f) > .001)
        return fail("offscreen step target");
    controller.undoEdit();
    editor->setRackViewport(0, 0);
    down(0, .55f, left);
    setRackGraphVisible(false);
    if (controller.hasGesture() || std::abs(velocity(0) - .55f) > .001)
        return fail("hide did not commit owned drag");
    controller.undoEdit();
    setRackGraphVisible(true);
    const auto editorSize = juce::Point<int>{editor->getWidth(), editor->getHeight()};
    editor->setSize(320, 220);
    if (graph->getY() < 108 || graph->getBounds().getBottom() > editor->getHeight() - scrollBarThickness ||
        graph->chart().getHeight() < 40)
        return fail("small Rack graph overlaps rows/gutter or has unusable bars");
    const auto beforeScroll = controller.project.state();
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -.1f;
    graph->mouseWheelMove(event(point(0, .5f), juce::ModifierKeys::shiftModifier, false), wheel);
    if (graph->offset <= 0 || graph->offset != int(editor->rackViewport()["firstStep"]) ||
        !controller.project.state().isEquivalentTo(beforeScroll))
        return fail("actual Graph wheel changed Channel order or missed shared horizontal scroll");
    editor->setSize(editorSize.x, editorSize.y);
    editor->setRackViewport(0, 0);
    // A drop over the graph never replaces the Channel row hidden behind it; it may add a new
    // Channel (the drop-target contract), which this fixture then removes with Undo.
    const auto channelsBeforeDrop = controller.project.channels;
    const auto beforeDrop = controller.project.state();
    editor->filesDroppedWithModifiers({"builtin:snare"}, 60, graph->getY() + 24, {});
    const auto &channelsAfterDrop = controller.project.channels;
    if (channelsAfterDrop.size() < channelsBeforeDrop.size() ||
        !std::equal(channelsBeforeDrop.begin(), channelsBeforeDrop.end(), channelsAfterDrop.begin(),
                    [](const auto &a, const auto &b) { return a.id == b.id && a.source == b.source; }))
        return fail("graph file drop replaced an obscured Channel");
    if (!controller.project.state().isEquivalentTo(beforeDrop))
        controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeDrop))
        return fail("graph file drop did not undo to the prior Project");
    auto parentEvent = [&](juce::Point<float> point, bool moved)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(left), 1, 0, 0, 0, 0, editor, editor,
                                juce::Time::getCurrentTime(), point, juce::Time::getCurrentTime(), 1, moved);
    };
    const auto countBefore = controller.project.pattern(selectedPattern)->notes.size();
    editor->mouseDown(parentEvent({float(graph->left + 5.5 * graph->width), 60.f}, false));
    editor->mouseDrag(
        parentEvent({float(graph->left + 5.5 * graph->width), float(graph->getY() + 30)}, true));
    editor->mouseUp(parentEvent({float(graph->left + 5.5 * graph->width), float(graph->getY() + 30)}, true));
    if (controller.project.pattern(selectedPattern)->notes.size() != countBefore + 1)
        return fail("captured step drag into graph edited a hidden Channel");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeDrop))
        return fail("step drag boundary Undo");
    // Component-only source-expression cases. Preserve the original provider,
    // velocity/pitch, held-take and musical scenarios below without replacing them.
    {
        auto expressionSeed = controller.project;
        const auto local = std::find_if(expressionSeed.channels.begin(), expressionSeed.channels.end(),
                                        [](const auto &c) { return !c.instrument && !c.audioClip; });
        if (local == expressionSeed.channels.end())
            return fail("Expression fixture requires an actual local sampler Channel");
        const auto expressionChannel = local->id;
        auto *expressionPattern = expressionSeed.pattern(selectedPattern);
        std::erase_if(expressionPattern->notes,
                      [&](const auto &n) { return n.channel == expressionChannel; });
        std::vector<Id> expressionIds;
        for (int i = 0; i < 5; ++i)
        {
            Note n{expressionSeed.allocate(),
                   expressionChannel,
                   i == 4 ? .25 : i * .25,
                   .25,
                   60 + i,
                   i == 4 ? 0.f : .7f,
                   true,
                   i == 4};
            n.pan = i == 4 ? .6 : (i == 0 ? -.8 : i == 1 ? -.4 : i == 2 ? .4 : .8);
            n.fineCents = n.pan * 100.;
            const double modValues[]{.1, .3, .7, .9, .8}; // Independent literal source fixtures.
            n.modX = modValues[i];
            n.modY = modValues[i];
            expressionIds.push_back(n.id);
            expressionPattern->notes.push_back(n);
        }
        // Retain an off-grid step and an out-of-window source, never target them.
        expressionPattern->notes.push_back(
            {expressionSeed.allocate(), expressionChannel, .125, .25, 72, 0.f, true, true});
        expressionPattern->notes.push_back(
            {expressionSeed.allocate(), expressionChannel, 21., .25, 74, .5f, true});
        controller.replace(expressionSeed);
        selectedChannel = expressionChannel;
        selectedChannels = {expressionChannel};
        refresh();
        editor->setRackViewport(0, 0);
        const auto expressionBaseline = controller.project;
        const auto baselineState = expressionBaseline.state();
        auto expressionValue = [&](int index, StepExpression property)
        {
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (n.id == expressionIds[size_t(index)])
                {
                    // Independently verify the generic accessor against each actual source field.
                    const double raw = property == StepExpression::pan    ? n.pan
                                       : property == StepExpression::fine ? n.fineCents
                                       : property == StepExpression::modX ? n.modX
                                                                          : n.modY;
                    return stepExpressionValue(n, property) == raw ? raw
                                                                   : std::numeric_limits<double>::quiet_NaN();
                }
            return std::numeric_limits<double>::quiet_NaN();
        };
        auto untouched = [&](StepExpression property)
        {
            auto normalized = controller.project;
            for (auto &n : normalized.pattern(selectedPattern)->notes)
                for (const auto &before : expressionBaseline.pattern(selectedPattern)->notes)
                    if (n.id == before.id &&
                        std::find(expressionIds.begin(), expressionIds.end(), n.id) != expressionIds.end())
                    {
                        if (!setStepExpressionValue(n, property, stepExpressionValue(before, property)))
                            return false;
                    }
            return normalized.state().isEquivalentTo(baselineState);
        };
        struct ExpressionCase
        {
            StepExpression property;
            int picker;
            const char *name;
            double minimum, maximum, neutral;
        };
        const ExpressionCase expressionCases[]{{StepExpression::pan, 3, "pan", -1, 1, 0},
                                               {StepExpression::fine, 4, "fine", -100, 100, 0},
                                               {StepExpression::modX, 8, "modX", 0, 1, .5},
                                               {StepExpression::modY, 9, "modY", 0, 1, .5}};
        for (const auto &example : expressionCases)
        {
            const auto property = example.property;
            const bool isFine = property == StepExpression::fine;
            const bool isMod = property == StepExpression::modX || property == StepExpression::modY;
            const double unit = isFine ? 100. : 1.; // Original signed test ranges remain literal.
            propertyPicker->setSelectedId(example.picker, juce::sendNotificationSync);
            const auto readback = rackGraphState();
            if (rackGraphProperty != example.name || !bool(readback["localSamplerTarget"]) ||
                !bool(readback["supported"]) || !bool(readback["editable"]) ||
                double(readback["minimum"]) != example.minimum ||
                double(readback["maximum"]) != example.maximum ||
                double(readback["defaultValue"]) != example.neutral || !bool(readback["centered"]) ||
                !readback["applicationAcknowledged"].isVoid() || int(readback["eligibleNoteCount"]) != 5)
                return fail("Expression Graph picker/range/support/readback");
            if (isMod)
            {
                if (!bool(readback["noteModulationSupported"]) ||
                    !readback["noteModulationAcknowledged"].isVoid() ||
                    !readback["noteModulationScope"].toString().contains(
                        "clamp(global + 2*(note-0.5),0,1)") ||
                    !readback["noteModulationScope"].toString().contains("Filter Off is a no-op"))
                    return fail("Mod X/Y capability/composition must be source metadata, not a DSP ACK");
                const auto rows = readback["notes"];
                if (!rows.getArray())
                    return fail("Mod X/Y source readback rows unavailable");
                const double expected[]{.1, .3, .7, .9, .8};
                for (size_t i = 0; i < expressionIds.size(); ++i)
                {
                    bool found = false;
                    for (const auto &row : *rows.getArray())
                        if (Id(juce::int64(row["id"])) == expressionIds[i])
                            found = std::abs(double(row["value"]) - expected[i]) < .0001 &&
                                    double(row["modX"]) == double(row["modY"]) &&
                                    std::abs(double(row[example.name]) - expected[i]) < .0001;
                    if (!found)
                        return fail("Mod X/Y readback lost actual raw source IDs/neutral-range values");
                }
            }
            auto nearLiteral = [&](int index, double expected)
            { return std::abs(expressionValue(index, property) - expected) < unit * .0001; };
            auto expressionNear = [&](int index, double expected)
            { return nearLiteral(index, expected * unit); };
            down(0, .25f, left);
            drag(3, .75f, left);
            up();
            if ((isMod
                     ? !nearLiteral(0, .25) || !nearLiteral(1, 5. / 12) || !nearLiteral(2, 7. / 12) ||
                           !nearLiteral(3, .75) || !nearLiteral(4, 5. / 12)
                     : !expressionNear(0, -.5) || !expressionNear(1, -1. / 6) || !expressionNear(2, 1. / 6) ||
                           !expressionNear(3, .5) || !expressionNear(4, -1. / 6)) ||
                !untouched(property))
                return fail("Signed paint interpolation lost source/chord/muted/zero identity");
            const auto paintedExpression = controller.project.state();
            if (controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(baselineState) ||
                controller.redoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(paintedExpression) ||
                controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(baselineState))
                return fail("Signed paint must own one exact Undo/Redo");
            down(0, .2f, left | juce::ModifierKeys::ctrlModifier);
            drag(0, .4f, left | juce::ModifierKeys::ctrlModifier);
            if ((isMod ? !nearLiteral(0, .4) || !nearLiteral(1, .6) || !nearLiteral(2, 1.) ||
                             !nearLiteral(3, 1.) || !nearLiteral(4, 1.)
                       : !expressionNear(0, -.2) || !expressionNear(1, .2) || !expressionNear(2, 1.) ||
                             !expressionNear(3, 1.) || !expressionNear(4, 1.)) ||
                !untouched(property))
                return fail("Signed Ctrl offset compounded baseline or failed clamp/chord");
            keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            if (controller.hasGesture() || !controller.project.state().isEquivalentTo(baselineState))
                return fail("Signed Escape exact cancellation");
            down(0, .5f, left | juce::ModifierKeys::altModifier);
            drag(3, .5f, left | juce::ModifierKeys::altModifier);
            up();
            for (int i = 0; i < 5; ++i)
                if (!nearLiteral(i, example.neutral))
                    return fail("Expression Alt reset must use its exact neutral for the whole source chord");
            if (!untouched(property) || controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(baselineState))
                return fail("Signed reset exact source/history");
            const auto finePoint = point(0, .5f);
            graph->mouseDown(event(finePoint, fineFlags, false));
            if (isMod ? !nearLiteral(0, .1) : !expressionNear(0, -.8))
                return fail("Signed Shift-fine click jumped from its actual source value");
            graph->mouseDrag(
                event(finePoint.translated(0, -graph->chart().getHeight() * .25f), fineFlags, true));
            up();
            if ((isMod ? !nearLiteral(0, .125) : !expressionNear(0, -.75)) || !untouched(property) ||
                controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(baselineState))
                return fail("Signed Shift fine delta/history");
            down(3, 1.f, juce::ModifierKeys::rightButtonModifier);
            drag(0, 0.f, juce::ModifierKeys::rightButtonModifier);
            drag(1, .25f, juce::ModifierKeys::rightButtonModifier);
            up();
            if ((isMod ? !nearLiteral(0, .1) || !nearLiteral(1, .25) || !nearLiteral(2, .625) ||
                             !nearLiteral(3, 1.) || !nearLiteral(4, .25)
                       : !expressionNear(0, -.8) || !expressionNear(1, -.5) || !expressionNear(2, .25) ||
                             !expressionNear(3, 1.) || !expressionNear(4, -.5)) ||
                !untouched(property))
                return fail("Signed reverse/shrinking ramp must restore outside baseline");
            juce::PNGImageFormat expressionPng;
            auto stream =
                folder.getChildFile(juce::String(example.name) + "-graph-component.png").createOutputStream();
            if (!stream)
                return fail("Signed Graph component PNG stream");
            stream->setPosition(0);
            stream->truncate();
            if (!expressionPng.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                                  *stream))
                return fail("Signed Graph component PNG write");
            stream.reset();
            if (controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(baselineState))
                return fail("Signed ramp Undo");
        }
        // Shift edits the raw source cell; visual columns do not follow its
        // delayed playback occurrence. Keep the signed-property cases intact.
        propertyPicker->setSelectedId(5, juce::sendNotificationSync);
        const auto shiftReadback = rackGraphState();
        if (rackGraphProperty != "shift" || !bool(shiftReadback["supported"]) ||
            shiftReadback["units"].toString() != "percent" || double(shiftReadback["minimum"]) != 0. ||
            double(shiftReadback["maximum"]) != 100. || double(shiftReadback["defaultValue"]) != 0. ||
            bool(shiftReadback["centered"]) || !shiftReadback["applicationAcknowledged"].isVoid())
            return fail("Shift picker/source range/readback");
        auto shiftValue = [&](int index)
        {
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (n.id == expressionIds[size_t(index)])
                    return n.shiftPercent;
            return std::numeric_limits<double>::quiet_NaN();
        };
        auto nearShift = [&](int index, double value) { return std::abs(shiftValue(index) - value) < .0001; };
        auto shiftOnly = [&]
        {
            auto normalized = controller.project;
            for (auto &n : normalized.pattern(selectedPattern)->notes)
                if (std::find(expressionIds.begin(), expressionIds.end(), n.id) != expressionIds.end())
                    n.shiftPercent = 0;
            return normalized.state().isEquivalentTo(baselineState);
        };
        down(0, .2f, left);
        drag(3, .8f, left);
        up();
        if (!nearShift(0, 20) || !nearShift(1, 40) || !nearShift(2, 60) || !nearShift(3, 80) ||
            !nearShift(4, 40) || !shiftOnly())
            return fail("Shift paint/raw source/chord/muted-zero preservation");
        const auto shiftPainted = controller.project.state();
        if (controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(baselineState) ||
            controller.redoEditChecked().failed() || !controller.project.state().isEquivalentTo(shiftPainted))
            return fail("Shift paint one exact Undo/Redo");
        const double expectedDelays[] = {.0495, .099, .1485, .198, .099};
        const auto shiftRows = rackGraphState()["notes"];
        if (!shiftRows.getArray())
            return fail("Shift source readback array missing");
        for (int i = 0; i < 5; ++i)
        {
            bool found = false;
            for (const auto &row : *shiftRows.getArray())
                if (juce::int64(row["id"]) == expressionIds[size_t(i)])
                {
                    found = true;
                    if (std::abs(double(row["shiftDelayBeats"]) - expectedDelays[i]) > .0000001 ||
                        std::abs(double(row["shiftPercent"]) - shiftValue(i)) > .0001 ||
                        std::abs(double(row["beat"]) - (i == 4 ? .25 : i * .25)) > .0000001)
                        return fail("Shift source/readback delay moved logical cells or used a DSP ACK");
                }
            if (!found)
                return fail("Shift source readback lost muted/zero chord identity");
        }
        const auto shiftFile = folder.getChildFile("shift-graph-source.vibra");
        Project reopenedShift;
        if (saveProject(controller.project, shiftFile).failed() ||
            loadProject(shiftFile, reopenedShift).failed() ||
            !reopenedShift.state().isEquivalentTo(shiftPainted))
            return fail("Shift populated save/reopen exact stored timing/source");
        juce::PNGImageFormat shiftPng;
        auto shiftStream = folder.getChildFile("shift-graph-component.png").createOutputStream();
        if (!shiftStream)
            return fail("Shift Graph component PNG stream");
        shiftStream->setPosition(0);
        shiftStream->truncate();
        if (!shiftPng.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                         *shiftStream))
            return fail("Shift Graph component PNG write");
        shiftStream.reset();
        down(0, .4f, left | juce::ModifierKeys::ctrlModifier);
        drag(0, .3f, left | juce::ModifierKeys::ctrlModifier);
        if (!nearShift(0, 30) || !nearShift(1, 60) || !nearShift(2, 90) || !nearShift(3, 100) ||
            !nearShift(4, 60) || !shiftOnly())
            return fail("Shift Ctrl ratio compounded baseline or failed clamp");
        keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        if (controller.hasGesture() || !controller.project.state().isEquivalentTo(shiftPainted))
            return fail("Shift Escape exact source/history Cancel");
        down(0, .5f, left | juce::ModifierKeys::altModifier);
        drag(3, .5f, left | juce::ModifierKeys::altModifier);
        up();
        for (int i = 0; i < 5; ++i)
            if (!nearShift(i, 0))
                return fail("Shift Alt reset must use zero percent");
        if (controller.undoEditChecked().failed() || !controller.project.state().isEquivalentTo(shiftPainted))
            return fail("Shift reset Undo");
        const auto shiftFinePoint = point(0, .5f);
        graph->mouseDown(event(shiftFinePoint, fineFlags, false));
        if (!nearShift(0, 20))
            return fail("Shift fine click jumped from stored percent");
        graph->mouseDrag(
            event(shiftFinePoint.translated(0, -graph->chart().getHeight() * .1f), fineFlags, true));
        up();
        if (!nearShift(0, 21) || !shiftOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(shiftPainted))
            return fail("Shift fine one-percent delta/history");
        down(0, .1f, juce::ModifierKeys::rightButtonModifier);
        drag(3, .7f, juce::ModifierKeys::rightButtonModifier);
        drag(1, .5f, juce::ModifierKeys::rightButtonModifier);
        up();
        if (!nearShift(0, 10) || !nearShift(1, 50) || !nearShift(2, 60) || !nearShift(3, 80) ||
            !nearShift(4, 50) || !shiftOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(shiftPainted) ||
            controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(baselineState))
            return fail("Shift shrinking ramp preserves outside baseline/raw cells and exact Undo");
        down(0, .2f, left | juce::ModifierKeys::ctrlModifier);
        drag(0, .3f, left | juce::ModifierKeys::ctrlModifier);
        for (int i = 0; i < 5; ++i)
            if (!nearShift(i, 30))
                return fail("Shift zero-anchor Ctrl lift must use its immutable zero baseline");
        keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        if (controller.hasGesture() || !controller.project.state().isEquivalentTo(baselineState))
            return fail("Shift zero-anchor lift Cancel");
        propertyPicker->setSelectedId(6, juce::sendNotificationSync);
        const auto repeatReadback = rackGraphState();
        if (rackGraphProperty != "repeat" || !bool(repeatReadback["supported"]) ||
            repeatReadback["units"].toString() != "rate index" || double(repeatReadback["minimum"]) != 0. ||
            double(repeatReadback["maximum"]) != 14. || double(repeatReadback["defaultValue"]) != 0. ||
            bool(repeatReadback["centered"]) || !repeatReadback["applicationAcknowledged"].isVoid())
            return fail("Repeat picker/index range/support/readback");
        auto repeatValue = [&](int index)
        {
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (n.id == expressionIds[size_t(index)])
                    return n.repeatMode;
            return -1;
        };
        auto repeatOnly = [&]
        {
            auto normalized = controller.project;
            for (auto &n : normalized.pattern(selectedPattern)->notes)
                if (std::find(expressionIds.begin(), expressionIds.end(), n.id) != expressionIds.end())
                    n.repeatMode = 0;
            return normalized.state().isEquivalentTo(baselineState);
        };
        down(0, 2.f / 14, left);
        drag(3, 1.f, left);
        up();
        if (repeatValue(0) != 2 || repeatValue(1) != 6 || repeatValue(2) != 10 || repeatValue(3) != 14 ||
            repeatValue(4) != 6 || !repeatOnly())
            return fail("Repeat literal paint/source chord/muted-zero/other fields");
        const auto repeatPainted = controller.project.state();
        graph->mouseMove(event(point(3, .5f), 0, false));
        if (graph->repeatStepReadout(0) != "Step 1: 1/8 dotted" ||
            graph->repeatStepReadout(1) != "Step 2: 1/8 triplet" ||
            !graph->getTooltip().startsWith("Step 4: 1/64 triplet | ") ||
            !controller.project.state().isEquivalentTo(repeatPainted))
            return fail("Repeat musical-rate readout/hover must inspect current source without editing");
        CommandArgs mixedRepeat;
        mixedRepeat.pattern = selectedPattern;
        mixedRepeat.channel = selectedChannel;
        mixedRepeat.ids = {expressionIds[4]};
        mixedRepeat.value = 4;
        if (controller.execute("steps.repeat", mixedRepeat).failed() ||
            graph->repeatStepReadout(1) != "Step 2: Mixed rates" || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(repeatPainted))
            return fail("Repeat chord readout must not mislabel mixed source rates");
        if (controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(baselineState) ||
            controller.redoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(repeatPainted))
            return fail("Repeat paint exactly one Undo/Redo");
        const char *expectedRepeatLabels[] = {"1/8 dotted", "1/8 triplet", "1/32", "1/64 triplet",
                                              "1/8 triplet"};
        const double expectedRepeatIntervals[] = {.75, 1. / 3, .125, 1. / 24, 1. / 3};
        const auto repeatRows = rackGraphState()["notes"];
        if (!repeatRows.getArray())
            return fail("Repeat source readback array missing");
        for (int i = 0; i < 5; ++i)
        {
            bool found = false;
            for (const auto &row : *repeatRows.getArray())
                if (juce::int64(row["id"]) == expressionIds[size_t(i)])
                {
                    found = true;
                    if (row["repeatRate"].toString() != expectedRepeatLabels[i] ||
                        std::abs(double(row["repeatIntervalBeats"]) - expectedRepeatIntervals[i]) >
                            .0000001 ||
                        int(row["repeatMode"]) != repeatValue(i) ||
                        std::abs(double(row["beat"]) - (i == 4 ? .25 : i * .25)) > .0000001)
                        return fail("Repeat rate/interval/source logical-cell readback");
                }
            if (!found)
                return fail("Repeat source readback lost muted/zero chord identity");
        }
        const auto repeatFile = folder.getChildFile("repeat-graph-source.vibra");
        Project reopenedRepeat;
        if (saveProject(controller.project, repeatFile).failed() ||
            loadProject(repeatFile, reopenedRepeat).failed() ||
            !reopenedRepeat.state().isEquivalentTo(repeatPainted))
            return fail("Repeat populated save/reopen preserves source/IDs and all sibling fields");
        juce::PNGImageFormat repeatPng;
        auto repeatStream = folder.getChildFile("repeat-graph-component.png").createOutputStream();
        if (!repeatStream)
            return fail("Repeat Graph component PNG stream");
        repeatStream->setPosition(0);
        repeatStream->truncate();
        if (!repeatPng.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                          *repeatStream))
            return fail("Repeat Graph component PNG write");
        repeatStream.reset();
        down(0, 4.f / 14, left | juce::ModifierKeys::ctrlModifier);
        drag(0, 0.f, left | juce::ModifierKeys::ctrlModifier);
        if (repeatValue(0) != 0 || repeatValue(1) != 4 || repeatValue(2) != 8 || repeatValue(3) != 12 ||
            repeatValue(4) != 4 || !repeatOnly())
            return fail("Repeat Ctrl lower clamp/negative baseline index translation");
        drag(0, 3.f / 14, left | juce::ModifierKeys::ctrlModifier);
        if (repeatValue(0) != 3 || repeatValue(1) != 7 || repeatValue(2) != 11 || repeatValue(3) != 14 ||
            repeatValue(4) != 7 || !repeatOnly())
            return fail("Repeat Ctrl additive index translation compounded baseline or failed clamp");
        keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        if (controller.hasGesture() || !controller.project.state().isEquivalentTo(repeatPainted))
            return fail("Repeat Escape exact Cancel");
        down(0, .5f, left | juce::ModifierKeys::altModifier);
        drag(3, .5f, left | juce::ModifierKeys::altModifier);
        up();
        for (int i = 0; i < 5; ++i)
            if (repeatValue(i) != 0)
                return fail("Repeat Alt reset must select Off0");
        const auto offRows = rackGraphState()["notes"];
        if (!offRows.getArray())
            return fail("Repeat Off readback array missing");
        for (const auto &row : *offRows.getArray())
            if (std::find(expressionIds.begin(), expressionIds.end(), Id(juce::int64(row["id"]))) !=
                expressionIds.end())
                if (row["repeatRate"].toString() != "Off" || double(row["repeatIntervalBeats"]) != 0.)
                    return fail("Repeat Off must expose zero interval source metadata");
        if (!repeatOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(repeatPainted))
            return fail("Repeat reset Undo/source preservation");
        const auto repeatFinePoint = point(0, .5f);
        graph->mouseDown(event(repeatFinePoint, fineFlags, false));
        if (repeatValue(0) != 2)
            return fail("Repeat fine click jumped from stored index");
        graph->mouseDrag(
            event(repeatFinePoint.translated(0, -graph->chart().getHeight() * 10.f / 14), fineFlags, true));
        up();
        if (repeatValue(0) != 3 || !repeatOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(repeatPainted))
            return fail("Repeat Shift-fine one-index delta/history");
        down(3, 12.f / 14, juce::ModifierKeys::rightButtonModifier);
        drag(0, 0.f, juce::ModifierKeys::rightButtonModifier);
        drag(1, 3.f / 14, juce::ModifierKeys::rightButtonModifier);
        up();
        if (repeatValue(0) != 2 || repeatValue(1) != 3 || repeatValue(2) != 8 || repeatValue(3) != 12 ||
            repeatValue(4) != 3 || !repeatOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(repeatPainted) ||
            controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(baselineState))
            return fail("Repeat reverse/shrinking ramp restores outside baseline and exact Undo");
        propertyPicker->setSelectedId(7, juce::sendNotificationSync);
        const auto releaseReadback = rackGraphState();
        graph->mouseMove(event(point(0, .5f), 0, false));
        if (rackGraphProperty != "release" || !bool(releaseReadback["supported"]) ||
            releaseReadback["units"].toString() != "normalized" || double(releaseReadback["minimum"]) != 0. ||
            double(releaseReadback["maximum"]) != 1. || double(releaseReadback["defaultValue"]) != 0. ||
            releaseReadback["releaseVelocityAudioResponse"].toString() != "ignored" ||
            bool(releaseReadback["appliedEligible"]) || bool(releaseReadback["hostedMidiEligible"]) ||
            !bool(releaseReadback["midiExportEligible"]) ||
            !releaseReadback["applicationAcknowledged"].isVoid() ||
            !releaseReadback["midiDeliveryAcknowledged"].isVoid() ||
            !releaseReadback["applicationScope"].toString().contains("Stored/export-only") ||
            !propertyPicker->getText().contains("MIDI") || !graph->getTooltip().contains("MIDI note-off") ||
            !graph->getTooltip().contains("not ADSR"))
            return fail(
                "Release selector/readback must honestly expose sampler stored/export-only note-off data");
        auto releaseValue = [&](int index)
        {
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (n.id == expressionIds[size_t(index)])
                    return n.releaseVelocity;
            return std::numeric_limits<double>::quiet_NaN();
        };
        auto releaseNear = [&](int index, double value)
        { return std::abs(releaseValue(index) - value) < .0001; };
        auto releaseOnly = [&]
        {
            auto normalized = controller.project;
            for (auto &n : normalized.pattern(selectedPattern)->notes)
                if (std::find(expressionIds.begin(), expressionIds.end(), n.id) != expressionIds.end())
                    n.releaseVelocity = 0;
            return normalized.state().isEquivalentTo(baselineState);
        };
        down(0, .25f, left);
        drag(3, .75f, left);
        up();
        if (!releaseNear(0, .25) || !releaseNear(1, 5. / 12) || !releaseNear(2, 7. / 12) ||
            !releaseNear(3, .75) || !releaseNear(4, 5. / 12) || !releaseOnly())
            return fail("Release literal paint/chord/muted-zero and sibling source preservation");
        const auto releasePainted = controller.project.state();
        if (controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(baselineState) ||
            controller.redoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(releasePainted))
            return fail("Release paint must own exactly one Undo/Redo");
        const int expectedReleaseBytes[] = {32, 53, 74, 95, 53};
        const auto releaseRows = rackGraphState()["notes"];
        if (!releaseRows.getArray())
            return fail("Release source readback array missing");
        for (int i = 0; i < 5; ++i)
        {
            bool found = false;
            for (const auto &row : *releaseRows.getArray())
                if (juce::int64(row["id"]) == expressionIds[size_t(i)])
                {
                    found = true;
                    if (std::abs(double(row["releaseVelocity"]) - releaseValue(i)) > .0001 ||
                        int(row["releaseMidiVelocity"]) != expectedReleaseBytes[i] ||
                        std::abs(double(row["beat"]) - (i == 4 ? .25 : i * .25)) > .0000001)
                        return fail("Release normalized/rounded note-off/raw logical-cell readback");
                }
            if (!found)
                return fail("Release source readback lost muted/zero chord identity");
        }
        const auto releaseFile = folder.getChildFile("release-graph-source.vibra");
        Project reopenedRelease;
        if (saveProject(controller.project, releaseFile).failed() ||
            loadProject(releaseFile, reopenedRelease).failed() ||
            !reopenedRelease.state().isEquivalentTo(releasePainted))
            return fail("Release populated save/reopen preserves note-off source and sibling fields");
        juce::PNGImageFormat releasePng;
        auto releaseStream = folder.getChildFile("release-graph-component.png").createOutputStream();
        if (!releaseStream)
            return fail("Release Graph component PNG stream");
        releaseStream->setPosition(0);
        releaseStream->truncate();
        if (!releasePng.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                           *releaseStream))
            return fail("Release Graph component PNG write");
        releaseStream.reset();
        down(0, .5f, left | juce::ModifierKeys::ctrlModifier);
        drag(0, .375f, left | juce::ModifierKeys::ctrlModifier);
        if (!releaseNear(0, .375) || !releaseNear(1, .625) || !releaseNear(2, .875) || !releaseNear(3, 1.) ||
            !releaseNear(4, .625) || !releaseOnly())
            return fail("Release Ctrl positive ratio compounded baseline or failed clamp");
        keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        if (controller.hasGesture() || !controller.project.state().isEquivalentTo(releasePainted))
            return fail("Release Escape exact Cancel");
        down(0, .5f, left | juce::ModifierKeys::altModifier);
        drag(3, .5f, left | juce::ModifierKeys::altModifier);
        up();
        for (int i = 0; i < 5; ++i)
            if (!releaseNear(i, 0))
                return fail("Release Alt reset must preserve legacy neutral0");
        const auto zeroReleaseRows = rackGraphState()["notes"];
        if (!zeroReleaseRows.getArray())
            return fail("Release reset readback array missing");
        for (const auto &row : *zeroReleaseRows.getArray())
            if (std::find(expressionIds.begin(), expressionIds.end(), Id(juce::int64(row["id"]))) !=
                expressionIds.end())
                if (int(row["releaseMidiVelocity"]) != 0)
                    return fail("Release zero must remain note-off byte0, not a minimum positive velocity");
        if (!releaseOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(releasePainted))
            return fail("Release reset exact Undo");
        const auto releaseFinePoint = point(0, .5f);
        graph->mouseDown(event(releaseFinePoint, fineFlags, false));
        if (!releaseNear(0, .25))
            return fail("Release fine click jumped from stored normalized value");
        graph->mouseDrag(
            event(releaseFinePoint.translated(0, -graph->chart().getHeight() * .1f), fineFlags, true));
        up();
        if (!releaseNear(0, .26) || !releaseOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(releasePainted))
            return fail("Release Shift-fine normalized delta/history");
        down(3, .9f, juce::ModifierKeys::rightButtonModifier);
        drag(0, .1f, juce::ModifierKeys::rightButtonModifier);
        drag(1, .3f, juce::ModifierKeys::rightButtonModifier);
        up();
        if (!releaseNear(0, .25) || !releaseNear(1, .3) || !releaseNear(2, .6) || !releaseNear(3, .9) ||
            !releaseNear(4, .3) || !releaseOnly() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(releasePainted) ||
            controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(baselineState))
            return fail("Release reverse/shrinking ramp restores outside baseline and exact history");
        down(0, .2f, left | juce::ModifierKeys::ctrlModifier);
        drag(0, .3f, left | juce::ModifierKeys::ctrlModifier);
        for (int i = 0; i < 5; ++i)
            if (!releaseNear(i, .3))
                return fail("Release zero-anchor Ctrl lift must use immutable zero baseline");
        keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        if (controller.hasGesture() || !controller.project.state().isEquivalentTo(baselineState))
            return fail("Release zero-anchor lift exact Cancel");
        if (fixture != juce::File{})
        {
            selectedChannel = target; // The actual installed native instrument, not a mock descriptor.
            selectedChannels = {target};
            refresh();
            propertyPicker->setSelectedId(5, juce::sendNotificationSync);
            const auto nativeShiftBefore = controller.project.state();
            const auto nativeShiftReadback = rackGraphState();
            down(0, .4f, left);
            drag(3, .7f, left);
            up();
            if (!bool(nativeShiftReadback["supported"]) || !bool(nativeShiftReadback["editable"]) ||
                bool(nativeShiftReadback["localSamplerTarget"]) || status != "Ready")
                return fail("Actual native/VST3 Shift must remain available without pan/fine privilege");
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (n.channel == target && n.step && (n.beat == 0. || n.beat == .75))
                    if (std::abs(n.shiftPercent - (n.beat == 0. ? 40. : 70.)) > .0001)
                        return fail("Actual native/VST3 raw Shift UI source value");
            if (controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(nativeShiftBefore))
                return fail("Actual native/VST3 Shift UI exact Undo");
            propertyPicker->setSelectedId(6, juce::sendNotificationSync);
            const auto nativeRepeatBefore = controller.project.state();
            const auto nativeRepeatReadback = rackGraphState();
            down(0, 10.f / 14, left);
            drag(3, 13.f / 14, left);
            up();
            if (!bool(nativeRepeatReadback["supported"]) || !bool(nativeRepeatReadback["editable"]) ||
                bool(nativeRepeatReadback["localSamplerTarget"]) || status != "Ready")
                return fail("Actual native/VST3 Repeat remains available without pan/fine privilege");
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (n.channel == target && n.step && (n.beat == 0. || n.beat == .75))
                    if (n.repeatMode != (n.beat == 0. ? 10 : 13))
                        return fail("Actual native/VST3 Repeat UI source index");
            if (controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(nativeRepeatBefore))
                return fail("Actual native/VST3 Repeat UI exact Undo");
            propertyPicker->setSelectedId(7, juce::sendNotificationSync);
            const auto nativeReleaseBefore = controller.project.state();
            const auto nativeReleaseReadback = rackGraphState();
            const auto nativeReleaseResponse =
                nativeReleaseReadback["releaseVelocityAudioResponse"].toString();
            graph->mouseMove(event(point(0, .5f), 0, false));
            down(0, .2f, left);
            drag(3, .8f, left);
            up();
            if (!bool(nativeReleaseReadback["supported"]) || !bool(nativeReleaseReadback["editable"]) ||
                !bool(nativeReleaseReadback["hostedMidiEligible"]) ||
                !bool(nativeReleaseReadback["midiExportEligible"]) ||
                bool(nativeReleaseReadback["localSamplerTarget"]) ||
                (nativeReleaseResponse != "ignored" && nativeReleaseResponse != "instrument_defined") ||
                bool(nativeReleaseReadback["appliedEligible"]) != (nativeReleaseResponse != "ignored") ||
                !nativeReleaseReadback["applicationAcknowledged"].isVoid() ||
                !nativeReleaseReadback["midiDeliveryAcknowledged"].isVoid() ||
                !graph->getTooltip().contains("MIDI note-off") || status != "Ready")
                return fail("Native Release must expose note-off eligibility without a universal "
                            "audio-response claim");
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (n.channel == target && n.step && (n.beat == 0. || n.beat == .75))
                    if (std::abs(n.releaseVelocity - (n.beat == 0. ? .2 : .8)) > .0001)
                        return fail("Actual native Release UI source normalized value");
            if (controller.undoEditChecked().failed() ||
                !controller.project.state().isEquivalentTo(nativeReleaseBefore))
                return fail("Native Release UI exact Undo");
            for (int property : {3, 4, 8, 9})
            {
                propertyPicker->setSelectedId(property, juce::sendNotificationSync);
                const auto before = controller.project.state();
                const auto history = controller.undo.getUndoDescriptions();
                const auto readback = rackGraphState();
                down(0, .75f, left);
                up();
                if (bool(readback["supported"]) || bool(readback["editable"]) ||
                    bool(readback["localSamplerTarget"]) || controller.hasGesture() ||
                    ((property == 8 || property == 9) &&
                     (bool(readback["noteModulationSupported"]) ||
                      !readback["noteModulationAcknowledged"].isVoid())) ||
                    !controller.project.state().isEquivalentTo(before) ||
                    history != controller.undo.getUndoDescriptions() ||
                    !status.startsWith("stepExpression.unsupportedTarget"))
                    return fail(
                        "Actual native/VST3 Graph expression edit must refuse without mutation/history");
            }
        }
        propertyPicker->setSelectedId(1, juce::sendNotificationSync);
        status = "Ready";
        selectedChannel = target;
        selectedChannels = {target};
        // The immediately following original held/provider loop restores its
        // unchanged seed and uses its original preparation deadline.
        folder.getChildFile("step-expression-component.txt")
            .replaceWithText(
                "PASS actual local sampler Graph pan/fine/Mod X/Mod Y picker, paint, neutral reset, fine "
                "delta, baseline "
                "offset, "
                "reverse/shrinking ramp, exact source/one Undo/Redo/Escape and readback. Native/VST3 edit "
                "refusal "
                "is exercised only in the actual provider branch. Component events only; DSP application, "
                "OS input, hardware and listening are separate.\n");
    }
    for (bool pattern : {true, false})
    {
        audio.stop();
        auto takeSeed = seed;
        // Author the send topology before recording. Ordinary level edits stay
        // live; new routing is explicitly refused while a take owns capture.
        takeSeed.mixer[1].routes.push_back({3, 1.f});
        // Keep the real held/optional native recipient unchanged. Expression
        // edits address a different prepared local sampler explicitly.
        const auto localExpression =
            std::find_if(takeSeed.channels.rbegin(), takeSeed.channels.rend(),
                         [&](const auto &c) { return c.id != target && !c.instrument && !c.audioClip; });
        if (localExpression == takeSeed.channels.rend())
            return fail("Held expression fixture requires a distinct local sampler");
        const auto liveExpressionChannel = localExpression->id;
        const auto liveExpressionMixer = localExpression->mixer;
        auto *liveExpressionPattern = takeSeed.pattern(seed.patterns.front().id);
        const auto expressionSounding = takeSeed.allocate();
        const auto expressionRetained = takeSeed.allocate();
        liveExpressionPattern->notes.push_back(
            {expressionSounding, liveExpressionChannel, 0., 4., 60, .45f, true});
        liveExpressionPattern->notes.push_back(
            {expressionRetained, liveExpressionChannel, .25, .25, 67, 0.f, true, true});
        controller.replace(std::move(takeSeed));
        selectedPattern = seed.patterns.front().id;
        selectedChannel = target;
        selectedChannels = {target};
        patternMode = pattern;
        refresh();
        if (mode.getButtonText() != (pattern ? "Pattern" : "Song"))
            return fail("transport caption disagrees with model");
        if (!prepare())
            return fail("held-take preparation");
        audio.setLooping(true);
        audio.seekBeat(0);
        audio.setCountInBars(0);
        recordArmed = false;
        record.onClick();
        play.onClick();
        if (!midiTaking)
            return fail("held take did not start");
        for (int i = 0; i < 8; ++i)
            tick();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 64, juce::uint8(100)));
        for (int i = 0; i < 8; ++i)
            tick();
        const auto lifecycle = audio.projectionDiagnostics();
        {
            const auto heldExpressionSession = audio.midiInput.capture.session.load();
            const auto beforeExpression = controller.project.state();
            const auto expressionBaseline = controller.project;
            const auto *filterChannel = expressionBaseline.channel(liveExpressionChannel);
            if (!filterChannel || filterChannel->sampler.filterType != 1 ||
                filterChannel->sampler.filterCutoff != 1. || filterChannel->sampler.filterResonance != 0.)
                return fail("Held modulation fixture requires the genuine default LP12 filter settings");
            const auto nativeBeforeExpression = audio.pluginDiagnostics();
            if (!audio.previewChannelNote(liveExpressionChannel, 72, true, 80))
                return fail("Held expression local preview admission");
            for (int i = 0; i < 4; ++i)
                tick();
            auto channelDiagnostic = [](const auto &projection, Id id)
            {
                for (const auto &channel : projection.channels)
                    if (channel.channel == id)
                        return channel;
                return AudioEngine::ChannelDiagnostic{};
            };
            auto processedExpression = [&](const auto &before, const auto &after)
            {
                const auto oldLocal = channelDiagnostic(before, liveExpressionChannel);
                const auto newLocal = channelDiagnostic(after, liveExpressionChannel);
                const auto recipient = channelDiagnostic(after, target);
                const auto meter = audio.mixerMeter(liveExpressionMixer);
                return oldLocal.sampler != 0 && oldLocal.sampler == newLocal.sampler &&
                       oldLocal.sample == newLocal.sample && oldLocal.track == newLocal.track &&
                       oldLocal.graphInitialisations == newLocal.graphInitialisations &&
                       newLocal.renderCalls > oldLocal.renderCalls &&
                       newLocal.timelineMisses == oldLocal.timelineMisses && recipient.heldInputKeys > 0 &&
                       after.edit == lifecycle.edit && after.playbackContext == lifecycle.playbackContext &&
                       after.replacements == lifecycle.replacements &&
                       after.samplerCreations == lifecycle.samplerCreations &&
                       after.pluginDeviceEpoch == lifecycle.pluginDeviceEpoch &&
                       after.headSeconds > before.headSeconds && after.headPlaying &&
                       after.transportPlaying && meter.available && std::isfinite(meter.peak[0]) &&
                       std::isfinite(meter.peak[1]) && meter.peak[0] > .000001f && meter.peak[1] > .000001f &&
                       !needsRebuild && projectionError.isEmpty() && midiTaking && audio.playing() &&
                       selectedChannel == target && selectedPattern == seed.patterns.front().id &&
                       audio.midiInput.capture.session.load() == heldExpressionSession;
            };
            auto untouchedExpression = [&](StepExpression property)
            {
                auto normalized = controller.project;
                for (auto &n : normalized.pattern(selectedPattern)->notes)
                    if (n.id == expressionSounding || n.id == expressionRetained)
                    {
                        const auto *before = expressionBaseline.pattern(selectedPattern);
                        const auto original = std::find_if(before->notes.begin(), before->notes.end(),
                                                           [&](const auto &note) { return note.id == n.id; });
                        if (original == before->notes.end() ||
                            !setStepExpressionValue(n, property, stepExpressionValue(*original, property)))
                            return false;
                    }
                // Normalize only this one property; any sibling/source/allocator change stays visible.
                return normalized.state().isEquivalentTo(beforeExpression);
            };
            if (block.getNumSamples() != 256 || device->getCurrentBufferSizeSamples() != 256 ||
                device->getCurrentSampleRate() != 48000.)
                return fail("Held expression timing requires the declared 256/48k synthetic callback");
            expressionCallbacks = expressionOverruns = expressionNonfinite = expressionSilent = 0;
            expressionInvalidSpans = expressionStartDelta = expressionStopDelta = 0;
            expressionMaximumMs = 0;
            expressionWorst = {};
            expressionTimingMode = pattern ? "pattern" : "song";
            expressionStarts = device->starts;
            expressionStops = device->stops;
            expressionTimingOpen = observeExpressionCallbacks = true;
            for (const auto property :
                 {StepExpression::pan, StepExpression::fine, StepExpression::modX, StepExpression::modY})
            {
                const auto *metadata = stepExpressionInfo(property);
                if (!metadata)
                    return fail("Held expression metadata unavailable");
                const auto baselineProjection = audio.projectionDiagnostics();
                const auto history = controller.undo.getUndoDescriptions();
                CommandArgs expressionArgs;
                expressionArgs.pattern = selectedPattern;
                expressionArgs.channel = liveExpressionChannel;
                expressionArgs.ids = {expressionSounding, expressionRetained};
                expressionArgs.text = "set";
                // Preserve literal Pan/Fine values. With default LP12/global cutoff1/resonance0,
                // Mod X.25 selects effective cutoff.5; Mod Y.75 selects resonance.5, so each
                // makes the sounding source's intended filter non-neutral. The muted peer below
                // proves source retention only; it is not an audible-response oracle.
                expressionArgs.value = property == StepExpression::fine   ? 37.5
                                       : property == StepExpression::modY ? .75
                                                                          : .25;
                command(metadata->command, expressionArgs);
                if (status != "Ready" || !prepare())
                    return fail("Held expression canonical/incremental projection: " + status);
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto published = audio.projectionDiagnostics();
                const auto editedExpression = controller.project.state();
                for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                    if (n.id == expressionSounding || n.id == expressionRetained)
                    {
                        // Verify metadata dispatch against the actual field, independently of its accessor.
                        const double raw = property == StepExpression::pan    ? n.pan
                                           : property == StepExpression::fine ? n.fineCents
                                           : property == StepExpression::modX ? n.modX
                                                                              : n.modY;
                        if (raw != expressionArgs.value || stepExpressionValue(n, property) != raw)
                            return fail("Held expression source value/metadata field mismatch");
                    }
                if (!untouchedExpression(property) || published.updates <= baselineProjection.updates ||
                    !processedExpression(baselineProjection, published))
                    return fail("Held expression publication/sampler PCM/head/preview/capture continuity");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held expression checked Undo/incremental projection");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto undone = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(beforeExpression) ||
                    controller.undo.getUndoDescriptions() != history ||
                    !processedExpression(published, undone))
                    return fail("Held expression one Undo changed source/history or active lanes");
                if (controller.redoEditChecked().failed() || !prepare())
                    return fail("Held expression checked Redo/incremental projection");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto redone = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(editedExpression) ||
                    !processedExpression(undone, redone))
                    return fail("Held expression Redo/source/playback continuity");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held expression final restoration");
                for (int i = 0; i < 4; ++i)
                    tick();
                if (!controller.project.state().isEquivalentTo(beforeExpression) ||
                    !processedExpression(redone, audio.projectionDiagnostics()))
                    return fail("Held expression final exact source/active lane restoration");
            }
            // Shift is score timing for both the local sampler and the actual
            // selected native instrument when this is the provider branch.
            for (const auto shiftChannel : {liveExpressionChannel, target})
            {
                const auto beforeShift = controller.project;
                const auto beforeShiftState = beforeShift.state();
                const auto shiftHistory = controller.undo.getUndoDescriptions();
                const auto beforeShiftProjection = audio.projectionDiagnostics();
                CommandArgs shiftArgs;
                shiftArgs.pattern = selectedPattern;
                shiftArgs.channel = shiftChannel;
                shiftArgs.text = "set";
                shiftArgs.value = 35.;
                for (const auto &n : beforeShift.pattern(selectedPattern)->notes)
                    if (graphStep(n, shiftChannel, beforeShift.pattern(selectedPattern)->length))
                        shiftArgs.ids.push_back(n.id);
                if (shiftArgs.ids.empty())
                    return fail("Held Shift requires real raw source step IDs");
                command("steps.shift", shiftArgs);
                if (status != "Ready" || !prepare())
                    return fail("Held Shift canonical incremental publication: " + status);
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto afterShiftProjection = audio.projectionDiagnostics();
                const auto shiftedState = controller.project.state();
                auto normalized = controller.project;
                for (auto &n : normalized.pattern(selectedPattern)->notes)
                    if (std::find(shiftArgs.ids.begin(), shiftArgs.ids.end(), n.id) != shiftArgs.ids.end())
                    {
                        if (n.shiftPercent != 35.)
                            return fail("Held Shift source percent not published");
                        for (const auto &before : beforeShift.pattern(selectedPattern)->notes)
                            if (before.id == n.id)
                                n.shiftPercent = before.shiftPercent;
                    }
                if (!normalized.state().isEquivalentTo(beforeShiftState) ||
                    afterShiftProjection.updates <= beforeShiftProjection.updates ||
                    !processedExpression(beforeShiftProjection, afterShiftProjection))
                    return fail("Held Shift changed raw source/extent/IDs/other properties or interrupted "
                                "processing");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held Shift checked Undo/publication");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto undoneShift = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(beforeShiftState) ||
                    controller.undo.getUndoDescriptions() != shiftHistory ||
                    !processedExpression(afterShiftProjection, undoneShift))
                    return fail("Held Shift exact one Undo/source/processing continuity");
                if (controller.redoEditChecked().failed() || !prepare())
                    return fail("Held Shift checked Redo/publication");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto redoneShift = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(shiftedState) ||
                    !processedExpression(undoneShift, redoneShift))
                    return fail("Held Shift exact Redo/source/processing continuity");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held Shift final source restoration");
                for (int i = 0; i < 4; ++i)
                    tick();
                if (!controller.project.state().isEquivalentTo(beforeShiftState) ||
                    !processedExpression(redoneShift, audio.projectionDiagnostics()))
                    return fail("Held Shift final Undo lost held/preview/recipient/source");
            }
            const auto shiftCancelBefore = controller.project.state();
            const auto shiftCancelProjection = audio.projectionDiagnostics();
            const auto shiftLease =
                mcpCall("vibera_gesture",
                        juce::JSON::parse(R"({"action":"begin","domain":"stepGraph"})"))["structuredContent"];
            auto shiftUpdate =
                juce::JSON::parse(R"({"action":"update","command":"steps.shift","args":{"value":65}})");
            shiftUpdate.getDynamicObject()->setProperty("operation", shiftLease["operation"]);
            const auto shiftPreview = mcpCall("vibera_gesture", shiftUpdate);
            for (int i = 0; i < 4; ++i)
                tick();
            if (bool(shiftPreview["isError"]) ||
                shiftPreview["structuredContent"]["completionState"].toString() != "pending" ||
                !processedExpression(shiftCancelProjection, audio.projectionDiagnostics()))
                return fail("Held Shift semantic preview lost capture/processing or falsely completed");
            bool shiftedNativeSource = false;
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (graphStep(n, target, controller.project.pattern(selectedPattern)->length))
                {
                    shiftedNativeSource = true;
                    if (n.shiftPercent != 65.)
                        return fail("Held Shift semantic preview did not edit its captured raw score source");
                }
            if (!shiftedNativeSource)
                return fail("Held Shift semantic preview lost captured source IDs");
            auto shiftCancel = juce::JSON::parse(R"({"action":"cancel"})");
            shiftCancel.getDynamicObject()->setProperty("operation", shiftLease["operation"]);
            const auto shiftCancelled = mcpCall("vibera_gesture", shiftCancel);
            for (int i = 0; i < 4; ++i)
                tick();
            if (shiftCancelled["structuredContent"]["completionState"].toString() != "cancelled" ||
                controller.hasGesture() || !controller.project.state().isEquivalentTo(shiftCancelBefore) ||
                !processedExpression(shiftCancelProjection, audio.projectionDiagnostics()))
                return fail("Held Shift semantic exact Cancel/source/processing continuity");
            // Repeat indices10/13 are actual local/selected score edits. No
            // raw notes are duplicated; occurrence scheduling remains upstream.
            for (const auto repeatChannel : {liveExpressionChannel, target})
            {
                const auto beforeRepeat = controller.project;
                const auto beforeRepeatState = beforeRepeat.state();
                const auto repeatHistory = controller.undo.getUndoDescriptions();
                const auto beforeRepeatProjection = audio.projectionDiagnostics();
                CommandArgs repeatArgs;
                repeatArgs.pattern = selectedPattern;
                repeatArgs.channel = repeatChannel;
                repeatArgs.text = "set";
                repeatArgs.value = repeatChannel == liveExpressionChannel ? 10. : 13.;
                for (const auto &n : beforeRepeat.pattern(selectedPattern)->notes)
                    if (graphStep(n, repeatChannel, beforeRepeat.pattern(selectedPattern)->length))
                        repeatArgs.ids.push_back(n.id);
                if (repeatArgs.ids.empty())
                    return fail("Held Repeat requires real raw source step IDs");
                command("steps.repeat", repeatArgs);
                if (status != "Ready" || !prepare())
                    return fail("Held Repeat canonical incremental publication: " + status);
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto afterRepeatProjection = audio.projectionDiagnostics();
                const auto repeatedState = controller.project.state();
                auto normalized = controller.project;
                for (auto &n : normalized.pattern(selectedPattern)->notes)
                    if (std::find(repeatArgs.ids.begin(), repeatArgs.ids.end(), n.id) != repeatArgs.ids.end())
                    {
                        if (n.repeatMode != int(repeatArgs.value))
                            return fail("Held Repeat source rate index not published");
                        for (const auto &before : beforeRepeat.pattern(selectedPattern)->notes)
                            if (before.id == n.id)
                                n.repeatMode = before.repeatMode;
                    }
                if (!normalized.state().isEquivalentTo(beforeRepeatState) ||
                    afterRepeatProjection.updates <= beforeRepeatProjection.updates ||
                    !processedExpression(beforeRepeatProjection, afterRepeatProjection))
                    return fail("Held Repeat changed raw source/extent/IDs/other properties or interrupted "
                                "processing");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held Repeat checked Undo/publication");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto undoneRepeat = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(beforeRepeatState) ||
                    controller.undo.getUndoDescriptions() != repeatHistory ||
                    !processedExpression(afterRepeatProjection, undoneRepeat))
                    return fail("Held Repeat exact one Undo/source/processing continuity");
                if (controller.redoEditChecked().failed() || !prepare())
                    return fail("Held Repeat checked Redo/publication");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto redoneRepeat = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(repeatedState) ||
                    !processedExpression(undoneRepeat, redoneRepeat))
                    return fail("Held Repeat exact Redo/source/processing continuity");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held Repeat final source restoration");
                for (int i = 0; i < 4; ++i)
                    tick();
                if (!controller.project.state().isEquivalentTo(beforeRepeatState) ||
                    !processedExpression(redoneRepeat, audio.projectionDiagnostics()))
                    return fail("Held Repeat final Undo lost held/preview/recipient/source");
            }
            const auto repeatCancelBefore = controller.project.state();
            const auto repeatCancelProjection = audio.projectionDiagnostics();
            const auto repeatLease =
                mcpCall("vibera_gesture",
                        juce::JSON::parse(R"({"action":"begin","domain":"stepGraph"})"))["structuredContent"];
            auto repeatUpdate =
                juce::JSON::parse(R"({"action":"update","command":"steps.repeat","args":{"value":13}})");
            repeatUpdate.getDynamicObject()->setProperty("operation", repeatLease["operation"]);
            const auto repeatPreview = mcpCall("vibera_gesture", repeatUpdate);
            for (int i = 0; i < 4; ++i)
                tick();
            if (bool(repeatPreview["isError"]) ||
                repeatPreview["structuredContent"]["completionState"].toString() != "pending" ||
                !processedExpression(repeatCancelProjection, audio.projectionDiagnostics()))
                return fail("Held Repeat semantic preview lost capture/processing or falsely completed");
            bool repeatedNativeSource = false;
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (graphStep(n, target, controller.project.pattern(selectedPattern)->length))
                {
                    repeatedNativeSource = true;
                    if (n.repeatMode != 13)
                        return fail(
                            "Held Repeat semantic preview did not edit its captured raw score source");
                }
            if (!repeatedNativeSource)
                return fail("Held Repeat semantic preview lost captured source IDs");
            auto repeatCancel = juce::JSON::parse(R"({"action":"cancel"})");
            repeatCancel.getDynamicObject()->setProperty("operation", repeatLease["operation"]);
            const auto repeatCancelled = mcpCall("vibera_gesture", repeatCancel);
            for (int i = 0; i < 4; ++i)
                tick();
            if (repeatCancelled["structuredContent"]["completionState"].toString() != "cancelled" ||
                controller.hasGesture() || !controller.project.state().isEquivalentTo(repeatCancelBefore) ||
                !processedExpression(repeatCancelProjection, audio.projectionDiagnostics()))
                return fail("Held Repeat semantic exact Cancel/source/processing continuity");
            // Release velocity is real score/MIDI-export data. Continuity here
            // does not assert an audible response or prove hosted note-off receipt.
            for (const auto releaseChannel : {liveExpressionChannel, target})
            {
                const auto beforeRelease = controller.project;
                const auto beforeReleaseState = beforeRelease.state();
                const auto releaseHistory = controller.undo.getUndoDescriptions();
                const auto beforeReleaseProjection = audio.projectionDiagnostics();
                CommandArgs releaseArgs;
                releaseArgs.pattern = selectedPattern;
                releaseArgs.channel = releaseChannel;
                releaseArgs.text = "set";
                releaseArgs.value = releaseChannel == liveExpressionChannel ? .25 : .75;
                for (const auto &n : beforeRelease.pattern(selectedPattern)->notes)
                    if (graphStep(n, releaseChannel, beforeRelease.pattern(selectedPattern)->length))
                        releaseArgs.ids.push_back(n.id);
                if (releaseArgs.ids.empty())
                    return fail("Held Release requires real raw source step IDs");
                command("steps.release", releaseArgs);
                if (status != "Ready" || !prepare())
                    return fail("Held Release canonical incremental publication: " + status);
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto afterReleaseProjection = audio.projectionDiagnostics();
                const auto releasedState = controller.project.state();
                auto normalized = controller.project;
                for (auto &n : normalized.pattern(selectedPattern)->notes)
                    if (std::find(releaseArgs.ids.begin(), releaseArgs.ids.end(), n.id) !=
                        releaseArgs.ids.end())
                    {
                        if (n.releaseVelocity != releaseArgs.value)
                            return fail("Held Release source note-off velocity not published");
                        for (const auto &before : beforeRelease.pattern(selectedPattern)->notes)
                            if (before.id == n.id)
                                n.releaseVelocity = before.releaseVelocity;
                    }
                if (!normalized.state().isEquivalentTo(beforeReleaseState) ||
                    afterReleaseProjection.updates <= beforeReleaseProjection.updates ||
                    !processedExpression(beforeReleaseProjection, afterReleaseProjection))
                    return fail("Held Release changed raw source/extent/IDs/other properties or interrupted "
                                "processing");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held Release checked Undo/publication");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto undoneRelease = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(beforeReleaseState) ||
                    controller.undo.getUndoDescriptions() != releaseHistory ||
                    !processedExpression(afterReleaseProjection, undoneRelease))
                    return fail("Held Release exact one Undo/source/processing continuity");
                if (controller.redoEditChecked().failed() || !prepare())
                    return fail("Held Release checked Redo/publication");
                for (int i = 0; i < 4; ++i)
                    tick();
                const auto redoneRelease = audio.projectionDiagnostics();
                if (!controller.project.state().isEquivalentTo(releasedState) ||
                    !processedExpression(undoneRelease, redoneRelease))
                    return fail("Held Release exact Redo/source/processing continuity");
                if (controller.undoEditChecked().failed() || !prepare())
                    return fail("Held Release final source restoration");
                for (int i = 0; i < 4; ++i)
                    tick();
                if (!controller.project.state().isEquivalentTo(beforeReleaseState) ||
                    !processedExpression(redoneRelease, audio.projectionDiagnostics()))
                    return fail("Held Release final Undo lost held/preview/recipient/source");
            }
            const auto releaseCancelBefore = controller.project.state();
            const auto releaseCancelProjection = audio.projectionDiagnostics();
            const auto releaseLease =
                mcpCall("vibera_gesture",
                        juce::JSON::parse(R"({"action":"begin","domain":"stepGraph"})"))["structuredContent"];
            auto releaseUpdate =
                juce::JSON::parse(R"({"action":"update","command":"steps.release","args":{"value":0.65}})");
            releaseUpdate.getDynamicObject()->setProperty("operation", releaseLease["operation"]);
            const auto releasePreview = mcpCall("vibera_gesture", releaseUpdate);
            for (int i = 0; i < 4; ++i)
                tick();
            if (bool(releasePreview["isError"]) ||
                releasePreview["structuredContent"]["completionState"].toString() != "pending" ||
                !processedExpression(releaseCancelProjection, audio.projectionDiagnostics()))
                return fail("Held Release semantic preview lost capture/processing or falsely completed");
            bool releasedNativeSource = false;
            for (const auto &n : controller.project.pattern(selectedPattern)->notes)
                if (graphStep(n, target, controller.project.pattern(selectedPattern)->length))
                {
                    releasedNativeSource = true;
                    if (n.releaseVelocity != .65)
                        return fail(
                            "Held Release semantic preview did not edit its captured raw score source");
                }
            if (!releasedNativeSource)
                return fail("Held Release semantic preview lost captured source IDs");
            auto releaseCancel = juce::JSON::parse(R"({"action":"cancel"})");
            releaseCancel.getDynamicObject()->setProperty("operation", releaseLease["operation"]);
            const auto releaseCancelled = mcpCall("vibera_gesture", releaseCancel);
            for (int i = 0; i < 4; ++i)
                tick();
            if (releaseCancelled["structuredContent"]["completionState"].toString() != "cancelled" ||
                controller.hasGesture() || !controller.project.state().isEquivalentTo(releaseCancelBefore) ||
                !processedExpression(releaseCancelProjection, audio.projectionDiagnostics()))
                return fail("Held Release semantic exact Cancel/source/processing continuity");
            observeExpressionCallbacks = false;
            expressionStartDelta = device->starts - expressionStarts;
            expressionStopDelta = device->stops - expressionStops;
            if (expressionCallbacks == 0 || expressionOverruns != 0 || expressionNonfinite != 0 ||
                expressionSilent != 0 || expressionInvalidSpans != 0 || expressionStartDelta != 0 ||
                expressionStopDelta != 0)
                return fail("Held expression synthetic callback deadline/PCM/device continuity");
            const auto nativeAfterExpression = audio.pluginDiagnostics();
            if (nativeAfterExpression.size() != nativeBeforeExpression.size())
                return fail("Held expression replaced optional native worker inventory");
            for (std::size_t i = 0; i < nativeBeforeExpression.size(); ++i)
            {
                const auto &before = nativeBeforeExpression[i];
                const auto &after = nativeAfterExpression[i];
                if (after.id != before.id || !after.alive || after.processId != before.processId ||
                    after.latency != before.latency || after.packets.missed != before.packets.missed ||
                    after.packets.dropped != before.packets.dropped ||
                    after.packets.midiOverflow != before.packets.midiOverflow ||
                    after.packets.parametersRejected != before.packets.parametersRejected ||
                    after.packets.auxiliaryRejected != before.packets.auxiliaryRejected ||
                    after.packets.processed <= before.packets.processed)
                    return fail("Held expression changed optional native worker/packet continuity");
            }
            if (!audio.previewChannelNote(liveExpressionChannel, 72, false))
                return fail("Held expression exact local preview release");
            for (int i = 0; i < 4; ++i)
                tick();
            if (!controller.project.state().isEquivalentTo(expressionBaseline.state()) || !midiTaking ||
                !audio.playing() || selectedChannel != target ||
                audio.midiInput.capture.session.load() != heldExpressionSession)
                return fail("Expression preview release recorded/mutated source or lost held input");
            if (!writeExpressionTiming(true))
                return fail("Held expression callback metrics output");
            expressionTimingOpen = false;
            folder
                .getChildFile(pattern ? "step-expression-pattern-held.txt" : "step-expression-song-held.txt")
                .replaceWithText(
                    "PASS local sampler pan/fine/Mod X/Y and local/native score Shift/Repeat/Release "
                    "canonical edits "
                    "and one "
                    "Undo/Redo during held "
                    "Pattern/Song capture, independent local preview, actual later sampler "
                    "processing/PCM/head, "
                    "retained graph/native workers and packet counters. CONTROL publication plus later "
                    "processing "
                    "is observed, not an exact consumed document generation ACK or literal DSP-value oracle. "
                    "No OS input/hardware/listening.\n");
        }
        down(0, .25f, left);
        drag(3, .65f, left);
        up();
        for (int i = 0; i < 4; ++i)
            tick();
        if (!midiTaking || !audio.playing() || selectedChannel != target)
            return fail("UI graph interrupted held take");
        auto *swing = editor->rackSwingControl();
        if (!swing)
            return fail("Swing slider unavailable during held take");
        swing->onDragStart();
        swing->setValue(30, juce::sendNotificationSync);
        swing->setValue(65, juce::sendNotificationSync);
        swing->onDragEnd();
        for (int i = 0; i < 4; ++i)
            tick();
        if (!midiTaking || !audio.playing() || controller.project.swing != .65)
            return fail("Swing UI interrupted held MIDI take");
        auto object = [](const juce::String &json) { return juce::JSON::parse(json); };
        if (setRackGraphProperty("pitch").failed())
            return fail("Pitch selection during held capture");
        down(0, 67.f / 127, left);
        drag(3, 72.f / 127, left);
        up();
        for (int i = 0; i < 4; ++i)
            tick();
        if (!midiTaking || !audio.playing() || selectedChannel != target || pitch(0) != 67 || pitch(3) != 72)
            return fail("Actual Pitch graph edit interrupted held MIDI take");
        // Exercise the narrow activation lease against the same real held
        // capture and transport used by the UI/Graph scenarios below.
        const auto beforeActivationLease = controller.project.state();
        const auto activationCaptureSession = audio.midiInput.capture.session.load();
        auto activationLease = mcpCall(
            "vibera_gesture", object(R"({"action":"begin","domain":"stepActivation"})"))["structuredContent"];
        auto activationUpdate = object(
            R"({"action":"update","command":"steps.set","args":{"beat":0,"enabled":false,"retainSteps":true}})");
        activationUpdate.getDynamicObject()->setProperty("operation", activationLease["operation"]);
        const auto activationPreview = mcpCall("vibera_gesture", activationUpdate);
        for (int i = 0; i < 4; ++i)
            tick();
        if (bool(activationPreview["isError"]) ||
            activationPreview["structuredContent"]["completionState"].toString() != "pending" ||
            !midiTaking || !audio.playing() || selectedChannel != target ||
            audio.midiInput.capture.session.load() != activationCaptureSession)
            return fail("Step activation preview interrupted held take or falsely completed");
        bool retainedActivation = false;
        for (const auto &note : controller.project.pattern(selectedPattern)->notes)
            if (note.channel == target && note.step && std::abs(note.beat) < .00001)
            {
                if (!note.muted)
                    return fail("Held step activation did not retain the disabled source chord");
                retainedActivation = true;
            }
        if (!retainedActivation)
            return fail("Held step activation deleted its source instead of retaining it");
        auto activationCancel = object(R"({"action":"cancel"})");
        activationCancel.getDynamicObject()->setProperty("operation", activationLease["operation"]);
        const auto activationCancelled = mcpCall("vibera_gesture", activationCancel);
        for (int i = 0; i < 4; ++i)
            tick();
        if (activationCancelled["structuredContent"]["completionState"].toString() != "cancelled" ||
            !beforeActivationLease.isEquivalentTo(controller.project.state()) || !midiTaking ||
            !audio.playing() || selectedChannel != target ||
            audio.midiInput.capture.session.load() != activationCaptureSession)
            return fail("Step activation Cancel lost source, held take, recipient or capture session");
        const auto beforePitchLease = controller.project.state();
        auto pitchLease = mcpCall("vibera_gesture",
                                  object(R"({"action":"begin","domain":"stepGraph"})"))["structuredContent"];
        auto pitchUpdate = object(R"({"action":"update","command":"steps.pitch","args":{"pitch":75}})");
        pitchUpdate.getDynamicObject()->setProperty("operation", pitchLease["operation"]);
        mcpCall("vibera_gesture", pitchUpdate);
        auto pitchCancel = object(R"({"action":"cancel"})");
        pitchCancel.getDynamicObject()->setProperty("operation", pitchLease["operation"]);
        if (mcpCall("vibera_gesture", pitchCancel)["structuredContent"]["completionState"].toString() !=
                "cancelled" ||
            !beforePitchLease.isEquivalentTo(controller.project.state()) || !midiTaking || !audio.playing() ||
            setRackGraphProperty("velocity").failed())
            return fail("Pitch semantic Cancel lost held take, source or property transition");
        auto swingLease =
            mcpCall("vibera_gesture", object(R"({"action":"begin","domain":"swing"})"))["structuredContent"];
        auto *swingUpdate = new juce::DynamicObject;
        swingUpdate->setProperty("action", "update");
        swingUpdate->setProperty("operation", swingLease["operation"]);
        swingUpdate->setProperty("command", "project.swing");
        swingUpdate->setProperty("args", object(R"({"value":0.5})"));
        auto swingResult = mcpCall("vibera_gesture", juce::var(swingUpdate));
        if (bool(swingResult["isError"]) || !midiTaking || controller.project.swing != .5)
            return fail("Swing semantic preview interrupted held MIDI take");
        auto *swingCommit = new juce::DynamicObject;
        swingCommit->setProperty("action", "commit");
        swingCommit->setProperty("operation", swingLease["operation"]);
        if (mcpCall("vibera_gesture", juce::var(swingCommit))["structuredContent"]["completionState"]
                .toString() != "completed")
            return fail("Swing semantic held-take commit");

        auto lease = mcpCall("vibera_gesture",
                             object(R"({"action":"begin","domain":"stepVelocity"})"))["structuredContent"];
        auto *request = new juce::DynamicObject;
        request->setProperty("action", "update");
        request->setProperty("operation", lease["operation"]);
        request->setProperty("command", "steps.velocity");
        request->setProperty("args", object(R"({"text":"set","value":0.45})"));
        mcpCall("vibera_gesture", juce::var(request));
        for (int i = 0; i < 4; ++i)
            tick();
        if (!midiTaking || !controller.hasGesture())
            return fail("scoped MCP lease cancelled held take");
        auto *commit = new juce::DynamicObject;
        commit->setProperty("action", "commit");
        commit->setProperty("operation", lease["operation"]);
        if (mcpCall("vibera_gesture", juce::var(commit))["structuredContent"]["completionState"].toString() !=
            "completed")
            return fail("scoped MCP take commit");
        focus = Scope::piano;
        selectedNotes = {controller.project.pattern(selectedPattern)->notes.front().id};
        if (!performAction("notes.mute") || !midiTaking ||
            !controller.project.pattern(selectedPattern)->notes.front().muted)
            return fail("Piano UI mute interrupted held take");
        auto muteLease = mcpCall("vibera_gesture",
                                 object(R"({"action":"begin","domain":"noteMute"})"))["structuredContent"];
        auto *muteUpdate = new juce::DynamicObject;
        muteUpdate->setProperty("action", "update");
        muteUpdate->setProperty("operation", muteLease["operation"]);
        muteUpdate->setProperty("command", "notes.unmute");
        muteUpdate->setProperty("args", object(R"({})"));
        if (bool(mcpCall("vibera_gesture", juce::var(muteUpdate))["isError"]))
            return fail("Note mute semantic held-take update failed");
        for (int i = 0; i < 4; ++i)
            tick();
        auto *muteCommit = new juce::DynamicObject;
        muteCommit->setProperty("action", "commit");
        muteCommit->setProperty("operation", muteLease["operation"]);
        if (mcpCall("vibera_gesture", juce::var(muteCommit))["structuredContent"]["completionState"]
                    .toString() != "completed" ||
            !midiTaking || !audio.playing() || selectedChannel != target ||
            controller.project.pattern(selectedPattern)->notes.front().muted)
            return fail("Note mute semantic held-take commit interrupted recording");
        selectedNotes.clear();
        focus = Scope::rack;
        focus = Scope::piano;
        const auto beforeSlice = controller.project.state();
        if (beginNoteSlice(false).failed() || updateNoteSlice({.125, .125, 59.5, 60.5, 0, false}).failed() ||
            !controller.project.state().isEquivalentTo(beforeSlice) || !midiTaking)
            return fail("UI Slice preview interrupted held take or changed the source");
        if (finishNoteSlice(true).failed() || !midiTaking || !audio.playing())
            return fail("UI Slice commit interrupted recording");
        auto sliceLease = mcpCall("vibera_slice", object(R"({"action":"begin"})"))["structuredContent"];
        auto *sliceUpdate = new juce::DynamicObject;
        sliceUpdate->setProperty("action", "update");
        sliceUpdate->setProperty("operation", sliceLease["operation"]);
        sliceUpdate->setProperty("slice", sliceLineState({.0625, .0625, 59.5, 60.5, 0, false}));
        const auto sliceBaseline = controller.project.state();
        if (bool(mcpCall("vibera_slice", juce::var(sliceUpdate))["isError"]) || !midiTaking ||
            !controller.project.state().isEquivalentTo(sliceBaseline))
            return fail("Semantic Slice preview changed held-take source");
        auto *sliceCommit = new juce::DynamicObject;
        sliceCommit->setProperty("action", "commit");
        sliceCommit->setProperty("operation", sliceLease["operation"]);
        if (mcpCall("vibera_slice", juce::var(sliceCommit))["structuredContent"]["completionState"]
                    .toString() != "completed" ||
            !midiTaking || !audio.playing() || selectedChannel != target)
            return fail("Semantic Slice commit interrupted held MIDI take");
        std::vector<Id> stretchIds;
        for(const auto &note:controller.project.pattern(selectedPattern)->notes)
            if(note.channel==target) stretchIds.push_back(note.id);
        const auto beforeStretch=controller.project.state();
        if(beginNoteStretch(stretchIds,false).failed() || updateNoteStretch(.9).failed() ||
           !controller.project.state().isEquivalentTo(beforeStretch) || !midiTaking)
            return fail("UI stretch preview changed held-take source");
        if(finishNoteStretch(true).failed() || !midiTaking || !audio.playing() || selectedChannel!=target)
            return fail("UI stretch commit interrupted held take");
        auto *stretchBegin=new juce::DynamicObject;
        stretchBegin->setProperty("action","begin");juce::Array<juce::var> stretchTargets;
        for(auto id:stretchIds)stretchTargets.add(juce::int64(id));
        stretchBegin->setProperty("ids",stretchTargets);
        auto stretchLease=mcpCall("vibera_stretch",juce::var(stretchBegin))["structuredContent"];
        auto *stretchUpdate=new juce::DynamicObject;
        stretchUpdate->setProperty("action","update");stretchUpdate->setProperty("operation",stretchLease["operation"]);
        stretchUpdate->setProperty("ratio",.95);
        const auto stretchBaseline=controller.project.state();
        if(bool(mcpCall("vibera_stretch",juce::var(stretchUpdate))["isError"]) || !midiTaking ||
           !controller.project.state().isEquivalentTo(stretchBaseline)) return fail("Semantic stretch preview changed held take");
        auto *stretchCommit=new juce::DynamicObject;
        stretchCommit->setProperty("action","commit");stretchCommit->setProperty("operation",stretchLease["operation"]);
        if(mcpCall("vibera_stretch",juce::var(stretchCommit))["structuredContent"]["completionState"].toString()!="completed" ||
           !midiTaking || !audio.playing() || selectedChannel!=target) return fail("Semantic stretch commit interrupted held take");

        for (int i = 0; i < 4; ++i)
            tick();
        const auto beforeRoute = controller.project.state();
        const auto routeRevision = controller.revision;
        const auto routeHistory = controller.undo.getNumberOfUnitsTakenUpByStoredCommands();
        const auto captureSession = audio.midiInput.capture.session.load();
        CommandArgs route;
        route.mixer = 1;
        route.target = 4;
        const auto routeResult = controller.execute("mixer.route", route);
        if (routeResult.wasOk() || !routeResult.getErrorMessage().startsWith("effectChain.controlBusy:") ||
            !controller.project.state().isEquivalentTo(beforeRoute) || controller.revision != routeRevision ||
            controller.undo.getNumberOfUnitsTakenUpByStoredCommands() != routeHistory ||
            audio.midiInput.capture.session.load() != captureSession || !midiTaking || !audio.playing() ||
            selectedChannel != target || mixerRoute(controller.project, 1, 4) ||
            !mixerRoute(controller.project, 1, 3))
            return fail("Structural routing refusal changed held MIDI take, model or history");
        auto *sendBegin = new juce::DynamicObject;
        sendBegin->setProperty("action", "begin");
        sendBegin->setProperty("domain", "mixerSend");
        sendBegin->setProperty("sourceInsert", 1);
        sendBegin->setProperty("targetInsert", 3);
        auto sendLease = mcpCall("vibera_gesture", juce::var(sendBegin))["structuredContent"];
        auto *sendUpdate = new juce::DynamicObject;
        sendUpdate->setProperty("action", "update");
        sendUpdate->setProperty("operation",sendLease["operation"]);sendUpdate->setProperty("command","mixer.send");
        auto *sendArgs=new juce::DynamicObject;sendArgs->setProperty("mixer",1);sendArgs->setProperty("target",3);sendArgs->setProperty("value",.4);
        sendUpdate->setProperty("args",juce::var(sendArgs));
        if(bool(mcpCall("vibera_gesture",juce::var(sendUpdate))["isError"]) || !midiTaking)
            return fail("Semantic send gesture interrupted held MIDI take");
        auto *sendCommit=new juce::DynamicObject;sendCommit->setProperty("action","commit");sendCommit->setProperty("operation",sendLease["operation"]);
        if(mcpCall("vibera_gesture",juce::var(sendCommit))["structuredContent"]["completionState"].toString()!="completed" ||
           !midiTaking || !audio.playing() || selectedChannel!=target)
            return fail("Send gesture commit interrupted held MIDI take");
        CommandArgs control;control.mixer=1;control.text="Held drum bus";command("mixer.rename",control);
        command("mixer.solo",control);command("mixer.unsolo",control);
        if(!midiTaking || !audio.playing() || selectedChannel!=target)return fail("Named Mixer/solo interrupted held take");
        control.enabled = true;
        command("mixer.mute", control);
        command("mixer.unmuteAll", {});
        if (!midiTaking || !audio.playing() || selectedChannel != target || controller.project.mixerSolo ||
            std::any_of(controller.project.mixer.begin(), controller.project.mixer.end(),
                        [](const auto &insert) { return insert.muted; }))
            return fail("Unmute All interrupted held take or failed to restore mute mask");
        auto *controlBegin=new juce::DynamicObject;controlBegin->setProperty("action","begin");controlBegin->setProperty("domain","mixerControl");controlBegin->setProperty("sourceInsert",1);
        auto controlLease=mcpCall("vibera_gesture",juce::var(controlBegin))["structuredContent"];
        auto *controlUpdate=new juce::DynamicObject;controlUpdate->setProperty("action","update");controlUpdate->setProperty("operation",controlLease["operation"]);controlUpdate->setProperty("command","mixer.pan");
        auto *controlArgs=new juce::DynamicObject;controlArgs->setProperty("mixer",1);controlArgs->setProperty("value",-.25);controlUpdate->setProperty("args",juce::var(controlArgs));
        if(bool(mcpCall("vibera_gesture",juce::var(controlUpdate))["isError"]) || !midiTaking)return fail("Mixer-control lease interrupted held take");
        auto *controlCommit=new juce::DynamicObject;controlCommit->setProperty("action","commit");controlCommit->setProperty("operation",controlLease["operation"]);
        if(mcpCall("vibera_gesture",juce::var(controlCommit))["structuredContent"]["completionState"].toString()!="completed" || !midiTaking || !audio.playing() || selectedChannel!=target)return fail("Mixer-control commit interrupted held take");
        focus = Scope::rack;
        const auto beforeStop = controller.project.state();
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 64));
        for (int i = 0; i < 8; ++i)
            tick();
        stop.onClick();
        const auto afterStop = controller.project.state();
        const auto &notes = controller.project.pattern(selectedPattern)->notes;
        if (midiTaking || std::count_if(notes.begin(), notes.end(), [&](const auto &n)
                                        { return n.channel == target && n.pitch == 64 && !n.step; }) != 1)
            return fail("held note did not commit exactly once");
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(beforeStop))
            return fail("recording Undo swallowed graph edit");
        controller.redoEdit();
        if (!controller.project.state().isEquivalentTo(afterStop))
            return fail("recording Redo");
        controller.undoEdit();
        const auto after = audio.projectionDiagnostics();
        if (lifecycle.edit != after.edit || lifecycle.replacements != after.replacements ||
            lifecycle.samplerCreations != after.samplerCreations)
            return fail("UI/MCP velocity replaced graph");
    }
    folder.getChildFile("note-stretch-held-take.txt").replaceWithText("PASS actual Studio UI/shared semantic stretch preview/commit retain held Pattern/Song MIDI take, playback and target; Stop has separate exact Undo/Redo. Virtual injection; hardware/listening separate.\n");
    folder.getChildFile("mixer-routing-held-take.txt")
        .replaceWithText("PASS structural route addition explicitly refuses with effectChain.controlBusy and "
                         "exact model/history/capture-session preservation; existing-route shared semantic "
                         "send gesture retains held Pattern/Song MIDI take, playback and target; Stop has "
                         "separate exact Undo/Redo. Virtual injection; hardware/listening separate.\n");
    folder.getChildFile("mixer-controls-held-take.txt").replaceWithText("PASS actual Studio rename/solo/restore and semantic leased pan/commit retain held Pattern/Song MIDI take, recipient/playback and graph; Stop has separate exact Undo/Redo. Virtual injection; not native/hardware/listening.\n");
    folder.getChildFile("note-slice-held-take.txt")
        .replaceWithText("PASS actual Studio UI/shared semantic nondestructive Slice preview/commit retain "
                         "Pattern/Song held MIDI take, target, playback and engine resources; recording Stop "
                         "has separate exact Undo/Redo. Virtual injection, hardware/listening separate.\n");
    folder.getChildFile("note-mute-held-take.txt")
        .replaceWithText("PASS shared Piano UI mute and semantic noteMute lease/unmute/commit preserve "
                         "actual Studio held MIDI take, Pattern/Song playback and recipient; recording Stop "
                         "commits one separate Undo. Not hardware/listening.\n");
    folder.getChildFile("swing-held-take.txt")
        .replaceWithText("PASS Pattern/Song actual Swing slider gestures and scoped semantic swing lease "
                         "retain held MIDI take/target/transport/graph; recording Stop has separate exact "
                         "Undo/Redo. Virtual injection, no hardware/listening.\n");
    folder.getChildFile("step-velocity-held-take.txt")
        .replaceWithText("PASS Pattern/Song actual graph gestures and scoped semantic lease retain held MIDI "
                         "take/target/transport/graph; Stop commits one note with separate exact Undo/Redo. "
                         "Virtual injection, no physical input/listening.\n");
    folder.getChildFile("step-pitch-held-take.txt")
        .replaceWithText("PASS actual Pitch selector/paint and scoped stepGraph pitch preview/Cancel retain "
                         "Pattern/Song held MIDI take, target, transport and graph. Stop has separate "
                         "capture history. Virtual injection, no hardware/listening.\n");
    audio.stop();
    tick();
    // Later Slice/Stretch cases intentionally leave melodic notes. Author a
    // real step melody for the captures through the public application path.
    command("pattern.add", {});
    const auto capturePattern = controller.project.patterns.back().id;
    selectedPattern = capturePattern;
    selectedChannel = target;
    selectedChannels = {target};
    CommandArgs captureArgs;
    captureArgs.pattern = capturePattern;
    captureArgs.channel = target;
    captureArgs.text = "Pitch Graph";
    command("pattern.rename", captureArgs);
    const int melody[] = {60, 64, 67, 72};
    for (int i = 0; i < 4; ++i)
    {
        captureArgs.beat = i;
        captureArgs.enabled = true;
        command("steps.set", captureArgs);
        if (status != "Ready")
            return fail("Capture fixture step creation");
        const auto *score = controller.project.pattern(capturePattern);
        const auto note = std::find_if(score->notes.begin(), score->notes.end(),
                                       [&](const auto &n) { return n.channel == target && n.beat == i; });
        if (note == score->notes.end())
            return fail("Capture fixture stable note missing");
        captureArgs.ids = {note->id};
        captureArgs.pitch = melody[i];
        captureArgs.text = "set";
        command("steps.pitch", captureArgs);
        if (status != "Ready")
            return fail("Capture fixture canonical pitch");
    }
    captureArgs.ids.clear();
    captureArgs.beat = 0;
    captureArgs.length = 4;
    captureArgs.lane = 1;
    command("clip.add", captureArgs);
    if (status != "Ready" || controller.project.validate().failed())
        return fail("Capture fixture genuine arrangement");
    editor->setRackViewport(0, 0);
    refresh();
    setRackGraphVisible(true);
    if (int(rackGraphState()["eligibleNoteCount"]) != 4)
        return fail("Capture fixture must show four genuine editable step notes");
    if (saveProject(controller.project, folder.getChildFile("velocity-graph-workspace.vibra")).failed())
        return fail("populated save");
    const juce::Point<int> normal{getWidth(), getHeight()};
    juce::PNGImageFormat png;
    for (auto size : {juce::Point<int>{1280, 684}, juce::Point<int>{2560, 1356}})
    {
        setSize(size.x, size.y);
        resized();
        dispatchMessages(10);
        auto stream =
            folder.getChildFile(size.x == 1280 ? "velocity-graph-1080p.png" : "velocity-graph-4k.png")
                .createOutputStream();
        if (!stream)
            return fail("PNG stream");
        stream->setPosition(0);
        stream->truncate();
        if (!png.writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f), *stream))
            return fail("PNG write");
        stream.reset();
        if (setRackGraphProperty("pitch").failed())
            return fail("Pitch screenshot property selection");
        stream = folder.getChildFile(size.x == 1280 ? "pitch-graph-1080p.png" : "pitch-graph-4k.png")
                     .createOutputStream();
        if (!stream)
            return fail("Pitch PNG stream");
        stream->setPosition(0);
        stream->truncate();
        if (!png.writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f), *stream) ||
            setRackGraphProperty("velocity").failed())
            return fail("Pitch PNG write/property restore");
    }
    setSize(normal.x, normal.y);
    resized();
    folder.getChildFile("step-velocity-gui.txt")
        .replaceWithText(
            "PASS actual Ctrl+K and graph paint/scale/ramp/reset/Escape/hide/scroll handlers, exact one-drag "
            "history, empty-column no-op and populated component PNGs. No OS input or listening.\n");
    return juce::Result::ok();
}
} // namespace vibera
