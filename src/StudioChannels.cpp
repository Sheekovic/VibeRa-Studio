// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include <map>
#include "synth/SynthRegistry.h"
#include "NativeInstruments.h"
#include "Tuning.h"
#include "SliceDetect.h"
#include "ChannelManagement.h"
#include "ChannelPreset.h"
#include "Editors.h"
#include "McpSchema.h"
#include "../tests/SyntheticDevice.h"
#include <algorithm>

namespace vibera
{
juce::Result Studio::beginChannelEdit(const juce::String &action, const std::vector<Id> &targets,
                                      juce::String &operation)
{
    if (action != "channel.clone" && action != "channel.delete")
        return juce::Result::fail("Unsupported structural Channel action");
    if (controller.hasGesture() || midiTaking || hasPendingMidiTake() || customization.running || afterPluginState || pendingExport ||
        audio.rendering())
        return juce::Result::fail("Finish the active edit, recording take, macro or file operation first");
    CommandArgs args;
    args.ids = targets;
    auto trial = controller.project;
    if (auto valid = applyChannelManagement(trial, action, args); valid.failed())
        return valid;
    ChannelEditJob job;
    job.id = juce::Uuid().toString();
    job.project = controller.projectIdentity;
    job.command = action;
    job.revision = controller.revision;
    job.targets = targets;
    std::vector<Id> pluginIds;
    for (auto id : targets)
        if (const auto *channel = controller.project.channel(id); channel && channel->instrument)
        {
            const auto plugin = channel->instrument->id;
            pluginIds.push_back(plugin);
            const auto host = audio.parameterHost(plugin);
            job.runtimes.emplace_back(plugin, host ? host->runtimeIdentity() : juce::String{});
        }
    while (channelEditJobs.size() >= 64)
        channelEditJobs.erase(channelEditJobs.begin());
    operation = job.id;
    channelEditJobs.push_back(job);
    auto safe = juce::Component::SafePointer<Studio>(this);
    withPluginState(
        [safe, token = job.id](juce::Result captured, Project snapshot)
        {
            if (!safe)
                return;
            const auto found = std::find_if(safe->channelEditJobs.begin(), safe->channelEditJobs.end(),
                                            [&](const auto &value) { return value.id == token; });
            if (found == safe->channelEditJobs.end() || found->state != "pending")
                return;
            auto &current = *found;
            auto fail = [&](const juce::String &code, const juce::String &message)
            {
                current.state = "failed";
                current.error = code;
                current.message = message;
                safe->status = message;
                safe->repaint();
            };
            if (captured.failed())
                return fail("capture_failed", captured.getErrorMessage());
            if (safe->controller.projectIdentity != current.project ||
                safe->controller.revision != current.revision)
                return fail("stale_revision",
                            "Channel operation cancelled: project changed during state capture");
            for (const auto &[plugin, instance] : current.runtimes)
            {
                const auto host = safe->audio.parameterHost(plugin);
                if ((host ? host->runtimeIdentity() : juce::String{}) != instance)
                    return fail("stale_target", "Instrument runtime changed during Channel state capture");
            }
            CommandArgs values;
            values.ids = current.targets;
            for (auto id : current.targets)
                if (const auto *channel = snapshot.channel(id); channel && channel->instrument)
                    values.capturedInstruments.push_back(*channel->instrument);
            const auto firstNewId = safe->controller.project.nextId;
            safe->command(current.command, values);
            if (safe->status != "Ready")
                return fail("validation_failed", safe->status);
            if (current.command == "channel.clone")
            {
                for (const auto &channel : safe->controller.project.channels)
                    if (channel.id >= firstNewId)
                        current.created.push_back(channel.id);
                if (!current.created.empty())
                    safe->selectRackChannels(current.created, "replace", current.created.front());
            }
            current.state = "completed";
            current.message =
                "Document transaction committed; read audio/plugin state for engine preparation";
            safe->status = current.command == "channel.clone"
                               ? "Channels cloned (settings only; paste score if wanted)"
                               : "Channels deleted; Undo restores them";
            safe->refresh();
        },
        &pluginIds);
    return juce::Result::ok();
}
juce::var Studio::channelEditState(const juce::String &id) const
{
    const auto found = std::find_if(channelEditJobs.begin(), channelEditJobs.end(),
                                    [&](const auto &value) { return value.id == id; });
    if (found == channelEditJobs.end())
        throw McpFailure("unknown_operation", "Channel job is foreign, expired or unknown");
    auto *object = new juce::DynamicObject;
    auto set = [&](const char *name, juce::var value) { object->setProperty(name, value); };
    auto ids = [](const std::vector<Id> &values)
    {
        juce::Array<juce::var> result;
        for (auto value : values)
            result.add(juce::int64(value));
        return juce::var(result);
    };
    set("operation", found->id);
    set("command", found->command);
    set("completionState", found->state);
    set("projectIdentity", found->project);
    set("sourceRevision", juce::int64(found->revision));
    set("targetChannels", ids(found->targets));
    set("createdChannels", ids(found->created));
    set("modelCommitted", found->state == "completed");
    const auto audioState = needsRebuild                            ? "pending"
                            : projectionError.isNotEmpty()          ? "failed"
                            : engineRevision != controller.revision ? "pending"
                                                                    : "ready";
    set("audioPreparationPending", juce::String(audioState) == "pending");
    set("audioPreparationState", audioState);
    set("audioError", projectionError);
    set("message", found->message);
    set("cloneIncludesScore", false);
    if (found->state == "failed")
    {
        auto *error = new juce::DynamicObject;
        error->setProperty("code", found->error);
        error->setProperty("message", found->message);
        set("error", juce::var(error));
    }
    return juce::var(object);
}
juce::Result Studio::cancelChannelEdit(const juce::String &id)
{
    for (auto &job : channelEditJobs)
        if (job.id == id)
        {
            if (job.state != "pending")
                return juce::Result::fail("Channel job is already terminal");
            job.state = "cancelled";
            job.message = "Cancelled before document mutation; pending capture will drain safely";
            return juce::Result::ok();
        }
    return juce::Result::fail("Channel job is foreign, expired or unknown");
}
std::optional<juce::PluginDescription> Studio::instrumentByIdentifier(const juce::String &identifier) const
{
    for (const auto &native : nativeInstrumentDescriptions())
        if (native.createIdentifierString() == identifier)
            return native;
    for (const auto &d : plugins.snapshot().plugins)
        if (d.isInstrument && d.createIdentifierString() == identifier)
            return d;
    return std::nullopt;
}
void Studio::addInstrument(const juce::PluginDescription &description, Id replace)
{
    CommandArgs a;
    a.text = description.createXml()->toString();
    a.channel = replace ? replace : selectedChannel;
    const auto revision = controller.revision;
    command(replace ? "plugin.instrument.replace" : "plugin.instrument.create", a);
    if (controller.revision != revision)
    {
        recentSources.record("plugin:" + description.createIdentifierString());
        if (!replace)
            selectedChannel = controller.project.channels.back().id;
        status = (replace ? "Replaced with " : "Added ") + description.name;
    }
    refresh();
}
void Studio::addInstrumentItems(juce::PopupMenu &menu, Id replace)
{
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto add = [&](juce::PopupMenu &target, const juce::PluginDescription &d)
    {
        juce::PopupMenu::Item item(d.name);
        item.action = [safe, d, replace]
        {
            if (safe)
                safe->addInstrument(d, replace);
        };
        target.addItem(item);
    };
    menu.addSectionHeader("VibeRa");
    for (const auto &native : nativeInstrumentDescriptions())
        add(menu, native);
    // Scanned instruments: an all-plugins list, then one submenu per category.
    auto list = plugins.snapshot().plugins;
    std::erase_if(list, [](const auto &d) { return !d.isInstrument; });
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.name.compareIgnoreCase(b.name) < 0; });
    if (!list.empty())
    {
        menu.addSectionHeader("Plugins");
        std::map<juce::String, juce::PopupMenu> categories;
        for (const auto &d : list)
        {
            add(menu, d);
            const auto category = d.category.isNotEmpty() ? d.category.replace("|", " / ") : juce::String("Other");
            add(categories[category], d);
            if (d.manufacturerName.isNotEmpty())
                add(categories["By maker / " + d.manufacturerName], d);
        }
        juce::PopupMenu byCategory;
        for (auto &[name, sub] : categories)
            byCategory.addSubMenu(name, sub);
        menu.addSubMenu("Categories", byCategory);
    }
    menu.addSeparator();
    if (!replace)
    {
        juce::PopupMenu::Item sample("Load sample...");
        sample.action = [safe]
        {
            if (!safe)
                return;
            safe->chooser = std::make_unique<juce::FileChooser>("Load sample", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            safe->chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [safe](const juce::FileChooser &fc)
                                       {
                                           if (safe && fc.getResult().existsAsFile())
                                               safe->loadSample(fc.getResult().getFullPathName(), false);
                                       });
        };
        menu.addItem(sample);
    }
    juce::PopupMenu::Item more(list.empty() ? "Find plugins (scan VST3 folders)..." : "More plugins / rescan...");
    more.action = [safe]
    {
        if (safe)
            safe->showPlugins();
    };
    menu.addItem(more);
}
void Studio::showInstrumentMenu(Id replace)
{
    juce::PopupMenu menu;
    addInstrumentItems(menu, replace);
    menu.showMenuAsync({});
}
void Studio::addChannelManagementItems(juce::PopupMenu &menu, const std::vector<Id> &ids)
{
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    int menuId = 801;
    for (const auto &[action, label] :
         std::array<std::pair<const char *, const char *>, 4>{{{"channel.clone", "Clone"},
                                                               {"channel.delete", "Delete"},
                                                               {"channel.moveUp", "Move up"},
                                                               {"channel.moveDown", "Move down"}}})
    {
        juce::PopupMenu::Item item(label);
        item.itemID = menuId++;
        item.isEnabled = !ids.empty();
        item.action = [safe = juce::Component::SafePointer<Studio>(this), ids, identity, revision,
                       action = juce::String(action)]
        {
            if (!safe || safe->controller.projectIdentity != identity ||
                safe->controller.revision != revision)
                return;
            if (action == "channel.clone" || action == "channel.delete")
            {
                juce::String job;
                const auto started = safe->beginChannelEdit(action, ids, job);
                if (started.failed())
                    safe->status = started.getErrorMessage();
            }
            else
            {
                CommandArgs args;
                args.ids = ids;
                safe->command(action, args);
            }
            safe->repaint();
        };
        menu.addItem(item);
    }
}
// Tuning: equal temperament, maqam presets on a tonic, or a 12-note Scala file.
void Studio::addChannelTuningItems(juce::PopupMenu &menu, Id channel)
{
    const auto *target = controller.project.channel(channel);
    if (!target || target->audioClip)
        return;
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto apply = [safe, channel](const tuning::Table &table)
    {
        if (!safe)
            return;
        CommandArgs args;
        args.channel = channel;
        args.tuning = table;
        safe->command("channel.tuning", args);
    };
    const char *roots[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    juce::PopupMenu tuningMenu;
    const bool equal = target->scale.empty() &&
                       std::all_of(target->tuning.begin(), target->tuning.end(), [](double c) { return c == 0; });
    tuningMenu.addItem("Equal temperament", true, equal, [apply] { apply({}); });
    for (const char *name : {"rast", "bayati", "saba", "sikah", "jiharkah"})
    {
        juce::PopupMenu tonics;
        for (int root = 0; root < 12; ++root)
            tonics.addItem(roots[root], [apply, name, root]
                           {
                               tuning::Table table;
                               tuning::maqamTable(name, root, table);
                               apply(table);
                           });
        tuningMenu.addSubMenu(juce::String("Maqam ") + name, tonics);
    }
    juce::PopupMenu scala;
    for (int root = 0; root < 12; ++root)
        scala.addItem(juce::String("Tonic ") + roots[root] + "...", [safe, apply, root, channel]
                      {
                          if (!safe)
                              return;
                          safe->chooser = std::make_unique<juce::FileChooser>(
                              "Load a Scala scale (any size)", juce::File(), "*.scl");
                          safe->chooser->launchAsync(
                              juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, apply, root, channel](const juce::FileChooser &fc)
                              {
                                  if (!safe || fc.getResult() == juce::File{})
                                      return;
                                  const auto text = fc.getResult().loadFileAsString();
                                  tuning::Table table{};
                                  std::vector<double> degrees;
                                  if (tuning::parseScala(text, root, table).wasOk())
                                      apply(table); // 12 notes per octave: a per-pitch-class table.
                                  else if (auto parsed = tuning::parseScalaDegrees(text, degrees); parsed.failed())
                                      safe->status = parsed.getErrorMessage();
                                  else
                                  {
                                      // Any other size or period: consecutive keys walk the degrees,
                                      // with the tonic key above middle C playing at its normal pitch.
                                      CommandArgs args;
                                      args.channel = channel;
                                      args.scale = degrees;
                                      args.pitch = 60 + root;
                                      safe->command("channel.scale", args);
                                      safe->status = "Mapped a " + juce::String(int(degrees.size())) +
                                                     "-note scale to the keys from " + juce::String(60 + root);
                                  }
                              });
                      });
    tuningMenu.addSubMenu("Load Scala (.scl)", scala);
    tuningMenu.addSeparator();
    tuningMenu.addItem(target->mtsEsp && !mtsEsp.registered() ? "Broadcast via MTS-ESP (library not found or busy)"
                                                              : "Broadcast via MTS-ESP",
                       true, target->mtsEsp, [safe, channel, on = !target->mtsEsp]
                       {
                           if (!safe)
                               return;
                           CommandArgs args;
                           args.channel = channel;
                           args.enabled = on;
                           safe->command("channel.mtsEsp", args);
                       });
    if (target->instrument)
    {
        tuningMenu.addItem("Play tuning through MPE (plugin)", true, target->mpe, [safe, channel, on = !target->mpe]
                           {
                               if (!safe)
                                   return;
                               CommandArgs args;
                               args.channel = channel;
                               args.enabled = on;
                               safe->command("channel.mpe", args);
                           });
    }
    menu.addSubMenu("Tuning", tuningMenu);
    const bool sliceable = !target->instrument && juce::File::isAbsolutePath(target->source);
    menu.addItem("Slice to Channels (auto)", sliceable, false, [safe, channel]
                 {
                     if (safe)
                         if (auto result = safe->sliceChannel(channel, .5); result.failed())
                             safe->status = result.getErrorMessage();
                 });
}
juce::Result Studio::sliceChannel(Id channel, double sensitivity)
{
    const auto *target = controller.project.channel(channel);
    if (!target || target->instrument || target->audioClip || !juce::File::isAbsolutePath(target->source))
        return juce::Result::fail("Slicing needs a sampler Channel with a sample file");
    const juce::File file(target->source);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0)
        return juce::Result::fail("Cannot read " + file.getFileName());
    CommandArgs args;
    args.channel = channel;
    args.slicePoints = detectSlices(file, sensitivity);
    args.length = double(reader->lengthInSamples) / reader->sampleRate;
    if (args.slicePoints.size() < 2)
        return juce::Result::fail("No clear hits found to slice in " + file.getFileName());
    command("channel.sliceToChannels", args);
    status = "Sliced " + target->name + " into " + juce::String(int(args.slicePoints.size())) + " Channels";
    return juce::Result::ok();
}
void Studio::addChannelPresetItems(juce::PopupMenu &menu, Id channel)
{
    const auto *target = controller.project.channel(channel);
    const bool supported = target && !target->audioClip;
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.addItem("Save Channel preset...", supported, false, [safe, channel]
                 { if (safe) safe->saveChannelPreset(channel); });
    menu.addItem("Load Channel preset...", supported, false, [safe, channel]
                 { if (safe) safe->loadChannelPreset(channel); });
}
static juce::File channelPresetFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("VibeRa Studio")
        .getChildFile("Channel presets");
}
void Studio::captureChannelPreset(Id channel, std::function<void(juce::Result, juce::String)> done)
{
    const auto *current = controller.project.channel(channel);
    if (current && current->instrument)
    {
        // Native state must be captured fresh from the running plugin, never the restore cache.
        withPluginState(
            [channel, done](juce::Result result, Project snapshot)
            {
                juce::String xml;
                if (result.wasOk())
                    result = encodeChannelPreset(snapshot, channel, xml);
                done(result, xml);
            });
        return;
    }
    juce::String xml;
    const auto encoded = encodeChannelPreset(controller.project, channel, xml);
    done(encoded, xml);
}
juce::Result Studio::applyChannelPresetFile(Id channel, const juce::File &file)
{
    if (!file.existsAsFile())
        return juce::Result::fail("channelPreset.missingFile: Preset file not found");
    if (file.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail("channelPreset.tooLarge: Preset file is too large");
    Channel preset;
    if (auto decoded = decodeChannelPreset(file.loadFileAsString(), preset); decoded.failed())
        return decoded;
    // A moved preset folder keeps working when its sample travels beside it.
    if (!preset.instrument && !preset.source.startsWith("builtin:") && !juce::File(preset.source).existsAsFile())
        if (const auto beside = file.getSiblingFile(juce::File(preset.source).getFileName()); beside.existsAsFile())
            preset.source = beside.getFullPathName();
    CommandArgs args;
    args.channel = channel;
    if (auto encoded = encodeChannelPreset(preset, args.text); encoded.failed())
        return encoded;
    const auto revision = controller.revision;
    command("channel.preset", args);
    if (controller.revision == revision)
        return juce::Result::fail(status);
    status = "Loaded Channel preset " + file.getFileNameWithoutExtension();
    return juce::Result::ok();
}
void Studio::saveChannelPreset(Id channel)
{
    auto safe = juce::Component::SafePointer<Studio>(this);
    status = "Capturing Channel preset...";
    captureChannelPreset(channel,
                         [safe, channel](juce::Result result, juce::String xml)
                         {
                             if (!safe)
                                 return;
                             if (result.failed())
                             {
                                 safe->status = result.getErrorMessage();
                                 safe->repaint();
                                 return;
                             }
                             safe->chooseChannelPresetFile(channel, xml);
                         });
    repaint();
}
void Studio::chooseChannelPresetFile(Id channel, const juce::String &xml)
{
    const auto folder = channelPresetFolder();
    folder.createDirectory();
    const auto *source = controller.project.channel(channel);
    if (!source)
    {
        status = "channelPreset.missingChannel: Channel was removed before saving";
        repaint();
        return;
    }
    const auto suggested = folder.getChildFile(juce::File::createLegalFileName(source->name))
                               .withFileExtension(channelPresetExtension);
    chooser = std::make_unique<juce::FileChooser>("Save Channel preset", suggested,
                                                  juce::String("*") + channelPresetExtension);
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe, xml](const juce::FileChooser &fc)
                         {
                             if (!safe || fc.getResult() == juce::File{})
                                 return;
                             const auto file = fc.getResult().withFileExtension(channelPresetExtension);
                             safe->status = file.replaceWithText(xml)
                                                ? "Saved Channel preset " + file.getFileName()
                                                : "channelPreset.writeFailed: Could not write " +
                                                      file.getFullPathName();
                             safe->repaint();
                         });
}
void Studio::loadChannelPreset(Id channel)
{
    const auto folder = channelPresetFolder();
    chooser = std::make_unique<juce::FileChooser>("Load Channel preset", folder.isDirectory() ? folder : juce::File{},
                                                  juce::String("*") + channelPresetExtension);
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto identity = controller.projectIdentity;
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, channel, identity](const juce::FileChooser &fc)
        {
            const auto file = fc.getResult();
            if (!safe || !file.existsAsFile() || safe->controller.projectIdentity != identity)
                return;
            if (auto applied = safe->applyChannelPresetFile(channel, file); applied.failed())
                safe->status = applied.getErrorMessage();
            safe->repaint();
        });
}
// Burn a Channel loop while Pattern playback runs on the owned synthetic device.
juce::Result Studio::verifyLiveBurn(const juce::File &folder)
{
    folder.createDirectory();
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    customization = Customization::familiarDefaults();
    auto project = Project::demo();
    for (auto &insert : project.mixer)
        insert.effects.clear();
    auto &pattern = project.patterns.front();
    const auto channel = project.channels[3].id; // Built-in bass: sustained melodic notes.
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(),
                                       [&](const auto &n) { return n.channel == channel; }),
                        pattern.notes.end());
    pattern.notes.push_back({project.allocate(), channel, 0, .5, 48, .9f, false});
    pattern.notes.push_back({project.allocate(), channel, .5, .5, 55, .9f, false});
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
        return juce::Result::fail("Live Burn: no virtual test device");
    controller.replace(project);
    selectedPattern = project.patterns.front().id;
    selectedChannel = channel;
    selectedChannels = {channel};
    patternMode = true;
    refresh();
    juce::AudioBuffer<float> block(2, 256);
    float peak = 0;
    auto tick = [&]
    {
        timerCallback();
        device->process(block);
        peak = std::max(peak, block.getMagnitude(0, block.getNumSamples()));
        dispatchMessages(2);
    };
    auto settle = [&](int blocks)
    {
        for (int i = 0; i < blocks; ++i)
            tick();
    };
    for (int i = 0; i < 400 && needsRebuild; ++i)
        tick();
    CommandArgs loop;
    loop.pattern = selectedPattern;
    loop.channel = channel;
    loop.text = "fixed";
    loop.length = 1;
    command("pattern.channelLoop", loop);
    if (status != "Ready" && !status.startsWith("Routing"))
        return juce::Result::fail("Live Burn: loop setter refused: " + status);
    settle(40);
    audio.setLooping(true);
    audio.playStop();
    settle(120);
    if (!audio.playing())
        return juce::Result::fail("Live Burn: transport did not start");
    const auto beforeBurn = controller.project.state();
    const auto revision = controller.revision;
    peak = 0;
    command("pattern.burnChannelLoop", loop);
    if (controller.revision == revision)
        return juce::Result::fail("Live Burn: refused during playback: " + status);
    const auto *burned = controller.project.pattern(selectedPattern);
    const auto copies = std::count_if(burned->notes.begin(), burned->notes.end(),
                                      [&](const auto &n) { return n.channel == channel; });
    if (copies < 8 || !burned->channelLoops.empty())
        return juce::Result::fail("Live Burn: repetitions were not materialized");
    settle(400);
    if (!audio.playing() || peak <= .001f)
        return juce::Result::fail("Live Burn: playback stopped or went silent after Burn");
    if (controller.undoEditChecked().failed() || !controller.project.state().isEquivalentTo(beforeBurn))
        return juce::Result::fail("Live Burn: Undo did not restore the loop exactly");
    settle(100);
    if (!audio.playing())
        return juce::Result::fail("Live Burn: Undo interrupted playback");
    audio.playStop();
    settle(400); // Release tails (built-in bass ~12 ms release) must decay: no stuck notes.
    peak = 0;
    settle(40);
    if (peak > 1e-4f)
        return juce::Result::fail("Live Burn: sound continued after Stop (stuck notes), peak " + juce::String(peak));
    recordArmed = true;
    const auto armedRevision = controller.revision;
    command("pattern.burnChannelLoop", loop);
    recordArmed = false;
    if (controller.revision != armedRevision || !status.startsWith("channelLoop.transportBusy:"))
        return juce::Result::fail("Live Burn: armed recording must still refuse Burn");
    return juce::Result::ok();
}
juce::Result Studio::verifyChannelManagement(const juce::File &folder, const juce::File &fixture)
{
    const auto obj = [] { return juce::var(new juce::DynamicObject); };
    const auto set = [](juce::var &object, const char *name, juce::var value)
    { object.getDynamicObject()->setProperty(name, value); };
    juce::Array<juce::var> waits;
    const auto record = [&](bool passed, const juce::String &message)
    {
        auto report = obj();
        set(report, "passed", passed);
        set(report, "message", message);
        set(report, "waits", juce::var(waits));
        set(report, "state", mcpCall("vibera_state", obj())["structuredContent"]);
        auto query = obj();
        set(query, "action", "list");
        set(report, "plugins", mcpPluginOperation(query));
        folder.getChildFile("channel-management-readback.json").replaceWithText(juce::JSON::toString(report));
    };
    auto fail = [&](const juce::String &message)
    {
        record(false, message);
        return juce::Result::fail("Channel management: " + message);
    };
    folder.createDirectory();
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    customization = Customization::familiarDefaults();
    Project project = Project::demo();
    if (fixture != juce::File{})
    {
        if (auto loaded = loadProject(fixture, project); loaded.failed())
            return loaded;
        // Qualify the installed instrument independently of unrelated effect/demo behavior.
        for (auto &insert : project.mixer)
            insert.effects.clear();
    }
    auto source = project.channels.front().id;
    if (fixture != juce::File{})
    {
        const auto found = std::find_if(project.channels.begin(), project.channels.end(),
                                        [](const auto &c) { return c.instrument.has_value(); });
        if (found == project.channels.end())
            return fail("installed fixture has no instrument");
        source = found->id;
    }
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
        return fail("no virtual test device");
    controller.replace(project);
    selectedPattern = project.patterns.front().id;
    selectedChannel = source;
    selectedChannels = {source};
    focus = Scope::rack;
    patternMode = true;
    refresh();
    juce::AudioBuffer<float> block(2, 256);
    auto tick = [&]
    {
        timerCallback();
        device->process(block);
        dispatchMessages(2);
    };
    auto wait = [&](auto ready)
    {
        const auto began = juce::Time::getMillisecondCounter();
        const auto deadline = began + 20000;
        const auto beforeStatus = status;
        while (!ready() && juce::Time::getMillisecondCounter() < deadline)
            tick();
        const bool completed = ready();
        auto row = obj();
        set(row, "completed", completed);
        set(row, "elapsedMs", int(juce::Time::getMillisecondCounter() - began));
        set(row, "beforeStatus", beforeStatus);
        set(row, "afterStatus", status);
        waits.add(row);
        return completed;
    };
    if (!wait([&] { return !needsRebuild; }))
        return fail("initial playback preparation: " + status);
    const auto before = controller.project.state();
    if (!performAction("clipboard.copy"))
        return fail("shared source score Copy");
    // Real context-item callback; do not replace this by a direct document edit.
    juce::PopupMenu menu;
    addChannelManagementItems(menu, {source});
    std::function<void()> cloneAction;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().text == "Clone")
        {
            if (it.getItem().itemID == 0)
                return fail("Clone context item has no native dispatch ID");
            cloneAction = it.getItem().action;
        }
    if (!cloneAction)
        return fail("missing actual Clone context action");
    audio.playStop();
    cloneAction();
    if (channelEditJobs.empty() || channelEditJobs.back().state != "pending" ||
        !controller.project.state().isEquivalentTo(before))
        return fail("Clone mutated before asynchronous capture completion");
    if (!wait([&] { return channelEditJobs.back().state != "pending" && !needsRebuild; }) ||
        channelEditJobs.back().state != "completed" || !audio.playing())
        return fail("Clone did not commit/prepare while playback continued: " + status);
    const auto created = channelEditJobs.back().created;
    if (created.size() != 1 || selectedChannel != created.front())
        return fail("Clone selection/readback");
    const auto clone = created.front();
    const auto *original = controller.project.channel(source);
    const auto *copy = controller.project.channel(clone);
    if (!copy || copy->source != original->source || copy->group != original->group ||
        copy->mixer != original->mixer)
        return fail("cloned settings/routing");
    if (original->instrument)
    {
        auto first = audio.parameterHost(original->instrument->id),
             second = audio.parameterHost(copy->instrument->id);
        if (!first || !second || first == second || !first->alive() || !second->alive() ||
            first->processId() == second->processId() ||
            original->instrument->state != copy->instrument->state)
            return fail("fresh captured state and independent actual instrument workers");
    }
    if (!performAction("clipboard.paste") || status != "Ready")
        return fail("shared Clone/Paste score workflow");
    const auto *part = controller.project.pattern(selectedPattern);
    const auto pasted = std::find_if(part->notes.begin(), part->notes.end(),
                                     [&](const auto &note) { return note.channel == clone; });
    if (pasted == part->notes.end())
        return fail("Paste did not reach cloned instrument");
    auto *piano = static_cast<Editor *>(windows[1]->getContentComponent());
    const auto bounds = piano->noteBounds(*pasted);
    if (bounds.getY() < 44 || bounds.getBottom() > piano->getHeight() - 44 || focus != Scope::rack)
        return fail("first pasted notes are not revealed without stealing Rack focus");
    const auto clonedState = controller.project.state();
    // Actual name-target Shift-wheel route; active MIDI/selection must not move.
    auto *editor = static_cast<Editor *>(windows[0]->getContentComponent());
    const auto visible = visibleRackChannels();
    const auto row = std::find(visible.begin(), visible.end(), clone) - visible.begin();
    juce::Point<float> point{75.f, float(44 + row * 36 + 16)};
    juce::MouseEvent wheelEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 1.f, 0.f, 0.f, 0.f,
                                0.f, editor, editor, juce::Time::getCurrentTime(), point,
                                juce::Time::getCurrentTime(), 1, false);
    const auto graph = audio.projectionDiagnostics();
    const auto cloneIndex =
        std::find_if(controller.project.channels.begin(), controller.project.channels.end(),
                     [&](const auto &channel) { return channel.id == clone; }) -
        controller.project.channels.begin();
    editor->mouseWheelMove(wheelEvent, juce::MouseWheelDetails{0, 1, false, false, false});
    tick();
    if (cloneIndex < 1 || controller.project.channels[size_t(cloneIndex - 1)].id != clone ||
        selectedChannel != clone || audio.projectionDiagnostics().replacements != graph.replacements ||
        !audio.playing())
        return fail("Shift-wheel reorder restarted graph or redirected active instrument");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(clonedState))
        return fail("Shift-wheel exact Undo");
    tick();
    // No callback is pumped between accept/cancel or accept/local edit here.
    juce::String token;
    if (beginChannelEdit("channel.clone", {source}, token).failed() || cancelChannelEdit(token).failed())
        return fail("shared cancellation admission");
    tick();
    if (channelEditState(token)["completionState"].toString() != "cancelled" ||
        !controller.project.state().isEquivalentTo(clonedState))
        return fail("cancelled state capture mutated document");
    if (!wait([&] { return !afterPluginState; }))
        return fail("cancelled capture did not drain");
    if (beginChannelEdit("channel.clone", {source}, token).failed())
        return fail("stale operation admission");
    CommandArgs note;
    note.pattern = selectedPattern;
    note.channel = source;
    note.beat = 3;
    note.length = .25;
    command("notes.add", note);
    if (!wait([&] { return channelEditState(token)["completionState"].toString() != "pending"; }) ||
        channelEditState(token)["error"]["code"].toString() != "stale_revision")
        return fail("stale async mutation guard");
    controller.undoEdit();
    tick();
    // Delete freshly captures the clone too; Undo must restore its independent state.
    if (beginChannelEdit("channel.delete", {clone}, token).failed() ||
        !wait(
            [&]
            {
                return channelEditState(token)["completionState"].toString() != "pending" && !needsRebuild;
            }) ||
        controller.project.channel(clone))
        return fail("delete transaction during playback");
    if (auto undone = controller.undoEditChecked(); undone.failed())
        return fail("delete Undo admission: " + undone.getErrorMessage());
    // Native topology history is an owned asynchronous transaction. Acceptance
    // is not completion: await its terminal result before inspecting the model.
    const auto historyOperation = lastMixerChainHistoryOperation;
    if (const auto *channel = controller.project.channel(source); channel && channel->instrument)
    {
        // Reproduce the accepted structural transition deterministically:
        // a retiring graph or capture gap can acknowledge an all-notes-off
        // after history pins were admitted. No document/native state changes.
        const auto host = audio.parameterHost(channel->instrument->id);
        if (!host)
            return fail("missing source worker for structural reset regression");
        const auto previousReset = host->midiResetGeneration();
        host->resetNotes();
        const auto deadline = juce::Time::getMillisecondCounter() + 20000;
        while (host->midiResetGeneration() == previousReset && juce::Time::getMillisecondCounter() < deadline)
        {
            device->process(block);
            dispatchMessages(2);
        }
        if (host->midiResetGeneration() == previousReset)
            return fail("structural reset was not actually acknowledged by native DSP");
    }
    if (!wait([&] { return !mixerChainApplying() && !needsRebuild; }) ||
        (historyOperation.isNotEmpty() &&
         mixerChainReadback(historyOperation)["completionState"].toString() != "completed") ||
        !controller.project.channel(clone) || !audio.playing())
        return fail("delete Undo did not restore runnable instrument: " + status);
    if (const auto *restored = controller.project.channel(clone); restored->instrument)
    {
        const auto host = audio.parameterHost(restored->instrument->id);
        if (!host || !host->alive() || restored->instrument->state.isEmpty())
            return fail("delete Undo restored only a placeholder instead of the installed instrument");
        audio.setMidiTarget(clone);
        audio.typingInput.handleIncomingMidiMessage(nullptr,
                                                    juce::MidiMessage::noteOn(1, 64, juce::uint8(110)));
        float recoveredPeak = 0;
        for (int n = 0; n < 16; ++n)
        {
            tick();
            recoveredPeak = std::max(recoveredPeak, block.getMagnitude(0, 256));
        }
        audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 64));
        tick();
        if (!std::isfinite(recoveredPeak) || recoveredPeak <= .0001f)
            return fail("structural-reset Undo did not recover actual finite native PCM");
    }
    audio.stop();
    setSize(1920, 1040);
    resized();
    timerCallback(); // Render actual stopped transport/status after its normal UI synchronization.
    auto stream = folder.getChildFile("channel-management-workspace.png").createOutputStream();
    if (!stream)
        return fail("snapshot stream");
    stream->setPosition(0);
    stream->truncate();
    if (!juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), *stream))
        return fail("workspace snapshot");
    record(true, "All Channel management assertions passed");
    folder.getChildFile("channel-management-gui.txt")
        .replaceWithText(
            "PASS actual Clone menu action; shared async acceptance/cancel/stale failure; clone/delete/Undo "
            "during virtual playback; independent saved instrument state; Shift-wheel stable target/graph "
            "and exact Undo; populated component PNG. No OS input, physical MIDI or listening.\n");
    return juce::Result::ok();
}
} // namespace vibera
