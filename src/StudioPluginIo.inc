// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared CONTROL-only native I/O readback and menu intents. No audio processing.
namespace plugin_io_detail
{
juce::var selection(const PluginMainIo &io)
{
    auto *value = new juce::DynamicObject;
    value->setProperty("inputBus", io.inputBus);
    value->setProperty("outputBus", io.outputBus);
    value->setProperty("inputChannels", io.inputChannels);
    value->setProperty("monoSource", io.monoSource);
    return juce::var(value);
}
} // namespace plugin_io_detail
juce::var Studio::pluginIoState(Id id) const
{
    const auto *spec = controller.project.plugin(id);
    const auto host = audio.preparedPluginHost(controller.project, id);
    auto *result = new juce::DynamicObject;
    auto *state = new juce::DynamicObject;
    result->setProperty("completionState", "completed");
    result->setProperty("documentChanged", false);
    result->setProperty("undoable", false);
    result->setProperty("plugin", juce::int64(id));
    result->setProperty("instance", host ? host->runtimeIdentity() : juce::String{});
    state->setProperty("requested", spec ? plugin_io_detail::selection(pluginMainIoFor(*spec)) : juce::var{});
    PluginBusCatalogue catalogue;
    const bool available = host && host->alive() && host->readBusCatalogue(catalogue);
    state->setProperty("catalogueKnown", available);
    state->setProperty("available", available);
    state->setProperty("mixerChannels", 2);
    state->setProperty("simultaneousMultiOutput", false);
    state->setProperty("capabilityScope", "Actual bus names/default and negotiated counts; another combined "
                                          "layout must negotiate before commit");
    if (available)
    {
        const auto actual = host->busLayout();
        state->setProperty("negotiated", plugin_io_detail::selection(
                                             {actual.mainInputBus, actual.mainOutputBus,
                                              actual.mainInputChannels, actual.mainInputMonoSource}));
        state->setProperty("auxiliaryInputBus", actual.auxiliaryInputBus);
        state->setProperty("auxiliaryChannels", actual.auxiliaryChannels);
    }
    else
        state->setProperty("negotiated", juce::var{});
    juce::Array<juce::var> inputs, outputs;
    if (available)
        for (int i = 0; i < catalogue.inputCount + catalogue.outputCount; ++i)
        {
            const auto &bus = catalogue.entries[std::size_t(i)];
            auto *row = new juce::DynamicObject;
            row->setProperty("index", bus.index);
            row->setProperty("name", juce::String::fromUTF8(bus.name));
            row->setProperty("defaultChannels", bus.defaultChannels);
            row->setProperty("actualChannels", bus.actualChannels);
            (bus.input ? inputs : outputs).add(juce::var(row));
        }
    state->setProperty("inputs", inputs);
    state->setProperty("outputs", outputs);
    result->setProperty("audioIo", juce::var(state));
    return juce::var(result);
}
void Studio::addPluginIoMenu(juce::PopupMenu &parent, Id id, int &nextItem)
{
    const auto *spec = controller.project.plugin(id);
    const auto host = audio.preparedPluginHost(controller.project, id);
    PluginBusCatalogue catalogue;
    const bool available = spec && host && host->alive() && host->readBusCatalogue(catalogue);
    juce::PopupMenu menu;
    if (!available)
    {
        menu.addItem(nextItem++, "Prepare plugin to discover native buses", false);
        parent.addSubMenu("Audio input/output", menu, false);
        return;
    }
    const auto original = pluginMainIoFor(*spec);
    const auto project = controller.projectIdentity;
    const auto instance = host->runtimeIdentity();
    const bool enabled =
        !mixerChainBusy() && !devicePreparationBusy() && !recordArmed && !midiTaking && !hasPendingMidiTake();
    const auto add = [&](juce::PopupMenu &target, const juce::String &name, PluginMainIo selection)
    {
        juce::PopupMenu::Item item(name);
        item.itemID = nextItem++;
        item.isEnabled = enabled && selection.valid() &&
                         (!spec->detector || spec->detector->inputBus != selection.inputBus);
        item.isTicked = selection == original;
        item.action =
            [safe = juce::Component::SafePointer<Studio>(this), id, selection, original, project, instance]
        {
            if (!safe)
                return;
            const auto *current = safe->controller.project.plugin(id);
            const auto owner = safe->audio.preparedPluginHost(safe->controller.project, id);
            if (safe->controller.projectIdentity != project || !current ||
                pluginMainIoFor(*current) != original || !owner || owner->runtimeIdentity() != instance)
            {
                safe->status = "Plugin I/O changed; reopen its menu";
                safe->repaint();
                return;
            }
            CommandArgs args;
            args.target = id;
            args.pluginIo = selection;
            safe->command("plugin.io", args);
        };
        target.addItem(item);
    };
    juce::PopupMenu input, output, mapping;
    for (int i = 0; i < catalogue.inputCount + catalogue.outputCount; ++i)
    {
        const auto &bus = catalogue.entries[std::size_t(i)];
        auto choice = original;
        if (bus.input)
            choice.inputBus = bus.index;
        else
            choice.outputBus = bus.index;
        const auto label = juce::String::fromUTF8(bus.name) + " [" + juce::String(bus.index) + "] (default " +
                           juce::String(bus.defaultChannels) + " ch)";
        add(bus.input ? input : output, label, choice);
    }
    if (!catalogue.inputCount)
        input.addItem(nextItem++, "No native audio inputs", false);
    if (catalogue.inputCount)
        for (int mode = 0; mode < 4; ++mode)
        {
            auto choice = original;
            choice.inputChannels = mode == 0 ? 2 : 1;
            choice.monoSource = mode;
            static constexpr const char *names[]{"Stereo L/R", "Mono from left", "Mono from right",
                                                 "Mono from average"};
            add(mapping, names[mode], choice);
        }
    menu.addSubMenu("Native main input bus", input);
    menu.addSubMenu("Native stereo output bus", output);
    menu.addSubMenu("Input channel mapping", mapping, catalogue.inputCount != 0);
    menu.addSeparator();
    add(menu, "Restore default audio I/O", {});
    menu.addItem(nextItem++, "Other layouts negotiate on Apply; one stereo output", false);
    parent.addSubMenu("Audio input/output", menu, enabled);
}
