// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "StepComposeContracts.h"
#include "StepComposePanel.h"
#include "BuildAcceptanceOutput.h"

namespace vibera
{
namespace
{
juce::var composeObject(const char *action)
{
    auto *o = new juce::DynamicObject;
    o->setProperty("action", action);
    return juce::var(o);
}
std::vector<StepComposeRecipe> changedRecipes(const std::vector<StepComposeRecipe> &recipes,
                                              const std::vector<StepComposeRecipe> &original)
{
    std::vector<StepComposeRecipe> changed;
    for (size_t i = 0; i < recipes.size(); ++i)
        if (juce::JSON::toString(stepComposeRecipeState(recipes[i])) !=
            juce::JSON::toString(stepComposeRecipeState(original[i])))
            changed.push_back(recipes[i]);
    return changed;
}
class ComposeWindow final : public juce::DocumentWindow
{
  public:
    std::function<void()> close;
    ComposeWindow() : DocumentWindow("Advanced Fill", panel, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(false);
        setResizable(true, true);
        setResizeLimits(760, 500, 1600, 1400);
    }
    void closeButtonPressed() override
    {
        if (close)
            close();
    }
};
} // namespace
bool Studio::stepComposePending() const
{
    return stepComposeSession && stepComposeSession->state == "pending";
}
void Studio::pollStepCompose()
{
    if (!stepComposePending())
        return;
    auto &s = *stepComposeSession;
    const bool owned = s.project == controller.projectIdentity && controller.hasGesture() &&
                       controller.gestureOwner() == s.owner && controller.gestureSerial() == s.serial;
    if (!owned)
    {
        // Another owner/project must never receive a stale baseline restoration.
        s.state = "cancelled";
        s.reason = "Project or preview ownership changed";
        s.preview = false;
        s.solo = false;
    }
    else if (s.revision != controller.revision ||
             (s.remote && juce::Time::getMillisecondCounterHiRes() - s.lastActivity >= s.leaseMs))
    {
        const auto cancelled = controller.cancelGestureChecked(s.owner);
        if (cancelled.wasOk())
        {
            s.state = "cancelled";
            s.reason = "Preview expired or source changed; baseline restored";
            s.preview = false;
            s.solo = false;
            s.revision = controller.revision;
        }
        else
            s.reason = cancelled.getErrorMessage();
    }
    if (stepComposeWindow)
        if (auto *view = dynamic_cast<StepComposePanel *>(stepComposeWindow->getContentComponent()))
            view->refresh();
}
juce::var Studio::stepComposeState() const
{
    auto *o = new juce::DynamicObject;
    o->setProperty("completionState", stepComposeSession ? stepComposeSession->state : "idle");
    o->setProperty("playing", audio.playing());
    o->setProperty("dspSettlingAcknowledged", false);
    o->setProperty("pendingAudioUpdate", mixerChainApplying() || devicePreparationBusy() || needsRebuild ||
                                             routingCoordinator.mutationBlocked() ||
                                             !audio.deferredWorkerPreparationIdle() ||
                                             audio.projectionDiagnostics().pluginSourceState == "pending");
    if (!stepComposeSession)
        return juce::var(o);
    const auto &s = *stepComposeSession;
    o->setProperty("operation", s.id);
    o->setProperty("pattern", juce::int64(s.pattern));
    o->setProperty("activeChannel", juce::int64(s.activeChannel));
    o->setProperty("preview", s.preview);
    o->setProperty("solo", s.solo);
    o->setProperty("reason", s.reason);
    o->setProperty("projectIdentity", s.project);
    o->setProperty("sourceRevision", juce::int64(s.revision));
    o->setProperty("remote", s.remote);
    o->setProperty("leaseMs", s.leaseMs);
    o->setProperty("dirtyRecipes", int(changedRecipes(s.recipes, s.original).size()));
    juce::Array<juce::var> rows;
    for (const auto &r : s.recipes)
    {
        auto row = stepComposeRecipeState(r);
        const auto *channel = s.baseline->channel(r.channel);
        row.getDynamicObject()->setProperty("name", channel ? channel->name : "Missing Channel");
        std::vector<std::uint8_t> mask;
        (void)stepCompositionMask(r, mask);
        juce::Array<juce::var> bits;
        for (auto bit : mask)
            bits.add(int(bit));
        row.getDynamicObject()->setProperty("mask", bits);
        rows.add(row);
    }
    o->setProperty("recipes", rows);
    return juce::var(o);
}
juce::Result Studio::stepComposeAction(const juce::var &args, bool remote)
{
    try
    {
        validateMcpSchema(stepComposeSessionSchema(), args);
    }
    catch (const McpFailure &e)
    {
        return juce::Result::fail("stepCompose.invalidArguments: " + juce::String(e.what()));
    }
    if (args.hasProperty("projectIdentity") &&
        args["projectIdentity"].toString() != controller.projectIdentity)
        return juce::Result::fail("stepCompose.staleProject: Project session identity changed");
    pollStepCompose();
    const auto action = args["action"].toString();
    auto fail = [](const juce::String &message) { return juce::Result::fail("stepCompose." + message); };
    if (action == "state")
        return juce::Result::ok();
    if (action == "begin")
    {
        if (!args.hasProperty("pattern") || !args.hasProperty("channels"))
            return fail("invalidArguments: Begin requires explicit Pattern and Channels");
        if (stepComposePending() || controller.hasGesture() || noteSlicePending() || noteStretchPending() ||
            afterPluginState || pendingExport || audio.rendering() || midiFileBusy || customization.running ||
            automationRecordingActive() || recordArmed || midiTaking || hasPendingMidiTake())
            return fail("busy: Finish the active edit, take, file operation or macro first");
        const auto patternId = Id(juce::int64(args["pattern"]));
        const auto *pattern = controller.project.pattern(patternId);
        if (!pattern)
            return fail("missingTarget: Existing Pattern required");
        StepComposeSession s;
        s.id = juce::Uuid().toString();
        s.owner = "step-compose:" + s.id;
        s.project = controller.projectIdentity;
        s.pattern = patternId;
        s.remote = remote;
        s.leaseMs = int(args.getProperty("leaseMs", 30000));
        s.lastActivity = juce::Time::getMillisecondCounterHiRes();
        s.baseline = std::make_unique<Project>(controller.project);
        for (const auto &id : *args["channels"].getArray())
        {
            StepComposeRecipe r;
            r.channel = juce::int64(id);
            const auto *channel = controller.project.channel(r.channel);
            if (!channel || channel->audioClip)
                return fail("missingTarget: Playable instrument Channel required");
            const auto loop = std::find_if(pattern->channelLoops.begin(), pattern->channelLoops.end(),
                                           [&](const auto &l) { return l.channel == r.channel; });
            const bool existingLoop = loop != pattern->channelLoops.end();
            const auto cycle =
                existingLoop ? channelLoopLength(controller.project, *pattern, r.channel) : pattern->length;
            // A changed shorter draft repeats by default. Unchanged opening
            // drafts are never applied, so opening/Accept retain existing metadata.
            r.loop = true;
            r.sequenceSteps = juce::jlimit(1, 4096, int(std::min(4096., std::ceil(cycle * 4))));
            r.every = std::min(4, r.sequenceSteps);
            r.pulses = std::min(4, r.sequenceSteps);
            r.mode = StepComposeMode::manual;
            r.manual.assign(size_t(r.sequenceSteps), 0);
            for (const auto &note : pattern->notes)
                if (note.channel == r.channel && note.beat < r.sequenceSteps * .25)
                    r.manual[size_t(int(std::floor(note.beat * 4)))] = 1;
            s.recipes.push_back(std::move(r));
        }
        s.original = s.recipes;
        s.activeChannel = s.recipes.front().channel;
        if (!controller.beginGesture(s.owner))
            return fail("busy: Cannot acquire document preview");
        s.serial = controller.gestureSerial();
        s.revision = controller.revision;
        stepComposeSession = std::move(s);
        if (remote && stepComposeWindow)
            stepComposeWindow->setVisible(false);
        return juce::Result::ok();
    }
    if (!stepComposePending())
        return fail("notPending: No pending Advanced Fill session");
    auto &s = *stepComposeSession;
    if (args["operation"].toString() != s.id)
        return fail("invalidOwner: Exact operation token required");
    s.lastActivity = juce::Time::getMillisecondCounterHiRes();
    auto activeChannel = s.activeChannel;
    if (action == "select")
    {
        const auto channel = Id(juce::int64(args["channel"]));
        if (std::none_of(s.recipes.begin(), s.recipes.end(),
                         [&](const auto &r) { return r.channel == channel; }))
            return fail("missingTarget: Channel is outside this session");
        activeChannel = channel;
    }
    if (action == "play")
    {
        const bool wasPlaying = audio.playing();
        if (!performAction("transport.play"))
            return fail("transportRefused: Transport did not accept the action");
        if (audio.playing() == wasPlaying && !devicePreparationBusy())
            return fail("playbackUnavailable: " + status);
        s.reason = devicePreparationBusy() ? "Playback preparation pending; inspect devicePreparation"
                                           : "Transport toggled";
        return juce::Result::ok();
    }
    if (action == "cancel")
    {
        if (auto result = controller.cancelGestureChecked(s.owner); result.failed())
            return result;
        s.state = "cancelled";
        s.reason = "Opening score, allocator, dirty state and history restored";
        s.preview = false;
        s.solo = false;
        s.revision = controller.revision;
        return juce::Result::ok();
    }
    auto recipes = s.recipes;
    if (action == "update")
    {
        std::vector<StepComposeRecipe> updates;
        if (auto result = parseStepComposeRecipes(args["recipes"], updates); result.failed())
            return result;
        for (auto &r : updates)
        {
            auto found = std::find_if(recipes.begin(), recipes.end(),
                                      [&](const auto &old) { return old.channel == r.channel; });
            if (found == recipes.end())
                return fail("missingTarget: Recipe Channel is outside this session");
            *found = std::move(r);
        }
    }
    else if (action == "reset")
    {
        for (size_t i = 0; i < recipes.size(); ++i)
            if (bool(args.getProperty("all", false)) || recipes[i].channel == s.activeChannel)
                recipes[i] = s.original[i];
    }
    else if (action == "dice" || action == "manual")
    {
        auto &r = *std::find_if(recipes.begin(), recipes.end(),
                                [&](const auto &row) { return row.channel == activeChannel; });
        if (action == "dice")
        {
            r.seed += std::uint32_t(1); // Defined unsigned wrap, fully reproducible readback.
            r.dice = true;
        }
        else
        {
            std::vector<std::uint8_t> mask;
            if (auto checked = stepCompositionMask(r, mask); checked.failed())
                return checked;
            r.mode = StepComposeMode::manual;
            r.manual = std::move(mask);
            r.dice = false;
            r.rotation = 0;
        }
    }
    if ((action == "preview" || action == "solo") && !args.hasProperty("enabled"))
        return fail("invalidArguments: Preview/Solo requires enabled");
    const bool preview = action == "preview" ? bool(args["enabled"]) : s.preview;
    const bool solo = action == "commit" ? false : action == "solo" ? bool(args["enabled"]) : s.solo;
    const auto changed = changedRecipes(recipes, s.original);
    // Validate the complete batch against the immutable opening score before
    // publishing any draft or audible preview. All work is on CONTROL.
    if (!changed.empty())
    {
        auto candidate = *s.baseline;
        if (auto checked =
                applyStepComposition(candidate, s.pattern, changed, customization.muteRemovedSteps);
            checked.failed())
            return checked;
    }
    if (preview || solo || s.solo || action == "preview" || action == "commit")
    {
        CommandArgs a;
        a.pattern = s.pattern;
        a.composition = changed;
        a.enabled = !changed.empty() && (preview || action == "commit");
        a.compositionSolo = solo ? activeChannel : 0;
        command("steps.compose", a, s.owner);
        if (status != "Ready")
            return juce::Result::fail(status);
        s.revision = controller.revision;
    }
    s.recipes = std::move(recipes);
    s.activeChannel = activeChannel;
    s.preview = preview;
    s.solo = solo;
    if (action == "commit")
    {
        if (auto result = controller.endGestureChecked("Advanced Fill", s.owner); result.failed())
        {
            // Publication succeeded; history is still pending. Keep the actual
            // preview truthful and retain retry/cancellation ownership.
            s.preview = !s.baseline->state().isEquivalentTo(controller.project.state());
            s.reason = result.getErrorMessage();
            return result;
        }
        s.state = "completed";
        s.reason = "Canonical document committed; poll audio state separately for adoption";
        s.preview = false;
        s.revision = controller.revision;
    }
    return juce::Result::ok();
}
void Studio::openStepComposer(Id pattern, const std::vector<Id> &channels)
{
    pollStepCompose();
    if (stepComposePending())
    {
        if (!stepComposeSession->remote && stepComposeWindow)
        {
            stepComposeWindow->setVisible(true);
            stepComposeWindow->toFront(true);
        }
        else
            status = "Advanced Fill is owned by an MCP session";
        return;
    }
    auto request = composeObject("begin");
    request.getDynamicObject()->setProperty("pattern", juce::int64(pattern));
    juce::Array<juce::var> ids;
    // The dropdown spans the Rack, not only the initiating bulk selection.
    // Drafts remain nonmutating until changed; the live MIDI target stays independent.
    for (const auto &channel : controller.project.channels)
        if (!channel.audioClip)
            ids.add(juce::int64(channel.id));
    request.getDynamicObject()->setProperty("channels", ids);
    if (auto result = stepComposeAction(request, false); result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    if (!channels.empty())
    {
        auto select = composeObject("select");
        select.getDynamicObject()->setProperty("operation", stepComposeSession->id);
        select.getDynamicObject()->setProperty("channel", juce::int64(channels.front()));
        if (auto result = stepComposeAction(select, false); result.failed())
        {
            status = result.getErrorMessage();
            auto cancel = composeObject("cancel");
            cancel.getDynamicObject()->setProperty("operation", stepComposeSession->id);
            (void)stepComposeAction(cancel, false);
            return;
        }
    }
    if (stepComposeWindow)
        delete stepComposeWindow.getComponent();
    auto *window = new ComposeWindow;
    stepComposeWindow = window;
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto token = stepComposeSession->id;
    auto close = [safe, token]
    {
        if (!safe)
            return;
        if (safe->stepComposePending() && safe->stepComposeSession->id == token)
        {
            auto cancel = composeObject("cancel");
            cancel.getDynamicObject()->setProperty("operation", token);
            if (auto result = safe->stepComposeAction(cancel, false); result.failed())
            {
                safe->status = result.getErrorMessage();
                return;
            }
        }
        if (safe->stepComposeWindow)
            safe->stepComposeWindow->setVisible(false);
    };
    window->close = close;
    window->setContentOwned(new StepComposePanel(
                                [safe, token]
                                {
                                    return safe && safe->stepComposeSession &&
                                                   safe->stepComposeSession->id == token
                                               ? safe->stepComposeState()
                                               : juce::var{};
                                },
                                [safe, token](const juce::var &value)
                                {
                                    if (!safe)
                                        return juce::Result::fail("stepCompose.closed: Application closed");
                                    auto request = value.clone();
                                    request.getDynamicObject()->setProperty("operation", token);
                                    return safe->stepComposeAction(request, false);
                                },
                                close),
                            true);
    window->centreAroundComponent(this, 840, 600);
    window->addToDesktop();
    window->setVisible(true);
    window->toFront(true);
}
juce::Result Studio::verifyStepCompose(const juce::File &requested)
{
    juce::File folder;
    if (auto admitted = test::prepareBuildAcceptanceAttempt(requested.getFullPathName(), folder);
        admitted.failed())
        return admitted;
    stopTimer();
    audio.stop();
    auto project = Project::empty();
    project.patterns[0].autoLength = false;
    project.patterns[0].length = 8;
    const auto pattern = project.patterns[0].id;
    const auto kick = project.channels[0].id, hat = project.channels[2].id;
    project.patterns[0].notes = {{project.allocate(), kick, 0, .25, 60, .4f, true},
                                 {project.allocate(), project.channels[3].id, 1, 2, 67, .7f, false}};
    controller.replace(project);
    selectedPattern = pattern;
    selectedChannel = project.channels[3].id;
    selectedChannels = {kick, hat};
    refresh();
    const auto baseline = controller.project.state();
    const bool dirty = controller.dirty;
    auto fail = [&](const juce::String &message)
    {
        folder.getChildFile("failure.txt").replaceWithText(message);
        return juce::Result::fail(message);
    };
    juce::PopupMenu menu;
    addStepFillItems(menu, {kick, hat}, pattern);
    bool opened = false;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().itemID == 109 && it.getItem().isEnabled)
        {
            it.getItem().action();
            opened = true;
        }
    if (!opened || !stepComposeWindow || !stepComposePending() || stepComposeSession->preview ||
        !baseline.isEquivalentTo(controller.project.state()) || controller.dirty != dirty)
        return fail("Actual Advanced Fill menu opening mutated score or failed");
    if (stepComposeSession->recipes.size() != project.channels.size() ||
        stepComposeSession->activeChannel != kick ||
        std::any_of(stepComposeSession->recipes.begin(), stepComposeSession->recipes.end(),
                    [](const auto &r) { return !r.loop; }))
        return fail("Opening one Rack tool must expose all instrument drafts with automatic loop defaults");
    auto *window = stepComposeWindow.getComponent();
    openStepComposer(pattern, {kick});
    if (window != stepComposeWindow.getComponent())
        return fail("Duplicate Advanced Fill window");
    auto *view = dynamic_cast<StepComposePanel *>(window->getContentComponent());
    if (!view)
        return fail("Actual Advanced Fill component missing");
    auto combo = [&](const char *id, int value)
    {
        auto *control = dynamic_cast<juce::ComboBox *>(view->findChildWithID(id));
        if (!control)
            return false;
        control->setSelectedId(value, juce::sendNotificationSync);
        return true;
    };
    auto slider = [&](const char *id, double value)
    {
        auto *control = dynamic_cast<juce::Slider *>(view->findChildWithID(id));
        if (!control)
            return false;
        control->setValue(value, juce::sendNotificationSync);
        return true;
    };
    auto click = [&](const char *id)
    {
        auto *control = dynamic_cast<juce::Button *>(view->findChildWithID(id));
        if (!control || !control->isEnabled())
            return false;
        if (control->getClickingTogglesState())
            control->setToggleState(!control->getToggleState(), juce::dontSendNotification);
        if (control->onClick)
            control->onClick();
        return true;
    };
    if (!combo("stepCompose.mode", 2) || !slider("stepCompose.steps", 8) || !slider("stepCompose.every", 3))
        return fail("Actual draft controls unavailable");
    if (!baseline.isEquivalentTo(controller.project.state()))
        return fail("Preview-off draft changed score");
    if (!click("stepCompose.preview") || !stepComposeSession->preview ||
        controller.project.pattern(pattern)->notes.size() != 4)
        return fail("Actual Preview control did not publish expected three drum hits plus melody");
    const auto preview = controller.project.state();
    if (!slider("stepCompose.every", 2) || !slider("stepCompose.every", 3) ||
        !preview.isEquivalentTo(controller.project.state()))
        return fail("Preview compounded or reallocated surviving IDs");
    record.onClick();
    if (recordArmed || !stepComposePending())
        return fail("Native Record bypassed exclusive preview ownership");
    if (!click("stepCompose.preview") || !baseline.isEquivalentTo(controller.project.state()))
        return fail("Preview off did not restore exact baseline");
    if (!click("stepCompose.solo") || !stepComposeSession->solo || !controller.project.solo ||
        controller.project.solo->channel != kick || !combo("stepCompose.channel", 3) ||
        !controller.project.solo || controller.project.solo->channel != hat || !click("stepCompose.solo") ||
        !baseline.isEquivalentTo(controller.project.state()))
        return fail("Actual Solo/Channel controls did not transfer audition and restore opening mute state");
    if (!combo("stepCompose.mode", 3) || !slider("stepCompose.steps", 7) ||
        !slider("stepCompose.pulses", 3) || !slider("stepCompose.rotation", 2) ||
        !click("stepCompose.preview"))
        return fail("Second retained draft controls");
    if (selectedChannel != project.channels[3].id || stepComposeState()["dirtyRecipes"] != juce::var(2))
        return fail("Channel picker changed MIDI recipient or lost another draft");
    if (!click("stepCompose.dice"))
        return fail("Actual Dice control");
    const auto oldSeed = juce::int64(stepComposeState()["recipes"][2]["seed"]);
    if (!click("stepCompose.reroll") || juce::int64(stepComposeState()["recipes"][2]["seed"]) != oldSeed + 1)
        return fail("Actual repeated Dice button did not use canonical seed advance");
    auto expected = stepComposeState()["recipes"][2]["mask"].clone();
    if (!click("stepCompose.manual") ||
        juce::JSON::toString(stepComposeState()["recipes"][2]["mask"]) != juce::JSON::toString(expected))
        return fail("Actual Manual control changed the final generated rhythm");
    auto *viewport = dynamic_cast<juce::Viewport *>(view->findChildWithID("stepCompose.viewport"));
    auto *mask = viewport ? viewport->getViewedComponent() : nullptr;
    if (!mask || !expected.isArray())
        return fail("Actual manual mask component");
    const auto point = juce::Point<float>{float(mask->getWidth()) / 32, 30};
    const auto event = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.f, 0.f,
                                        0.f, 0.f, 0.f, mask, mask, juce::Time::getCurrentTime(), point,
                                        juce::Time::getCurrentTime(), 1, false);
    expected.getArray()->set(0, 1);
    mask->mouseDown(event);
    const auto manualState = stepComposeState()["recipes"][2];
    if (manualState["mode"].toString() != "manual" || bool(manualState["dice"]) ||
        int(manualState["rotation"]) != 0 ||
        juce::JSON::toString(manualState["mask"]) != juce::JSON::toString(expected))
        return fail("Actual generated-mask mouse edit did not use final mask/clear transforms");
    mask->mouseUp(event);
    const auto erase = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                        juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.f, 0.f,
                                        0.f, 0.f, 0.f, mask, mask, juce::Time::getCurrentTime(), point,
                                        juce::Time::getCurrentTime(), 1, false);
    mask->mouseDown(erase);
    const auto thirdCell = juce::Point<float>{float(mask->getWidth()) * 2.5f / 16, 30};
    mask->mouseDrag(erase.withNewPosition(thirdCell));
    mask->mouseUp(erase);
    for (int i = 0; i < 3; ++i)
        if (int(stepComposeState()["recipes"][2]["mask"][i]) != 0)
            return fail("Actual right-drag did not erase every crossed grid step");
    mask->mouseDown(event);
    mask->mouseDrag(event.withNewPosition(thirdCell));
    mask->mouseUp(event);
    for (int i = 0; i < 3; ++i)
        if (int(stepComposeState()["recipes"][2]["mask"][i]) != 1)
            return fail("Actual left-drag did not activate every crossed grid step");
    auto *ring = view->findChildWithID("stepCompose.ring");
    auto *knob =
        ring ? dynamic_cast<juce::Slider *>(ring->findChildWithID("stepCompose.rotationKnob")) : nullptr;
    if (!ring || !ring->isVisible() || !knob)
        return fail("Actual step ring/center rotation control unavailable");
    const auto topCell = juce::Point<float>{ring->getWidth() * .5f,
                                            ring->getHeight() * .5f -
                                                (std::min(ring->getWidth(), ring->getHeight()) * .5f - 20.f)};
    const auto ringErase = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), topCell,
                                            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.f,
                                            0.f, 0.f, 0.f, 0.f, ring, ring, juce::Time::getCurrentTime(),
                                            topCell, juce::Time::getCurrentTime(), 1, false);
    ring->mouseDown(ringErase);
    ring->mouseUp(ringErase);
    if (int(stepComposeState()["recipes"][2]["mask"][0]) != 0)
        return fail("Actual ring LED did not share the canonical Manual mask");
    knob->setValue(3, juce::sendNotificationSync);
    if (int(stepComposeState()["recipes"][2]["rotation"]) != 3)
        return fail("Center rotation knob did not update shared recipe");
    if (!click("stepCompose.reset") || int(stepComposeState()["dirtyRecipes"]) != 1 ||
        !combo("stepCompose.mode", 3) || !slider("stepCompose.steps", 7) ||
        !slider("stepCompose.pulses", 3) || !slider("stepCompose.rotation", 2))
        return fail("Actual Reset current lost another draft or failed to restore original loop policy");
    auto capture = [&](const char *name, int width, int height, float scale)
    {
        view->setSize(width, height);
        auto stream = folder.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        return juce::PNGImageFormat{}.writeImageToStream(
            view->createComponentSnapshot(view->getLocalBounds(), true, scale), *stream);
    };
    if (!capture("advanced-fill.png", 820, 560, 1) || !capture("advanced-fill-minimum.png", 760, 500, 1) ||
        !capture("advanced-fill-150.png", 1000, 680, 1.5f))
        return fail("Advanced Fill component PNG");
    const auto composed = controller.project.state();
    if (!click("stepCompose.accept") || stepComposePending() || stepComposeSession->state != "completed" ||
        controller.undoEditChecked().failed() || !baseline.isEquivalentTo(controller.project.state()) ||
        controller.redoEditChecked().failed() || !composed.isEquivalentTo(controller.project.state()))
        return fail("Actual Accept did not produce one exact Undo/Redo");
    openStepComposer(pattern, {kick, hat});
    window = stepComposeWindow.getComponent();
    view = dynamic_cast<StepComposePanel *>(window->getContentComponent());
    if (!slider("stepCompose.steps", 5) || !click("stepCompose.preview"))
        return fail("Second preview setup");
    window->closeButtonPressed();
    if (stepComposePending() || !composed.isEquivalentTo(controller.project.state()))
        return fail("Native window close did not Cancel exactly");
    openStepComposer(pattern, {project.channels[3].id});
    view = dynamic_cast<StepComposePanel *>(stepComposeWindow->getContentComponent());
    const auto revision = controller.revision;
    if (!click("stepCompose.accept") || stepComposePending() ||
        !composed.isEquivalentTo(controller.project.state()) || controller.revision != revision)
        return fail("Unchanged melodic draft Accept was not a no-op");
    openStepComposer(pattern, {kick});
    view = dynamic_cast<StepComposePanel *>(stepComposeWindow->getContentComponent());
    if (!slider("stepCompose.steps", 4))
        return fail("History failure draft setup");
    auto admission = controller.applicationChangeAdmission;
    controller.applicationChangeAdmission = [&, admission](const Project &destination)
    {
        if (!composed.isEquivalentTo(controller.project.state()))
            return juce::Result::fail("history.fixtureRefusal: Injected failure after preview publication");
        return admission ? admission(destination) : juce::Result::ok();
    };
    const auto undoDescriptions = controller.undo.getUndoDescriptions();
    const bool clicked = click("stepCompose.accept");
    controller.applicationChangeAdmission = std::move(admission);
    if (!clicked || !stepComposePending() || !stepComposeSession->preview ||
        composed.isEquivalentTo(controller.project.state()) ||
        controller.undo.getUndoDescriptions() != undoDescriptions || !click("stepCompose.cancel") ||
        !composed.isEquivalentTo(controller.project.state()))
        return fail("Failed history commit was not truthful, pending, owned and exactly cancellable");
    openStepComposer(pattern, {kick});
    auto replacement = Project::empty();
    controller.replace(replacement);
    pollStepCompose();
    if (stepComposePending() || !replacement.state().isEquivalentTo(controller.project.state()))
        return fail("Stale preview restored over replacement project");
    if (stepComposeWindow)
        stepComposeWindow->setVisible(false);
    folder.getChildFile("verification.json")
        .replaceWithText(
            "{\"passed\":true,\"scope\":\"Actual JUCE menu/control callbacks, multi-Channel drafts, baseline "
            "preview/off, exact Accept/Cancel/history, native Record refusal, stale-project safety and "
            "component renders; no native OS musical input, physical scaling, hardware or listening\"}\n");
    return juce::Result::ok();
}
} // namespace vibera
