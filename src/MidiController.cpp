// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiController.h"
#include <cmath>
#include <limits>
#include <juce_cryptography/juce_cryptography.h>

namespace vibera
{
static juce::File controllerPrefs()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VibeRa Studio/controller-mappings.json");
}
MidiController::MidiController(MidiControlInput &source, bool hardware) : inbox(source), persistent(hardware)
{
    previous.fill(-1);
    if (persistent && controllerPrefs().existsAsFile())
    {
        auto result = importState(juce::JSON::parse(controllerPrefs()));
        if (result.failed())
            error = result.getErrorMessage();
    }
}
MidiController::~MidiController()
{
    cancelLearn();
    if (projectLearnToken)
        cancelProjectLearn(projectLearnToken);
    close();
    inbox.clearMappings();
}
void MidiController::close()
{
    if (output && dawEnabled)
        output->sendMessageNow(juce::MidiMessage::noteOn(16, 12, juce::uint8(0)));
    dawEnabled = false;
    lastFeedback.fill(-1);
    if (input)
    {
        input->stop();
        input.reset();
    }
    output.reset();
    layers = {};
    pressed = {};
    previous.fill(-1);
    inbox.discard();
}
juce::Result MidiController::setPorts(const juce::String &inputId, const juce::String &outputId)
{
    if (inputId.isNotEmpty() && inputAllowed && !inputAllowed(inputId))
        return juce::Result::fail(
            "Performance and control inputs must be separate; use Performance input mappings instead");
    close();
    wantedInput = inputId;
    wantedOutput = outputId;
    error.clear();
    if (persistent && wantedInput.isNotEmpty())
    {
        input = juce::MidiInput::openDevice(wantedInput, this);
        if (!input)
            error = "Control input unavailable or in use";
        else
            input->start();
    }
    if (persistent && wantedOutput.isNotEmpty())
    {
        output = juce::MidiOutput::openDevice(wantedOutput);
        if (!output)
            error = "Control output unavailable or in use";
    }
    if (input && output && profile == "Launchkey MK3")
    {
        juce::String name;
        for (const auto &info : juce::MidiOutput::getAvailableDevices())
            if (info.identifier == wantedOutput)
                name = info.name;
        if (name.containsIgnoreCase("LKMK3") && name.containsIgnoreCase("MIDIOUT2"))
        {
            output->sendMessageNow(juce::MidiMessage::noteOn(16, 12, juce::uint8(127)));
            dawEnabled = true;
            for (int pad = 0; pad < 16; ++pad)
                output->sendMessageNow(
                    juce::MidiMessage::noteOn(1, pad < 8 ? 96 + pad : 112 + pad - 8, juce::uint8(3)));
        }
        else
            error = "Launchkey profile needs its MIDIOUT2 DAW output; mode not enabled";
    }
    save();
    return error.isEmpty() ? juce::Result::ok() : juce::Result::fail(error);
}
void MidiController::feedback(bool playing, bool recording, bool metronome)
{
    if (!output || !dawEnabled)
        return;
    const std::array<int, 3> values{playing ? 127 : 0, recording ? 127 : 0, metronome ? 127 : 0};
    const std::array<int, 3> controls{115, 117, 76};
    for (size_t i = 0; i < values.size(); ++i)
        if (lastFeedback[i] != values[i])
        {
            output->sendMessageNow(juce::MidiMessage::controllerEvent(16, controls[i], values[i]));
            lastFeedback[i] = values[i];
        }
}
juce::String MidiController::status() const
{
    return (error.isNotEmpty()      ? error
            : wantedInput.isEmpty() ? "Control port off; MIDI Learn can use performance input"
            : input                 ? "Control port connected"
                                    : "Control port disconnected") +
           " | dropped " + juce::String(inbox.dropped());
}
void MidiController::poll()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPoll < 1000 || !persistent)
        return;
    lastPoll = now;
    bool present = wantedInput.isEmpty();
    for (const auto &info : juce::MidiInput::getAvailableDevices())
        present = present || info.identifier == wantedInput;
    if (!present && input)
    {
        close();
        error = "Control device disconnected; waiting";
    }
    else if (present && wantedInput.isNotEmpty() && !input)
        setPorts(wantedInput, wantedOutput);
}
const juce::StringArray &MidiController::parameterActions()
{
    static const juce::StringArray values{"project.tempo",
                                          "pattern.length",
                                          "notes.velocity",
                                          "channel.gain",
                                          "channel.pan",
                                          "mixer.gain",
                                          "mixer.pan",
                                          "mixer.stereoSeparation",
                                          "mixer.send",
                                          "channel.sampler.root",
                                          "channel.sampler.cents",
                                          "channel.sampler.start",
                                          "channel.sampler.end",
                                          "channel.sampler.loopStart",
                                          "channel.sampler.loopEnd",
                                          "channel.sampler.attack",
                                          "channel.sampler.decay",
                                          "channel.sampler.sustain",
                                          "channel.sampler.release",
                                          "channel.sampler.sourceTempo",
                                          "channel.sampler.cut",
                                          "channel.sampler.cutBy",
                                          "channel.sampler.maxPoly",
                                          "channel.sampler.filterType",
                                          "channel.sampler.filterCutoff",
                                          "channel.sampler.filterResonance",
                                          "channel.sampler.mono",
                                          "channel.sampler.portamento",
                                          "channel.sampler.slideTime",
                                          "channel.sampler.gateBeats",
                                          "channel.sampler.fullPortamento"};
    return values;
}
bool MidiController::valid(const ControllerBinding &binding)
{
    if (binding.port < 0 || binding.port > 1 || binding.channel < 0 || binding.channel > 16 ||
        binding.kind < 0 || binding.kind > 6 || binding.number < 0 || binding.number > 127 ||
        binding.layer < 0 || binding.layer > 3 || binding.mode < 0 || binding.mode > 4 ||
        binding.mixer < -1 || (binding.banked && binding.mixer > MixerNativeIdentity::capacity - 1) ||
        !std::isfinite(binding.minimum) || !std::isfinite(binding.maximum) ||
        binding.minimum >= binding.maximum || binding.minimum < -100000 || binding.maximum > 100000 ||
        binding.label.length() > 120)
        return false;
    if (isSamplerVoiceModeCommand(binding.action))
    {
        const bool flag = binding.action == "channel.sampler.mono" ||
                          binding.action == "channel.sampler.portamento" ||
                          binding.action == "channel.sampler.fullPortamento";
        if (binding.nativeArea != 0 || binding.mode == 4 || (!flag && binding.mode == 0) ||
            (flag && (binding.mode > 1 || binding.minimum != 0 || binding.maximum != 1)) ||
            (!flag &&
             (binding.minimum < 0 || binding.maximum > (binding.action.endsWith("slideTime") ? 10 : 64))))
            return false;
    }
    if (binding.action == "mixer.send"
            ? (binding.sendTarget < 0 || binding.mode == 0 || binding.mode == 4 || binding.nativeArea != 0 ||
               binding.minimum < 0 || binding.maximum > 2)
            : binding.sendTarget != -1)
        return false;
    if (binding.nativeArea < 0 || binding.nativeArea > 2)
        return false;
    if (binding.nativeArea &&
        (binding.port != 1 || binding.channel != 16 || binding.kind != 0 || binding.mode != 1 ||
         (binding.nativeArea == 1 ? (binding.number < 21 || binding.number > 28)
                                  : (binding.number < 53 || binding.number > 61))))
        return false;
    if (binding.mode == 4)
        return binding.layer > 0 && binding.kind < 2;
    if ((binding.kind == 3 && binding.number != 0) ||
        (binding.kind >= 4 && (binding.number != 0 || binding.channel != 0)) ||
        ((binding.mode == 2 || binding.mode == 3) && binding.kind != 0))
        return false;
    if (binding.mode > 0 && (!parameterActions().contains(binding.action) || binding.kind > 3))
        return false;
    if (binding.action.startsWith("macro."))
        return binding.mode == 0 && binding.action.length() <= 120;
    for (const auto &command : commandRegistry())
        if (command.id == binding.action)
            return command.userAction;
    return false;
}
bool MidiController::conflicts(const ControllerBinding &candidate, size_t skip) const
{
    if (!candidate.enabled)
        return false;
    for (size_t i = 0; i < bindings.size(); ++i)
    {
        if (i == skip)
            continue;
        const auto &existing = bindings[i];
        if (!existing.enabled)
            continue;
        if (candidate.port == existing.port && candidate.kind == existing.kind &&
            candidate.number == existing.number &&
            (!candidate.channel || !existing.channel || candidate.channel == existing.channel) &&
            (candidate.layer == existing.layer || candidate.mode == 4 || existing.mode == 4))
            return true;
    }
    return false;
}
// CONTROL-only projection. Global preference/profile bytes remain independent of Project.
juce::Result MidiController::prepareProjection(const std::vector<ControllerBinding> &globals,
                                               const Project *project, const juce::String &identity,
                                               std::shared_ptr<EffectiveProjection> &output) const
{
    try
    {
        if (globals.size() > 512 ||
            (project && (identity.isEmpty() || project->samplerControllerLinks.size() > 512)))
            return juce::Result::fail("controller.projectionCapacity: Global512 plus project512 required");
        // Value-only document edits do not rebuild mapping storage or CC epoch.
        // Compare every retained ID as well as the count so duplicate/missing rows
        // cannot masquerade as the same projection.
        if (effective && globals.size() == effective->globals &&
            std::equal(globals.begin(), globals.end(), effective->rows.begin(),
                       [](const auto &binding, const auto &row) { return binding == row.binding; }))
        {
            bool unchanged = !project || (identity == effective->projectIdentity &&
                                          project->samplerControllerLinks.size() == effective->projects);
            if (project && unchanged)
                for (size_t i = 512; i < 1024; ++i)
                    if (effective->rows[i].present)
                    {
                        const auto &row = effective->rows[i];
                        const auto found = std::find_if(
                            project->samplerControllerLinks.begin(), project->samplerControllerLinks.end(),
                            [&](const auto &link) { return link.id == row.link.id; });
                        const auto *channel = project->channel(row.link.channel);
                        if (found == project->samplerControllerLinks.end() || *found != row.link ||
                            !channel || row.available != (!channel->instrument && !channel->audioClip))
                        {
                            unchanged = false;
                            break;
                        }
                    }
            if (unchanged)
            {
                output = effective;
                return juce::Result::ok();
            }
        }
        auto next = std::make_shared<EffectiveProjection>();
        next->globals = globals.size();
        next->projectIdentity =
            project ? identity : (effective ? effective->projectIdentity : juce::String{});
        next->generation = effective ? effective->generation : 0;
        next->stateSerial = effective ? effective->stateSerial : 0;
        for (size_t i = 0; i < globals.size(); ++i)
        {
            if (!valid(globals[i]))
                return juce::Result::fail(
                    "controller.invalidBinding: Global mapping invalid; projection preserved");
            auto &row = next->rows[i];
            row.present = true;
            row.binding = globals[i];
        }
        // Reserve every retained ID before assigning NEW rows. Otherwise inserting a
        // new first row could steal a later unchanged row's pickup/pressed slot.
        std::array<bool, 1024> retained{};
        if (effective && effective->projectIdentity == next->projectIdentity)
            for (size_t i = 512; i < 1024; ++i)
                if (effective->rows[i].present)
                {
                    const auto id = effective->rows[i].link.id;
                    retained[i] = !project || std::any_of(project->samplerControllerLinks.begin(),
                                                          project->samplerControllerLinks.end(),
                                                          [&](const auto &link) { return link.id == id; });
                }
        const auto install = [&](const SamplerControllerLink &link, bool available) -> juce::Result
        {
            if (link.id <= 0 || validateSamplerControllerLink(link).failed())
                return juce::Result::fail(
                    "controller.invalidProjectLink: Valid allocated Project link required");
            size_t slot = 1024;
            if (effective && effective->projectIdentity == next->projectIdentity)
                for (size_t i = 512; i < 1024; ++i)
                    if (effective->rows[i].present && effective->rows[i].link.id == link.id)
                    {
                        slot = i;
                        break;
                    }
            if (slot == 1024 || next->rows[slot].present)
            {
                slot = 512;
                while (slot < 1024 && (next->rows[slot].present || retained[slot]))
                    ++slot;
            }
            if (slot == 1024)
                return juce::Result::fail("controller.projectionCapacity: Project512 exceeded");
            for (size_t i = 512; i < 1024; ++i)
                if (next->rows[i].present)
                {
                    if (next->rows[i].link.id == link.id ||
                        samplerControllerSourceOverlaps(next->rows[i].link, link))
                        return juce::Result::fail(
                            "controller.projectConflict: Duplicate Project source or ID");
                }
            auto &row = next->rows[slot];
            row.present = row.project = true;
            row.available = available;
            row.link = link;
            row.parameter = lookupSamplerAutomationParameter(link.parameter);
            if (!row.parameter)
                return juce::Result::fail("controller.invalidProjectLink: Unknown sampler field");
            row.binding.action = "channel.sampler.modulation." +
                                 juce::String(row.parameter->id.data(), row.parameter->id.size());
            row.binding.label = link.label;
            row.binding.port = link.port;
            row.binding.channel = link.midiChannel;
            row.binding.kind = 0;
            row.binding.number = link.cc;
            row.binding.layer = link.layer;
            row.binding.mode = 1;
            row.binding.minimum = link.minimum;
            row.binding.maximum = link.maximum;
            row.binding.pickup = link.pickup;
            row.binding.enabled = link.enabled;
            ++next->projects;
            return juce::Result::ok();
        };
        if (project)
            for (const auto &link : project->samplerControllerLinks)
            {
                const auto *channel = project->channel(link.channel);
                if (!channel)
                    return juce::Result::fail("controller.missingProjectTarget: Link Channel missing");
                if (auto result = install(link, !channel->instrument && !channel->audioClip); result.failed())
                    return result;
            }
        else if (effective)
            for (size_t i = 512; i < 1024; ++i)
                if (effective->rows[i].present)
                    if (auto result = install(effective->rows[i].link, effective->rows[i].available);
                        result.failed())
                        return result;
        for (size_t g = 0; g < next->globals; ++g)
        {
            auto &row = next->rows[g];
            const auto &b = row.binding;
            if (b.enabled && b.kind == 0)
                for (size_t i = 512; i < 1024; ++i)
                {
                    const auto &p = next->rows[i];
                    const auto &link = p.link;
                    if (p.present && link.enabled && link.port == b.port && link.cc == b.number &&
                        (!link.midiChannel || !b.channel || link.midiChannel == b.channel) &&
                        (link.layer == b.layer || b.mode == 4))
                        row.shadowedBy.push_back(link.id);
                }
            std::sort(row.shadowedBy.begin(), row.shadowedBy.end());
        }
        const auto mask = [&](int channel, int kind, int number)
        {
            if (kind >= 3)
            {
                next->masks.extra[size_t(channel)] |= 1u << unsigned(kind);
                return;
            }
            auto &words = kind == 1   ? next->masks.notes
                          : kind == 2 ? next->masks.programs
                                      : next->masks.controllers;
            words[size_t((channel - 1) * 2 + number / 64)] |= std::uint64_t(1) << (number % 64);
        };
        bool changed = !effective || effective->projectIdentity != next->projectIdentity;
        for (size_t i = 0; i < 1024; ++i)
        {
            auto &row = next->rows[i];
            const EffectiveRow *old = effective ? &effective->rows[i] : nullptr;
            const bool same =
                old && old->present == row.present &&
                (!row.present ||
                 (old->project == row.project && old->binding == row.binding && old->link == row.link &&
                  old->available == row.available && old->shadowedBy == row.shadowedBy &&
                  (!row.project || effective->projectIdentity == next->projectIdentity)));
            changed |= !same;
            if (row.present)
            {
                if (same)
                    row.stateGeneration = old->stateGeneration;
                else
                {
                    if (next->stateSerial == std::numeric_limits<std::uint64_t>::max())
                        return juce::Result::fail("controller.projectionExhausted: State serial exhausted");
                    row.stateGeneration = ++next->stateSerial;
                }
                const auto &b = row.binding;
                if (b.enabled && b.port == 0)
                {
                    if (b.channel)
                        mask(b.channel, b.kind, b.number);
                    else
                        for (int channel = b.kind >= 4 ? 0 : 1; channel <= (b.kind >= 4 ? 0 : 16); ++channel)
                            mask(channel, b.kind, b.number);
                }
            }
        }
        if (changed)
        {
            if (next->generation == std::numeric_limits<std::uint64_t>::max())
                return juce::Result::fail("controller.projectionExhausted: Projection serial exhausted");
            ++next->generation;
        }
        output = std::move(next);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("controller.preparationFailed: Projection unchanged");
    }
}
void MidiController::publishPrepared(std::shared_ptr<EffectiveProjection> next, bool resetGlobals) noexcept
{
    if (effective && effective->generation == next->generation && !resetGlobals)
        return;
    for (size_t i = 0; i < 1024; ++i)
        if ((resetGlobals && i < 512) || !effective ||
            effective->rows[i].stateGeneration != next->rows[i].stateGeneration)
        {
            pressed[i] = false;
            previous[i] = -1;
        }
    layers = {};
    for (size_t i = 0; i < next->globals; ++i)
        if (next->rows[i].binding.enabled && next->rows[i].binding.mode == 4 && pressed[i])
            layers[size_t(next->rows[i].binding.layer)] = true;
    effective.swap(next); // Retired config is destroyed on CONTROL, never producer/DSP.
    if (resetGlobals)
        inbox.discard(); // Existing explicit global-profile publication policy only.
    inbox.replaceMappings(effective->masks);
}
juce::Result MidiController::publishProjectBindings(const Project &project, const juce::String &identity)
{
    std::shared_ptr<EffectiveProjection> next;
    if (auto result = prepareProjection(bindings, &project, identity, next); result.failed())
        return result;
    publishPrepared(std::move(next), false);
    return juce::Result::ok();
}
std::uint64_t MidiController::projectProjectionGeneration() const noexcept
{
    return effective ? effective->generation : 0;
}
bool MidiController::projectInvocationCurrent(const ControllerInvocation &value, const Project &project,
                                              const juce::String &identity) const noexcept
{
    if (!effective || value.binding < 512 || value.binding >= 1024 ||
        identity != effective->projectIdentity || value.projectIdentity != identity ||
        value.projectProjectionGeneration != effective->generation)
        return false;
    const auto &row = effective->rows[value.binding];
    if (!row.present || !row.project || !row.link.enabled || row.link.id != value.projectLink ||
        row.link.channel != value.channel || row.link.parameter != value.samplerParameter ||
        row.stateGeneration != value.bindingStateGeneration || row.available != value.samplerAvailable ||
        value.action != row.binding.action || !row.parameter ||
        std::string_view(value.samplerDestination.toRawUTF8(),
                         size_t(value.samplerDestination.getNumBytesAsUTF8())) != row.parameter->target)
        return false;
    const auto found =
        std::find_if(project.samplerControllerLinks.begin(), project.samplerControllerLinks.end(),
                     [&](const auto &link) { return link.id == value.projectLink; });
    const auto *channel = project.channel(value.channel);
    return found != project.samplerControllerLinks.end() && *found == row.link && channel &&
           row.available == (!channel->instrument && !channel->audioClip);
}
juce::var MidiController::projectBindingState() const
{
    auto result = std::make_unique<juce::DynamicObject>();
    result->setProperty("projectIdentity", effective ? effective->projectIdentity : juce::String{});
    result->setProperty("projectionGeneration", juce::String(projectProjectionGeneration()));
    result->setProperty("capacity", 1024);
    result->setProperty("globalLimit", 512);
    result->setProperty("projectLimit", 512);
    juce::Array<juce::var> globals, projects;
    if (effective)
        for (size_t i = 0; i < 1024; ++i)
        {
            const auto &row = effective->rows[i];
            if (!row.present)
                continue;
            auto item = std::make_unique<juce::DynamicObject>();
            item->setProperty("slot", int(i));
            item->setProperty("bindingStateGeneration", juce::String(row.stateGeneration));
            item->setProperty("capacityBlocked", false);
            if (row.project)
            {
                item->setProperty("linkID", juce::int64(row.link.id));
                item->setProperty("channel", juce::int64(row.link.channel));
                item->setProperty("samplerParameter", int(row.link.parameter));
                item->setProperty("enabled", row.link.enabled);
                item->setProperty("available", row.available);
                item->setProperty("reservesSource", row.link.enabled);
                item->setProperty("port", row.link.port);
                item->setProperty("midiChannel", row.link.midiChannel);
                item->setProperty("cc", row.link.cc);
                item->setProperty("layer", row.link.layer);
                item->setProperty("minimum", row.link.minimum);
                item->setProperty("maximum", row.link.maximum);
                item->setProperty("pickup", row.link.pickup);
                item->setProperty("reason", row.available ? "localSampler" : "dormantUnsupportedChannelRole");
                projects.add(juce::var(item.release()));
            }
            else
            {
                item->setProperty("globalIndex", int(i));
                juce::Array<juce::var> ids;
                for (auto id : row.shadowedBy)
                    ids.add(juce::int64(id));
                item->setProperty("shadowedByProjectLinks", ids);
                item->setProperty("shadowScope", "Matching enabled project CC/channel/layer only; wildcard "
                                                 "global remains on other channels");
                globals.add(juce::var(item.release()));
            }
        }
    result->setProperty("globalRows", globals);
    result->setProperty("projectRows", projects);
    return juce::var(result.release());
}
juce::Result
MidiController::armProjectLearn(std::uint64_t token,
                                std::function<void(std::uint64_t, const ControlMessage &)> capture)
{
    if (!token || !capture)
        return juce::Result::fail("controller.invalidLearn: Owned token/capture callback required");
    if (projectLearnToken || learnIndex >= 0)
        return juce::Result::fail("controller.learnBusy: Finish the existing Learn owner");
    projectLearnCandidate.reset();
    projectLearnCapture = std::move(capture);
    projectLearnToken = token;
    inbox.beginControllerLearn();
    return juce::Result::ok();
}
bool MidiController::cancelProjectLearn(std::uint64_t token)
{
    if (!token || token != projectLearnToken)
        return false;
    inbox.endControllerLearn();
    projectLearnToken = 0;
    projectLearnCandidate.reset();
    projectLearnCapture = {};
    return true;
}
juce::var MidiController::projectLearnState() const
{
    auto result = std::make_unique<juce::DynamicObject>();
    result->setProperty("token", juce::String(projectLearnToken));
    result->setProperty("active", projectLearnToken != 0);
    result->setProperty("captured", projectLearnCandidate.has_value());
    result->setProperty("controllerGeneration", juce::int64(inbox.controllerGeneration()));
    if (projectLearnCandidate)
    {
        result->setProperty("port", projectLearnCandidate->port);
        result->setProperty("midiChannel", projectLearnCandidate->channel);
        result->setProperty("cc", projectLearnCandidate->number);
        result->setProperty("value", projectLearnCandidate->value);
    }
    return juce::var(result.release());
}
void MidiController::publish()
{
    std::shared_ptr<EffectiveProjection> next;
    if (auto prepared = prepareProjection(bindings, nullptr, {}, next); prepared.failed())
    {
        error = prepared.getErrorMessage();
        lastMessage = error;
        return;
    }
    cancelLearn();
    publishPrepared(std::move(next), true);
}
juce::Result MidiController::armLearnImpl(int index, const juce::File *owned)
{
    if (projectLearnToken)
        return juce::Result::fail("controller.learnBusy: Project Learn owns capture");
    cancelLearn();
    if (index < 0 || index >= int(bindings.size()))
        return juce::Result::fail("controller.invalidIndex: Existing Learn row required");
    juce::String expected;
    auto result = owned ? bindingProfileIdentity(*owned, expected) : bindingProfileIdentity(expected);
    if (result.failed())
        return result;
    if (owned)
        learnFile = *owned;
    learnExpectedProfile = std::move(expected);
    learnIndex = index;
    inbox.learning = true;
    return juce::Result::ok();
}
void MidiController::armLearn(int index)
{
    if (auto result = armLearnImpl(index, nullptr); result.failed())
        lastMessage = result.getErrorMessage();
}
juce::Result MidiController::armLearn(int index, const juce::File &owned)
{
    return armLearnImpl(index, &owned);
}
void MidiController::cancelLearn()
{
    learnIndex = -1;
    inbox.learning = false;
    learnExpectedProfile.clear();
    learnFile.reset();
}
void MidiController::drain()
{
    if (lastDropped != inbox.dropped())
    {
        lastDropped = inbox.dropped();
        inbox.discard();
        layers = {};
        pressed = {};
        previous.fill(-1);
    }
    ControlMessage event;
    for (int port = 0; port < 2; ++port)
        for (int count = 0; count < 1024 && inbox.pop(port, event); ++count)
        {
            if (!inbox.current(event))
                continue;
            lastMessage =
                (port ? "Control" : "Performance") + juce::String(" port / Ch ") +
                juce::String(event.channel) + " / " +
                juce::StringArray{"CC", "Note", "Program", "Pitch", "Start", "Stop", "Continue"}[event.kind] +
                " " + juce::String(event.number) + " = " + juce::String(event.value);
            if (projectLearnToken && MidiControlInput::projectLearnEligible(event))
            {
                // All eligible CC remains captured until explicit Accept/Cancel. The
                // first source is never applied, replayed or retargeted by later CC.
                if (!projectLearnCandidate)
                {
                    projectLearnCandidate = event;
                    const auto token = projectLearnToken;
                    auto capture = projectLearnCapture; // CONTROL pin across reentrant cancel.
                    if (capture)
                        capture(token, event);
                }
                continue;
            }
            if (learnIndex >= 0)
            {
                if (event.kind == 1 && event.value == 0)
                    continue;
                auto binding = bindings[size_t(learnIndex)];
                binding.port = port;
                binding.channel = event.channel;
                binding.kind = event.kind;
                binding.number = event.number;
                binding.enabled = true;
                if (binding.nativeArea && !valid(binding))
                    binding.nativeArea = 0;
                if (!valid(binding))
                {
                    lastMessage = "This message type is incompatible with the mapping mode; choose another "
                                  "control or Cancel Learn";
                    continue;
                }
                if (conflicts(binding, size_t(learnIndex)))
                {
                    lastMessage =
                        "That control is already mapped in this layer; choose another or Cancel Learn";
                    continue;
                }
                const auto index = size_t(learnIndex);
                const auto result = learnFile ? setBinding(index, binding, learnExpectedProfile, *learnFile)
                                              : setBinding(index, binding, learnExpectedProfile);
                if (result.failed())
                    lastMessage = result.getErrorMessage();
                continue; // Learning never executes the captured action.
            }
            auto projection = effective; // CONTROL lifetime pin across canonical/reentrant invoke.
            if (!projection)
                continue;
            int activeLayer = 0;
            for (int candidate = 1; candidate < 4; ++candidate)
                if (layers[size_t(candidate)])
                    activeLayer = candidate;
            const EffectiveRow *projectRow = nullptr;
            size_t projectSlot = 0;
            if (event.kind == 0)
                for (size_t i = 512; i < 1024; ++i)
                {
                    const auto &row = projection->rows[i];
                    const auto &b = row.binding;
                    if (row.present && b.enabled && b.port == port && b.number == event.number &&
                        (!b.channel || b.channel == event.channel) && b.layer == activeLayer)
                    {
                        projectRow = &row;
                        projectSlot = i;
                        break;
                    }
                }
            if (projectRow)
            {
                // A release intercepted in the now-active Project layer must still
                // retire an earlier GLOBAL press owner. It never executes that
                // global action, but otherwise a held modifier could stick forever.
                if (event.value < 64)
                {
                    for (size_t i = 0; i < projection->globals; ++i)
                    {
                        const auto &old = projection->rows[i].binding;
                        if (old.enabled && old.kind == 0 && old.port == port && old.number == event.number &&
                            (!old.channel || old.channel == event.channel))
                            pressed[i] = false;
                    }
                    layers = {};
                    for (size_t i = 0; i < projection->globals; ++i)
                        if (projection->rows[i].binding.mode == 4 && pressed[i])
                            layers[size_t(projection->rows[i].binding.layer)] = true;
                }
                const auto &row = *projectRow;
                const auto &b = row.binding;
                ControllerInvocation invocation;
                invocation.action = b.action;
                invocation.parameter = true;
                invocation.pickup = b.pickup;
                invocation.minimum = b.minimum;
                invocation.maximum = b.maximum;
                invocation.normalized = double(event.value) / 127.;
                invocation.binding = projectSlot;
                invocation.projectIdentity = projection->projectIdentity;
                invocation.projectLink = row.link.id;
                invocation.channel = row.link.channel;
                invocation.samplerParameter = row.link.parameter;
                invocation.samplerAvailable = row.available;
                invocation.projectProjectionGeneration = projection->generation;
                invocation.bindingStateGeneration = row.stateGeneration;
                invocation.automationSession = event.automationSession;
                invocation.automationSeconds = event.automationSeconds;
                invocation.automationClockValid = event.automationClockValid;
                const double physical = event.value == 0 ? b.minimum
                                        : event.value == 127
                                            ? b.maximum
                                            : b.minimum + invocation.normalized * (b.maximum - b.minimum);
                if (!row.parameter ||
                    !quantizeSamplerAutomationValue(*row.parameter, physical, invocation.value))
                    lastMessage = "controller.invalidProjectValue: Reserved sampler source refused";
                else
                {
                    invocation.samplerDestination =
                        juce::String(row.parameter->target.data(), row.parameter->target.size());
                    if (invoke)
                        invoke(invocation);
                }
                continue; // Dormant/failed project targets never fall through to a global action.
            }
            if (profile == "Launchkey MK3" && port == 1 && event.kind == 0 && event.channel == 16 &&
                (event.number == 9 || event.number == 10))
            {
                if (event.value >= 0 && event.value <= 9)
                {
                    if (event.number == 9)
                        potMode = event.value;
                    else
                        faderMode = event.value;
                }
                continue;
            }
            for (size_t i = 0; i < projection->globals; ++i)
            {
                const auto &binding = projection->rows[i].binding;
                if (!binding.enabled || binding.port != port ||
                    (binding.channel && binding.channel != event.channel) || binding.kind != event.kind ||
                    binding.number != event.number || !valid(binding))
                    continue;
                const bool down = event.kind >= 2 || (event.kind == 1 ? event.value > 0 : event.value >= 64);
                const bool rising = down && !pressed[i];
                pressed[i] = down;
                if (binding.mode == 4)
                {
                    layers[size_t(binding.layer)] = down;
                    continue;
                }
                int layer = 0;
                for (int candidate = 1; candidate < 4; ++candidate)
                    if (layers[size_t(candidate)])
                        layer = candidate;
                if (binding.layer != layer)
                    continue;
                ControllerInvocation invocation;
                invocation.automationSession = event.automationSession;
                invocation.automationSeconds = event.automationSeconds;
                invocation.automationClockValid = event.automationClockValid;
                invocation.action = binding.action;
                invocation.mixer = binding.mixer;
                invocation.sendTarget = binding.sendTarget;
                invocation.mixerBanked = binding.banked;
                invocation.mixerBankOffset = mixerBank;
                invocation.binding = i;
                invocation.bindingStateGeneration = projection->rows[i].stateGeneration;
                invocation.minimum = binding.minimum;
                invocation.maximum = binding.maximum;
                if (binding.nativeArea && profile == "Launchkey MK3")
                {
                    if (binding.nativeArea == 1 && (potMode == 1 || potMode == 3))
                    {
                        invocation.action = potMode == 1 ? "mixer.gain" : "mixer.pan";
                        invocation.minimum = potMode == 1 ? 0 : -1;
                        invocation.maximum = potMode == 1 ? 1.5 : 1;
                    }
                    else if (binding.nativeArea == 2 && faderMode == 1)
                    {
                        invocation.action = "mixer.gain";
                    }
                    else
                    {
                        lastMessage = "Device/Send controls are pending; select Volume/Pan or disable Follow "
                                      "hardware mode";
                        continue;
                    }
                }
                if (binding.mode == 0)
                {
                    if (!rising && event.kind < 2)
                        continue;
                }
                else
                {
                    if (event.kind == 1 && event.value == 0)
                        continue; // A velocity link is not a release-to-zero gate.
                    invocation.parameter = true;
                    invocation.pickup = binding.pickup && binding.mode == 1;
                    invocation.normalized = double(event.value) / (event.kind == 3 ? 16383 : 127);
                    invocation.value = invocation.minimum +
                                       invocation.normalized * (invocation.maximum - invocation.minimum);
                    if (binding.mode == 2 || binding.mode == 3)
                    {
                        const int delta = binding.mode == 2
                                              ? (event.value < 64 ? event.value : event.value - 128)
                                              : event.value - 64;
                        if (delta == 0)
                            continue;
                        invocation.delta = delta * (invocation.maximum - invocation.minimum) / 127.;
                    }
                }
                if (invoke)
                    invoke(invocation);
                if (effective != projection)
                    break; // Publication owns new slot state; never write through stale config.
            }
        }
}
void MidiController::launchkeyPreset()
{
    profile = "Launchkey MK3";
    mixerBank = 0;
    potMode = 3;
    faderMode = 1;
    bindings.clear();
    auto button = [&](int cc, const char *action, const char *label)
    {
        ControllerBinding binding;
        binding.channel = 16;
        binding.number = cc;
        binding.action = action;
        binding.label = label;
        bindings.push_back(binding);
    };
    button(115, "transport.play", "Play / pause");
    button(116, "transport.stop", "Stop");
    button(117, "transport.record", "Record arm");
    button(76, "transport.metronome", "Metronome");
    button(77, "edit.undo", "Undo");
    button(74, "score.dump", "Capture score history");
    button(75, "notes.quantize", "Quantize notes");
    button(102, "controller.bankPrevious", "Previous Mixer bank");
    button(103, "controller.bankNext", "Next Mixer bank");
    const juce::StringArray pads{"view.playlist",
                                 "view.rack",
                                 "view.piano",
                                 "view.mixer",
                                 "channel.openInstrument",
                                 "workspace.tileBalanced",
                                 "workspace.tileFocused",
                                 "view.settings",
                                 "clipboard.copy",
                                 "clipboard.paste",
                                 "notes.duplicate",
                                 "notes.quantize",
                                 "clip.duplicate",
                                 "clip.unique",
                                 "score.dump"};
    for (int i = 0; i < pads.size(); ++i)
    {
        ControllerBinding binding;
        binding.channel = 1;
        binding.kind = 1;
        binding.number = i < 8 ? 96 + i : 112 + i - 8;
        binding.action = pads[i];
        juce::String name = pads[i];
        for (const auto &command : commandRegistry())
            if (command.id == pads[i])
                name = command.name;
        binding.label = "Pad " + juce::String(i + 1) + " / " + name;
        bindings.push_back(binding);
    }
    ControllerBinding shiftPad;
    shiftPad.channel = 1;
    shiftPad.kind = 1;
    shiftPad.number = 119;
    shiftPad.mode = 4;
    shiftPad.layer = 1;
    shiftPad.label = "Pad 16 / hold Layer 1";
    bindings.push_back(shiftPad);
    const juce::StringArray shiftedPads{"project.save",           "project.open",      "project.export",
                                        "pattern.clone",          "pattern.insert",    "pattern.findEmpty",
                                        "workspace.tileDetached", "view.restoreLayout"};
    for (int i = 0; i < shiftedPads.size(); ++i)
    {
        ControllerBinding binding;
        binding.channel = 1;
        binding.kind = 1;
        binding.number = 96 + i;
        binding.layer = 1;
        binding.action = shiftedPads[i];
        juce::String name = shiftedPads[i];
        for (const auto &command : commandRegistry())
            if (command.id == shiftedPads[i])
                name = command.name;
        binding.label = "Layer 1 / " + name;
        bindings.push_back(binding);
    }
    for (int i = 0; i < 9; ++i)
    {
        ControllerBinding binding;
        binding.channel = 16;
        binding.number = 53 + i;
        binding.mode = 1;
        binding.action = "mixer.gain";
        binding.minimum = 0;
        binding.maximum = 1.5;
        binding.mixer = i == 8 ? 0 : i + 1;
        binding.label = "Fader - " + juce::String(i == 8 ? "Master" : "Insert " + juce::String(i + 1));
        binding.banked = i != 8;
        binding.nativeArea = 2;
        bindings.push_back(binding);
    }
    for (int i = 0; i < 8; ++i)
    {
        ControllerBinding binding;
        binding.channel = 16;
        binding.number = 21 + i;
        binding.mode = 1;
        binding.action = "mixer.pan";
        binding.minimum = -1;
        binding.maximum = 1;
        binding.mixer = i + 1;
        binding.banked = true;
        binding.nativeArea = 1;
        binding.label = "Pot " + juce::String(i + 1);
        bindings.push_back(binding);
    }
    publish();
}
juce::var MidiController::state() const
{
    return stateForBindings(bindings);
}
juce::var MidiController::stateForBindings(const std::vector<ControllerBinding> &snapshot) const
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("version", 1);
    object->setProperty("profile", profile);
    object->setProperty("mixerBank", mixerBank);
    object->setProperty("potMode", potMode);
    object->setProperty("faderMode", faderMode);
    object->setProperty("input", wantedInput);
    object->setProperty("output", wantedOutput);
    juce::Array<juce::var> list;
    for (const auto &binding : snapshot)
    {
        auto item = std::make_unique<juce::DynamicObject>();
        item->setProperty("action", binding.action);
        item->setProperty("label", binding.label);
        item->setProperty("port", binding.port);
        item->setProperty("channel", binding.channel);
        item->setProperty("kind", binding.kind);
        item->setProperty("number", binding.number);
        item->setProperty("layer", binding.layer);
        item->setProperty("mode", binding.mode);
        item->setProperty("mixer", binding.mixer);
        if (binding.action == "mixer.send")
            item->setProperty("sendTarget", binding.sendTarget);
        item->setProperty("minimum", binding.minimum);
        item->setProperty("maximum", binding.maximum);
        item->setProperty("pickup", binding.pickup);
        item->setProperty("banked", binding.banked);
        item->setProperty("enabled", binding.enabled);
        item->setProperty("nativeArea", binding.nativeArea);
        list.add(juce::var(item.release()));
    }
    object->setProperty("bindings", list);
    return juce::var(object.release());
}
juce::Result MidiController::parseBinding(const juce::var &item, ControllerBinding &output)
{
    const auto fail = []
    { return juce::Result::fail("controller.invalidBinding: Complete supported binding required"); };
    if (!item.isObject())
        return fail();
    const juce::StringArray fields{"action", "label",   "port",       "channel",   "kind",    "number",
                                   "layer",  "mode",    "mixer",      "minimum",   "maximum", "pickup",
                                   "banked", "enabled", "nativeArea", "sendTarget"};
    for (const auto &field : item.getDynamicObject()->getProperties())
        if (!fields.contains(field.name.toString()))
            return fail();
    for (auto field : {"port", "channel", "kind", "number", "layer", "mode", "mixer", "nativeArea"})
        if (!item[field].isInt())
            return fail();
    if (!item["action"].isString() || !item["label"].isString() || !item["pickup"].isBool() ||
        !item["banked"].isBool() || !item["enabled"].isBool())
        return fail();
    for (auto field : {"minimum", "maximum"})
        if (!item[field].isDouble() && !item[field].isInt() && !item[field].isInt64())
            return fail();
    ControllerBinding binding;
    binding.action = item["action"].toString();
    binding.label = item["label"].toString();
    binding.port = int(item["port"]);
    binding.channel = int(item["channel"]);
    binding.kind = int(item["kind"]);
    binding.number = int(item["number"]);
    binding.layer = int(item["layer"]);
    binding.mode = int(item["mode"]);
    binding.mixer = int(item["mixer"]);
    if (item.hasProperty("sendTarget"))
    {
        if (!item["sendTarget"].isInt() || binding.action != "mixer.send")
            return fail();
        binding.sendTarget = int(item["sendTarget"]);
    }
    binding.minimum = double(item["minimum"]);
    binding.maximum = double(item["maximum"]);
    binding.pickup = bool(item["pickup"]);
    binding.banked = bool(item["banked"]);
    binding.enabled = bool(item["enabled"]);
    binding.nativeArea = int(item["nativeArea"]);
    if (!valid(binding))
        return fail();
    output = std::move(binding);
    return juce::Result::ok();
}
juce::Result MidiController::importState(const juce::var &value)
{
    if (projectLearnToken)
        return juce::Result::fail("controller.learnBusy: Project Learn owns capture");
    auto fail = [] { return juce::Result::fail("Invalid controller profile; current mappings preserved"); };
    if (!value.isObject() || !value["version"].isInt() || int(value["version"]) != 1 ||
        !value["profile"].isString() || !value["input"].isString() || !value["output"].isString() ||
        !value["mixerBank"].isInt() || int(value["mixerBank"]) < 0 ||
        int(value["mixerBank"]) > controllerMixerBankLimit(MixerNativeIdentity::capacity - 1) ||
        int(value["mixerBank"]) % controllerMixerBankWidth != 0 || !value["bindings"].isArray() ||
        value["bindings"].size() > 512 || !value["potMode"].isInt() || int(value["potMode"]) < 0 ||
        int(value["potMode"]) > 9 || !value["faderMode"].isInt() || int(value["faderMode"]) < 0 ||
        int(value["faderMode"]) > 9)
        return fail();
    std::vector<ControllerBinding> next;
    for (const auto &item : *value["bindings"].getArray())
    {
        ControllerBinding binding;
        if (auto parsed = parseBinding(item, binding); parsed.failed())
            return parsed;
        next.push_back(binding);
    }
    for (size_t i = 0; i < next.size(); ++i)
        for (size_t j = i + 1; j < next.size(); ++j)
        {
            const auto &a = next[i], &b = next[j];
            if (a.enabled && b.enabled && a.port == b.port && a.kind == b.kind && a.number == b.number &&
                (!a.channel || !b.channel || a.channel == b.channel) &&
                (a.layer == b.layer || a.mode == 4 || b.mode == 4))
                return fail();
        }
    if (persistent && inputAllowed && value["input"].toString().isNotEmpty() &&
        !inputAllowed(value["input"].toString()))
        return fail();
    std::shared_ptr<EffectiveProjection> prepared;
    if (auto result = prepareProjection(next, nullptr, {}, prepared); result.failed())
        return result;
    if (persistent)
        close();
    bindings = std::move(next);
    profile = value["profile"].toString();
    mixerBank = int(value["mixerBank"]);
    potMode = int(value["potMode"]);
    faderMode = int(value["faderMode"]);
    wantedInput = value["input"].toString();
    wantedOutput = value["output"].toString();
    cancelLearn();
    publishPrepared(std::move(prepared), true);
    return juce::Result::ok();
}
juce::Result MidiController::bindingIdentity(const juce::File *file, juce::String &output) const
{
    try
    {
        auto material = juce::JSON::toString(state(), true);
        if (file)
        {
            material += "\nfile:" + file->getFullPathName();
            if (file->exists())
            {
                juce::MemoryBlock disk;
                if (!file->existsAsFile() || file->getSize() > 1024 * 1024 || !file->loadFileAsData(disk))
                    return juce::Result::fail(
                        "controller.profileUnreadable: Saved mappings cannot be identified safely");
                material += "\nbytes:" + juce::SHA256(disk).toHexString();
            }
            else
                material += "\nabsent";
        }
        else
            material += "\nsessionOnly";
        auto next = juce::SHA256(material.toRawUTF8(), size_t(material.getNumBytesAsUTF8())).toHexString();
        output.swapWith(next);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("controller.preparationFailed: Profile identity could not be prepared");
    }
}
juce::Result MidiController::bindingProfileIdentity(juce::String &output) const
{
    try
    {
        if (!persistent)
            return bindingIdentity(nullptr, output);
        const auto file = controllerPrefs();
        return bindingIdentity(&file, output);
    }
    catch (...)
    {
        return juce::Result::fail("controller.preparationFailed: Profile identity could not be prepared");
    }
}
juce::Result MidiController::bindingProfileIdentity(const juce::File &ownedProfile,
                                                    juce::String &output) const
{
    return bindingIdentity(&ownedProfile, output);
}
juce::Result MidiController::setBinding(size_t index, const ControllerBinding &binding,
                                        const juce::String &expectedProfile)
{
    try
    {
        if (!persistent)
            return setBindingImpl(index, binding, expectedProfile, nullptr);
        const auto file = controllerPrefs();
        return setBindingImpl(index, binding, expectedProfile, &file);
    }
    catch (...)
    {
        return juce::Result::fail(
            "controller.preparationFailed: Mapping preparation failed; mappings preserved");
    }
}
juce::Result MidiController::setBinding(size_t index, const ControllerBinding &binding,
                                        const juce::String &expectedProfile, const juce::File &ownedProfile)
{
    return setBindingImpl(index, binding, expectedProfile, &ownedProfile);
}
void MidiController::publishValidatedBindings(std::shared_ptr<EffectiveProjection> prepared) noexcept
{
    cancelLearn();
    publishPrepared(std::move(prepared), true);
}
juce::Result MidiController::setBindingImpl(size_t index, const ControllerBinding &binding,
                                            const juce::String &expectedProfile, const juce::File *file)
{
    try
    {
        if (projectLearnToken)
            return juce::Result::fail("controller.learnBusy: Project Learn owns capture");
        if (index >= 512 || index > bindings.size())
            return juce::Result::fail(
                "controller.invalidIndex: Existing row or exact append position required");
        if (!valid(binding))
            return juce::Result::fail(
                "controller.invalidBinding: Complete supported binding and range required");
        juce::String current;
        if (auto read = bindingIdentity(file, current); read.failed())
            return read;
        if (expectedProfile.length() != 64 || expectedProfile != current)
            return juce::Result::fail(
                "controller.staleProfile: Mappings or saved bytes changed; read state again");
        auto next = bindings;
        if (index == next.size())
            next.push_back(binding);
        else
            next[index] = binding;
        for (size_t i = 0; i < next.size(); ++i)
        {
            if (!valid(next[i]))
                return juce::Result::fail(
                    "controller.invalidBinding: An existing row is invalid; mappings preserved");
            for (size_t j = i + 1; j < next.size(); ++j)
            {
                const auto &a = next[i], &b = next[j];
                if (a.enabled && b.enabled && a.port == b.port && a.kind == b.kind && a.number == b.number &&
                    (!a.channel || !b.channel || a.channel == b.channel) &&
                    (a.layer == b.layer || a.mode == 4 || b.mode == 4))
                    return juce::Result::fail(
                        "controller.bindingConflict: Control is already mapped in this layer");
            }
        }
        std::shared_ptr<EffectiveProjection> prepared;
        if (auto result = prepareProjection(next, nullptr, {}, prepared); result.failed())
            return result;
        const auto serialized = juce::JSON::toString(stateForBindings(next));
        if (serialized.getNumBytesAsUTF8() > 1024 * 1024)
            return juce::Result::fail("controller.profileTooLarge: Saved profile exceeds1MiB");
        if (file)
        {
            juce::TemporaryFile temporary(*file);
            if (file->getParentDirectory().createDirectory().failed() ||
                !temporary.getFile().replaceWithText(serialized))
                return juce::Result::fail(
                    "controller.profileSaveFailed: Candidate mappings could not be saved");
            juce::String fresh;
            if (auto read = bindingIdentity(file, fresh); read.failed())
                return read;
            if (fresh != expectedProfile)
                return juce::Result::fail(
                    "controller.staleProfile: Saved mappings changed during preparation");
            if (!temporary.overwriteTargetFileWithTemporary())
                return juce::Result::fail("controller.profileSaveFailed: Atomic profile replacement failed");
        }
        // No fallible work or observers after the file commit. Retired values
        // are destroyed on CONTROL when this function returns, never DSP.
        bindings.swap(next);
        publishValidatedBindings(std::move(prepared));
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail(
            "controller.preparationFailed: Mapping preparation failed; mappings preserved");
    }
}
juce::var MidiController::bindingSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["action","label","port","channel","kind","number","layer","mode","mixer","minimum","maximum","pickup","banked","enabled","nativeArea"],
      "properties":{
        "action":{"type":"string","description":"Existing registered action or macro.ID. Continuous actions must be in parameterActions readback; mode4 holds layer rather than executing action."},
        "label":{"type":"string","maxLength":120},
        "port":{"type":"integer","minimum":0,"maximum":1,"description":"0 existing performance ingress,1 existing control ingress. Metadata only; does not configure/open any port."},
        "channel":{"type":"integer","minimum":0,"maximum":16,"description":"0 all channels;1..16 exact."},
        "kind":{"type":"integer","minimum":0,"maximum":6,"description":"0 CC,1 note,2 program,3 pitch bend,4 Start,5 Stop,6 Continue."},
        "number":{"type":"integer","minimum":0,"maximum":127},
        "layer":{"type":"integer","minimum":0,"maximum":3},
        "mode":{"type":"integer","minimum":0,"maximum":4,"description":"0 press,1 absolute,2 relative twos-complement,3 relative offset,4 modifier hold. Shared validator enforces action/kind/native-mode combinations."},
        "mixer":{"type":"integer","minimum":-1,"maximum":2147483647,"description":"-1 selected,0 Master,positive stable ID. Banked positives are1-based non-Master display slots up to128; invocation validates current target/route and retains missing saved bindings."},
        "sendTarget":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required only for mixer.send, explicit stable destination (0 Master); source from mixer. Route required at invocation; never redirected."},
        "minimum":{"type":"number","minimum":-100000,"maximum":100000},
        "maximum":{"type":"number","minimum":-100000,"maximum":100000,"description":"Must exceed minimum. mixer.send requires range0..2 and relative/absolute mode without nativeArea."},
        "pickup":{"type":"boolean"},"banked":{"type":"boolean"},"enabled":{"type":"boolean"},
        "nativeArea":{"type":"integer","minimum":0,"maximum":2,"description":"0 normal,1 MK3 pots,2 MK3 faders. Existing exact mode/port/channel/CC validation retained; send requires0."}}})JSON");
}
juce::Result MidiController::save() const
{
    if (!persistent)
        return juce::Result::ok();
    return save(controllerPrefs());
}
juce::Result MidiController::save(const juce::File &file) const
{
    juce::TemporaryFile temporary(file);
    if (file.getParentDirectory().createDirectory().failed() ||
        !temporary.getFile().replaceWithText(juce::JSON::toString(state())) ||
        !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Controller profile could not be saved");
    return juce::Result::ok();
}
} // namespace vibera
