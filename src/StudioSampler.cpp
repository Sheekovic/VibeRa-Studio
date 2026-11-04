// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#include "Studio.h"
#include "InstrumentWindow.h"
#include "SamplerTempoContracts.h"
#include "SamplerGestures.h"
#include "MidiScore.h"
#include "McpSchema.h"
#include "../tests/SyntheticDevice.h"
#include <charconv>

namespace vibera
{

juce::Result Studio::performSamplerAutomationIntent(const SamplerAutomationUiIntent &intent,
                                                    SamplerAutomationUiAction action, Id source)
{
    const bool dormant = action == SamplerAutomationUiAction::removeInitialization ||
                         action == SamplerAutomationUiAction::unlink ||
                         action == SamplerAutomationUiAction::select;
    SamplerAutomationTarget target;
    if (auto result = intent.validate(controller.project, controller.projectIdentity, controller.revision,
                                      target, dormant);
        result.failed())
        return result;
    if (auto result = finishRackEditsChecked(); result.failed())
        return result;
    if (auto result = intent.validate(controller.project, controller.projectIdentity, controller.revision,
                                      target, dormant);
        result.failed())
        return result;
    if (auto result = admitMixerAutomation(); result.failed())
        return result;
    if (action == SamplerAutomationUiAction::select)
    {
        const auto *item = controller.project.automationSource(source);
        if (!item || std::none_of(item->links.begin(), item->links.end(),
                                  [&](const auto &link) { return link.key == target.key; }))
            return juce::Result::fail("automation.missingLink: Source is not linked to this sampler control");
        return selectAutomationSource(source);
    }
    CommandArgs args;
    args.channel = intent.channel;
    args.samplerAutomationParameter = intent.parameter;
    args.target = source;
    args.value = target.manual;
    juce::String commandId;
    switch (action)
    {
    case SamplerAutomationUiAction::create:
    {
        args.length = std::max(4., controller.project.endBeat());
        const auto occupied = [&](int lane)
        {
            const auto uses = [lane](const auto &clip) { return clip.lane == lane; };
            return std::any_of(controller.project.placements.begin(), controller.project.placements.end(),
                               uses) ||
                   std::any_of(controller.project.audioPlacements.begin(),
                               controller.project.audioPlacements.end(), uses) ||
                   std::any_of(controller.project.automation.placements.begin(),
                               controller.project.automation.placements.end(), uses);
        };
        while (args.lane < 128 && occupied(args.lane))
            ++args.lane;
        if (args.lane == 128)
            return juce::Result::fail("automation.noFreeLane: Free a Playlist lane before creating a clip");
        commandId = "automation.sampler.create";
        break;
    }
    case SamplerAutomationUiAction::initialize:
        commandId = "automation.sampler.init";
        break;
    case SamplerAutomationUiAction::removeInitialization:
        commandId = "automation.sampler.init.remove";
        break;
    case SamplerAutomationUiAction::link:
        commandId = "automation.sampler.link";
        break;
    case SamplerAutomationUiAction::unlink:
        commandId = "automation.sampler.unlink";
        break;
    default:
        return juce::Result::fail("automation.unsupportedAction: Unknown sampler UI action");
    }
    const auto result = controller.execute(commandId, args);
    if (result.wasOk() && action == SamplerAutomationUiAction::create &&
        controller.projectIdentity == intent.project && controller.revision == intent.revision + 1 &&
        !controller.project.automation.placements.empty())
    {
        const auto &created = controller.project.automation.placements.back();
        const auto *item = controller.project.automationSource(created.source);
        if (item && std::any_of(item->links.begin(), item->links.end(),
                                [&](const auto &link) { return link.key == target.key; }))
        {
            selectedClips = {created.id};
            selectAutomationSource(created.source);
        }
    }
    refresh();
    return result; // Canonical model completion; no claim of active-DSP acknowledgement.
}
void Studio::samplerAutomationMenu(Id channel, std::uint32_t parameter)
{
    SamplerAutomationTarget target;
    if (auto result = resolveSamplerAutomationTarget(controller.project, channel, parameter, target);
        result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    if (auto result = admitMixerAutomation(); result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    const SamplerAutomationUiIntent intent{controller.projectIdentity, controller.revision, channel,
                                           parameter};
    struct Choice
    {
        SamplerAutomationUiAction action;
        Id source;
        bool range = false;
        bool controller = false;
    };
    std::vector<Choice> choices;
    juce::PopupMenu menu, existing, linked, unlink, ranges;
    const auto add = [&](juce::PopupMenu &to, const juce::String &label, SamplerAutomationUiAction action,
                         Id source = 0, bool enabled = true, bool range = false)
    {
        choices.push_back({action, source, range});
        to.addItem(int(choices.size()), label, enabled);
    };
    menu.addSectionHeader(target.name);
    add(menu, "Create automation clip", SamplerAutomationUiAction::create);
    choices.push_back({SamplerAutomationUiAction::select, 0, false, true});
    menu.addItem(int(choices.size()), "Link to controller...");
    add(menu, "Init song with this position", SamplerAutomationUiAction::initialize);
    const bool initialized = std::any_of(controller.project.automation.initialValues.begin(),
                                         controller.project.automation.initialValues.end(),
                                         [&](const auto &item) { return item.key == target.key; });
    add(menu, "Delete initialized control", SamplerAutomationUiAction::removeInitialization, 0, initialized);
    for (const auto &source : controller.project.automation.sources)
    {
        const auto label = juce::String::fromUTF8(source.name.c_str()) + " [" + juce::String(source.id) + "]";
        add(existing, label, SamplerAutomationUiAction::link, source.id);
        if (std::any_of(source.links.begin(), source.links.end(),
                        [&](const auto &link) { return link.key == target.key; }))
        {
            add(linked, label, SamplerAutomationUiAction::select, source.id);
            add(unlink, label, SamplerAutomationUiAction::unlink, source.id);
            add(ranges, label, SamplerAutomationUiAction::select, source.id, true, true);
        }
    }
    menu.addSubMenu("Link to existing source", existing, existing.getNumItems() > 0);
    menu.addSubMenu("Select linked source in Playlist", linked, linked.getNumItems() > 0);
    menu.addSubMenu("Unlink linked source", unlink, unlink.getNumItems() > 0);
    menu.addSubMenu("Edit linked output range...", ranges, ranges.getNumItems() > 0);
    menu.showMenuAsync({},
                       [safe = juce::Component::SafePointer<Studio>(this), intent, choices](int choice)
                       {
                           if (!safe || choice <= 0 || choice > int(choices.size()))
                               return;
                           const auto selected = choices[size_t(choice - 1)];
                           if (selected.controller)
                           {
                               SamplerAutomationTarget current;
                               const auto result =
                                   intent.validate(safe->controller.project, safe->controller.projectIdentity,
                                                   safe->controller.revision, current);
                               if (result.wasOk())
                                   safe->showSamplerControllerLearn(intent.channel, intent.parameter);
                               else
                                   safe->status = result.getErrorMessage();
                               return;
                           }
                           if (selected.range)
                           {
                               safe->samplerAutomationRangeDialog(intent, selected.source);
                               return;
                           }
                           const auto result =
                               safe->performSamplerAutomationIntent(intent, selected.action, selected.source);
                           safe->status = result.wasOk() ? "Ready" : result.getErrorMessage();
                           safe->repaint();
                       });
}
void Studio::samplerAutomationRangeDialog(const SamplerAutomationUiIntent &intent, Id source)
{
    SamplerAutomationTarget target;
    if (auto result =
            intent.validate(controller.project, controller.projectIdentity, controller.revision, target);
        result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    if (auto result = admitMixerAutomation(); result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    const auto *item = controller.project.automationSource(source);
    const auto *link = item ? [&]() -> const AutomationLink *
    {
        for (const auto &candidate : item->links)
            if (candidate.key == target.key)
                return &candidate;
        return nullptr;
    }()
        : nullptr;
    if (!link)
    {
        status = "automation.missingLink: Source is not linked to this sampler control";
        return;
    }
    double minimum = 0, maximum = 1;
    if (!samplerAutomationFloatToPhysical(target.parameter, link->minimum, minimum) ||
        !samplerAutomationFloatToPhysical(target.parameter, link->maximum, maximum))
    {
        status = "automation.invalidRange: Stored sampler range is invalid";
        return;
    }
    auto units = juce::String(target.parameter.units.data(), target.parameter.units.size());
    if (target.parameter.timebase != SamplerAutomationTimebase::none)
    {
        const auto &settings = controller.project.channel(intent.channel)
                                   ->sampler.modulation[size_t(target.parameter.registryTarget)];
        const bool tempo = target.parameter.timebase == SamplerAutomationTimebase::envelopeTempo
                               ? settings.tempo
                               : settings.lfoTempo;
        units = tempo ? "quarter-note beats" : "seconds";
    }
    auto *dialog = new juce::AlertWindow("Sampler automation output range",
                                         target.name + "\nPhysical " + units +
                                             ". Inverted and constant ranges are allowed.",
                                         juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("minimum", juce::String(minimum, 15), "Output at source0");
    dialog->addTextEditor("maximum", juce::String(maximum, 15), "Output at source1");
    dialog->addButton("Apply", 1);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             view = juce::Component::SafePointer<juce::AlertWindow>(dialog), intent, source](int action)
            {
                if (!safe || !view || action != 1)
                    return;
                SamplerAutomationTarget current;
                auto result = intent.validate(safe->controller.project, safe->controller.projectIdentity,
                                              safe->controller.revision, current);
                if (result.wasOk())
                    result = safe->finishRackEditsChecked();
                if (result.wasOk())
                    result = intent.validate(safe->controller.project, safe->controller.projectIdentity,
                                             safe->controller.revision, current);
                if (result.wasOk())
                    result = safe->admitMixerAutomation();
                const auto parse = [](juce::String text, double &value)
                {
                    auto bytes = text.trim().toStdString();
                    if (!bytes.empty() && bytes.front() == '+')
                        bytes.erase(bytes.begin());
                    const auto parsed = vibera::number_text::fromChars(bytes.data(), bytes.data() + bytes.size(), value);
                    return parsed.ec == std::errc{} && parsed.ptr == bytes.data() + bytes.size() &&
                           std::isfinite(value);
                };
                double low = 0, high = 1;
                if (result.wasOk() && (!parse(view->getTextEditorContents("minimum"), low) ||
                                       !parse(view->getTextEditorContents("maximum"), high) ||
                                       !current.contains(low) || !current.contains(high)))
                    result = juce::Result::fail(
                        "automation.invalidRange: Finite values in the exact parameter domain required");
                if (result.wasOk())
                {
                    CommandArgs args;
                    args.channel = intent.channel;
                    args.samplerAutomationParameter = intent.parameter;
                    args.target = source;
                    args.beat = low;
                    args.length = high;
                    result = safe->controller.execute("automation.sampler.range", args);
                }
                safe->status = result.wasOk() ? "Ready" : result.getErrorMessage();
                safe->refresh();
            }),
        true);
}
juce::var Studio::samplerTempoOperation(const juce::var &args)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    validateMcpSchema(samplerTempoTool()["inputSchema"], args);
    const auto action = args["action"].toString();
    const bool targetAction = action == "state" || action == "capture" || action == "detect";
    if (targetAction != args.hasProperty("channel") || targetAction == args.hasProperty("operation"))
        throw McpFailure("invalid_arguments",
                         "Use channel for state/capture/detect, operation for job/cancel/apply");
    for (const auto *field : {"sourceBpm", "beats", "bars", "fit"})
        if (action != "apply" && args.hasProperty(field))
            throw McpFailure("invalid_arguments", "Tempo values apply only to apply");
    if (int(args.hasProperty("sourceBpm")) + int(args.hasProperty("beats")) + int(args.hasProperty("bars")) >
        1)
        throw McpFailure("invalid_arguments", "Specify only one of sourceBpm, beats or bars");
    if (args.hasProperty("projectIdentity") &&
        args["projectIdentity"].toString() != controller.projectIdentity && action != "job" &&
        action != "cancel")
        throw McpFailure("stale_project", "Project changed; capture the new source before applying tempo");
    auto result = juce::var(new juce::DynamicObject);
    auto set = [&](const char *key, juce::var value)
    { result.getDynamicObject()->setProperty(key, std::move(value)); };
    std::shared_ptr<SamplerTempoJob> job;
    if (targetAction)
    {
        const auto channel = Id(juce::int64(args["channel"]));
        const auto *current = controller.project.channel(channel);
        auto prepared = audio.channelSample(channel);
        if (!current || current->instrument || current->audioClip || !prepared || prepared->sampleRate <= 0 ||
            prepared->data.getNumSamples() == 0 || !audio.channelSampleMatches(channel, current->source))
            throw McpFailure("samplerTempo.unavailable",
                             "A prepared sampler Channel is required; wait for source preparation");
        if (action == "state")
        {
            set("completionState", "completed");
            set("phase", "state");
            set("channel", juce::int64(channel));
            set("sourceBpm", current->sampler.sourceTempo);
            set("fit", current->sampler.fitTempo);
            const auto seconds = prepared->data.getNumSamples() / prepared->sampleRate;
            set("sourceSeconds", seconds);
            set("trimmedSeconds", seconds * (current->sampler.end - current->sampler.start));
            set("method", "resample");
            set("changesPitch", true);
            set("projectIdentity", controller.projectIdentity);
            juce::Array<juce::var> operations;
            for (const auto &entry : samplerTempoJobs)
                if (entry->channel == channel && entry->project == controller.projectIdentity)
                    operations.add(entry->id);
            set("operations", operations); // UI-created sessions are discoverable by semantic clients.
            return result;
        }
        if (std::count_if(samplerTempoJobs.begin(), samplerTempoJobs.end(), [](const auto &entry)
                          { return !entry->finished.load(std::memory_order_acquire); }) >= 4)
            throw McpFailure("samplerTempo.busy", "Wait for or cancel pending analysis jobs");
        if (samplerTempoJobs.size() >= 32)
        {
            const auto oldest =
                std::find_if(samplerTempoJobs.begin(), samplerTempoJobs.end(), [](const auto &entry)
                             { return entry->finished.load(std::memory_order_acquire); });
            if (oldest == samplerTempoJobs.end())
                throw McpFailure("samplerTempo.busy", "No finished tempo session can be retired");
            samplerTempoJobs.erase(oldest);
        }
        job = std::make_shared<SamplerTempoJob>();
        job->id = "sampler-tempo-" + juce::Uuid().toString();
        job->channel = channel;
        job->project = controller.projectIdentity;
        job->source = current->source;
        job->settings = current->sampler;
        job->sample = std::move(prepared);
        job->analyzed = action == "detect";
        samplerTempoJobs.push_back(job);
        if (job->analyzed)
        {
            try
            {
                midiFileJobs.addJob(
                    [job]
                    {
                        try
                        {
                            job->estimate = estimateSampleTempo(*job->sample, &job->cancel);
                        }
                        catch (...)
                        {
                            job->analysisFailed = true;
                        }
                        job->finished.store(true, std::memory_order_release);
                    });
            }
            catch (...)
            {
                job->analysisFailed = true;
                job->finished.store(true, std::memory_order_release);
            }
        }
        else
            job->finished.store(true, std::memory_order_release);
    }
    else
    {
        const auto found =
            std::find_if(samplerTempoJobs.begin(), samplerTempoJobs.end(),
                         [&](const auto &entry) { return entry->id == args["operation"].toString(); });
        if (found == samplerTempoJobs.end())
            throw McpFailure("samplerTempo.unknownOperation",
                             "Tempo session expired or belongs to another application");
        job = *found;
    }
    const auto *current = controller.project.channel(job->channel);
    const bool stale = job->project != controller.projectIdentity || !current || current->instrument ||
                       current->audioClip || current->source != job->source ||
                       current->sampler != job->settings || audio.channelSample(job->channel) != job->sample;
    const bool finished = job->finished.load(std::memory_order_acquire);
    if (action == "cancel")
    {
        if (job->applied)
            throw McpFailure("samplerTempo.alreadyApplied", "Tempo was applied; use Undo to restore it");
        job->cancel.store(true, std::memory_order_relaxed);
    }
    bool changed = false;
    if (action == "apply")
    {
        if (job->applied)
            throw McpFailure("samplerTempo.alreadyApplied", "Capture a new session for another edit");
        if (!finished)
            throw McpFailure("samplerTempo.pending", "Analysis is not complete; poll job before applying");
        if (job->cancel.load(std::memory_order_relaxed))
            throw McpFailure("samplerTempo.cancelled", "Cancelled sessions cannot be applied");
        if (stale)
            throw McpFailure("samplerTempo.stale",
                             "Source or sampler settings changed; capture/analyze again");
        double bpm = args.hasProperty("sourceBpm") ? double(args["sourceBpm"])
                     : job->analyzed               ? job->estimate
                                                   : job->settings.sourceTempo;
        const bool musicalLength = args.hasProperty("beats") || args.hasProperty("bars");
        if (musicalLength)
        {
            const double beats = args.hasProperty("beats")
                                     ? double(args["beats"])
                                     : double(args["bars"]) * controller.project.numerator * 4. /
                                           controller.project.denominator;
            const auto seconds = job->sample->data.getNumSamples() / job->sample->sampleRate *
                                 (job->settings.end - job->settings.start);
            bpm = 60 * beats / seconds;
        }
        if (!std::isfinite(bpm) || bpm < 20 || bpm > 400)
            throw McpFailure("samplerTempo.range",
                             "Resulting source BPM must be20-400; enter another length or BPM");
        if (controller.hasGesture() || mcpPointerTarget || noteSlicePending() || noteStretchPending() ||
            devicePreparationBusy() || mixerChainBusy() || automationRecordingActive() ||
            bool(afterPluginState) || bool(pendingExport) || audio.rendering() || midiFileBusy)
            throw McpFailure("samplerTempo.busy", "Finish the active edit/preparation before applying tempo");
        CommandArgs edit;
        edit.channel = job->channel;
        edit.value = bpm;
        edit.enabled = args.hasProperty("fit") ? bool(args["fit"]) : musicalLength || job->settings.fitTempo;
        const auto before = controller.revision;
        command("channel.sampler.tempo", edit);
        if (status != "Ready")
            throw McpFailure("samplerTempo.applyFailed", status);
        job->applied = true;
        changed = before != controller.revision;
        job->documentChanged = changed;
        job->appliedBpm = bpm;
        job->appliedFit = edit.enabled;
    }
    const bool cancelled = job->cancel.load(std::memory_order_relaxed);
    const bool noEstimate = finished && job->analyzed && job->estimate <= 0;
    set("operation", job->id);
    set("channel", juce::int64(job->channel));
    set("sourceProjectIdentity", job->project);
    set("stale", !job->applied && stale);
    set("applied", job->applied);
    set("documentChanged", job->documentChanged);
    set("completionState", !finished                                ? "pending"
                           : cancelled                              ? "cancelled"
                           : !job->applied && (stale || noEstimate) ? "failed"
                                                                    : "completed");
    set("phase", job->applied ? "applied" : !finished ? "analysis" : "confirmation");
    set("estimateBpm", finished ? job->estimate : 0.);
    if (!job->applied && stale)
        set("error", "samplerTempo.stale");
    else if (!job->applied && !cancelled && finished && job->analysisFailed)
        set("error", "samplerTempo.analysisFailed");
    else if (!job->applied && !cancelled && noEstimate)
        set("error", "samplerTempo.noEstimate");
    set("sourceBpm", job->applied ? job->appliedBpm : job->settings.sourceTempo);
    set("fit", job->applied ? job->appliedFit : job->settings.fitTempo);
    set("audioUpdatePending", needsRebuild || engineRevision != controller.revision);
    return result;
}
juce::Result Studio::verifySamplerGestures(const juce::File &folder)
{
    stopTimer();
    if (auto result = folder.createDirectory(); result.failed())
        return result;
    auto fail = [](const juce::String &message) { return juce::Result::fail(message); };
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device)
        return fail("Sampler gesture virtual device unavailable");
    const auto source = folder.getChildFile("original-preview-tone.wav");
    {
        juce::AudioBuffer<float> pcm(2, 48000);
        for (int frame = 0; frame < 48000; ++frame)
            for (int channel = 0; channel < 2; ++channel)
                pcm.setSample(
                    channel, frame,
                    float(.25 * std::sin(2 * juce::MathConstants<double>::pi * 220 * frame / 48000.)));
        auto stream = source.createOutputStream();
        if (!stream)
            return fail("Sampler gesture tone stream unavailable");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 24, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, 48000))
            return fail("Sampler gesture tone write failed");
    }
    auto project = Project::empty();
    project.tempo = 240;
    project.channels.resize(2);
    project.channels[0].source = source.getFullPathName();
    project.channels[0].name = "SAMPLE";
    project.channels[0].pan = -1;
    project.channels[0].sampler.envelope = true;
    project.channels[1].source = "builtin:bass";
    project.channels[1].pan = 1;
    const auto first = project.channels[0].id, second = project.channels[1].id;
    project.placements.push_back({project.allocate(), project.patterns.front().id, 0, 0, 4, 0});
    controller.replace(project);
    selectedPattern = project.patterns.front().id;
    selectedChannel = second; // The open settings must target their Channel, not this selection.
    refresh();
    auto rebuild = [&] { return audio.rebuild(controller.project, patternMode, selectedPattern); };
    if (auto result = rebuild(); result.failed())
        return result;
    openInstrument(first);
    selectedChannel = second;
    refresh();
    auto *window = instrumentWindows[first].get();
    if (!window)
        return fail("Sampler settings did not open");
    // This owned GUI test must expose its target above unrelated desktop windows.
    // JUCE's keyboard hit test includes native WindowFromPoint occlusion checks.
    window->setAlwaysOnTop(true);
    window->toFront(true);
    auto *controls = window->controlsComponent();
    auto *range = dynamic_cast<SamplerRangeView *>(controls->findChildWithID("instrument.sampler.waveform"));
    auto *keyboard =
        dynamic_cast<SamplerKeyboard *>(controls->findChildWithID("instrument.sampler.keyboard"));
    auto *showKeys =
        dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.showKeyboard"));
    if (!range || !keyboard || !showKeys)
        return fail("Sampler gesture components missing");
    auto event = [](juce::Component &component, juce::Point<float> point, int flags)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(flags), 1, 0, 0, 0, 0, &component, &component, now, point,
                                now, 1, false);
    };
    const auto original = controller.project.state();
    auto *normalize = dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.normalize"));
    if (!normalize || !normalize->isEnabled())
        return fail("Normalize control missing or unavailable on a real sample");
    normalize->setToggleState(true, juce::dontSendNotification);
    normalize->onClick();
    if (!controller.project.channel(first)->sampler.normalize ||
        controller.project.channel(second)->sampler.normalize || selectedChannel != second)
        return fail("Normalize checkbox changed the wrong Channel/MIDI target");
    {
        auto stream = folder.getChildFile("sampler-normalize.png").createOutputStream();
        if (!stream)
            return fail("Normalize screenshot unavailable");
        stream->setPosition(0);
        stream->truncate();
        juce::PNGImageFormat png;
        png.writeImageToStream(controls->createComponentSnapshot(controls->getLocalBounds()), *stream);
    }
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Normalize checkbox is not one exact Undo");
    auto *cut = dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.cut"));
    auto *cutBy = dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.cutBy"));
    auto *maxPoly = dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.maxPoly"));
    auto *self = dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.cutItself"));
    if (!cut || !cutBy || !maxPoly || !self)
        return fail("Sampler voice controls missing");
    // An unrelated UI baseline must not be absorbed by this window. Then use
    // the actual instrument drag handlers so the fixture owns its real serial.
    if (!cut->onDragStart || !cut->onDragEnd || !controller.beginGesture())
        return fail("Sampler voice drag/foreign-owner fixture unavailable");
    cut->setValue(4, juce::sendNotificationSync);
    if (!controller.project.state().isEquivalentTo(original) ||
        !status.startsWith("Finish or cancel the current edit"))
        return fail("Sampler control joined an unrelated UI gesture");
    if (auto result = controller.cancelGestureChecked(); result.failed()) return result;
    cut->onDragStart();
    cut->setValue(4, juce::sendNotificationSync);
    cutBy->setValue(4, juce::sendNotificationSync);
    maxPoly->setValue(1, juce::sendNotificationSync);
    cut->onDragEnd();
    const auto voiceState = controller.project.state();
    const auto &voiceSettings = controller.project.channel(first)->sampler;
    if (voiceSettings.cut != 4 || voiceSettings.cutBy != 4 || voiceSettings.maxPoly != 1 ||
        controller.project.channel(second)->sampler.cut != 0 || selectedChannel != second)
        return fail("Voice controls changed the wrong Channel or MIDI target");
    {
        auto stream = folder.getChildFile("sampler-voices.png").createOutputStream();
        if (!stream)
            return fail("Voice screenshot unavailable");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(
                window->createComponentSnapshot(window->getLocalBounds()), *stream))
            return fail("Voice screenshot failed");
    }
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Voice controls one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(voiceState))
        return fail("Voice controls exact Redo");
    controller.undoEdit();
    self->triggerClick();
    dispatchMessages(20);
    const auto &assigned = controller.project.channel(first)->sampler;
    if (assigned.cut <= 0 || assigned.cut != assigned.cutBy ||
        controller.project.channel(second)->sampler.cut != 0)
        return fail("Actual Cut itself button did not assign a matching pair on its own Channel");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Cut itself one exact Undo");
    folder.getChildFile("sampler-voices-gui.txt")
        .replaceWithText("PASS actual Cut/Cut-by/Max-poly/Cut-itself component callbacks, fixed Channel "
                         "targeting and grouped exact Undo/Redo. No OS input or listening claim.\n");
#include "../tests/SamplerVoiceModesUiAcceptance.inc"
#include "../tests/SamplerVoiceModesShortcutAcceptance.inc"
#include "../tests/SamplerFilterUiAcceptance.inc"
#include "../tests/SamplerEnvelopeUiAcceptance.inc"
    auto &keys = keyboard->keyComponent();
    auto keyPoint = [&](int pitch)
    {
        const auto rectangle = keys.getRectangleForKey(pitch);
        return juce::Point<float>(rectangle.getCentreX(), rectangle.getBottom() - 8);
    };
    showKeys->triggerClick();
    // triggerClick posts a JUCE command; the viewport and native peer can finish
    // layout later. Prove that the requested key is a real hit target before
    // injecting its mouse event rather than assuming a 12 ms delay is enough.
    const auto keyboardDeadline = juce::Time::getMillisecondCounter() + 2000;
    while (!keys.reallyContains(keyPoint(72), false) &&
           juce::Time::getMillisecondCounter() < keyboardDeadline)
        dispatchMessages(5);
    if (!keys.reallyContains(keyPoint(72), false))
    {
        juce::String details;
        for (juce::Component *component = &keys; component; component = component->getParentComponent())
            details += component->getComponentID() + " bounds=" + component->getBounds().toString() +
                       " visible=" + juce::String(int(component->isVisible())) + "\n";
        if (auto *viewport = keyboard->findParentComponentOfClass<juce::Viewport>())
            details += "Viewport position=" + viewport->getViewPosition().toString() +
                       " height=" + juce::String(viewport->getViewHeight());
#if JUCE_WINDOWS
        if (auto *peer = window->getPeer())
        {
            const auto handle = static_cast<HWND>(peer->getNativeHandle());
            const auto local = window->getLocalPoint(&keys, keyPoint(72));
            const auto scale = peer->getPlatformScaleFactor();
            POINT point{juce::roundToInt(local.x * scale), juce::roundToInt(local.y * scale)};
            ClientToScreen(handle, &point);
            DWORD hitPid = 0;
            const auto hit = WindowFromPoint(point);
            GetWindowThreadProcessId(hit, &hitPid);
            details += "\nNative visible=" + juce::String(int(IsWindowVisible(handle))) +
                       " iconic=" + juce::String(int(IsIconic(handle))) + " scale=" + juce::String(scale) +
                       " screen=" + juce::String(point.x) + "," + juce::String(point.y) +
                       " ownHit=" + juce::String(int(hit == handle)) + " hitPid=" + juce::String(int(hitPid));
        }
#endif
        auto stream = folder.getChildFile("sampler-key-target-failure.png").createOutputStream();
        if (stream)
        {
            stream->setPosition(0);
            stream->truncate();
            juce::PNGImageFormat().writeImageToStream(
                window->createComponentSnapshot(window->getLocalBounds()), *stream);
        }
        return fail("Show keyboard did not expose its root-key click target\n" + details);
    }
    keys.mouseDown(event(keys, keyPoint(72), juce::ModifierKeys::rightButtonModifier));
    keys.mouseUp(event(keys, keyPoint(72), 0));
    if (controller.project.channel(first)->sampler.root != 72 ||
        controller.project.channel(second)->sampler.root != -1)
        return fail("Right-click root targeted the wrong Channel: first=" +
                    juce::String(controller.project.channel(first)->sampler.root) +
                    " second=" + juce::String(controller.project.channel(second)->sampler.root) +
                    " enabled=" + juce::String(int(keyboard->isEnabled())) +
                    " prepared=" + juce::String(int(bool(audio.channelSample(first)))) +
                    " point=" + keyPoint(72).toString() + " keys=" + keys.getBounds().toString());
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Root gesture exact Undo failed");
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = .25f;
    const double fraction = .3;
    range->mouseWheelMove(event(*range, {8.f + float(fraction) * (range->getWidth() - 16), 32}, 0), wheel);
    if (range->viewLength() >= 1 ||
        std::abs(range->viewStart() + fraction * range->viewLength() - fraction) > 1e-6 ||
        !controller.project.state().isEquivalentTo(original))
        return fail("Waveform pointer-anchor zoom changed source/model: enabled=" +
                    juce::String(int(range->isEnabled())) + " span=" + juce::String(range->viewLength(), 9) +
                    " begin=" + juce::String(range->viewStart(), 9) +
                    " width=" + juce::String(range->getWidth()) +
                    " model=" + juce::String(int(controller.project.state().isEquivalentTo(original))));
    range->setView(.2, .4);
    auto *scroll =
        dynamic_cast<juce::ScrollBar *>(range->findChildWithID("instrument.sampler.waveform.scroll"));
    if (!scroll)
        return fail("Waveform horizontal scrollbar missing");
    scroll->setCurrentRangeStart(.3, juce::sendNotification);
    dispatchMessages(20);
    if (std::abs(range->viewStart() - .3) > 1e-8 || range->viewLength() != .4)
        return fail("Waveform scrollbar pan failed");
    const juce::KeyPress zoomKey('j', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier,
                                 0);
    auto previousBindings = customization.bindings;
    if (customization.bind("sampler.view.zoomIn", Scope::sampler, zoomKey).failed() ||
        !range->keyPressed(zoomKey) || std::abs(range->viewLength() - .2) > 1e-8)
        return fail("Remapped waveform zoom shortcut failed");
    customization.bindings = std::move(previousBindings);
    range->navigation("sampler.view.all");
    auto startX = [&]
    { return 8.f + float(controller.project.channel(first)->sampler.start) * (range->getWidth() - 16); };
    range->mouseDown(event(*range, {startX(), 8}, juce::ModifierKeys::leftButtonModifier));
    range->mouseDrag(
        event(*range, {8.f + .25f * (range->getWidth() - 16), 8}, juce::ModifierKeys::leftButtonModifier));
    range->mouseDrag(
        event(*range, {8.f + .3f * (range->getWidth() - 16), 8}, juce::ModifierKeys::leftButtonModifier));
    range->mouseUp(event(*range, {0, 0}, 0));
    if (std::abs(controller.project.channel(first)->sampler.start - .3) > .00001)
        return fail("Sample handle drag did not update source bounds");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Sample drag must be one exact Undo");
    range->mouseDown(event(*range, {startX(), 8}, juce::ModifierKeys::leftButtonModifier));
    range->mouseDrag(
        event(*range, {8.f + .4f * (range->getWidth() - 16), 8}, juce::ModifierKeys::leftButtonModifier));
    if (!range->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) ||
        !controller.project.state().isEquivalentTo(original))
        return fail("Escape did not cancel the sample handle transaction");
    range->mouseUp(event(*range, {0, 0}, 0));
    CommandArgs args;
    args.channel = first;
    args.beat = .2;
    args.length = .6;
    command("channel.sampler.range", args);
    range->mouseDown(event(*range, {startX(), 8},
                           juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    range->mouseDrag(event(*range, {startX() + .1f * (range->getWidth() - 16), 8},
                           juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    range->mouseUp(event(*range, {0, 0}, 0));
    const auto moved = controller.project.channel(first)->sampler;
    if (std::abs(moved.start - .3) > .00001 || std::abs(moved.end - .7) > .00001)
        return fail("Shift sample-range drag changed its length");
    controller.undoEdit();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Range command/gesture Undo failed");
    if (auto result = rebuild(); result.failed())
        return result;
    juce::String report;
    juce::AudioBuffer<float> block(2, 256);
    for (int mode = 0; mode < 3; ++mode)
    {
        patternMode = mode != 2;
        audio.stop();
        if (auto result = rebuild(); result.failed())
            return result;
        audio.setMidiTarget(second);
        dispatchMessages(30);
        audio.seekBeat(0);
        audio.setLooping(true);
        if (mode)
            audio.playStop();
        int callbacks = 0, overruns = 0;
        std::array<double, 1024> durations{};
        std::array<std::uint64_t, 1024> cycles{};
        std::array<int, 1024> phases{}, gestures{};
        int phase = 0, gesture = -1;
#if JUCE_WINDOWS
        const auto observedThread = GetCurrentThreadId();
        const auto observedPriority = GetThreadPriority(GetCurrentThread());
#endif
        double maximum = 0;
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
                if (callbacks >= int(durations.size()))
                    throw std::runtime_error("Sampler gesture callback trace capacity exceeded");
#if JUCE_WINDOWS
                ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                const bool cycleClock = QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore) != 0;
#endif
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ms =
                    juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began) *
                    1000;
#if JUCE_WINDOWS
                if (cycleClock && QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter) &&
                    cyclesAfter >= cyclesBefore)
                    cycles[size_t(callbacks)] = cyclesAfter - cyclesBefore;
#endif
                durations[size_t(callbacks)] = ms;
                phases[size_t(callbacks)] = phase;
                gestures[size_t(callbacks)] = gesture;
                maximum = std::max(maximum, ms);
                overruns += ms > 256000. / 48000.;
                ++callbacks;
            }
        };
        audio.previewChannelNote(first, 60, true);
        tick(3);
        if (mode == 1)
        {
            needsRebuild = false; // This fixture has just applied its projection explicitly.
            auto object = [](std::initializer_list<std::pair<const char *, juce::var>> fields)
            {
                juce::var value(new juce::DynamicObject);
                for (const auto &field : fields)
                    value.getDynamicObject()->setProperty(field.first, field.second);
                return value;
            };
            auto state = [&] { return mcpCall("vibera_state", object({}))["structuredContent"]; };
            const auto beforeLoop = controller.project.state();
            const auto loopRevision = controller.revision;
            loopButton.onClick();
            if (audio.looping())
                return fail("Actual Loop button did not disable playback looping");
            auto enable = object({{"action", "loop"}, {"enabled", true}});
            mcpCall("vibera_transport", enable);
            mcpCall("vibera_transport", enable);
            if (!audio.looping() || controller.revision != loopRevision)
                return fail("Semantic Loop enable is not idempotent/document-independent");
            const auto oldLength = controller.project.pattern(selectedPattern)->length;
            mcpCall("vibera_command",
                    object({{"command", "pattern.length"},
                            {"args", object({{"pattern", juce::int64(selectedPattern)}, {"length", 8.}})}}));
            timerCallback();
            const auto bounds = state()["audio"]["loopBounds"];
            if (!bool(bounds["appliedAvailable"]) || !bool(bounds["appliedEnabled"]) ||
                double(bounds["endBeat"]) != 8 || double(bounds["appliedEndBeat"]) != 8)
                return fail("Actual MCP service did not read back the applied virtual loop after growth");
            mcpCall("vibera_command", object({{"command", "edit.undo"}}));
            timerCallback();
            if (!controller.project.state().isEquivalentTo(beforeLoop) ||
                double(state()["audio"]["loopBounds"]["appliedEndBeat"]) != oldLength)
                return fail("MCP loop grow Undo did not restore exact model/applied range");
            record.onClick();
            if (!midiTaking || !recordArmed)
                return fail("Loop guard fixture did not start a real MIDI take");
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::noteOn(1, 48, juce::uint8(100)));
            tick(4);
            timerCallback();
            bool refused = false;
            try
            {
                mcpCall("vibera_transport", object({{"action", "loop"}, {"enabled", false}}));
            }
            catch (const McpFailure &error)
            {
                refused = error.code == "busy";
            }
            loopButton.onClick();
            if (!refused || !audio.looping() || !midiTaking || !recordArmed ||
                !controller.project.state().isEquivalentTo(beforeLoop))
                return fail(
                    "Loop mode change interrupted the existing held MIDI take or escaped shared admission");
            normalize->setToggleState(true, juce::dontSendNotification);
            normalize->onClick();
            mcpCall("vibera_command",
                    object({{"command", "channel.sampler.normalize"},
                            {"args", object({{"channel", juce::int64(second)}, {"enabled", true}})}}));
            tick(2);
            timerCallback();
            if (!midiTaking || !recordArmed || !audio.midiInput.capture.active ||
                !controller.project.channel(first)->sampler.normalize ||
                !controller.project.channel(second)->sampler.normalize ||
                audio.projectionDiagnostics().channels[1].heldInputKeys != 1)
                return fail("UI/MCP Normalize interrupted the actual held MIDI take");
            normalize->setToggleState(false, juce::dontSendNotification);
            normalize->onClick();
            mcpCall("vibera_command",
                    object({{"command", "channel.sampler.normalize"},
                            {"args", object({{"channel", juce::int64(second)}, {"enabled", false}})}}));
            tick(2);
            timerCallback();
            folder.getChildFile("normalize-mcp-gui.txt")
                .replaceWithText("PASS actual Normalize checkbox/target/exact Undo and shared MCP setting "
                                 "during a real held MIDI take; no OS-input/listening claim.\n");
            const auto beforeFilterTake = controller.project.state();
            const auto filterLease = mcpCall(
                "vibera_gesture",
                object({{"action", "begin"}, {"domain", "samplerFilter"}}))["structuredContent"]["operation"];
            const auto filterUpdate =
                mcpCall("vibera_gesture",
                        object({{"action", "update"},
                                {"operation", filterLease},
                                {"command", "channel.sampler.filterCutoff"},
                                {"args", object({{"channel", juce::int64(second)}, {"value", .6}})}}));
            tick(2);
            if (filterUpdate["structuredContent"]["completionState"].toString() != "pending" || !midiTaking ||
                !recordArmed || !audio.midiInput.capture.active ||
                controller.project.channel(second)->sampler.filterCutoff != .6 ||
                audio.projectionDiagnostics().channels[1].heldInputKeys != 1)
                return fail("Sampler filter MCP lease interrupted the actual held MIDI take");
            const auto filterCancel =
                mcpCall("vibera_gesture", object({{"action", "cancel"}, {"operation", filterLease}}));
            tick(2);
            if (filterCancel["structuredContent"]["completionState"].toString() != "cancelled" ||
                !controller.project.state().isEquivalentTo(beforeFilterTake) || !midiTaking ||
                audio.projectionDiagnostics().channels[1].heldInputKeys != 1)
                return fail("Sampler filter held-take cancellation changed source/input ownership");
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 48));
            tick(4);
            finishMidiTake(true); // Cancel only this disposable fixture take.
            refresh();
            folder.getChildFile("loop-bounds-mcp-gui.txt")
                .replaceWithText("PASS actual Loop component callback, shared MCP-service "
                                 "enable/idempotence, model/applied virtual readback, growth/exact Undo and "
                                 "held-take busy admission. Not OS input or listening.\n");
        }
        keyboard->release();
        phase = 1;
        tick(6);
        audio.midiInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 48, juce::uint8(100)));
        phase = 2;
        tick(4);
        const auto baseline = audio.projectionDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto capture = audio.midiInput.capture.write.load(),
                   history = audio.midiInput.history.write.load();
        for (int i = 0; i < 64; ++i)
        {
            phase = 3;
            gesture = i;
            const auto point = keyPoint(60 + i % 5);
            keys.mouseDown(event(keys, point, juce::ModifierKeys::leftButtonModifier));
            tick(2);
            if (i % 8 == 0)
            {
                auto *cutoff = dynamic_cast<juce::Slider *>(
                    controls->findChildWithID("instrument.sampler.filterCutoff"));
                if (!cutoff)
                    return fail("Live sampler filter control unavailable");
                const auto beforeFilter = controller.project.state();
                cutoff->onDragStart();
                cutoff->setValue(55. + i / 2., juce::sendNotificationSync);
                cutoff->onDragEnd();
                tick(2);
                const auto editedFilter = controller.project.state();
                controller.undoEdit();
                tick(2);
                if (!controller.project.state().isEquivalentTo(beforeFilter))
                    return fail("Live filter edit Undo changed the surrounding musical state");
                controller.redoEdit();
                tick(2);
                if (!controller.project.state().isEquivalentTo(editedFilter))
                    return fail("Live filter edit Redo failed");
                controller.undoEdit();
                tick(2);
                if (!controller.project.state().isEquivalentTo(beforeFilter))
                    return fail("Live filter history did not restore the original fixture");
            }
            if (block.getMagnitude(0, 0, 256) < .0001f || block.getMagnitude(1, 0, 256) < .0001f)
                return fail("Keyboard audition did not sound alongside the held independent MIDI input");
            phase = 4;
            if (i % 4 == 0)
                keys.focusLost(juce::Component::focusChangedDirectly);
            else if (i % 4 == 1)
                keyboard->release();
            else if (i % 4 == 2)
            {
                window->closeButtonPressed();
                window->setVisible(true);
                window->toFront(true); // Restore this window without selecting/retargeting the Channel.
                if (instrumentWindows[first].get() != window)
                    return fail("Sampler reopen duplicated its window");
            }
            else
                keys.mouseUp(event(keys, point, 0));
            tick(4);
            if (i == 8)
            {
                phase = 7;
                auto object = [](std::initializer_list<std::pair<const char *, juce::var>> fields)
                {
                    juce::var value(new juce::DynamicObject);
                    for (const auto &field : fields)
                        value.getDynamicObject()->setProperty(field.first, field.second);
                    return value;
                };
                const auto beforeTempo = controller.project.state();
                const auto session =
                    samplerTempoOperation(object({{"action", "capture"}, {"channel", juce::int64(first)}}));
                const auto applied = samplerTempoOperation(object({{"action", "apply"},
                                                                   {"operation", session["operation"]},
                                                                   {"sourceBpm", 100.},
                                                                   {"fit", true}}));
                if (!bool(applied["applied"]) || applied["completionState"].toString() != "completed")
                    return fail("Live shared sampler tempo did not actually apply");
                for (int historyStep = 0; historyStep < 4; ++historyStep)
                {
                    if (historyStep == 1 || historyStep == 3)
                        controller.undoEdit();
                    if (historyStep == 2)
                        controller.redoEdit();
                    if (auto result = rebuild(); result.failed())
                        return result;
                    tick(2);
                    if (block.getMagnitude(1, 0, 256) < .0001f || !audio.midiInput.capture.active ||
                        audio.playing() != (mode != 0))
                        return fail("Tempo fit/history interrupted unaffected held MIDI or transport");
                }
                if (!controller.project.state().isEquivalentTo(beforeTempo))
                    return fail("Tempo fit/history did not restore exact sampler source settings");
            }
            if (i == 16 || i == 32)
            {
                range->mouseDown(event(*range, {startX(), 8}, juce::ModifierKeys::leftButtonModifier));
                range->mouseDrag(event(*range, {8.f + .15f * (range->getWidth() - 16), 8},
                                       juce::ModifierKeys::leftButtonModifier));
                if (auto result = rebuild(); result.failed())
                    return result;
                if (i == 16)
                {
                    range->mouseUp(event(*range, {0, 0}, 0));
                    controller.undoEdit();
                }
                else
                    range->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
                if (auto result = rebuild(); result.failed())
                    return result;
                if (!controller.project.state().isEquivalentTo(original))
                    return fail("Live range Undo/Escape changed unrelated state");
            }
            if (block.getMagnitude(0, 0, 256) != 0 || block.getMagnitude(1, 0, 256) < .0001f)
                return fail("Preview release/focus loss: mode=" + juce::String(mode) + " cycle=" +
                            juce::String(i) + " left=" + juce::String(block.getMagnitude(0, 0, 256), 9) +
                            " right=" + juce::String(block.getMagnitude(1, 0, 256), 9));
            if (i == 48)
            {
                phase = 6;
                const auto beforeVoices = controller.project.state();
                cut->onDragStart();
                cut->setValue(3, juce::sendNotificationSync);
                cutBy->setValue(3, juce::sendNotificationSync);
                maxPoly->setValue(2, juce::sendNotificationSync);
                cut->onDragEnd();
                if (auto result = rebuild(); result.failed())
                    return result;
                tick(2);
                controller.undoEdit();
                if (auto result = rebuild(); result.failed())
                    return result;
                tick(2);
                if (!controller.project.state().isEquivalentTo(beforeVoices) ||
                    block.getMagnitude(1, 0, 256) < .0001f || !audio.midiInput.capture.active)
                    return fail("Live voice widgets/history interrupted another Channel or its capture");
                // Exercise the new actual fixed-Channel widgets while the
                // unrelated score/MIDI take continues through real callbacks.
                for (const auto *name : {"mono", "portamento", "fullPortamento", "slideTime", "gateBeats"})
                {
                    const auto id = juce::String("instrument.sampler.") + name;
                    if (auto *button = dynamic_cast<juce::Button *>(controls->findChildWithID(id)))
                    {
                        button->setToggleState(true, juce::dontSendNotification);
                        button->onClick();
                    }
                    else if (auto *slider = dynamic_cast<juce::Slider *>(controls->findChildWithID(id)))
                    {
                        slider->onDragStart();
                        slider->setValue(id.endsWith("slideTime") ? 125. : .5, juce::sendNotificationSync);
                        slider->onDragEnd();
                    }
                    else
                        return fail("New sampler voice widget missing during live playback: " + id);
                    if (auto result = rebuild(); result.failed())
                        return result;
                    tick(1);
                    controller.undoEdit();
                    if (auto result = rebuild(); result.failed())
                        return result;
                    tick(1);
                    if (!controller.project.state().isEquivalentTo(beforeVoices) ||
                        block.getMagnitude(1, 0, 256) < .0001f || !audio.midiInput.capture.active)
                        return fail("Live voice-mode edit/history lost the unrelated score/take: " + id);
                }
            }
        }
        for (int step = 0; step < 32; ++step)
        {
            phase = 5;
            gesture = step;
            wheel.deltaY = step % 2 ? -.08f : .1f;
            range->mouseWheelMove(event(*range, {range->getWidth() * .5f, 32}, 0), wheel);
            tick(2);
        }
        range->navigation("sampler.view.all");
        juce::String trace = "callback,phase,gesture,wallMs,threadCycles,deadlineOverrun\n";
        for (int i = 0; i < callbacks; ++i)
            trace += juce::String(i) + "," + juce::String(phases[size_t(i)]) + "," +
                     juce::String(gestures[size_t(i)]) + "," + juce::String(durations[size_t(i)], 6) + "," +
                     juce::String(juce::int64(cycles[size_t(i)])) + "," +
                     juce::String(int(durations[size_t(i)] > 256000. / 48000.)) + "\n";
        folder.getChildFile("sampler-gesture-" + juce::String(mode) + "-callbacks.csv")
            .replaceWithText(trace);
#if JUCE_WINDOWS
        folder.getChildFile("sampler-gesture-" + juce::String(mode) + "-thread.txt")
            .replaceWithText("Owned synthetic caller TID=" + juce::String(int(observedThread)) +
                             " priority=" + juce::String(observedPriority) +
                             "; no priority/buffer/gate change. Phases0 entry,1 release warmup,2 held MIDI,3 "
                             "keyboard-on,4 release/focus/close,5 wheel,6 voice widgets/history,7 tempo fit/history. Raw cycles "
                             "are not CPU-time "
                             "conversion or a failure explanation.\n");
#endif
        if (callbacks != (mode == 1 ? 485 : 473) + 64 + (mode == 1 ? 4 : 0) + 10)
            return fail("Incomplete sampler gesture diagnostic callback capture");
        const auto after = audio.projectionDiagnostics();
        if (!controller.project.state().isEquivalentTo(original) || overruns || after.edit != baseline.edit ||
            after.playbackContext != baseline.playbackContext ||
            after.replacements != baseline.replacements ||
            after.cache.preparations != baseline.cache.preparations || device->starts != starts ||
            device->stops != stops || !audio.midiInput.capture.active ||
            audio.midiInput.capture.write != capture || audio.midiInput.history.write != history)
            return fail(
                "Sampler gesture playback/model/MIDI/lifecycle invariant failed: mode=" + juce::String(mode) +
                " maxMs=" + juce::String(maximum, 4) + " overruns=" + juce::String(overruns) +
                " model=" + juce::String(int(controller.project.state().isEquivalentTo(original))) +
                " edit=" + juce::String(int(after.edit == baseline.edit)) +
                " context=" + juce::String(int(after.playbackContext == baseline.playbackContext)) +
                " replacements=" + juce::String(int(after.replacements == baseline.replacements)) +
                " preparations=" +
                juce::String(int(after.cache.preparations == baseline.cache.preparations)) +
                " starts=" + juce::String(int(device->starts == starts)) +
                " stops=" + juce::String(int(device->stops == stops)) +
                " captureActive=" + juce::String(int(bool(audio.midiInput.capture.active))) +
                " capture=" + juce::String(int(audio.midiInput.capture.write == capture)) +
                " history=" + juce::String(int(audio.midiInput.history.write == history)));
        report +=
            juce::String(mode == 0   ? "Stopped"
                         : mode == 1 ? "Pattern"
                                     : "Song") +
            ": callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
            " overruns=0; independent MIDI capture/voice retained; document/graph/device/PCM retained\n";
        audio.midiInput.capture.active = false;
        audio.midiInput.panic();
        tick(8);
    }
    audio.stop();
    auto *loopToggle = dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.loop"));
    auto *pingPongToggle =
        dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.pingPong"));
    auto *loopStart = dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.loopStart"));
    auto *loopEnd = dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.loopEnd"));
    if (!loopToggle || !pingPongToggle || !loopStart || !loopEnd)
        return fail("Sampler loop controls missing");
    loopStart->setValue(25, juce::sendNotificationSync);
    loopEnd->setValue(75, juce::sendNotificationSync);
    loopToggle->triggerClick();
    dispatchMessages(20);
    pingPongToggle->triggerClick();
    dispatchMessages(20);
    const auto &loopSettings = controller.project.channel(first)->sampler;
    if (!loopSettings.loop || !loopSettings.pingPong || loopSettings.loopStart != .25 ||
        loopSettings.loopEnd != .75 || controller.project.channel(second)->sampler.loop)
        return fail("Sampler loop controls targeted wrong settings/Channel");
    const auto beforeLoopGesture = controller.project.state();
    const float loopY = (range->getHeight() - 4) * .5f;
    range->mouseDown(event(*range, {range->sourceX(.25), loopY}, juce::ModifierKeys::leftButtonModifier));
    range->mouseDrag(event(*range, {range->sourceX(.3), loopY}, juce::ModifierKeys::leftButtonModifier));
    range->mouseUp(event(*range, {0, 0}, 0));
    if (std::abs(controller.project.channel(first)->sampler.loopStart - .3) > 1e-6 ||
        controller.project.channel(first)->sampler.start != 0)
        return fail("Loop handle modified source trim");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeLoopGesture))
        return fail("Loop handle exact one-Undo failed");
    range->mouseDown(event(*range, {range->sourceX(.25), loopY}, juce::ModifierKeys::leftButtonModifier));
    range->mouseDrag(event(*range, {range->sourceX(.35), loopY},
                           juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    if (std::abs(controller.project.channel(first)->sampler.loopStart - .35) > 1e-6 ||
        std::abs(controller.project.channel(first)->sampler.loopEnd - .85) > 1e-6)
        return fail("Shift loop handles did not move together");
    range->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!controller.project.state().isEquivalentTo(beforeLoopGesture))
        return fail("Loop handle Escape failed");
    if (auto *viewport = dynamic_cast<juce::Viewport *>(window->getContentComponent()->getChildComponent(0)))
        viewport->setViewPosition(0, 0);
    auto loopStream = folder.getChildFile("sampler-loop-handles.png").createOutputStream();
    if (!loopStream)
        return fail("Loop screenshot unavailable");
    loopStream->setPosition(0);
    loopStream->truncate();
    if (!juce::PNGImageFormat().writeImageToStream(window->createComponentSnapshot(window->getLocalBounds()),
                                                   *loopStream))
        return fail("Loop screenshot failed");
    for (int i = 0; i < 4; ++i)
        controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Sampler loop controls exact Undo failed");
    dispatchMessages(220);
    report += "Actual Use loop points/Ping pong/loop start/end controls, waveform handles/Shift/Escape and "
              "exact Undo passed.\n";
    showKeys->triggerClick();
    dispatchMessages(20);
    for (auto name : {"sampler-keyboard.png", "sampler-source-handles.png"})
    {
        if (juce::String(name).contains("source"))
            if (auto *viewport =
                    dynamic_cast<juce::Viewport *>(window->getContentComponent()->getChildComponent(0)))
                viewport->setViewPosition(0, 0);
        auto stream = folder.getChildFile(name).createOutputStream();
        if (!stream)
            return fail("Sampler gesture screenshot stream unavailable");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(
                window->createComponentSnapshot(window->getLocalBounds()), *stream))
            return fail("Sampler gesture screenshot write failed");
    }
    CommandArgs ranged;
    ranged.channel = first;
    ranged.beat = .4;
    ranged.length = .6;
    command("channel.sampler.range", ranged);
    range->navigation("sampler.view.range");
    const auto beforeFine = controller.project.state();
    range->mouseDown(event(*range, {range->sourceX(.4), 8}, juce::ModifierKeys::leftButtonModifier));
    range->mouseDrag(event(*range, {range->sourceX(.401), 8}, juce::ModifierKeys::leftButtonModifier));
    range->mouseUp(event(*range, {0, 0}, 0));
    if (std::abs(controller.project.channel(first)->sampler.start - .401) > 1e-6)
        return fail("Zoomed handle source mapping failed");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeFine))
        return fail("Zoomed handle exact Undo failed");
    dispatchMessages(220); // Reconcile slider labels after Undo before visual evidence.
    auto waveStream = folder.getChildFile("sampler-waveform-zoom.png").createOutputStream();
    if (!waveStream)
        return fail("Zoom screenshot unavailable");
    waveStream->setPosition(0);
    waveStream->truncate();
    if (auto *viewport = dynamic_cast<juce::Viewport *>(window->getContentComponent()->getChildComponent(0)))
        viewport->setViewPosition(0, 0);
    if (!juce::PNGImageFormat().writeImageToStream(window->createComponentSnapshot(window->getLocalBounds()),
                                                   *waveStream))
        return fail("Zoom screenshot failed");
    controller.undoEdit();
    range->navigation("sampler.view.all");
    report += "Waveform pointer-anchor zoom, scrollbar, remapped shortcut, zoomed range/one-Undo and live "
              "view changes passed.\n";
    CommandArgs tempoArgs;
    tempoArgs.channel = first;
    tempoArgs.value = 120;
    auto *setBpm = dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.setTempo"));
    if (!setBpm)
        return fail("Set tempo button missing");
    setBpm->triggerClick();
    dispatchMessages(20);
    auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog || !dialog->getTextEditor("bpm"))
        return fail("Set tempo dialog missing");
    dialog->getTextEditor("bpm")->setText("120");
    dialog->exitModalState(1);
    dispatchMessages(20);
    if (controller.project.channel(first)->sampler.sourceTempo != 120)
        return fail("Set tempo dialog failed");
    auto *fit = dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.fitTempo"));
    if (!fit)
        return fail("Fit tempo button missing");
    fit->triggerClick();
    dispatchMessages(20);
    if (!controller.project.channel(first)->sampler.fitTempo)
        return fail("Fit tempo click failed");
    controller.undoEdit();
    if (controller.project.channel(first)->sampler.fitTempo)
        return fail("Fit tempo undo failed");
    fit->triggerClick();
    dispatchMessages(20);
    if (auto *viewport = dynamic_cast<juce::Viewport *>(window->getContentComponent()->getChildComponent(0)))
        viewport->setViewPosition(0, 0);
    auto tempoStream = folder.getChildFile("sampler-tempo.png").createOutputStream();
    if (!tempoStream)
        return fail("Tempo screenshot unavailable");
    tempoStream->setPosition(0);
    tempoStream->truncate();
    if (!juce::PNGImageFormat().writeImageToStream(window->createComponentSnapshot(window->getLocalBounds()),
                                                   *tempoStream))
        return fail("Tempo screenshot failed");
    report += "Actual Set BPM dialog + actual Fit button + Undo passed; detection algorithm is independently "
              "tested.\n";
    auto *fitByLength =
        dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.fitLength"));
    if (!fitByLength)
        return fail("Fit length button missing");
    const auto beforeLength = controller.project.state();
    const auto settingsBeforeLength = controller.project.channel(first)->sampler;
    const auto preparedLength = audio.channelSample(first);
    fitByLength->triggerClick();
    dispatchMessages(20);
    dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog || !dialog->getTextEditor("bpm") || !dialog->getComboBoxComponent("units"))
        return fail("Musical sample length dialog/units unavailable");
    dialog->getTextEditor("bpm")->setText("1");
    dialog->getComboBoxComponent("units")->setSelectedItemIndex(0, juce::dontSendNotification);
    {
        auto stream = folder.getChildFile("sampler-fit-length.png").createOutputStream();
        if (!stream)
            return fail("Fit length screenshot unavailable");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(
                dialog->createComponentSnapshot(dialog->getLocalBounds()), *stream))
            return fail("Fit length screenshot failed");
    }
    dialog->exitModalState(1);
    dispatchMessages(20);
    const auto expectedBpm = 60. / (preparedLength->data.getNumSamples() / preparedLength->sampleRate *
                                    (settingsBeforeLength.end - settingsBeforeLength.start));
    if (!controller.project.channel(first)->sampler.fitTempo ||
        std::abs(controller.project.channel(first)->sampler.sourceTempo - expectedBpm) > 1e-9)
        return fail("Actual Fit length dialog did not use the trimmed source in quarter notes");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeLength))
        return fail("Musical length fit did not restore exactly in one Undo");
    setBpm->triggerClick();
    dispatchMessages(20);
    dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog || !dialog->getTextEditor("bpm"))
        return fail("Stale tempo confirmation fixture missing");
    tempoArgs.value = 150;
    command("channel.sampler.sourceTempo", tempoArgs);
    const auto newerSettings = controller.project.state();
    const auto newerRevision = controller.revision;
    dialog->getTextEditor("bpm")->setText("120");
    dialog->exitModalState(1);
    dispatchMessages(20);
    if (controller.revision != newerRevision || !controller.project.state().isEquivalentTo(newerSettings))
        return fail("Old tempo dialog overwrote a newer same-source settings edit");
    controller.undoEdit();
    report +=
        "Actual musical-length dialog/units, one Undo, and stale same-source confirmation refusal passed.\n";
    const auto rhythmPath = folder.getChildFile("original-tempo-pulses.wav");
    {
        juce::AudioBuffer<float> pcm(1, 48000 * 12);
        pcm.clear();
        for (int i = 0; i < pcm.getNumSamples(); ++i)
            if (i % 24000 < 240)
                pcm.setSample(0, i, .8f);
        auto output = rhythmPath.createOutputStream();
        if (!output)
            return fail("Detection fixture stream unavailable");
        output->setPosition(0);
        output->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(output.release(), 48000., 1, 24, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()))
            return fail("Detection fixture WAV write failed");
    }
    CommandArgs replaceSource;
    replaceSource.channel = first;
    replaceSource.text = rhythmPath.getFullPathName();
    if (controller.execute("channel.source", replaceSource).failed())
        return fail("Detection source replace failed");
    if (auto result = rebuild(); result.failed())
        return result;
    dispatchMessages(250);
    auto *detect = dynamic_cast<juce::Button *>(controls->findChildWithID("instrument.sampler.detectTempo"));
    if (!detect || !detect->isEnabled())
        return fail("Detect tempo button unavailable");
    const auto beforeDetect = controller.project.state();
    detect->triggerClick();
    juce::AlertWindow *estimate = nullptr;
    for (int attempt = 0; attempt < 100 && !estimate; ++attempt)
    {
        dispatchMessages(20);
        estimate = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    }
    if (!estimate || estimate->getName() != "Confirm tempo estimate" || !estimate->getTextEditor("bpm"))
        return fail("Detection confirmation did not open");
    {
        juce::var request(new juce::DynamicObject);
        request.getDynamicObject()->setProperty("action", "state");
        request.getDynamicObject()->setProperty("channel", juce::int64(first));
        const auto discovered = mcpCall("vibera_samplerTempo", request)["structuredContent"];
        const auto *sessions = discovered["operations"].getArray();
        if (!sessions || sessions->isEmpty()) return fail("Semantic state cannot discover UI-created tempo analysis");
        request = juce::var(new juce::DynamicObject);
        request.getDynamicObject()->setProperty("action", "job");
        request.getDynamicObject()->setProperty("operation", sessions->getLast());
        const auto observed = mcpCall("vibera_samplerTempo", request)["structuredContent"];
        if (observed["completionState"].toString() != "completed" || bool(observed["applied"]) ||
            std::abs(double(observed["estimateBpm"]) - 120) > 1)
            return fail("UI analysis and semantic job observation disagree before confirmation");
    }
    if (!controller.project.state().isEquivalentTo(beforeDetect))
        return fail("Detection changed state before confirmation");
    if (std::abs(estimate->getTextEditorContents("bpm").getDoubleValue() - 120) > 1)
        return fail("Actual detection button did not estimate 120 BPM fixture");
    estimate->exitModalState(0);
    dispatchMessages(20);
    if (!controller.project.state().isEquivalentTo(beforeDetect))
        return fail("Detection Cancel changed state");
    detect->triggerClick();
    estimate = nullptr;
    for (int attempt = 0; attempt < 100 && !estimate; ++attempt)
    {
        dispatchMessages(20);
        estimate = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    }
    if (!estimate || !estimate->getTextEditor("bpm"))
        return fail("Detection repeat confirmation missing");
    estimate->exitModalState(1);
    dispatchMessages(20);
    if (std::abs(controller.project.channel(first)->sampler.sourceTempo - 120) > 1)
        return fail("Detection confirmation did not apply source BPM");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeDetect))
        return fail("Detected BPM Undo changed unrelated state");
    report += "Actual Detect button/background analysis/120 BPM confirmation/Cancel/repeat/Undo passed on "
              "original pulse WAV.\n";
    window->setVisible(false);
    folder.getChildFile("sampler-gesture-verification.txt")
        .replaceWithText(
            "PASS: root/handle/Shift/Escape/one-Undo handlers; isolated preview lane; actual sample PCM.\n" +
            report +
            "Component mouse/focus handlers and virtual callbacks/injected MIDI; no OS input, physical MIDI "
            "or listening.\n");
    return juce::Result::ok();
}
} // namespace vibera
