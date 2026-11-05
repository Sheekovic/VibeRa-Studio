// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "NativeWindowStyle.h"
#include "Settings.h"
#include "PluginStateEncoding.h"
#include "McpSchema.h"
#include "../tests/SyntheticDevice.h"
#if JUCE_WINDOWS
#include "../tests/NativeGestureFixture.h"
#include "../tests/MixerEffectOpeningCapture.h"
#include <UIAutomation.h>
#include <wrl/client.h>
#endif
#include <thread>

namespace vibera
{
juce::var Studio::mixerEffectBrowserState() const
{
    auto *out = new juce::DynamicObject;
    auto *tabs =
        settingsWindow ? dynamic_cast<SettingsContent *>(settingsWindow->getContentComponent()) : nullptr;
    auto *page = tabs ? tabs->pluginPage() : nullptr;
    const bool current =
        page && page->selectedMixerInsert() >= 0 &&
        page->targetProjectIdentity() == controller.projectIdentity &&
        std::any_of(controller.project.mixer.begin(), controller.project.mixer.end(),
                    [&](const auto &entry) { return entry.index == page->selectedMixerInsert(); });
    out->setProperty("open", page && settingsWindow->isVisible() && tabs->getCurrentTabIndex() == 3);
    out->setProperty("targetCurrent", current);
    out->setProperty("insert", current ? juce::var(page->selectedMixerInsert()) : juce::var{});
    out->setProperty("projectIdentity", current ? controller.projectIdentity : juce::String{});
    out->setProperty("insertsPlugin", false);
    return juce::var(out);
}
juce::Result Studio::browseMixerEffects(int index, juce::String &errorCode)
{
    errorCode.clear();
    auto refuse = [&](const char *code, const juce::String &message)
    {
        errorCode = code;
        status = message;
        return juce::Result::fail(message);
    };
    const auto project = controller.projectIdentity;
    if (std::none_of(controller.project.mixer.begin(), controller.project.mixer.end(),
                     [index](const auto &entry) { return entry.index == index; }))
        return refuse("unavailable_target", "Mixer insert is unavailable");
    if (controller.hasGesture() || noteSlicePending() || noteStretchPending())
        return refuse("busy", "Finish the active edit before browsing Mixer effects");
    showPlugins();
    if (controller.projectIdentity != project)
        return refuse("stale_project", "Project changed; reopen Add effect");
    auto *tabs =
        settingsWindow ? dynamic_cast<SettingsContent *>(settingsWindow->getContentComponent()) : nullptr;
    auto *page = tabs ? tabs->pluginPage() : nullptr;
    if (!page || !settingsWindow->isVisible() || tabs->getCurrentTabIndex() != 3)
        return refuse("unavailable_view", "Plugins browser is unavailable");
    if (auto result = page->selectMixerInsert(index, project); result.failed())
        return refuse("unavailable_target", result.getErrorMessage());
    // This is synchronous navigation only. No plugin is inserted, no scan is
    // started, and an existing class selection/scan configuration is preserved.
    status = "Choose a VibeRa or VST3 effect, then Insert effect";
    return juce::Result::ok();
}
juce::var Studio::pluginEditorState(Id id) const
{
    const auto host = audio.preparedPluginHost(controller.project, id);
    const auto window = host ? host->editorWindowState() : PluginEditorWindowState{};
    auto *state = new juce::DynamicObject;
    state->setProperty("instance", host ? host->runtimeIdentity() : juce::String{});
    state->setProperty("nativeProcessId", host && host->alive() ? juce::int64(host->processId()) : juce::int64(0));
    state->setProperty("known", window.known);
    state->setProperty("exists", window.exists);
    state->setProperty("visible", window.visible);
    state->setProperty("minimised", window.minimised);
    state->setProperty("revision", juce::int64(window.revision));
    state->setProperty("busy", host && host->editorBusy());
    state->setProperty("availability",
                       window.availability == PluginEditorAvailability::available        ? "available"
                       : window.availability == PluginEditorAvailability::unsupported    ? "unsupported"
                       : window.availability == PluginEditorAvailability::creationFailed ? "creation_failed"
                       : window.availability == PluginEditorAvailability::controlsOnly   ? "controls_only"
                                                                                         : "unknown");
    state->setProperty("nativeWidgetControl", false);
    state->setProperty("nativeInterfaceAvailable",
                       window.availability == PluginEditorAvailability::available ? juce::var(true)
                       : window.availability == PluginEditorAvailability::controlsOnly ||
                               window.availability == PluginEditorAvailability::unsupported
                           ? juce::var(false)
                           : juce::var{});
    if (host)
    {
        const auto wrapper = host->wrapperState();
        const auto selected = host->wrapperSelection();
        state->setProperty("wrapperView", wrapper.view == PluginWrapperView::settings     ? "settings"
                                          : wrapper.view == PluginWrapperView::parameters ? "parameters"
                                                                                          : "plugin");
        state->setProperty("wrapperReady", wrapper.ready);
        state->setProperty("wrapperPending", wrapper.pending);
        state->setProperty("wrapperParameter",
                           selected ? juce::var(juce::int64(std::uint32_t(selected))) : juce::var{});
        state->setProperty("wrapperGeneration",
                           selected ? juce::var(juce::int64(selected >> 32)) : juce::var{});
        state->setProperty("wrapperNotifications", wrapper.notifications);
        state->setProperty("wrapperLastTweakedParameter",
                           wrapper.lastTweakedSelection
                               ? juce::var(juce::int64(std::uint32_t(wrapper.lastTweakedSelection)))
                               : juce::var{});
        state->setProperty("wrapperLastTweakedGeneration",
                           wrapper.lastTweakedSelection
                               ? juce::var(juce::int64(wrapper.lastTweakedSelection >> 32))
                               : juce::var{});
        if (pluginWrapperPending && pluginWrapperPending->plugin == id &&
            pluginWrapperPending->host == host && pluginWrapperPending->project == controller.projectIdentity)
            state->setProperty("wrapperOperation", pluginWrapperPending->operation);
    }
    return juce::var(state);
}
juce::Result Studio::requestPluginEditor(Id id, bool open, const juce::String &instance,
                                         juce::String &operation, juce::String &errorCode,
                                         PluginWrapperView view, std::uint64_t selection)
{
    auto refuse = [&](const char *code, const char *message)
    {
        errorCode = code;
        return juce::Result::fail(message);
    };
    if (mixerChainApplying())
        return refuse("effectChain.busy", "Await or cancel chain Apply before changing native windows");
    if (devicePreparationBusy() && !devicePreparationDispatch)
        return refuse("device.busy", "Finish/cancel native preparation before changing native windows");
    if (routingCoordinator.mutationBlocked())
        return refuse("routing.busy",
                      "Wait for the current audio transaction before changing native windows");
    pollPluginOperations();
    const auto host = audio.preparedPluginHost(controller.project, id);
    if (!controller.project.plugin(id) || !host || !host->alive())
        return refuse("unavailable_target", "Plugin worker is unavailable; saved state is retained");
    if (host->runtimeIdentity() != instance)
        return refuse("stale_instance", "Plugin runtime changed; rediscover the current instance");
    if (view != PluginWrapperView::plugin && view != PluginWrapperView::settings &&
        view != PluginWrapperView::parameters)
        return refuse("invalid_arguments", "Unknown wrapper view");
    if (selection)
    {
        PluginParameterCatalogue catalogue;
        if (!open || view != PluginWrapperView::parameters ||
            !readPluginCatalogue(id, instance, catalogue) ||
            catalogue.generation != unsigned(selection >> 32) ||
            std::none_of(catalogue.parameters.begin(), catalogue.parameters.end(),
                         [&](const auto &p) { return p.id == std::uint32_t(selection); }))
            return refuse("stale_parameter", "Rediscover the exact parameter ID and catalogue generation");
    }
    if (!open && pendingInstrument && pendingInstrument->plugin == id)
        pendingInstrument.reset(); // Same cancellation of queued UI intent for UI and MCP callers.
    if (afterPluginState || pendingExport || audio.rendering() || midiFileBusy || pluginRecovery ||
        mcpExportOperation.isNotEmpty() ||
        std::any_of(pluginOperations.begin(), pluginOperations.end(),
                    [&](const auto &job) { return job.state == "pending" && job.plugin == id; }))
        return refuse("busy", "Finish the active plugin or project file operation first");
    if (pluginOperations.size() >= 64)
    {
        const auto old = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                      [](const auto &job) { return job.state != "pending"; });
        if (old == pluginOperations.end())
            return refuse("capacity_exceeded", "All plugin operation slots are in use");
        pluginOperations.erase(old);
    }
    // Prepare fallible application bookkeeping before native request ownership.
    pluginOperations.reserve(pluginOperations.size() + 1);
    PluginOperation job;
    job.id = juce::Uuid().toString();
    job.kind = "editor";
    job.action = !open ? "close" : view == PluginWrapperView::settings ? "settings"
                                 : view == PluginWrapperView::parameters ? "parameters" : "open";
    job.project = controller.projectIdentity;
    job.plugin = id;
    job.instance = instance;
    job.started = juce::Time::getMillisecondCounter();
    job.host = host;
    const auto admission =
        host->requestEditor(instance, !open ? PluginEditorAction::close
                                      : view == PluginWrapperView::settings ? PluginEditorAction::settings
                                      : view == PluginWrapperView::parameters ? PluginEditorAction::parameters
                                      : PluginEditorAction::open, selection);
    if (!admission.serial)
    {
        if (admission.code == PluginEditorAdmissionCode::busy)
            return refuse("busy", "Native editor, parameter or state work is already pending");
        if (admission.code == PluginEditorAdmissionCode::capacity ||
            admission.code == PluginEditorAdmissionCode::serialExhausted)
            return refuse("capacity_exceeded", "Native editor request capacity is exhausted");
        return refuse("unavailable_target", "Native editor request was not admitted");
    }
    job.serial = admission.serial;
    // Reserve application ownership before returning the accepted/pending token.
    // Allocation failure must not leave an unowned worker receipt.
    try
    {
        pluginOperations.push_back(std::move(job));
    }
    catch (...)
    {
        host->cancelEditorRequest(instance, admission.serial);
        host->abandonEditorRequest(instance, admission.serial);
        throw;
    }
    operation = pluginOperations.back().id;
    errorCode.clear();
    return juce::Result::ok();
}
void Studio::openPluginEditorUI(Id id, bool open)
{
    const auto host = audio.parameterHost(id);
    juce::String operation, code;
    const auto result =
        requestPluginEditor(id, open, host ? host->runtimeIdentity() : juce::String{}, operation, code);
    if (open && result.failed() && code == "busy")
        for (const auto &channel : controller.project.channels)
            if (channel.instrument && channel.instrument->id == id)
            {
                // Preserve the name-button intent while actual file/native work
                // owns admission. The timer retries the same canonical service;
                // replacement/descriptor checks can invalidate it before retry.
                pendingInstrument = PendingInstrumentOpening{controller.projectIdentity,
                                                             channel.instrument->descriptor, channel.id, id};
                status = "Waiting to open instrument editor...";
                return;
            }
    status = result.failed() ? result.getErrorMessage()
             : open          ? "Opening native plugin editor..."
                             : "Closing native plugin editor...";
}
unsigned Studio::refreshPluginCatalogue(Id id, const juce::String &instance)
{
    const auto host = audio.parameterHost(id);
    return controller.project.plugin(id) && host && host->alive() && host->runtimeIdentity() == instance
               ? host->requestParameterCatalogue()
               : 0;
}
bool Studio::readPluginCatalogue(Id id, const juce::String &instance, PluginParameterCatalogue &out,
                                 bool initial)
{
    const auto host = audio.parameterHost(id);
    if (!controller.project.plugin(id) || !host || !host->alive() || host->runtimeIdentity() != instance)
        return false;
    return initial ? host->readInitialParameterCatalogue(out) : host->readParameterCatalogue(out);
}
juce::Result Studio::setPluginNotifications(Id id, const juce::String &instance, bool enabled)
{
    const auto host = audio.parameterHost(id);
    if (!controller.project.plugin(id) || !host || !host->alive() || host->runtimeIdentity() != instance)
        return juce::Result::fail("Plugin worker instance changed or is unavailable");
    host->setParameterNotificationsEnabled(enabled);
    return host->parameterNotificationsEnabled() == enabled
               ? juce::Result::ok()
               : juce::Result::fail("Plugin notification configuration was not applied");
}
void Studio::pollPluginOperations()
{
    for (auto &job : pluginOperations)
    {
        if (job.kind == "set" || job.kind == "synthSave" || job.state != "pending")
            continue;
        if (job.kind == "chain")
        {
            servicePluginChain(job);
            continue;
        }
        auto finish = [&](const char *state, const juce::String &code, const juce::String &message)
        {
            juce::String terminalState(state), terminalCode(code), terminalMessage(message);
            job.state = std::move(terminalState);
            job.code = std::move(terminalCode);
            job.message = std::move(terminalMessage);
            job.completedRevision = controller.revision;
            if ((job.lastTweakWitness || job.kind == "audioIo") && job.project == controller.projectIdentity)
                status = job.message;
            job.host.reset(); // Reclaim retained resources only on the message/control thread.
        };
        if (job.kind == "editor")
        {
            const bool current = job.project == controller.projectIdentity &&
                                 controller.project.plugin(job.plugin) &&
                                 audio.preparedPluginHost(controller.project, job.plugin) == job.host;
            if (!current)
                job.host->cancelEditorRequest(job.instance, job.serial);
            PluginEditorResult result;
            if (job.editorResult || job.host->readEditorResult(job.instance, job.serial, result))
            {
                if (!job.editorResult)
                    job.editorResult = result;
                const auto &observed = *job.editorResult;
                const auto outcome = pluginEditorOutcomeName(observed.outcome);
                const auto diagnostic = observed.succeeded() ? juce::String{} : job.host->error();
                finish(observed.outcome == PluginEditorOutcome::cancelled ? "cancelled"
                       : observed.succeeded() && current                  ? "completed"
                                                                          : "failed",
                       !current               ? "target_replaced"
                       : observed.succeeded() ? ""
                                              : outcome,
                       !current ? "Plugin/project replaced; inspect the retained native outcome"
                                : juce::String("Native editor: ") + outcome +
                                      (diagnostic.isNotEmpty() ? " | " + diagnostic : juce::String{}));
                if (job.project == controller.projectIdentity)
                    status = job.message;
            }
            // The host's stamped native deadline owns timeout/recovery. A
            // separate UI polling timeout would reject timely late observations.
            continue;
        }
        if (job.kind == "audioIo")
        {
            if (job.childOperation.isEmpty())
                continue; // The synchronous admission frame still owns preparation.
            const auto child = mixerChainReadback(job.childOperation);
            job.historyCommitted = bool(child["documentChanged"]);
            const auto completion = child["completionState"].toString();
            if (completion != "pending")
                finish(completion == "completed"   ? "completed"
                       : completion == "cancelled" ? "cancelled"
                                                   : "failed",
                       child["code"].toString(), child["message"].toString());
            // Structural I/O intentionally replaces the original Host. Its
            // retained canonical job, not old-worker liveness, proves outcome.
            continue;
        }
        if (job.project != controller.projectIdentity || !controller.project.plugin(job.plugin))
        {
            finish("cancelled", "target_replaced", "Project/plugin replaced before completion");
            continue;
        }
        const auto host = audio.parameterHost(job.plugin);
        if (job.kind == "recover")
        {
            if (host && host->alive() && host->runtimeIdentity() != job.instance && !pluginRecovery &&
                !needsRebuild)
                finish("completed", "", "Replacement worker is ready; uncaptured changes may be lost");
            else if (!needsRebuild && !pluginRecovery && !projectionError.isEmpty())
                finish("failed", "recovery_failed", projectionError);
        }
        else if (!host || host != job.host || !host->alive() || host->runtimeIdentity() != job.instance)
            finish("failed", "instance_changed", "Plugin worker changed or became unavailable");
        else
        {
            PluginParameterCatalogue catalogue;
            if (readPluginCatalogue(job.plugin, job.instance, catalogue) && catalogue.serial >= job.serial)
            {
                job.completedSerial = catalogue.serial;
                job.completedGeneration = catalogue.generation;
                if (catalogue.error)
                    finish("failed", "catalogue_unavailable", catalogue.diagnostic);
                else if (job.kind == "refresh")
                    finish("completed", "", "Fresh parameter catalogue received");
                else if (catalogue.generation != job.generation)
                    finish("failed", "stale_catalogue", "Parameter schema changed before the action");
                else if (controller.hasGesture() || (job.revision && *job.revision != controller.revision))
                    finish("failed", "stale_context",
                           "Edit context changed before automation preparation completed");
                else if (job.lastTweakWitness &&
                         [&]
                         {
                             const auto current = lastPluginTweak();
                             const auto &expected = *job.lastTweakWitness;
                             return !current || current->plugin != job.plugin ||
                                    current->instance != job.instance || current->project != job.project ||
                                    current->gesture.serial != expected.serial ||
                                    current->gesture.id != expected.id ||
                                    current->gesture.generation != expected.generation;
                         }())
                    finish("failed", "stale_context", "Last tweaked target changed before completion");
                else
                {
                    const auto parameter =
                        std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                     [&](const auto &p) { return p.id == job.parameter; });
                    if (parameter == catalogue.parameters.end() || !parameter->automatable())
                        finish("failed", "unavailable_parameter", "Native parameter is not automatable");
                    else
                    {
                        const PluginBinding binding{parameter->id,    parameter->flags, parameter->steps,
                                                    parameter->value, parameter->name,  parameter->label};
                        const auto admission = host->validateAutomationBindings({&binding, 1});
                        if (admission.status != PluginBindingStatus::ready ||
                            admission.generation != catalogue.generation)
                            finish("failed", "unavailable_parameter",
                                   "Parameter capabilities changed or are unsupported");
                        else
                        {
                            const auto revision = controller.revision;
                            auto result = juce::Result::ok();
                            try
                            {
                                result = applyNativeAutomation(job.plugin, binding, job.action);
                            }
                            catch (const std::exception &error)
                            {
                                result = juce::Result::fail(error.what());
                            }
                            catch (...)
                            {
                                result = juce::Result::fail(
                                    "Automation action did not return; inspect actual document state");
                            }
                            job.historyCommitted = controller.revision != revision;
                            finish(result.wasOk() ? "completed" : "failed",
                                   result.wasOk() ? "" : "validation_failed",
                                   result.wasOk() ? "Canonical automation action completed"
                                                  : result.getErrorMessage());
                        }
                    }
                }
            }
        }
        if (job.state == "pending" &&
            juce::Time::getMillisecondCounter() - job.started > (job.kind == "recover" ? 20000u : 5000u))
            finish("failed", "operation_timeout", "Timed out; inspect actual runtime state before retrying");
    }
    pollNativeScalarOperations();
}
std::optional<Studio::LastPluginTweak> Studio::lastPluginTweak()
{
    if (lastPluginTweakProject != controller.projectIdentity)
    {
        lastPluginTweakProject = controller.projectIdentity;
        lastPluginTweakTicks = 0;
        lastPluginTweakAmbiguous = false;
        lastPluginTweakInstance.clear();
    }
    PluginGestureRecency order;
    std::vector<LastPluginTweak> candidates;
    for (const auto &runtime : audio.pluginRuntimeStates())
        if (controller.project.plugin(runtime.plugin) && runtime.available)
            if (auto host = audio.parameterHost(runtime.plugin))
                if (auto gesture = host->lastParameterGesture(); gesture && gesture->completedTicks)
                {
                    order.consider(runtime.plugin, gesture->completedTicks);
                    candidates.push_back(
                        {runtime.plugin, runtime.instance, controller.projectIdentity, *gesture});
                }
    // Retain a tombstone when the newest worker disappears or notifications
    // are disabled. Do not resurrect an older plugin as "last tweaked".
    if (order.newestTicks() < lastPluginTweakTicks)
        return {};
    if (order.newestTicks() > lastPluginTweakTicks)
    {
        lastPluginTweakTicks = order.newestTicks();
        lastPluginTweakAmbiguous = false;
        lastPluginTweakInstance.clear();
    }
    if (!order.selected() && order.newestTicks())
        lastPluginTweakAmbiguous = true;
    if (lastPluginTweakAmbiguous)
        return {};
    if (const auto selected = order.selected())
        for (const auto &candidate : candidates)
            if (candidate.plugin == *selected)
            {
                if (lastPluginTweakInstance.isNotEmpty() && candidate.instance != lastPluginTweakInstance)
                    return {};
                lastPluginTweakInstance = candidate.instance;
                return candidate;
            }
    return {};
}
juce::PopupMenu Studio::lastPluginTweakMenu(const std::optional<LastPluginTweak> &target)
{
    juce::PopupMenu menu;
    if (!target)
    {
        menu.addItem("No completed native plugin gesture", false, false, [] {});
        return menu;
    }
    const auto host = audio.parameterHost(target->plugin);
    const auto *spec = controller.project.plugin(target->plugin);
    PluginParameterCatalogue catalogue;
    if (!host || !spec || !host->readParameterCatalogue(catalogue) || catalogue.error ||
        catalogue.generation != target->gesture.generation)
    {
        menu.addItem("Parameter information unavailable; finish a fresh gesture", false, false, [] {});
        return menu;
    }
    const auto parameter = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                        [&](const auto &p) { return p.id == target->gesture.id; });
    if (parameter == catalogue.parameters.end())
        return menu;
    menu.addSectionHeader(spec->name + " - " + parameter->name);
    menu.addItem(300, "Open control...");
    const bool writable = parameter->automatable() && parameter->writable() &&
                          std::none_of(pluginOperations.begin(), pluginOperations.end(), [&](const auto &job)
                                       { return job.plugin == target->plugin && job.state == "pending"; });
    menu.addItem(301, "Create automation clip", writable);
    menu.addItem(302, "Init song with this position", writable);
    const AutomationKey key{AutomationParameter::pluginNormalized, target->plugin, target->gesture.id};
    const bool initialized = std::any_of(controller.project.automation.initialValues.begin(),
                                         controller.project.automation.initialValues.end(),
                                         [&](const auto &entry) { return entry.key == key; });
    menu.addItem(303, "Delete initial value", writable && initialized);
    return menu;
}
juce::Result Studio::requestLastPluginTweak(const LastPluginTweak &target, const juce::String &action,
                                            juce::String &operation, juce::String &errorCode)
{
    operation.clear();
    errorCode.clear();
    const auto refuse = [&](const juce::String &code, const juce::String &message)
    {
        errorCode = code;
        status = message;
        repaint();
        return juce::Result::fail(message);
    };
    const auto current = lastPluginTweak();
    if (!current || current->plugin != target.plugin || current->instance != target.instance ||
        current->project != target.project || current->gesture.serial != target.gesture.serial ||
        current->gesture.id != target.gesture.id || current->gesture.generation != target.gesture.generation)
        return refuse("stale_context", "Last tweaked target changed; finish a fresh gesture");
    if (action == "controls")
    {
        const auto result = requestPluginEditor(
            target.plugin, true, target.instance, operation, errorCode, PluginWrapperView::parameters,
            (std::uint64_t(target.gesture.generation) << 32) | target.gesture.id);
        if (result.failed())
            return refuse(errorCode, result.getErrorMessage());
    }
    else
    {
        if (action != "automation.plugin.create" && action != "automation.plugin.init" &&
            action != "automation.plugin.init.remove")
            return refuse("unsupported_operation", "Unknown Last tweaked action");
        auto args = juce::var(new juce::DynamicObject);
        auto *values = args.getDynamicObject();
        values->setProperty("action", "automate");
        values->setProperty("plugin", juce::int64(target.plugin));
        values->setProperty("instance", target.instance);
        values->setProperty("projectIdentity", target.project);
        values->setProperty("revision", juce::int64(controller.revision));
        values->setProperty("parameter", juce::int64(target.gesture.id));
        values->setProperty("generation", juce::int64(target.gesture.generation));
        values->setProperty("automation", action == "automation.plugin.create" ? "create"
                                          : action == "automation.plugin.init" ? "init"
                                                                               : "removeInit");
        try
        {
            mcpCall("vibera_plugin", args, &operation, &target.gesture);
        }
        catch (const std::exception &error)
        {
            // Admission retains ownership before response construction. A failed
            // response must not abandon an already accepted canonical action.
            if (operation.isEmpty())
            {
                const auto *failure = dynamic_cast<const McpFailure *>(&error);
                return refuse(failure ? failure->code : "operation_failed", error.what());
            }
        }
    }
    status = "Preparing last tweaked parameter...";
    repaint();
    return juce::Result::ok();
}
juce::Result Studio::applyNativeAutomation(Id plugin, const PluginBinding &binding,
                                           const juce::String &action)
{
    CommandArgs args;
    args.target = plugin;
    args.binding = binding;
    args.value = binding.manual;
    args.length = std::max(4., controller.project.endBeat());
    if (action == "automation.plugin.create")
    {
        const auto occupied = [&](int lane)
        {
            const auto uses = [lane](const auto &clip) { return clip.lane == lane; };
            const auto &project = controller.project;
            return std::any_of(project.placements.begin(), project.placements.end(), uses) ||
                   std::any_of(project.audioPlacements.begin(), project.audioPlacements.end(), uses) ||
                   std::any_of(project.automation.placements.begin(), project.automation.placements.end(),
                               uses);
        };
        while (args.lane < 128 && occupied(args.lane))
            ++args.lane;
        if (args.lane == 128)
            return juce::Result::fail("No free Playlist lane; free a lane before creating a clip");
    }
    const auto revision = controller.revision;
    command(action, args);
    if (controller.revision == revision)
        return status == "Ready" ? juce::Result::ok() : juce::Result::fail(status);
    if (action == "automation.plugin.create")
    {
        selectedClips = {controller.project.automation.placements.back().id};
        showEditor(2);
    }
    return juce::Result::ok();
}
class PluginControls final : public juce::Component, private juce::Timer
{
    friend class Studio;
    juce::Component::SafePointer<Studio> studio;
    Id pluginId;
    const juce::String projectIdentity;
    std::shared_ptr<PluginHost> host;
    PluginParameterCatalogue catalogue;
    std::vector<std::uint32_t> visibleIDs;
    std::optional<std::uint32_t> selected;
    struct Pending
    {
        std::uint32_t id;
        unsigned generation;
        float value;
    };
    std::optional<Pending> queued; // Legacy readback placeholder; shared service owns writes.
    juce::String scalarOperation;
    bool scalarDragging = false;
    struct AutomationRequest
    {
        juce::String action, project;
        std::uint32_t id;
        unsigned generation, serial;
    };
    std::optional<AutomationRequest> automationRequest;
    unsigned inFlight = 0;
    unsigned minimumRefresh = 0;
    int ticks = 0;
    bool metadataAvailable = false;
    int metadataError = 0;
    std::uint64_t observedGestureDrops = 0;
    juce::Label heading, details, feedback;
    juce::TextButton reset{"Default"}, refresh{"Refresh"};
    juce::TextButton initialize{"Init song"}, removeInitialization{"Delete init"};
    juce::TextButton lastTweaked{"Last tweaked"};
    juce::TextButton recover{"Recover saved state"};
    juce::ToggleButton notifications{"Notify native parameter changes"};
    static bool automatable(const PluginParameter *p)
    {
        if (!p || !p->automatable())
            return false;
        const PluginBinding binding{p->id, p->flags, p->steps, p->value, p->name, p->label};
        return validPluginBindings({&binding, 1});
    }
    void requestAutomation(const juce::String &action)
    {
        const auto *p = parameter();
        if (!studio || !host || !automatable(p) || pending())
            return;
        const auto serial = studio->refreshPluginCatalogue(pluginId, host->runtimeIdentity());
        if (!serial)
            return;
        minimumRefresh = serial;
        automationRequest = AutomationRequest{action, studio->controller.projectIdentity, p->id,
                                              catalogue.generation, serial};
        feedback.setText("Preparing selected parameter...", juce::dontSendNotification);
    }
    void finishAutomationRequest()
    {
        if (!automationRequest || catalogue.serial < automationRequest->serial)
            return;
        const auto request = *automationRequest;
        automationRequest.reset();
        if (!studio || request.project != studio->controller.projectIdentity || !selected ||
            request.id != *selected || request.generation != catalogue.generation)
        {
            feedback.setText("Selection or project changed; choose the parameter again.",
                             juce::dontSendNotification);
            return;
        }
        const auto binding = selectedBinding();
        if (!binding)
        {
            feedback.setText("Parameter is no longer available for automation.", juce::dontSendNotification);
            return;
        }
        const auto result = studio->applyNativeAutomation(pluginId, *binding, request.action);
        feedback.setText(result.wasOk() ? "Automation updated" : result.getErrorMessage(),
                         juce::dontSendNotification);
    }
    const PluginParameter *parameter() const
    {
        if (!selected)
            return nullptr;
        const auto it = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                     [&](const auto &p) { return p.id == *selected; });
        return it == catalogue.parameters.end() ? nullptr : &*it;
    }
    static juce::String displayName(const PluginParameter &p)
    {
        return (p.name.isNotEmpty() ? p.name : "Parameter " + juce::String(juce::int64(p.id))) +
               (p.writable()            ? ""
                : (p.availability & 1u) ? " (read-only)"
                                        : " (unavailable)");
    }
    void selectionChanged()
    {
        const int i = choices.getSelectedId() - 1;
        selected = i >= 0 && i < int(visibleIDs.size()) ? std::optional(visibleIDs[size_t(i)]) : std::nullopt;
        const auto *p = parameter();
        value.setEnabled(p && p->writable());
        reset.setEnabled(p && p->writable() && p->hasDefault());
        if (!p)
        {
            details.setText("No available parameter", juce::dontSendNotification);
            return;
        }
        const auto name = displayName(*p);
        if (choices.getItemText(i) != name)
            choices.changeItemText(i + 1, name);
        reset.setTooltip(p->hasDefault() ? "Reset to the plugin's default value"
                                         : "The plugin does not provide a valid default value");
        value.setRange(0, 1, p->steps > 0 ? 1. / p->steps : .0001);
        value.setValue(p->value, juce::dontSendNotification);
        details.setText(
            (p->label.isEmpty() ? juce::String{} : p->label + " | ") +
                (p->writable()            ? "Normalized value 0 to 1"
                 : (p->availability & 1u) ? "Read-only"
                                          : "Value unavailable") +
                (p->steps > 0 ? " | " + juce::String(juce::int64(p->steps) + 1) + " choices" : ""),
            juce::dontSendNotification);
    }
    void queueValue(float requested)
    {
        if (studio && (studio->mixerChainApplying() || studio->devicePreparationBusy() ||
                       studio->routingCoordinator.mutationBlocked()))
        {
            feedback.setText("Native preparation owns controls; retained value unchanged",
                             juce::dontSendNotification);
            return;
        }
        if (!std::isfinite(requested) || requested < 0 || requested > 1)
            return;
        const auto *p = parameter();
        if (!p || !p->writable() || !studio || studio->audio.parameterHost(pluginId) != host ||
            !host->alive())
            return;
        juce::String operation = scalarOperation;
        const auto result = studio->requestNativeScalar(pluginId, p->id, catalogue.generation, requested,
                                                       host->runtimeIdentity(), scalarDragging, operation);
        if (result.failed())
        { feedback.setText(result.getErrorMessage(), juce::dontSendNotification); return; }
        scalarOperation = std::move(operation);
        automationRequest.reset(); // A later explicit manual edit supersedes pending clip creation.
        feedback.setText("Updating...", juce::dontSendNotification);
    }
    void timerCallback() override
    {
        if (!studio || studio->controller.projectIdentity != projectIdentity ||
            studio->audio.parameterHost(pluginId) != host || !host->alive())
        {
            if (host && inFlight)
                host->abandonParameterRequest(inFlight);
            inFlight = 0;
            queued.reset();
            metadataAvailable = false;
            automationRequest.reset();
            choices.setEnabled(false);
            value.setEnabled(false);
            reset.setEnabled(false);
            refresh.setEnabled(false);
            automate.setEnabled(false);
            initialize.setEnabled(false);
            removeInitialization.setEnabled(false);
            lastTweaked.setEnabled(false);
            notifications.setEnabled(false);
            const bool failed = studio && studio->controller.projectIdentity == projectIdentity &&
                                studio->controller.project.plugin(pluginId) && host &&
                                studio->audio.parameterHost(pluginId) == host && !host->alive();
            feedback.setText(failed ? (host->error().isNotEmpty()
                                           ? host->error()
                                           : "Plugin worker unavailable; saved state retained")
                                    : "Plugin instance changed or stopped. Reopen controls.",
                             juce::dontSendNotification);
            recover.setVisible(failed);
            recover.setEnabled(failed);
            feedback.setTooltip(feedback.getText());
            stopTimer();
            host.reset(); // Release the UI's worker reference on the control thread.
            return;
        }
        studio->pollNativeScalarOperations();
        if (scalarOperation.isNotEmpty())
            for (const auto &job : studio->pluginOperations)
                if (job.id == scalarOperation && job.state != "pending")
                {
                    if (job.nativeApplied)
                    {
                        for (auto &p : catalogue.parameters)
                            if (p.id == job.parameter)
                                p.value = job.scalarAfter;
                        if (selected && *selected == job.parameter)
                            value.setValue(job.scalarAfter, juce::dontSendNotification);
                    }
                    feedback.setText(job.state == "completed" && job.nativeApplied ? "Parameter updated"
                                                                                   : job.message,
                                     juce::dontSendNotification);
                    feedback.setTooltip(job.message);
                    scalarOperation.clear();
                    minimumRefresh = studio->refreshPluginCatalogue(pluginId, host->runtimeIdentity());
                    break;
                }
        PluginParameterCatalogue next;
        next.serial = catalogue.serial;
        next.generation = catalogue.generation;
        if (studio->readPluginCatalogue(pluginId, host->runtimeIdentity(), next) &&
            next.serial >= minimumRefresh)
        {
            metadataError = next.error;
            if (next.error)
            {
                metadataAvailable = false;
                automationRequest.reset();
                value.setEnabled(false);
                reset.setEnabled(false);
                choices.setEnabled(false);
                queued.reset();
                feedback.setText("Plugin parameter information unavailable", juce::dontSendNotification);
            }
            else
            {
                metadataAvailable = true;
                const bool newSchema = catalogue.generation != next.generation;
                catalogue = std::move(next);
                if (newSchema || visibleIDs.empty())
                {
                    if (newSchema)
                        queued.reset();
                    choices.clear(juce::dontSendNotification);
                    visibleIDs.clear();
                    int choice = 0;
                    for (const auto &p : catalogue.parameters)
                    {
                        if ((p.flags & (1u << 4)) != 0)
                            continue;
                        visibleIDs.push_back(p.id);
                        choices.addItem(
                            (p.name.isNotEmpty() ? p.name : "Parameter " + juce::String(juce::int64(p.id))) +
                                (p.writable()            ? ""
                                 : (p.availability & 1u) ? " (read-only)"
                                                         : " (unavailable)"),
                            int(visibleIDs.size()));
                        if (selected && p.id == *selected)
                            choice = int(visibleIDs.size());
                    }
                    choices.setEnabled(!visibleIDs.empty());
                    choices.setSelectedId(choice               ? choice
                                          : visibleIDs.empty() ? 0
                                                               : 1,
                                          juce::dontSendNotification);
                    selectionChanged();
                }
                else if (!pending() && !value.isMouseButtonDown())
                    selectionChanged();
            }
        }
        finishAutomationRequest();
        const auto *p = parameter();
        const bool canAutomate = metadataAvailable && automatable(p) && !pending();
        automate.setEnabled(canAutomate);
        initialize.setEnabled(canAutomate);
        const AutomationKey key{AutomationParameter::pluginNormalized, pluginId, p ? p->id : 0};
        removeInitialization.setEnabled(
            canAutomate && std::any_of(studio->controller.project.automation.initialValues.begin(),
                                       studio->controller.project.automation.initialValues.end(),
                                       [&](const auto &entry) { return entry.key == key; }));
        const auto gesture = host->lastParameterGesture();
        const auto drops = host->droppedParameterGestures();
        if (drops != observedGestureDrops)
        {
            observedGestureDrops = drops;
            feedback.setText("Native edit notifications dropped; finish a fresh gesture.",
                             juce::dontSendNotification);
        }
        lastTweaked.setEnabled(
            metadataAvailable && !pending() && gesture && gesture->generation == catalogue.generation &&
            std::find(visibleIDs.begin(), visibleIDs.end(), gesture->id) != visibleIDs.end());
        notifications.setToggleState(host->parameterNotificationsEnabled(), juce::dontSendNotification);
        if (++ticks % 15 == 0)
            studio->refreshPluginCatalogue(pluginId, host->runtimeIdentity());
    }

  public:
    juce::ComboBox choices;
    juce::Slider value;
    juce::TextButton automate{"Create automation clip"};
    PluginControls(Studio &owner, Id id, std::shared_ptr<PluginHost> worker, const juce::String &name)
        : studio(&owner), pluginId(id), projectIdentity(owner.controller.projectIdentity),
          host(std::move(worker))
    {
        heading.setText(name, juce::dontSendNotification);
        heading.setFont(juce::Font(juce::FontOptions(20)));
        feedback.setText("Loading plugin controls...", juce::dontSendNotification);
        choices.setName("Plugin parameter");
        value.setName("Parameter value");
        value.setSliderStyle(juce::Slider::LinearHorizontal);
        value.setTextBoxStyle(juce::Slider::TextBoxRight, false, 95, 24);
        value.setRange(0, 1, .0001);
        value.setEnabled(false);
        reset.setEnabled(false);
        automate.setEnabled(false);
        initialize.setEnabled(false);
        removeInitialization.setEnabled(false);
        automate.setTooltip("Create a linked native-parameter curve on a free Playlist lane");
        initialize.setTooltip("Initialize this native parameter at song start");
        removeInitialization.setTooltip(
            "Remove this parameter's initialized song value; keep its curve and manual fallback");
        automate.onClick = [this] { requestAutomation("automation.plugin.create"); };
        initialize.onClick = [this] { requestAutomation("automation.plugin.init"); };
        removeInitialization.onClick = [this] { requestAutomation("automation.plugin.init.remove"); };
        choices.onChange = [this] { selectionChanged(); };
        value.onDragStart = [this] { scalarDragging = true; };
        value.onDragEnd = [this]
        {
            scalarDragging = false;
            if (studio) studio->finishNativeScalarGesture(scalarOperation);
        };
        value.onValueChange = [this] { queueValue(float(value.getValue())); };
        reset.onClick = [this]
        {
            if (const auto *p = parameter())
            {
                value.setValue(p->defaultValue, juce::dontSendNotification);
                queueValue(p->defaultValue);
            }
        };
        refresh.onClick = [this]
        { minimumRefresh = studio->refreshPluginCatalogue(pluginId, host->runtimeIdentity()); };
        recover.setTooltip(
            "Reload the last captured/startup state. Uncaptured edits may be lost; voices restart.");
        recover.onClick = [this]
        {
            if (studio)
            {
                if (studio->controller.projectIdentity != projectIdentity)
                {
                    feedback.setText("Project changed; reopen controls.", juce::dontSendNotification);
                    recover.setEnabled(false);
                    return;
                }
                const auto result = studio->recoverPlugin(pluginId, true);
                feedback.setText(result.wasOk() ? "Preparing replacement plugin..."
                                                : result.getErrorMessage(),
                                 juce::dontSendNotification);
            }
        };
        lastTweaked.setTooltip(
            "Select the last completed native-editor gesture; playback values are excluded");
        lastTweaked.onClick = [this]
        {
            const auto gesture = host->lastParameterGesture();
            if (!gesture || gesture->generation != catalogue.generation || !selectNative(gesture->id))
                return;
            minimumRefresh = studio->refreshPluginCatalogue(pluginId, host->runtimeIdentity());
            feedback.setText("Last tweaked parameter selected", juce::dontSendNotification);
        };
        notifications.setToggleState(host->parameterNotificationsEnabled(), juce::dontSendNotification);
        notifications.setTooltip(
            "This worker session only; automation playback and saved plugin values remain active");
        notifications.onClick = [this]
        {
            const auto result = studio->setPluginNotifications(pluginId, host->runtimeIdentity(),
                                                               notifications.getToggleState());
            if (result.failed())
            {
                feedback.setText(result.getErrorMessage(), juce::dontSendNotification);
                return;
            }
            feedback.setText(notifications.getToggleState() ? "Native edit notifications enabled"
                                                            : "Native edit notifications disabled",
                             juce::dontSendNotification);
        };
        for (auto *component : std::array<juce::Component *, 13>{
                 &heading, &details, &feedback, &choices, &value, &reset, &refresh, &automate, &initialize,
                 &removeInitialization, &lastTweaked, &notifications, &recover})
            addAndMakeVisible(component);
        recover.setVisible(false);
        setSize(600, 340);
        minimumRefresh = studio->refreshPluginCatalogue(pluginId, host->runtimeIdentity());
        startTimerHz(30);
    }
    ~PluginControls() override
    {
        stopTimer();
        if (studio) studio->finishNativeScalarGesture(scalarOperation);
        if (host && inFlight)
            host->abandonParameterRequest(inFlight);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        heading.setBounds(area.removeFromTop(32));
        area.removeFromTop(10);
        choices.setBounds(area.removeFromTop(30));
        area.removeFromTop(8);
        details.setBounds(area.removeFromTop(24));
        auto row = area.removeFromTop(36);
        reset.setBounds(row.removeFromRight(80));
        row.removeFromRight(10);
        value.setBounds(row);
        area.removeFromTop(10);
        auto bottom = area.removeFromTop(30);
        refresh.setBounds(bottom.removeFromRight(80));
        automate.setBounds(bottom.removeFromLeft(180));
        bottom.removeFromLeft(8);
        initialize.setBounds(bottom.removeFromLeft(104));
        bottom.removeFromLeft(8);
        removeInitialization.setBounds(bottom.removeFromLeft(110));
        area.removeFromTop(8);
        auto native = area.removeFromTop(30);
        lastTweaked.setBounds(native.removeFromLeft(180));
        native.removeFromLeft(8);
        notifications.setBounds(native);
        area.removeFromTop(8);
        feedback.setBounds(area.removeFromTop(24));
        area.removeFromTop(4);
        recover.setBounds(area.removeFromTop(28).removeFromLeft(180));
    }
    bool selectNative(std::uint32_t id)
    {
        const auto it = std::find(visibleIDs.begin(), visibleIDs.end(), id);
        if (it == visibleIDs.end())
            return false;
        choices.setSelectedId(int(it - visibleIDs.begin()) + 1, juce::dontSendNotification);
        selectionChanged();
        return true;
    }
    unsigned generation() const
    {
        return catalogue.generation;
    }
    bool pending() const
    {
        return inFlight != 0 || queued.has_value() || automationRequest.has_value() ||
               (studio && studio->nativeScalarPending(scalarOperation));
    }
    void reportPendingClose()
    {
        feedback.setText("Wait for the pending Controls edit before closing or switching inspectors.",
                         juce::dontSendNotification);
    }
    std::optional<PluginBinding> selectedBinding() const
    {
        const auto *p = parameter();
        if (!metadataAvailable || !p || !p->automatable() || inFlight != 0 || queued || !studio || !host ||
            !studio->controller.project.plugin(pluginId) || studio->audio.parameterHost(pluginId) != host)
            return {};
        PluginBinding binding{p->id, p->flags, p->steps, p->value, p->name, p->label};
        const auto admission = host->validateAutomationBindings({&binding, 1});
        return admission.status == PluginBindingStatus::ready && admission.generation == catalogue.generation
                   ? std::optional(binding)
                   : std::nullopt;
    }
    juce::var snapshot() const
    {
        auto *data = new juce::DynamicObject;
        data->setProperty("plugin", juce::int64(pluginId));
        data->setProperty("workerPid", juce::int64(host ? host->processId() : 0));
        data->setProperty("instance", host ? host->runtimeIdentity() : juce::String{});
        data->setProperty("visible", isShowing());
        data->setProperty("ready", metadataAvailable && metadataError == 0 && host && host->alive() &&
                                       catalogue.generation > 0 && catalogue.serial >= minimumRefresh &&
                                       !pending());
        data->setProperty("generation", juce::int64(catalogue.generation));
        data->setProperty("writable", value.isEnabled());
        if (selected)
            data->setProperty("parameter", juce::int64(*selected));
        data->setProperty("value", value.getValue());
        if (const auto *p = parameter(); p && (p->availability & 1u))
            data->setProperty("controllerValue", p->value);
        data->setProperty("scalarOperation", scalarOperation);
        data->setProperty("inFlight", juce::int64(studio && studio->nativeScalar &&
            studio->nativeScalar->operation == scalarOperation ? studio->nativeScalar->inFlight : inFlight));
        data->setProperty("queued", queued.has_value() || (studio && studio->nativeScalar &&
            studio->nativeScalar->operation == scalarOperation && studio->nativeScalar->queued.has_value()));
        data->setProperty("feedback", feedback.getText());
        data->setProperty("recoveryEnabled", recover.isVisible() && recover.isEnabled());
        auto rectangle = [](juce::Rectangle<int> r)
        { return juce::var(juce::Array<juce::var>{r.getX(), r.getY(), r.getWidth(), r.getHeight()}); };
        data->setProperty("valueBounds", rectangle(value.getBounds()));
        data->setProperty("defaultBounds", rectangle(reset.getBounds()));
        data->setProperty("choiceBounds", rectangle(choices.getBounds()));
        data->setProperty("automationEnabled", automate.isEnabled());
        data->setProperty("automationBounds", rectangle(automate.getBounds()));
        data->setProperty("lastTweakedEnabled", lastTweaked.isEnabled());
        data->setProperty("lastTweakedBounds", rectangle(lastTweaked.getBounds()));
        data->setProperty("nativeNotifications", notifications.getToggleState());
        data->setProperty("nativeGestureDrops", juce::int64(host ? host->droppedParameterGestures() : 0));
        data->setProperty("metadataAvailable", metadataAvailable);
        data->setProperty("metadataError", metadataError);
        const auto gesture = host ? host->lastParameterGesture() : std::nullopt;
        data->setProperty("nativeGestureSerial", juce::int64(gesture ? gesture->serial : 0));
        if (gesture)
        {
            data->setProperty("nativeGestureParameter", juce::int64(gesture->id));
            data->setProperty("nativeGestureGeneration", juce::int64(gesture->generation));
            data->setProperty("nativeGestureValue", gesture->after);
            data->setProperty("nativeGestureCompletedTicks", juce::int64(gesture->completedTicks));
        }
        return juce::var(data);
    }
};
class PluginControlWindow final : public juce::DocumentWindow
{
  public:
    explicit PluginControlWindow(juce::Component *content)
        : juce::DocumentWindow("Plugin controls", juce::Colour(0xff202327), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, false);
        setResizeLimits(560, 340, 1000, 600);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    juce::Result closeChecked()
    {
        if (auto *controls = dynamic_cast<PluginControls *>(getContentComponent());
            controls && controls->pending())
        {
            controls->reportPendingClose();
            return juce::Result::fail("busy: Wait for the queued or unacknowledged Controls edit");
        }
        setVisible(false);
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<PluginControlWindow>(this)]
            {
                if (safe)
                    delete safe.getComponent();
            });
        return juce::Result::ok();
    }
    void closeButtonPressed() override
    {
        (void)closeChecked(); // Same checked operation used by semantic closeControls.
    }
};
juce::Result Studio::recoverPlugin(Id id, bool reopenControls)
{
    if (mixerChainApplying())
        return juce::Result::fail("effectChain.busy: Finish chain Apply before plugin recovery");
    if (devicePreparationBusy())
        return juce::Result::fail("device.busy: Finish/cancel native preparation before recovery");
    if (routingCoordinator.mutationBlocked())
        return juce::Result::fail("routing.busy: Wait for the current audio transaction before recovery");
    // Keep the captured take before requesting structural worker recovery.
    // A refused take must not start recovery or replace its original target.
    if (auto take = finishMidiTake(); take.failed())
    {
        status = take.getErrorMessage();
        return take;
    }
    auto result = audio.requestPluginRecovery(controller.project, id);
    if (result.failed())
    {
        status = result.getErrorMessage();
        return result;
    }
    pluginRecovery = PluginRecovery{id, controller.projectIdentity, reopenControls};
    needsRebuild = true;
    status = "Recovering plugin from last captured state; uncaptured changes may be lost";
    repaint();
    return juce::Result::ok();
}
juce::Result Studio::loadEffectImpulse(Id id, const juce::File &file, std::function<void(juce::Result)> done)
{
    const auto *spec = controller.project.plugin(id);
    juce::PluginDescription description;
    const auto descriptor = spec ? juce::XmlDocument::parse(spec->descriptor) : nullptr;
    if (!spec || spec->instrument || !descriptor || !description.loadFromXml(*descriptor) ||
        effects::findNativeDescription(description) != effects::EffectId::convolution)
        return juce::Result::fail("effectImpulse.invalidTarget: Choose a Convolution Reverb effect");
    if (!file.existsAsFile() || !file.hasFileExtension("wav;aif;aiff;flac"))
        return juce::Result::fail("effectImpulse.invalidFile: An existing WAV, AIFF or FLAC file is required");
    if (mixerChainApplying() || devicePreparationBusy() || routingCoordinator.mutationBlocked())
        return juce::Result::fail("effectImpulse.busy: Wait for the current audio transaction");
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto identity = controller.projectIdentity;
    withPluginState(
        [safe, id, file, report = std::move(done), identity](juce::Result captured, Project snapshot)
        {
            if (!safe)
                return;
            const auto done = [&](juce::Result result)
            {
                // A refusal replaces the "capturing" status with its reason.
                if (result.failed())
                {
                    safe->status = result.getErrorMessage();
                    safe->repaint();
                }
                report(result);
            };
            if (captured.wasOk() && safe->controller.projectIdentity != identity)
                captured = juce::Result::fail("stale_project: The project changed while capturing");
            if (captured.failed())
                return done(captured);
            // Every plugin keeps its freshly captured state (as Save does) before the restart.
            for (auto &channel : safe->controller.project.channels)
                if (channel.instrument)
                    if (const auto *fresh = snapshot.plugin(channel.instrument->id))
                        channel.instrument->state = fresh->state;
            for (auto &insert : safe->controller.project.mixer)
                for (auto &effect : insert.effects)
                    if (const auto *fresh = snapshot.plugin(effect.id))
                        effect.state = fresh->state;
            auto *target = safe->controller.project.plugin(id);
            juce::MemoryBlock block;
            const auto xml = target && block.fromBase64Encoding(target->state)
                                 ? juce::AudioProcessor::getXmlFromBinary(block.getData(), int(block.getSize()))
                                 : nullptr;
            if (!xml || !xml->hasTagName("VIBERA_NATIVE_EFFECT"))
                return done(juce::Result::fail("effectImpulse.noState: The effect has no captured state yet"));
            xml->setAttribute("impulse", file.getFullPathName());
            juce::MemoryBlock next;
            juce::AudioProcessor::copyXmlToBinary(*xml, next);
            const auto previous = target->state;
            target->state = next.toBase64Encoding();
            if (auto reload = safe->audio.requestPluginStateReload(safe->controller.project, id); reload.failed())
            {
                target->state = previous; // Nothing changes unless the effect can restart with it.
                return done(juce::Result::fail("effectImpulse.notPrepared: " + reload.getErrorMessage() +
                                               " (needs an active audio device projection)"));
            }
            safe->controller.dirty = true;
            safe->needsRebuild = true;
            safe->status = "Loading impulse " + file.getFileName();
            safe->repaint();
            done(juce::Result::ok());
        });
    return juce::Result::ok();
}
void Studio::openPluginControlsUI(Id id)
{
    const auto host = audio.preparedPluginHost(controller.project, id);
    juce::String operation, code;
    const auto result = requestPluginEditor(id, true, host ? host->runtimeIdentity() : juce::String{},
                                            operation, code, PluginWrapperView::parameters);
    status = result.wasOk() ? "Opening plugin wrapper Parameters; waiting for actual window result"
                            : code + ": " + result.getErrorMessage();
    repaint();
}
void Studio::showPluginParameters(Id id)
{
    auto host = audio.parameterHost(id);
    if (!host || !host->alive())
    {
        status = audio.pluginStatus(id);
        repaint();
        return;
    }
    juce::String name = "Plugin controls";
    for (const auto &channel : controller.project.channels)
        if (channel.instrument && channel.instrument->id == id)
            name = channel.instrument->name;
    for (const auto &insert : controller.project.mixer)
        for (const auto &effect : insert.effects)
            if (effect.id == id)
                name = effect.name;
    // A hidden closing window may already have queued its asynchronous deletion.
    // Reuse only a still-open inspector; its queued close must not erase a reopen.
    if (parameterWindow && parameterWindow->isVisible())
        if (auto *controls = dynamic_cast<PluginControls *>(parameterWindow->getContentComponent());
            controls && controls->pluginId == id && controls->host == host)
        {
            parameterWindow->setVisible(true);
            parameterWindow->toFront(true);
            return;
        }
    if (pluginControlsPending())
    {
        status = "busy: Finish the pending Controls edit before switching inspectors";
        repaint();
        return;
    }
    if (parameterWindow)
        delete parameterWindow.getComponent();
    parameterWindow = new PluginControlWindow(new PluginControls(*this, id, std::move(host), name));
    parameterWindow->setCentrePosition(getScreenBounds().getCentre());
    parameterWindow->setLookAndFeel(&getLookAndFeel());
    roundNativeWindow(*parameterWindow);
    parameterWindow->toFront(true);
}
juce::Result Studio::selectPluginParameter(Id id, std::optional<std::uint32_t> parameter, bool lastTweaked)
{
    if (!controller.project.plugin(id))
        return juce::Result::fail("Unknown plugin in this project");
    const auto host = audio.parameterHost(id);
    if (!host || !host->alive())
        return juce::Result::fail("Plugin worker is not ready");
    showPluginParameters(id);
    auto *window = parameterWindow.getComponent();
    auto *controls = window ? dynamic_cast<PluginControls *>(window->getContentComponent()) : nullptr;
    if (!controls || controls->pluginId != id || controls->host != host)
        return juce::Result::fail("Plugin inspector changed");
    if (!parameter && !lastTweaked)
        return juce::Result::ok();
    if (!controls->metadataAvailable || controls->pending() || !controls->generation())
        return juce::Result::fail("Plugin parameter catalogue is not ready; poll state and retry");
    if (lastTweaked)
    {
        const auto gesture = host->lastParameterGesture();
        if (!gesture || gesture->generation != controls->generation())
            return juce::Result::fail("No completed native gesture for this worker/schema");
        parameter = gesture->id;
    }
    if (!parameter || !controls->selectNative(*parameter))
        return juce::Result::fail("Native parameter is not visible in this inspector");
    controls->minimumRefresh = host->requestParameterCatalogue();
    return juce::Result::ok();
}
bool Studio::pluginControlsPending() const
{
    if (parameterWindow)
        if (const auto *controls =
                dynamic_cast<const PluginControls *>(parameterWindow->getContentComponent()))
            return controls->pending();
    return false;
}
juce::Result Studio::closePluginParameters(Id id, const juce::String &instance)
{
    auto *window = dynamic_cast<PluginControlWindow *>(parameterWindow.getComponent());
    if (!window)
        return juce::Result::ok();
    const auto *controls = dynamic_cast<const PluginControls *>(window->getContentComponent());
    if (!controls || controls->pluginId != id || !controls->host ||
        controls->host->runtimeIdentity() != instance)
        return juce::Result::ok(); // Idempotent close must never close another target.
    return window->closeChecked();
}
juce::var Studio::pluginControlSnapshot() const
{
    if (parameterWindow)
        if (const auto *controls =
                dynamic_cast<const PluginControls *>(parameterWindow->getContentComponent()))
            return controls->snapshot();
    return {};
}
juce::Result Studio::verifyPluginParameters(const juce::File &folder, const juce::File &binary, bool wrapperOnly,
                                           bool controlsOnly)
{
#if !JUCE_WINDOWS
    juce::ignoreUnused(folder, binary, wrapperOnly, controlsOnly);
    return juce::Result::fail("Native plugin controls acceptance requires Windows");
#else

    stopTimer();
    auto fail = [](const juce::String &message) { return juce::Result::fail(message); };
    if (folder.createDirectory().failed())
        return fail("Plugin controls evidence directory");
    if (wrapperOnly)
        SetEnvironmentVariableW(L"VIBERA_WRAPPER_SOFTWARE_RENDERER", L"1"); // Owned test process only.
    if (controlsOnly)
        SetEnvironmentVariableW(L"VIBERA_FIXTURE_EDITOR_UNSUPPORTED", L"1"); // Original fixture capability, not a mock editor.
    std::vector<juce::PluginDescription> types;
    if (auto scan = PluginCatalog::scanOne(binary, types); scan.failed() || types.empty())
        return fail("Parameter GUI isolated fixture scan");
    auto project = Project::empty();
    project.channels.resize(1);
    project.channels[0].source = "builtin:bass";
    project.channels[0].mixer = 1;
    const Id pluginId = project.allocate();
    project.mixer[1].effects.push_back(
        {pluginId, types.front().name, types.front().createXml()->toString(), {}, false, false});
    project.patterns[0].notes.push_back({project.allocate(), project.channels[0].id, 0, 4, 48, .8f, false});
    project.placements.push_back({project.allocate(), project.patterns[0].id, 0, 0, 4, 0});
    controller.replace(project);
    selectedPattern = project.patterns[0].id;
    selectedChannel = project.channels[0].id;
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device)
        return fail("Plugin controls virtual device");
    juce::AudioBuffer<float> block(2, 256);
    juce::String report;
    for (const bool pattern : {true, false})
    {
        patternMode = pattern;
        const auto deadline = juce::Time::getMillisecondCounter() + 30000;
        juce::Result ready = juce::Result::fail("not prepared");
        do
        {
            ready = audio.rebuild(controller.project, pattern, selectedPattern);
            if (AudioEngine::preparationPending(ready))
                dispatchMessages(5);
        } while (AudioEngine::preparationPending(ready) && juce::Time::getMillisecondCounter() < deadline);
        if (ready.failed())
            return ready;
        needsRebuild = false;
        audio.seekBeat(0);
        audio.playStop();
        dispatchMessages(30);
        for (int n = 0; n < 16; ++n)
        {
            device->process(block);
            dispatchMessages(6);
        }
        const auto host = audio.parameterHost(pluginId);
        if (!host || !host->alive())
            return fail("Prepared fixture worker");
        if (wrapperOnly)
        {
#include "../tests/PluginWrapperWorkflowAcceptance.inc"
            continue;
        }
        showPluginParameters(pluginId);
        auto *controls = parameterWindow
                             ? dynamic_cast<PluginControls *>(parameterWindow->getContentComponent())
                             : nullptr;
        if (!controls)
            return fail("Real modeless plugin controls window");
        for (int n = 0; n < 200 && !controls->generation(); ++n)
            dispatchMessages(5);
        if (!controls->generation() || !controls->selectNative(0) || !controls->value.isEnabled())
            return fail("Native zero-ID controls selection");
        if (!controls->selectNative(99) || controls->value.isEnabled() || controls->selectNative(777))
            return fail("Read-only disabled/hidden excluded UI");
        if (!controls->selectNative(4000001234u) || controls->value.isEnabled())
            return fail("Unavailable native sentinel disabled in UI");
        if (!controls->selectNative(0xfedcba98u) || !controls->value.isEnabled())
            return fail("Grouped sparse-ID controls selection");
        controls->value.setValue(.05, juce::sendNotificationSync);
        const auto scalarDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (pluginControlsPending() && juce::Time::getMillisecondCounter() < scalarDeadline)
        {
            device->process(block);
            dispatchMessages(5);
            timerCallback(); // Same actual shared ACK/capture/history service as the running app.
        }
        if (pluginControlsPending())
            return fail("Initial manual scalar operation did not reach its actual terminal outcome");
        if (!controls->selectNative(0))
            return fail("Gain UI reselection");
        const auto before = audio.projectionDiagnostics();
        const auto workerPid = host->processId();
        const auto graphInitialisations = audio.mixerGraphInitialisations();
        const int starts = device->starts, stops = device->stops;
        record.onClick();
        if (!midiTaking || !audio.midiInput.capture.active.load())
            return fail("Plugin controls actual MIDI take arming");
        const auto session = audio.midiInput.capture.session.load();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 64, juce::uint8(100)));
        int nativeCallbacks = 0;
        double nativeMaximumMs = 0;
        const auto nativeCallback = [&]
        {
            const auto begin = juce::Time::getHighResolutionTicks();
            device->process(block);
            nativeMaximumMs = std::max(nativeMaximumMs, juce::Time::highResolutionTicksToSeconds(
                                                            juce::Time::getHighResolutionTicks() - begin) *
                                                            1000.);
            ++nativeCallbacks;
        };
        const auto previousGesture = host->lastParameterGesture();
        // Exercise the same admission and retained completion as UI/MCP.
        // The legacy void helper can discard a busy refusal while metadata or
        // a parameter write owns the worker. A missing window then says nothing
        // about whether an editor request was ever admitted.
        juce::String editorOperation, editorCode, editorMessage;
        int editorBusyRefusals = 0;
        HWND nativeEditor = nullptr;
        for (int i = 0; i < 200 && !nativeEditor; ++i)
        {
            if (editorOperation.isEmpty())
            {
                const auto admitted =
                    requestPluginEditor(pluginId, true, host->runtimeIdentity(), editorOperation, editorCode);
                editorMessage = admitted.getErrorMessage();
                if (admitted.failed() && editorCode != "busy")
                    break;
                if (admitted.failed())
                    ++editorBusyRefusals;
            }
            nativeCallback();
            dispatchMessages(5);
            pollPluginOperations();
            if (editorOperation.isNotEmpty())
            {
                const auto job =
                    std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                 [&](const auto &operation) { return operation.id == editorOperation; });
                if (job == pluginOperations.end())
                {
                    editorCode = "missing_operation";
                    break;
                }
                editorCode = job->code;
                editorMessage = job->message;
                if (job->state == "completed" && job->editorResult && job->editorResult->succeeded() &&
                    job->editorResult->window.known && job->editorResult->window.exists)
                    nativeEditor = test::gestureFixtureWindow(host->processId());
                else if (job->state != "pending")
                    break;
            }
        }
        const auto editorReadback = pluginEditorState(pluginId);
        auto editorEvidence = std::make_unique<juce::DynamicObject>();
        editorEvidence->setProperty("operation", editorOperation);
        editorEvidence->setProperty("busyRefusals", editorBusyRefusals);
        editorEvidence->setProperty("code", editorCode);
        editorEvidence->setProperty("message", editorMessage);
        editorEvidence->setProperty("window", editorReadback);
        editorEvidence->setProperty("originalChildFound", nativeEditor != nullptr);
        editorEvidence->setProperty("callbackCount", nativeCallbacks);
        editorEvidence->setProperty("nativeGlobalInput", false);
        SetLastError(ERROR_SUCCESS);
        const bool clicked = nativeEditor && test::clickGestureFixture(nativeEditor, host->processId(), 1);
        editorEvidence->setProperty("windowMessageSucceeded", clicked);
        editorEvidence->setProperty("windowMessageError", int(GetLastError()));
        if (!folder.getChildFile(pattern ? "pattern-editor-operation.json" : "song-editor-operation.json")
                 .replaceWithText(juce::JSON::toString(juce::var(editorEvidence.release()), true)))
            return fail("Retain actual native editor admission/completion/owned-message evidence");
        if (!clicked)
            return fail("Owned original native editor gesture during playback: " + editorCode + " " +
                        editorMessage);
        bool freshNativeGesture = false;
        for (int i = 0; i < 200; ++i)
        {
            nativeCallback();
            dispatchMessages(5);
            const auto current = host->lastParameterGesture();
            freshNativeGesture = current && (!previousGesture || current->serial > previousGesture->serial);
            if (freshNativeGesture && controls->lastTweaked.isEnabled())
                break;
        }
        if (!freshNativeGesture || !controls->lastTweaked.isEnabled() || nativeMaximumMs >= 256000. / 48000.)
            return fail("Native editor gesture reaches actual Last tweaked UI");
        const auto globalTarget = lastPluginTweak();
        auto globalMenu = lastPluginTweakMenu(globalTarget);
        bool hasGlobalCreate = false;
        for (juce::PopupMenu::MenuItemIterator item(globalMenu); item.next();)
            hasGlobalCreate |= item.getItem().itemID == 301 && item.getItem().isEnabled;
        if (!globalTarget || globalTarget->plugin != pluginId || globalTarget->gesture.id != 0xfedcba98u ||
            !globalTarget->gesture.completedTicks || !hasGlobalCreate)
            return fail("Tools Last tweaked names the actual native sparse-ID target");
        const auto globalBaseline = controller.project.state();
        const auto revisionBeforeGlobal = controller.revision;
        auto staleGlobal = *globalTarget;
        staleGlobal.project = "another-project";
        juce::String globalOperation, globalCode;
        const auto operationCount = pluginOperations.size();
        if (requestLastPluginTweak(staleGlobal, "automation.plugin.create", globalOperation, globalCode)
                .wasOk() ||
            globalCode != "stale_context" || !globalOperation.isEmpty() ||
            pluginOperations.size() != operationCount || controller.revision != revisionBeforeGlobal)
            return fail("Captured Tools target must refuse another project");
        delete parameterWindow.getComponent();
        controls = nullptr;
        const auto runGlobal = [&](const juce::String &action)
        {
            auto args = juce::var(new juce::DynamicObject);
            args.getDynamicObject()->setProperty("command", action);
            if (action == "tools.lastTweaked.create")
            {
                if (!performAction(action) || pluginOperations.empty() ||
                    pluginOperations.back().kind != "automate" || pluginOperations.back().state != "pending")
                    return false;
                globalOperation = pluginOperations.back().id;
            }
            else
            {
                const auto accepted = mcpCall("vibera_command", args)["structuredContent"];
                globalOperation = accepted["operation"].toString();
                if (globalOperation.isEmpty() || accepted["completionState"].toString() != "pending" ||
                    accepted["pollTool"].toString() != "vibera_plugin")
                    return false;
            }
            const auto outcome = [&]() -> const PluginOperation *
            {
                const auto it = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                             [&](const auto &job) { return job.id == globalOperation; });
                return it == pluginOperations.end() ? nullptr : &*it;
            };
            for (int i = 0; i < 200 && outcome() && outcome()->state == "pending"; ++i)
            {
                nativeCallback();
                dispatchMessages(5);
                pollPluginOperations();
            }
            const auto *completed = outcome();
            if (!completed || completed->state != "completed" ||
                (completed->kind == "automate" && !completed->historyCommitted))
                return false;
            auto read = juce::var(new juce::DynamicObject);
            read.getDynamicObject()->setProperty("action", "job");
            read.getDynamicObject()->setProperty("operation", globalOperation);
            const auto observed = mcpCall("vibera_plugin", read)["structuredContent"];
            if (observed["completionState"].toString() != "completed" ||
                (completed->kind == "automate" &&
                 (!bool(observed["documentChanged"]) || !bool(observed["historyCommitted"]) ||
                  !bool(observed["undoable"]) ||
                  juce::int64(observed["sourceGestureSerial"]) != juce::int64(globalTarget->gesture.serial))))
                return false;
            for (int i = 0; i < 200; ++i)
            {
                const auto result = audio.rebuild(controller.project, pattern, selectedPattern);
                if (result.wasOk())
                    return true;
                if (!AudioEngine::preparationPending(result) &&
                    result.getErrorMessage() != "Audio edit queue busy; retrying")
                    return false;
                nativeCallback();
                dispatchMessages(5);
            }
            return false;
        };
        if (!runGlobal("tools.lastTweaked.create") || parameterWindow ||
            controller.project.automation.placements.size() != 1 ||
            controller.project.automation.sources.size() != 1)
            return fail("Tools Create automation clip works with its inspector closed");
        const auto globalCreated = controller.project.state();
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(globalBaseline) ||
            audio.rebuild(controller.project, pattern, selectedPattern).failed())
            return fail("Tools native clip exact Undo preserves playback");
        controller.redoEdit();
        if (!controller.project.state().isEquivalentTo(globalCreated) ||
            audio.rebuild(controller.project, pattern, selectedPattern).failed())
            return fail("Tools native clip exact Redo preserves playback");
        controller.undoEdit();
        if (!runGlobal("tools.lastTweaked.init") || controller.project.automation.initialValues.size() != 1 ||
            !runGlobal("tools.lastTweaked.removeInit") ||
            !controller.project.automation.initialValues.empty())
            return fail("Tools native initialized value and deletion");
        controller.undoEdit();
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(globalBaseline))
            return fail("Tools initialization Undo restores the original document");
        if (requestLastPluginTweak(*globalTarget, "automation.plugin.init", globalOperation, globalCode)
                .failed())
            return fail("Admit pending Tools action before notification disable");
        host->setParameterNotificationsEnabled(false);
        for (int i = 0; i < 200; ++i)
        {
            nativeCallback();
            dispatchMessages(5);
            pollPluginOperations();
            const auto it = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                         [&](const auto &job) { return job.id == globalOperation; });
            if (it != pluginOperations.end() && it->state != "pending")
                break;
        }
        const auto cancelledGlobal = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                                  [&](const auto &job) { return job.id == globalOperation; });
        if (cancelledGlobal == pluginOperations.end() || cancelledGlobal->state != "failed" ||
            cancelledGlobal->historyCommitted || lastPluginTweak() ||
            !controller.project.state().isEquivalentTo(globalBaseline))
            return fail("Disabled notifications cancel pending Tools action without a stale target");
        host->setParameterNotificationsEnabled(true);
        if (!test::clickGestureFixture(nativeEditor, host->processId(), 1))
            return fail("Fresh Tools gesture after notification re-enable");
        for (int i = 0; i < 200 && !lastPluginTweak(); ++i)
        {
            nativeCallback();
            dispatchMessages(5);
        }
        const auto reenabledTarget = lastPluginTweak();
        if (!reenabledTarget ||
            requestLastPluginTweak(*reenabledTarget, "automation.plugin.init", globalOperation, globalCode)
                .failed() ||
            !test::clickGestureFixture(nativeEditor, host->processId(), 0))
            return fail("Admit Tools action before a genuinely newer native parameter gesture");
        bool newerGesture = false;
        for (int i = 0; i < 200 && !newerGesture; ++i)
        {
            nativeCallback();
            dispatchMessages(5);
            const auto current = host->lastParameterGesture();
            newerGesture = current && current->serial > reenabledTarget->gesture.serial;
        }
        for (int i = 0; i < 200; ++i)
        {
            pollPluginOperations();
            const auto job = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                          [&](const auto &entry) { return entry.id == globalOperation; });
            if (job != pluginOperations.end() && job->state != "pending")
                break;
            nativeCallback();
            dispatchMessages(5);
        }
        const auto superseded = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                             [&](const auto &job) { return job.id == globalOperation; });
        if (!newerGesture || superseded == pluginOperations.end() || superseded->state != "failed" ||
            superseded->code != "stale_context" || superseded->historyCommitted ||
            !controller.project.state().isEquivalentTo(globalBaseline))
            return fail("Newer native gesture invalidates pending Tools automation without retargeting");
        const auto previousSerial = host->lastParameterGesture()->serial;
        if (!test::clickGestureFixture(nativeEditor, host->processId(), 1))
            return fail("Restore actual sparse-ID native gesture after superseded Tools action");
        for (int i = 0; i < 200; ++i)
        {
            nativeCallback();
            dispatchMessages(5);
            const auto current = host->lastParameterGesture();
            if (current && current->serial > previousSerial)
                break;
        }
        if (!lastPluginTweak() || !runGlobal("tools.lastTweaked.controls"))
            return fail("Tools Open control selects fresh native target asynchronously");
        const auto wrapper = pluginEditorState(pluginId);
        if (parameterWindow || wrapper["wrapperView"].toString() != "parameters" ||
            juce::int64(wrapper["wrapperParameter"]) != juce::int64(0xfedcba98u) || !midiTaking ||
            audio.midiInput.capture.session.load() != session ||
            audio.projectionDiagnostics().replacements != before.replacements || device->starts != starts ||
            device->stops != stops || host->processId() != workerPid)
            return fail("Tools actions preserve held MIDI, graph/device/worker and explicit selection");
        report += juce::String(pattern ? "Pattern" : "Song") +
                  ": Tools Last tweaked closed-inspector create, exact Undo/Redo, initialization/delete, "
                  "stale-project, disabled-epoch and superseded-gesture refusal, fresh native re-enable and "
                  "shared-wrapper Open control PASS; "
                  "held MIDI/graph/device/worker retained.\n";
        // Explicit legacy MCP Controls remains supported, but Tools opens the
        // shared worker wrapper instead of silently creating a second inspector.
        showPluginParameters(pluginId);
        controls = parameterWindow ? dynamic_cast<PluginControls *>(parameterWindow->getContentComponent())
                                   : nullptr;
        if (!controls)
            return fail("Retain explicit legacy inspector compatibility after wrapper navigation");
        for (int i = 0; i < 200 && !controls->metadataAvailable; ++i)
        {
            nativeCallback();
            dispatchMessages(5);
        }
        const auto *inspectorIdentity = parameterWindow.getComponent();
        auto request = juce::var(new juce::DynamicObject);
        request.getDynamicObject()->setProperty("plugin", juce::int64(pluginId));
        request.getDynamicObject()->setProperty("instance", host->runtimeIdentity());
        request.getDynamicObject()->setProperty("lastTweaked", true);
        try
        {
            mcpCall("vibera_select", request);
        }
        catch (const std::exception &error)
        {
            return fail("Loaded-plugin MCP Last tweaked: " + juce::String(error.what()));
        }
        if (parameterWindow.getComponent() != inspectorIdentity || !midiTaking ||
            !audio.midiInput.capture.active.load() ||
            controls->snapshot().getProperty("parameter", {}) != juce::var(juce::int64(0xfedcba98u)))
            return fail("MCP native selection must reuse inspector and preserve held MIDI");
        request.getDynamicObject()->setProperty("instance", "stale-worker");
        bool staleRefused = false;
        try
        {
            mcpCall("vibera_select", request);
        }
        catch (const std::exception &)
        {
            staleRefused = true;
        }
        if (!staleRefused || parameterWindow.getComponent() != inspectorIdentity)
            return fail("Stale loaded-plugin MCP identity must refuse without replacing inspector");
        controls->lastTweaked.onClick();
        if (controls->snapshot().getProperty("parameter", {}) != juce::var(juce::int64(0xfedcba98u)))
            return fail("Last tweaked selects actual sparse native ID");
        if (!controls->selectNative(0))
            return fail("Restore gain selection after native gesture proof");
        for (int i = 0; i < 200 && !(controls->metadataAvailable &&
                                     controls->catalogue.serial >= controls->minimumRefresh);
             ++i)
            dispatchMessages(5);
        const auto musicalBeforeMetadata = controller.project.state();
        const auto schemaBeforeMetadata = controls->generation();
        if (!controls->metadataAvailable || !test::toggleFixtureMetadata(nativeEditor, host->processId()))
            return fail("Original native fixture metadata failure trigger");
        for (int i = 0; i < 200 && controls->metadataError == 0; ++i)
            dispatchMessages(5);
        const auto badMetadata = controls->snapshot();
        folder.getChildFile(juce::String(pattern ? "pattern" : "song") + "-mcp-metadata-error.json")
            .replaceWithText(juce::JSON::toString(badMetadata, true));
        if (controls->metadataError == 0 || controls->metadataAvailable ||
            bool(badMetadata.getProperty("ready", false)))
            return fail("Invalid native metadata must not report a ready cached inspector: " +
                        juce::JSON::toString(badMetadata, true));
        auto selectionDuringFailure = juce::var(new juce::DynamicObject);
        selectionDuringFailure.getDynamicObject()->setProperty("plugin", juce::int64(pluginId));
        selectionDuringFailure.getDynamicObject()->setProperty("instance", host->runtimeIdentity());
        selectionDuringFailure.getDynamicObject()->setProperty("parameter", juce::int64(0));
        bool invalidMetadataRefused = false;
        try
        {
            mcpCall("vibera_select", selectionDuringFailure);
        }
        catch (const std::exception &)
        {
            invalidMetadataRefused = true;
        }
        if (!invalidMetadataRefused || parameterWindow.getComponent() != inspectorIdentity)
            return fail("MCP must refuse rejected metadata without replacing its inspector");
        if (!test::toggleFixtureMetadata(nativeEditor, host->processId()))
            return fail("Original native fixture metadata recovery trigger");
        for (int i = 0;
             i < 200 && (!controls->metadataAvailable || controls->generation() <= schemaBeforeMetadata); ++i)
            dispatchMessages(5);
        if (!controls->metadataAvailable || controls->metadataError != 0 ||
            !bool(controls->snapshot().getProperty("ready", false)) ||
            !controller.project.state().isEquivalentTo(musicalBeforeMetadata) ||
            parameterWindow.getComponent() != inspectorIdentity || !midiTaking ||
            audio.midiInput.capture.session.load() != session || host->processId() != workerPid ||
            device->starts != starts || device->stops != stops ||
            audio.projectionDiagnostics().replacements != before.replacements)
            return fail("Metadata recovery must restore readiness and retain musical/worker/device state");
        report += juce::String(pattern ? "Pattern" : "Song") +
                  ": actual SDK invalid metadata refuses cached ready state; recovery retains inspector, "
                  "document, held MIDI, worker and graph/device. Structural metadata case, not a callback "
                  "deadline test.\n";
        host->editor(false);
        report += juce::String(pattern ? "Pattern" : "Song") +
                  ": fresh actual SDK native editor gesture -> Last tweaked sparse-ID selection during held "
                  "MIDI; " +
                  juce::String(nativeCallbacks) +
                  " virtual callbacks, maximumMs=" + juce::String(nativeMaximumMs, 6) +
                  "; no callback deadline overrun.\n";
        double maximumMs = 0;
        int callbacks = 0;
        double maximumAcknowledgementMs = 0;
        int acknowledgementsBeyond70Ms = 0;
        // The fixture stops Studio's timer. Settle actual native editor
        // ownership before issuing manual writes, as the running app's
        // operation poll does; metadata availability alone is not that ACK.
        const auto nativeReadyAt = juce::Time::getMillisecondCounter();
        while (host->editorBusy() && juce::Time::getMillisecondCounter() - nativeReadyAt < 5000)
        {
            nativeCallback();
            dispatchMessages(5);
            pollPluginOperations();
        }
        if (host->editorBusy())
            return fail("Actual native editor ownership did not settle before recording controls");
        report += "Native editor ownership settled before Controls in " +
                  juce::String(juce::Time::getMillisecondCounter() - nativeReadyAt) + "ms.\n";
        for (int edit = 0; edit < 12; ++edit)
        {
            if (edit == 0 && pattern)
            {
                CommandArgs declaration;
                declaration.target = pluginId;
                declaration.binding = controls->selectedBinding();
                if (!declaration.binding)
                    return fail("Real native metadata available for saved binding");
                command("plugin.binding.declare", declaration);
                if (!controller.project.plugin(pluginId) ||
                    controller.project.plugin(pluginId)->automationBindings.size() != 1 || !midiTaking ||
                    audio.midiInput.capture.session.load() != session)
                    return fail("Native declaration command preserves held MIDI take");
                if (auto rebuilt = audio.rebuild(controller.project, pattern, selectedPattern);
                    rebuilt.failed())
                    return rebuilt;
                needsRebuild = false;
            }
            const double requested = .2 + edit * .03;
            const auto historyBeforeWrite = controller.undo.getUndoDescription();
            const auto redoBeforeWrite = controller.undo.getRedoDescription();
            const auto requestedAt = juce::Time::getMillisecondCounterHiRes();
            controls->value.setValue(requested, juce::sendNotificationSync);
            // UI and worker timers are asynchronous. Wait for the actual acknowledgement,
            // not a fixed two-timer sleep; this does not relax the measured DSP deadline.
            do
            {
                dispatchMessages(5);
            } while (controls->pending() && juce::Time::getMillisecondCounterHiRes() - requestedAt < 2000);
            const double acknowledgementMs = juce::Time::getMillisecondCounterHiRes() - requestedAt;
            maximumAcknowledgementMs = std::max(maximumAcknowledgementMs, acknowledgementMs);
            acknowledgementsBeyond70Ms += acknowledgementMs > 70 ? 1 : 0;
            if (controls->pending() || !controller.dirty ||
                std::abs(controls->value.getValue() - requested) > .0001 ||
                controls->snapshot().getProperty("feedback", {}).toString() != "Parameter updated")
                return fail("Controls acknowledgement failed at edit " + juce::String(edit) + ": " +
                            juce::JSON::toString(controls->snapshot(), true));
            auto *read = new juce::DynamicObject;
            read->setProperty("action", "job");
            read->setProperty("operation", lastNativeScalarOperation);
            const auto write = mcpCall("vibera_plugin", juce::var(read))["structuredContent"];
            if (write["completionState"].toString() != "completed" || !bool(write["nativeApplied"]) ||
                !bool(write["recordingWriteOnly"]) || bool(write["historyCommitted"]) ||
                write["completionScope"].toString() != "nativeScalarAckDuringRecording" ||
                controller.undo.getUndoDescription() != historyBeforeWrite ||
                controller.undo.getRedoDescription() != redoBeforeWrite || audio.pluginStateCapturePending())
                return fail("Recording scalar must acknowledge actual write without state capture/history");
            for (int tick = 0; tick < 8; ++tick)
            {
                const auto start = juce::Time::getHighResolutionTicks();
                device->process(block);
                const double elapsed = 1000 * juce::Time::highResolutionTicksToSeconds(
                                                  juce::Time::getHighResolutionTicks() - start);
                maximumMs = std::max(maximumMs, elapsed);
                ++callbacks;
                if (elapsed > 256000. / 48000)
                    return fail("Plugin controls virtual callback deadline exceeded: " +
                                juce::String(elapsed, 4) + " ms");
                dispatchMessages(6);
            }
            if (!midiTaking || audio.midiInput.capture.session.load() != session ||
                host->processId() != workerPid || audio.parameterHost(pluginId) != host || !audio.playing())
                return fail("Controls edit changed held take/worker/transport");
        }
        // The semantic operation uses the same live recording write service,
        // including bound gain in Pattern mode. Never claim capture/history.
        auto *set = new juce::DynamicObject;
        set->setProperty("action", "set");
        set->setProperty("plugin", juce::int64(pluginId));
        set->setProperty("instance", host->runtimeIdentity());
        set->setProperty("generation", juce::int64(controls->generation()));
        set->setProperty("parameter", juce::int64(0));
        set->setProperty("value", .53);
        const auto acceptedWrite = mcpCall("vibera_plugin", juce::var(set))["structuredContent"];
        const auto writeOperation = acceptedWrite["operation"].toString();
        if (writeOperation.isEmpty())
            return fail("MCP recording write requires actual pending receipt");
        const auto namedBeforeWrite = controller.project.state();
        CommandArgs renameDuringWrite;
        renameDuringWrite.mixer = 1;
        renameDuringWrite.text = "MIDI take native ACK overlap";
        if (controller.execute("mixer.rename", renameDuringWrite).failed())
            return fail("Ordinary naming must remain available during recording native ACK");
        const auto writeDeadline = juce::Time::getMillisecondCounter() + 2000;
        while (nativeScalarPending(writeOperation) && juce::Time::getMillisecondCounter() < writeDeadline)
        {
            nativeCallback();
            dispatchMessages(5);
            pollPluginOperations();
        }
        auto *readWrite = new juce::DynamicObject;
        readWrite->setProperty("action", "job");
        readWrite->setProperty("operation", writeOperation);
        const auto written = mcpCall("vibera_plugin", juce::var(readWrite))["structuredContent"];
        if (written["completionState"].toString() != "completed" || !bool(written["nativeApplied"]) ||
            bool(written["undoable"]) || !bool(written["recordingWriteOnly"]) || !midiTaking ||
            audio.midiInput.capture.session.load() != session || audio.pluginStateCapturePending())
            return fail("MCP recording write must preserve take and return real native ACK without capture");
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(namedBeforeWrite) || !midiTaking ||
            audio.midiInput.capture.session.load() != session)
            return fail("Ordinary name Undo after native ACK preserves exact model and held take");
        const auto after = audio.projectionDiagnostics();
        if (std::abs(controls->value.getValue() - .53) > .0001)
            return fail("Plugin controls display overwritten by an older snapshot");
        if (before.edit != after.edit || before.playbackContext != after.playbackContext ||
            before.replacements != after.replacements || starts != device->starts || stops != device->stops ||
            graphInitialisations != audio.mixerGraphInitialisations())
            return fail("Plugin controls changed graph/device lifecycle");
        const auto priorClip = controller.project.state();
        const auto clipCount = controller.project.automation.placements.size();
        if (!controls->automate.isEnabled())
            return fail("Native clip button unavailable for selected automatable fixture control");
        controls->automate.triggerClick();
        const auto clipDeadline = juce::Time::getMillisecondCounter() + 5000;
        const auto tickClip = [&]
        {
            const auto start = juce::Time::getHighResolutionTicks();
            device->process(block);
            const double elapsed =
                1000 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
            maximumMs = std::max(maximumMs, elapsed);
            ++callbacks;
            return elapsed < 256000. / 48000;
        };
        while (controller.project.automation.placements.size() == clipCount &&
               juce::Time::getMillisecondCounter() < clipDeadline)
        {
            if (!tickClip())
                return fail("Native clip creation callback deadline");
            dispatchMessages(6);
        }
        if (controller.project.automation.placements.size() != clipCount + 1 || controls->pending())
            return fail("Actual Create automation clip button did not produce a settled native curve");
        const auto createdClip = controller.project.state();
        if (audio.rebuild(controller.project, pattern, selectedPattern).failed())
            return fail("Actual native clip graph publication");
        needsRebuild = false;
        for (int n = 0; n < 8; ++n)
        {
            if (!tickClip())
                return fail("Created native curve callback deadline");
            dispatchMessages(6);
        }
        const auto clipGraph = audio.projectionDiagnostics();
        if (!midiTaking || audio.midiInput.capture.session.load() != session ||
            audio.parameterHost(pluginId) != host || !audio.playing() || clipGraph.edit != before.edit ||
            clipGraph.playbackContext != before.playbackContext ||
            clipGraph.replacements != before.replacements || device->starts != starts ||
            device->stops != stops)
            return fail("Native clip creation interrupted held recording, graph, device or worker");
        controller.undoEdit();
        if (!priorClip.isEquivalentTo(controller.project.state()) ||
            audio.rebuild(controller.project, pattern, selectedPattern).failed())
            return fail("Actual native clip one exact Undo");
        controller.redoEdit();
        if (!createdClip.isEquivalentTo(controller.project.state()) ||
            audio.rebuild(controller.project, pattern, selectedPattern).failed())
            return fail("Actual native clip exact Redo");
        controller.undoEdit();
        if (audio.rebuild(controller.project, pattern, selectedPattern).failed())
            return fail("Native clip test teardown projection");
        needsRebuild = false;
        const auto image = parameterWindow->createComponentSnapshot(parameterWindow->getLocalBounds());
        auto output = folder.getChildFile(pattern ? "parameters-pattern.png" : "parameters-song.png")
                          .createOutputStream();
        if (!output)
            return fail("Controls PNG output");
        output->setPosition(0);
        output->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(image, *output))
            return fail("Controls PNG render");
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 64));
        device->process(block);
        dispatchMessages(20);
        audio.stop();
        if (auto take = finishMidiTake(); take.failed())
            return take;
        Project captured;
        juce::Result captureResult = juce::Result::ok();
        if (auto request = audio.requestPluginState(controller.project); request.failed())
            return request;
        const auto stateDeadline = juce::Time::getMillisecondCounter() + 10000;
        bool capturedState = false;
        do
        {
            capturedState = audio.pollPluginState(captured, captureResult);
            if (!capturedState)
                dispatchMessages(5);
        } while (!capturedState && juce::Time::getMillisecondCounter() < stateDeadline);
        if (!capturedState || captureResult.failed() || captured.mixer[1].effects.front().state.isEmpty())
            return fail("Controls normal project state capture");
        const auto file = folder.getChildFile(pattern ? "parameter-pattern.vibra" : "parameter-song.vibra");
        if (saveProject(captured, file).failed())
            return fail("Controls project save");
        Project loaded;
        if (loadProject(file, loaded).failed() || !captured.state().isEquivalentTo(loaded.state()))
            return fail("Controls project state disk roundtrip");
        PluginHost restored;
        if (restored.start(loaded.mixer[1].effects.front(), 48000, 256, true).failed())
            return fail("Controls project restored isolated worker");
        PluginParameterCatalogue values;
        const auto restoreDeadline = juce::Time::getMillisecondCounter() + 10000;
        while (!restored.readParameterCatalogue(values) &&
               juce::Time::getMillisecondCounter() < restoreDeadline)
            dispatchMessages(5);
        const auto gain = std::find_if(values.parameters.begin(), values.parameters.end(),
                                       [](const auto &p) { return p.id == 0; });
        if (values.error || gain == values.parameters.end() || std::abs(gain->value - .53f) > .0001f)
            return fail("Controls edited value survives normal project restore");
        // The original SDK control deliberately stalls/crashes only at these exact test values.
        // Recovery is an explicit structural action, distinct from the incremental edit tests above.
        audio.playStop();
        dispatchMessages(25);
        const auto recoveryBefore = audio.projectionDiagnostics();
        const auto failureRequest =
            host->setParameter(0, pattern ? .27182818f : .31415927f, controls->generation());
        PluginParameterResult failureReply;
        const auto failureDeadline = juce::Time::getMillisecondCounter() + 6500;
        while (!host->readParameterResult(failureRequest, failureReply) &&
               juce::Time::getMillisecondCounter() < failureDeadline)
        {
            device->process(block);
            audio.reclaimPluginHosts();
            dispatchMessages(5);
        }
        dispatchMessages(60);
        if (!failureRequest || host->alive() || !controls->recover.isVisible() ||
            !controls->recover.isEnabled() || controls->value.isEnabled())
            return fail("Failed inspector exposes recovery and disables stale native writes");
        {
            const auto failedImage =
                parameterWindow->createComponentSnapshot(parameterWindow->getLocalBounds());
            auto stream = folder.getChildFile(pattern ? "recovery-pattern.png" : "recovery-song.png")
                              .createOutputStream();
            if (!stream)
                return fail("Recovery screenshot output");
            stream->setPosition(0);
            stream->truncate();
            if (!juce::PNGImageFormat{}.writeImageToStream(failedImage, *stream))
                return fail("Recovery screenshot render");
        }
        const auto recoveryDocument = controller.project.state();
        record.onClick();
        if (!midiTaking || !audio.midiInput.capture.active.load())
            return fail("Arm an actual held MIDI take before inspector recovery");
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 72, juce::uint8(100)));
        for (int n = 0; n < 4; ++n)
        {
            device->process(block);
            dispatchMessages(5);
        }
        controls->recover.triggerClick();
        dispatchMessages(30);
        const auto recoveryDeadline = juce::Time::getMillisecondCounter() + 30000;
        while ((pluginRecovery || needsRebuild) && juce::Time::getMillisecondCounter() < recoveryDeadline)
        {
            timerCallback();
            device->process(block);
            dispatchMessages(5);
        }
        controls = parameterWindow ? dynamic_cast<PluginControls *>(parameterWindow->getContentComponent())
                                   : nullptr;
        const auto recoveredHost = audio.parameterHost(pluginId);
        const auto inspectorDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (controls && !bool(controls->snapshot().getProperty("ready", false)) &&
               juce::Time::getMillisecondCounter() < inspectorDeadline)
            dispatchMessages(5);
        if (pluginRecovery || needsRebuild || !controls || !recoveredHost || recoveredHost == host ||
            !recoveredHost->alive() || controls->host != recoveredHost || controls->recover.isVisible() ||
            !bool(controls->snapshot().getProperty("ready", false)) ||
            std::abs(controls->value.getValue() - .53) > .0001 || !audio.playing() ||
            audio.projectionDiagnostics().replacements != recoveryBefore.replacements + 1 ||
            starts != device->starts || stops != device->stops || midiTaking ||
            audio.midiInput.capture.active.load() || audio.typingInput.capture.active.load() ||
            recoveryDocument.isEquivalentTo(controller.project.state()))
            return fail(
                "Actual inspector recovery button restores captured state and commits the held MIDI take");
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
        device->process(block);
        const auto recoveredTake = controller.project.state();
        controller.undoEdit();
        if (!recoveryDocument.isEquivalentTo(controller.project.state()) ||
            audio.rebuild(controller.project, pattern, selectedPattern).failed())
            return fail("Recovery-held MIDI take has one exact Undo");
        controller.redoEdit();
        if (!recoveredTake.isEquivalentTo(controller.project.state()) ||
            audio.rebuild(controller.project, pattern, selectedPattern).failed() ||
            audio.parameterHost(pluginId) != recoveredHost || !recoveredHost->alive() || !audio.playing() ||
            starts != device->starts || stops != device->stops ||
            audio.projectionDiagnostics().replacements != recoveryBefore.replacements + 1)
            return fail("Recovery-held MIDI take exact Redo retains the recovered worker and device");
        needsRebuild = false;
        const auto inspectorProject = controller.projectIdentity;
        controller.projectIdentity = juce::Uuid().toString();
        controls->recover.onClick(); // Simulated project replacement, including a reused plugin ID.
        const bool staleRecoveryRefused = !pluginRecovery && !needsRebuild &&
                                          controls->feedback.getText() == "Project changed; reopen controls.";
        controller.projectIdentity = inspectorProject;
        if (!staleRecoveryRefused)
            return fail("An old inspector cannot recover a target in another project session");
        audio.stop();
        report +=
            juce::String(pattern ? "Pattern" : "Song") +
            ": failed inspector disables writes; actual Recover saved state button replaces failed worker, "
            "reopens ready inspector at captured0.53; held MIDI take committed with exact one Undo/Redo; "
            "transport/device retained; "
            "one explicit structural graph replacement PASS (voice continuity not claimed).\n";
        // Removal/replacement cannot silently retarget an open control to another worker.
        auto removed = controller.project;
        removed.mixer[1].effects.clear();
        if (audio.rebuild(removed, pattern, selectedPattern).failed())
            return fail("Controls fixture effect removal");
        dispatchMessages(80);
        if (controls->value.isEnabled() || controls->choices.isEnabled())
            return fail("Old controls disabled after owner removal");
        if (parameterWindow)
            delete parameterWindow.getComponent();
        report += (pattern ? "Pattern" : "Song") +
                  juce::String(": 12 native controls edits, held MIDI take, ") + juce::String(callbacks) +
                  " virtual callbacks; maxMs=" + juce::String(maximumMs, 4) +
                  "; acknowledgementMaxMs=" + juce::String(maximumAcknowledgementMs, 2) +
                  "; acknowledgementsBeyond70Ms=" + juce::String(acknowledgementsBeyond70Ms) +
                  "; actual native clip button and exact Undo/Redo during held MIDI; retained "
                  "Edit/context/graph/device/worker; normal save/reopen and removal guard PASS.\n";
    }
    return folder.getChildFile("parameter-gui.txt").replaceWithText(report) ? juce::Result::ok()
                                                                            : fail("Controls report output");
#endif
}
#if JUCE_WINDOWS
#include "StudioMixerEffectOpeningAcceptance.inc"
#else
juce::Result Studio::verifyMixerEffectOpening(const juce::File &, const juce::File &, bool, bool)
{
    return juce::Result::fail("Native effect window acceptance requires Windows");
}
#endif
#include "StudioNativeScalar.inc"
#if JUCE_WINDOWS
#include "StudioNativeScalarAcceptance.inc"
#else
juce::Result Studio::verifyNativeScalarHistory(const juce::File &, const juce::File &)
{
    return juce::Result::fail("Native scalar gesture acceptance requires Windows");
}
#endif
#include "StudioPluginWrapperRouting.inc"
#include "StudioPluginWrapperChains.inc"
} // namespace vibera

// Worker wrapper requests are application CONTROL work, never DSP work.
namespace vibera
{
void Studio::admitPluginWrapperRequest(Id plugin, const std::shared_ptr<PluginHost> &host,
                                       const PluginWrapperRequest &request)
{
    if (!host)
        return;
    const juce::ScopedValueSetter<bool> own(servicingPluginWrapper, true);
    auto refuse = [&](const juce::String &code, const juce::String &message)
    { host->completeWrapperRequest(request.serial, PluginWrapperOutcome::failed, {}, code, message); };
    if (pluginWrapperPending)
    {
        refuse("wrapper.busy", "Another wrapper request still owns its actual completion");
        return;
    }
    const auto *spec = controller.project.plugin(plugin);
    if (!request.serial || !spec || !host->alive() ||
        audio.preparedPluginHost(controller.project, plugin) != host)
    {
        refuse("wrapper.staleInstance", "Project/plugin worker is no longer the displayed instance");
        return;
    }
    const auto &intent = request.intent;
    if (!pluginWrapperIntentValid(intent))
    {
        refuse("wrapper.invalidRequest", "Malformed wrapper action/value/catalogue generation");
        return;
    }
    PluginWrapperPending next;
    next.plugin = plugin;
    next.host = host;
    next.project = controller.projectIdentity;
    next.instance = host->runtimeIdentity();
    next.request = request;
    next.value = intent.value;
    next.gestureOpen = intent.gestureOpen;
    next.lastActivity = juce::Time::getMillisecondCounter();
    pluginWrapperPending = std::move(next);
    auto &pending = *pluginWrapperPending;
    try
    {
        if (intent.action == PluginWrapperAction::parameter)
        {
            // The native service validates the actual writable catalogue generation,
            // owns physical ACK/capture, and commits the single real history edge.
            const auto result = requestNativeScalar(plugin, intent.parameter, intent.generation, intent.value,
                                                    pending.instance, intent.gestureOpen, pending.operation);
            if (result.failed())
            {
                refuse("wrapper.parameterRefused", result.getErrorMessage());
                pluginWrapperPending.reset();
            }
            return; // Admission is not completion.
        }
        if (intent.action == PluginWrapperAction::audioIo)
        {
            const auto admitted =
                requestPluginAudioIo(plugin, pending.instance, intent.io, pending.operation, pending.failure);
            if (admitted.failed())
            {
                refuse(pending.failure, admitted.getErrorMessage());
                pluginWrapperPending.reset();
            }
            return; // The parent job observes the actual canonical structural result.
        }
        if (intent.action >= PluginWrapperAction::chainCopy &&
            intent.action <= PluginWrapperAction::sidechain)
        {
            const auto action = intent.action == PluginWrapperAction::chainCopy        ? "copy"
                                : intent.action == PluginWrapperAction::chainSave      ? "save"
                                : intent.action == PluginWrapperAction::chainPaste     ? "paste"
                                : intent.action == PluginWrapperAction::chainLoad      ? "loadApply"
                                : intent.action == PluginWrapperAction::chainDirectory ? "configureDirectory"
                                                                                       : "sidechain";
            const auto admitted = requestPluginChain(plugin, pending.instance, action, {}, true,
                                                     pending.operation, pending.failure);
            if (admitted.failed())
            {
                refuse(pending.failure, admitted.getErrorMessage());
                pluginWrapperPending.reset();
            }
            return;
        }
        if (intent.action == PluginWrapperAction::notifications)
        {
            const auto result = setPluginNotifications(plugin, pending.instance, intent.value == 1);
            const bool applied =
                result.wasOk() && host->parameterNotificationsEnabled() == (intent.value == 1);
            host->completeWrapperRequest(
                request.serial, applied ? PluginWrapperOutcome::completed : PluginWrapperOutcome::failed, {},
                applied ? juce::String{} : juce::String("wrapper.notificationsRefused"),
                applied ? "Session notification setting applied; document unchanged"
                        : result.getErrorMessage());
            pluginWrapperPending.reset();
            return;
        }
        if (intent.action == PluginWrapperAction::refresh || pluginWrapperParameterAction(intent.action))
        {
            auto args = juce::var(new juce::DynamicObject);
            auto *data = args.getDynamicObject();
            data->setProperty("action",
                              intent.action == PluginWrapperAction::refresh ? "refresh" : "automate");
            data->setProperty("plugin", juce::int64(plugin));
            data->setProperty("instance", pending.instance);
            data->setProperty("revision", juce::int64(controller.revision));
            if (intent.action != PluginWrapperAction::refresh)
            {
                data->setProperty("parameter", juce::int64(intent.parameter));
                data->setProperty("generation", juce::int64(intent.generation));
                data->setProperty("automation",
                                  intent.action == PluginWrapperAction::automationCreate ? "create"
                                  : intent.action == PluginWrapperAction::initialize     ? "init"
                                                                                         : "removeInit");
            }
            const auto response = mcpCall("vibera_plugin", args, &pending.operation);
            const auto outcome = response["structuredContent"];
            if (bool(response["isError"]) || outcome["completionState"].toString() != "pending" ||
                pending.operation.isEmpty())
            {
                refuse("wrapper.operationRefused", "Canonical plugin operation was not admitted");
                pluginWrapperPending.reset();
            }
            return; // The existing catalogue/automation job owns actual completion and history.
        }
        if (intent.action >= PluginWrapperAction::channelOn && intent.action <= PluginWrapperAction::channelRange)
        {
            // The owning instrument Channel's ON / volume / pan / Mixer track, through the
            // canonical Channel commands (Undo, routing coordinator).
            const Channel *owner = nullptr;
            for (const auto &channel : controller.project.channels)
                if (channel.instrument && channel.instrument->id == plugin)
                    owner = &channel;
            bool applied = false;
            if (owner)
            {
                CommandArgs a;
                a.channel = owner->id;
                a.ids = {owner->id};
                const auto id = owner->id;
                juce::String commandId;
                if (intent.action == PluginWrapperAction::channelOn)
                    commandId = "channel.mute", a.enabled = intent.value == 0;
                else if (intent.action == PluginWrapperAction::channelGain)
                    commandId = "channel.gain", a.value = intent.value * 1.25;
                else if (intent.action == PluginWrapperAction::channelPan)
                    commandId = "channel.pan", a.value = intent.value * 2. - 1.;
                else if (intent.action == PluginWrapperAction::channelPitch)
                    commandId = "channel.pitch", a.value = intent.value * 2. - 1.;
                else if (intent.action == PluginWrapperAction::channelRange)
                    commandId = "channel.pitchRange", a.value = double(intent.parameter);
                else
                    commandId = "channel.mixer", a.mixer = int(intent.parameter);
                if (commandId == "channel.mixer")
                    applied = applyRoutingSequence({a}, "Channel Mixer track").wasOk();
                else
                {
                    command(commandId, a);
                    applied = status == "Ready";
                }
                if (const auto *after = controller.project.channel(id); applied && after)
                    applied = commandId == "channel.mixer" ? after->mixer == int(intent.parameter) || routingPending() : true;
            }
            host->completeWrapperRequest(request.serial,
                                         applied ? PluginWrapperOutcome::completed : PluginWrapperOutcome::failed, {},
                                         applied ? juce::String{} : juce::String("wrapper.channelRefused"),
                                         applied ? "Channel updated" : (owner ? status : juce::String("No instrument Channel")));
            pluginWrapperPending.reset();
            return;
        }
        auto args = juce::var(new juce::DynamicObject);
        args.getDynamicObject()->setProperty("target", juce::int64(plugin));
        args.getDynamicObject()->setProperty(
            intent.action == PluginWrapperAction::bypass ? "enabled" : "value",
            intent.action == PluginWrapperAction::bypass ? juce::var(intent.value == 1)
                                                         : juce::var(intent.value));
        auto commandArgs = juce::var(new juce::DynamicObject);
        commandArgs.getDynamicObject()->setProperty(
            "command", intent.action == PluginWrapperAction::bypass ? "plugin.bypass" : "plugin.effect.mix");
        commandArgs.getDynamicObject()->setProperty("args", args);
        commandArgs.getDynamicObject()->setProperty("projectIdentity", pending.project);
        commandArgs.getDynamicObject()->setProperty("revision", juce::int64(controller.revision));
        const auto response = mcpCall("vibera_command", commandArgs);
        const auto data = response["structuredContent"];
        const auto *current = controller.project.plugin(plugin);
        const bool applied =
            !bool(response["isError"]) && data["completionState"].toString() == "completed" &&
            pending.project == controller.projectIdentity && current &&
            audio.preparedPluginHost(controller.project, plugin) == host &&
            host->runtimeIdentity() == pending.instance &&
            (intent.action == PluginWrapperAction::bypass ? current->bypass == (intent.value == 1)
                                                          : current->wet == intent.value);
        if (applied)
            host->publishWrapperState(host->alive(), current->bypass, current->wet,
                                      host->wrapperState().lastTweakedSelection);
        host->completeWrapperRequest(request.serial,
                                     applied ? PluginWrapperOutcome::completed : PluginWrapperOutcome::failed,
                                     {}, applied ? juce::String{} : juce::String("wrapper.commandRefused"),
                                     applied ? "Canonical document edit completed; DSP settling is separate"
                                             : "Canonical command did not complete on the bound target");
        pluginWrapperPending.reset();
    }
    catch (const McpFailure &error)
    {
        if (pending.operation.isNotEmpty() &&
            std::any_of(pluginOperations.begin(), pluginOperations.end(), [&](const auto &job)
                        { return job.id == pending.operation && job.state == "pending"; }))
        {
            pending.failure = error.code + ": " + juce::String(error.what());
            pending.closing = true;
            finishNativeScalarGesture(pending.operation);
        }
        else
        {
            refuse(error.code, error.what());
            pluginWrapperPending.reset();
        }
    }
    catch (const std::exception &error)
    {
        // Scalar and catalogue/automation requests may already own physical work.
        // Retain the accepted token until its real canonical service is terminal.
        if (pending.operation.isNotEmpty() &&
            std::any_of(pluginOperations.begin(), pluginOperations.end(), [&](const auto &job)
                        { return job.id == pending.operation && job.state == "pending"; }))
        {
            pending.failure = "wrapper.exception: " + juce::String(error.what());
            pending.closing = true;
            finishNativeScalarGesture(pending.operation);
        }
        else
        {
            refuse("wrapper.exception", error.what());
            pluginWrapperPending.reset();
        }
    }
}

void Studio::servicePluginWrapperRequests()
{
    if (servicingPluginWrapper)
        return;
    const juce::ScopedValueSetter<bool> own(servicingPluginWrapper, true);
    if (pluginWrapperPending)
    {
        auto &pending = *pluginWrapperPending;
        const auto job = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                      [&](const auto &item) { return item.id == pending.operation; });
        if (job != pluginOperations.end() && job->state != "pending")
        {
            if (pending.request.intent.action != PluginWrapperAction::audioIo && job->kind != "chain" &&
                (pending.project != controller.projectIdentity ||
                 !controller.project.plugin(pending.plugin) || !pending.host->alive() ||
                 audio.preparedPluginHost(controller.project, pending.plugin) != pending.host ||
                 pending.host->runtimeIdentity() != pending.instance))
                pending.failure =
                    "Wrapper target changed; retained operation describes the previous instance";
            pending.host->completeWrapperRequest(
                pending.request.serial,
                pending.failure.isNotEmpty() ? PluginWrapperOutcome::failed
                : job->state == "completed"  ? PluginWrapperOutcome::completed
                : job->state == "cancelled"  ? PluginWrapperOutcome::cancelled
                                             : PluginWrapperOutcome::failed,
                pending.operation, pending.failure.isNotEmpty() ? "wrapper.staleRequest" : job->code,
                pending.failure.isNotEmpty() ? pending.failure + " | Actual native outcome: " + job->message
                                             : job->message);
            pluginWrapperPending.reset();
        }
        else
        {
            const bool structural = pending.request.intent.action == PluginWrapperAction::audioIo ||
                                    (job != pluginOperations.end() && job->kind == "chain");
            const bool current =
                pending.project == controller.projectIdentity && controller.project.plugin(pending.plugin) &&
                pending.host->alive() &&
                audio.preparedPluginHost(controller.project, pending.plugin) == pending.host &&
                pending.host->runtimeIdentity() == pending.instance;
            float desired = pending.value;
            bool open = pending.gestureOpen;
            const bool ownedMailbox = pending.host->readWrapperDesired(pending.request.serial, desired, open);
            const auto window = pending.host->editorWindowState();
            if (!structural && (!current || !ownedMailbox))
            {
                pending.failure = "Wrapper project/instance/serial changed; inspect actual native outcome";
                pending.closing = true;
            }
            // Displayed selection may lag a newly submitted native target.
            // The immutable request and atomic gesture end own this transaction.
            if ((window.known && !window.exists) ||
                juce::Time::getMillisecondCounter() - pending.lastActivity >= 30000u)
                pending.closing = true;
            if (pending.request.intent.action == PluginWrapperAction::parameter && !pending.closing &&
                (desired != pending.value || open != pending.gestureOpen))
            {
                const auto result = requestNativeScalar(pending.plugin, pending.request.intent.parameter,
                                                        pending.request.intent.generation, desired,
                                                        pending.instance, open, pending.operation);
                if (result.failed())
                {
                    pending.failure = result.getErrorMessage();
                    pending.closing = true;
                }
                else
                {
                    pending.value = desired;
                    pending.gestureOpen = open;
                    pending.lastActivity = juce::Time::getMillisecondCounter();
                }
            }
            if (pending.closing && pending.request.intent.action == PluginWrapperAction::parameter)
                finishNativeScalarGesture(pending.operation);
            if (job == pluginOperations.end() &&
                (!nativeScalar || nativeScalar->operation != pending.operation))
            {
                pending.host->completeWrapperRequest(pending.request.serial, PluginWrapperOutcome::failed,
                                                     pending.operation, "wrapper.missingOperation",
                                                     "Actual native operation receipt unavailable");
                pluginWrapperPending.reset();
            }
        }
    }
    // Publish canonical values and inspect only actual project-owned workers.
    // Keep at most one application proxy owner; other submitted mailboxes wait
    // until their exact request can be accepted, rather than losing its token.
    for (const auto &runtime : audio.pluginRuntimeStates())
    {
        const auto host = audio.preparedPluginHost(controller.project, runtime.plugin);
        const auto *spec = controller.project.plugin(runtime.plugin);
        if (!host || !spec || host->runtimeIdentity() != runtime.instance)
            continue;
        const auto lastGesture = host->lastParameterGesture();
        const auto lastSelection =
            lastGesture ? (std::uint64_t(lastGesture->generation) << 32) | lastGesture->id : 0;
        host->publishWrapperState(host->alive(), spec->bypass, spec->wet, lastSelection);
        {
            PluginWrapperChannel strip;
            for (const auto &channel : controller.project.channels)
                if (channel.instrument && channel.instrument->id == runtime.plugin)
                {
                    strip.present = true;
                    strip.on = !channel.muted;
                    strip.gain = juce::jlimit(0.f, 1.f, channel.gain / 1.25f);
                    strip.pan = juce::jlimit(0.f, 1.f, (channel.pan + 1.f) * .5f);
                    strip.track = channel.mixer;
                    strip.pitch = juce::jlimit(0.f, 1.f, (channel.pitchBend + 1.f) * .5f);
                    strip.range = channel.pitchRange;
                }
            for (const auto &insert : controller.project.mixer)
                strip.maxTrack = std::max(strip.maxTrack, insert.index);
            host->publishWrapperChannel(strip);
        }
        if (!pluginWrapperPending)
        {
            PluginWrapperRequest request;
            if (host->takeWrapperRequest(request))
                admitPluginWrapperRequest(runtime.plugin, host, request);
        }
    }
}
} // namespace vibera
