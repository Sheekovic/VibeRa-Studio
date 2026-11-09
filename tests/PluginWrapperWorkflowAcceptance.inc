// SPDX-License-Identifier: AGPL-3.0-or-later
// Included only in the owned original-SDK GUI acceptance driver. Exercises the
// actual worker window through scoped accessibility, never global/physical input.
struct WrapperDpiScope
{
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~WrapperDpiScope()
    {
        if (previous)
            SetThreadDpiAwarenessContext(previous);
    }
} wrapperDpiScope;
auto object = [] { return juce::var(new juce::DynamicObject); };
auto pluginCall = [&](const juce::String &action, const juce::String &view = juce::String{})
{
    auto args = object();
    args.getDynamicObject()->setProperty("action", action);
    args.getDynamicObject()->setProperty("plugin", juce::int64(pluginId));
    args.getDynamicObject()->setProperty("instance", host->runtimeIdentity());
    if (view.isNotEmpty())
        args.getDynamicObject()->setProperty("view", view);
    return mcpCall("vibera_plugin", args)["structuredContent"];
};
int wrapperCallbacks = 0;
double wrapperMaximumMs = 0;
auto tick = [&]
{
    const auto begin = juce::Time::getHighResolutionTicks();
    device->process(block);
    wrapperMaximumMs = std::max(
        wrapperMaximumMs,
        juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - begin) * 1000.);
    ++wrapperCallbacks;
    dispatchMessages(5);
    timerCallback();
    const auto published = host->wrapperState();
    if (published.ready && !published.pending && audio.parameterHost(pluginId) == host)
        if (const auto *current = controller.project.plugin(pluginId);
            current && (published.bypassed != current->bypass || published.mix != current->wet))
            throw std::runtime_error("Ready wrapper processing values must be published before terminal ACK");
};
auto await = [&](const std::function<bool()> &done, unsigned budget = 5000u)
{
    const auto began = juce::Time::getMillisecondCounter();
    do
    {
        tick();
        if (done())
            return true;
    } while (juce::Time::getMillisecondCounter() - began < budget);
    return false;
};
auto terminal = [&](const juce::var &admission, const juce::String &expected = "completed")
{
    if (admission["completionState"].toString() != "pending")
        return false;
    const auto token = admission["operation"].toString();
    return await(
        [&]
        {
            const auto job = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                          [&](const auto &j) { return j.id == token; });
            if (job == pluginOperations.end())
                return false;
            if (job->state == expected)
                return true;
            if (job->state != "pending")
                throw std::runtime_error((job->code + ": " + job->message).toStdString());
            return false;
        });
};
const auto originalWorker = host->processId();
const auto originalProjection = audio.projectionDiagnostics();
const auto documentBefore = controller.project.state();
if (controlsOnly)
{
    if (!terminal(pluginCall("openEditor"), "failed") ||
        pluginEditorState(pluginId)["availability"].toString() != "unsupported" ||
        bool(pluginEditorState(pluginId)["exists"]))
        return fail("Editorless original fixture refuses actual native Open without a fake window");
}
if (!terminal(pluginCall("wrapper", controlsOnly ? "parameters" : "plugin")))
    return fail(controlsOnly ? "Actual editorless host-control peer" : "Wrapper actual native attachment");
auto child = test::gestureFixtureWindow(originalWorker);
HWND window = child ? GetAncestor(child, GA_ROOT) : nullptr;
if (controlsOnly)
{
    struct Search { DWORD pid; HWND found = nullptr; unsigned count = 0; } search{originalWorker};
    EnumWindows([](HWND candidate, LPARAM context) -> BOOL
        {
            auto &target = *reinterpret_cast<Search *>(context);
            DWORD pid = 0;
            GetWindowThreadProcessId(candidate, &pid);
            if (pid == target.pid && IsWindowVisible(candidate) && !GetParent(candidate))
            {
                wchar_t title[256]{};
                GetWindowTextW(candidate, title, 256);
                if (juce::String(title).startsWith("VibeRa | "))
                { target.found = candidate; ++target.count; }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
    if (search.count != 1 || child)
        return fail("Editorless actual single wrapper exists with no native fixture child");
    window = search.found;
}
DWORD owner = 0;
GetWindowThreadProcessId(window, &owner);
if (!window || owner != originalWorker)
    return fail("Wrapper belongs to original prepared worker");
auto ownedWindow = [&]
{
    DWORD current = 0;
    return IsWindow(window) && GetWindowThreadProcessId(window, &current) && current == originalWorker &&
           host->alive() && host->processId() == originalWorker;
};
auto capture = [&](const juce::String &name)
{
    if (!ownedWindow())
        return false;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds))
        return false;
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width < 200 || height < 150 || width > 8192 || height > 8192)
        return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    auto dc = GetDC(nullptr);
    auto memory = CreateCompatibleDC(dc);
    void *pixels = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const auto previous = SelectObject(memory, bitmap);
    const bool printed = PrintWindow(window, memory, 0) != FALSE;
    juce::Image image(juce::Image::ARGB, width, height, false);
    if (printed && pixels)
    {
        juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto pixel = static_cast<const std::uint32_t *>(pixels)[y * width + x];
                data.setPixelColour(x, y, juce::Colour(pixel | 0xff000000u));
            }
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, dc);
    if (!printed)
        return false;
    auto stream = folder.getChildFile((pattern ? "pattern-" : "song-") + name + ".png").createOutputStream();
    if (!stream)
        return false;
    stream->setPosition(0);
    stream->truncate();
    return juce::PNGImageFormat{}.writeImageToStream(image, *stream);
};
if (!capture(controlsOnly ? "wrapper-controls-only" : "wrapper-plugin"))
    return fail("Capture actual worker native Plugin view");
const auto readback = pluginEditorState(pluginId);
if (!bool(readback["exists"]) || !bool(readback["wrapperReady"]) ||
    readback["wrapperView"].toString() != (controlsOnly ? "parameters" : "plugin") ||
    readback["availability"].toString() != (controlsOnly ? "controls_only" : "available") ||
    !readback["nativeInterfaceAvailable"].isBool() || bool(readback["nativeInterfaceAvailable"]) == controlsOnly)
    return fail("Wrapper real native and ready readback");
if (!terminal(pluginCall("wrapper", "settings")) || !capture("wrapper-settings"))
    return fail("Settings view shares the native wrapper");
// UI Automation runs on a windowless MTA, keeping message dispatch and virtual
// audio moving while Windows calls the worker's accessibility provider.
std::string accessibleFailure; // Why the last accessibleAction returned false (diagnostics only).
auto accessibleAction = [&](const wchar_t *name, bool toggleControl, double desired,
                            bool invokeControl = false, bool allowExpectedClose = false)
{
    accessibleFailure.clear();
    if (!ownedWindow())
    {
        accessibleFailure = "target window not owned before the action";
        return false;
    }
    std::atomic<bool> done{false}, success{false};
    std::thread worker(
        [&]
        {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(initialized))
            {
                {
                    Microsoft::WRL::ComPtr<IUIAutomation2> automation;
                    Microsoft::WRL::ComPtr<IUIAutomationElement> root, element;
                    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
                    VARIANT text{};
                    text.vt = VT_BSTR;
                    text.bstrVal = SysAllocString(name);
                    DWORD pid = 0;
                    const bool target =
                        IsWindow(window) && GetWindowThreadProcessId(window, &pid) && pid == originalWorker;
                    const bool ready =
                        target &&
                        SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER,
                                                   IID_PPV_ARGS(automation.GetAddressOf()))) &&
                        SUCCEEDED(automation->put_ConnectionTimeout(2000)) &&
                        SUCCEEDED(automation->put_TransactionTimeout(2000)) &&
                        SUCCEEDED(automation->CreatePropertyCondition(UIA_NamePropertyId, text,
                                                                      condition.GetAddressOf())) &&
                        SUCCEEDED(automation->ElementFromHandle(window, root.GetAddressOf())) &&
                        SUCCEEDED(
                            root->FindFirst(TreeScope_Descendants, condition.Get(), element.GetAddressOf()));
                    // A just-reopened window may not expose its accessibility tree yet. Retry the
                    // read-only lookup within the same 2 s budget; the action is still invoked once.
                    const auto lookupBegan = juce::Time::getMillisecondCounter();
                    while (ready && !element && juce::Time::getMillisecondCounter() - lookupBegan < 2000u)
                    {
                        Sleep(20);
                        root.Reset();
                        if (SUCCEEDED(automation->ElementFromHandle(window, root.GetAddressOf())))
                            root->FindFirst(TreeScope_Descendants, condition.Get(), element.GetAddressOf());
                    }
                    VariantClear(&text);
                    if (!ready && !element)
                        accessibleFailure = target ? "UI Automation lookup failed" : "window is not the original worker's";
                    else if (!element)
                        accessibleFailure = "control not found in the accessibility tree within 2 s";
                    if (ready && element)
                    {
                        BOOL enabled = FALSE;
                        const auto began = juce::Time::getMillisecondCounter();
                        while (SUCCEEDED(element->get_CurrentIsEnabled(&enabled)) && !enabled &&
                               juce::Time::getMillisecondCounter() - began < 2000u)
                            Sleep(10); // Read-only prerequisite; never repeat the mutation.
                        if (!enabled)
                            accessibleFailure = "control stayed disabled for 2 s";
                        if (enabled && invokeControl)
                        {
                            Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> action;
                            success = SUCCEEDED(element->GetCurrentPatternAs(
                                          UIA_InvokePatternId, IID_PPV_ARGS(action.GetAddressOf()))) &&
                                      SUCCEEDED(action->Invoke());
                            if (!success)
                                accessibleFailure = "Invoke pattern failed";
                        }
                        else if (enabled && toggleControl)
                        {
                            Microsoft::WRL::ComPtr<IUIAutomationTogglePattern> action;
                            success = SUCCEEDED(element->GetCurrentPatternAs(
                                          UIA_TogglePatternId, IID_PPV_ARGS(action.GetAddressOf()))) &&
                                      SUCCEEDED(action->Toggle());
                        }
                        else if (enabled)
                        {
                            Microsoft::WRL::ComPtr<IUIAutomationRangeValuePattern> action;
                            success = SUCCEEDED(element->GetCurrentPatternAs(
                                          UIA_RangeValuePatternId, IID_PPV_ARGS(action.GetAddressOf()))) &&
                                      SUCCEEDED(action->SetValue(desired));
                        }
                    }
                } // Release interfaces on their MTA before COM uninitialization.
                CoUninitialize();
            }
            done.store(true, std::memory_order_release);
        });
    const bool completed = await([&] { return done.load(std::memory_order_acquire); });
    worker.join(); // Never detach a provider call borrowing fixture state.
    if (!completed)
        accessibleFailure = "accessibility worker did not finish";
    else if (success.load() && !ownedWindow() && !allowExpectedClose)
        accessibleFailure = "window closed after the action";
    return completed && success.load() &&
           (ownedWindow() || (allowExpectedClose && !IsWindow(window) && host->alive() &&
                              host->processId() == originalWorker));
};
const auto revisionBefore = controller.revision;
if (!accessibleAction(L"Bypass", true, 0) ||
    !await([&] { return controller.project.plugin(pluginId)->bypass && !host->wrapperState().pending; }))
{
    capture("wrapper-bypass-failure");
    auto diagnostic = pluginEditorState(pluginId);
    diagnostic.getDynamicObject()->setProperty("dpi", int(GetDpiForWindow(window)));
    diagnostic.getDynamicObject()->setProperty("style", juce::int64(GetWindowLongPtrW(window, GWL_STYLE)));
    diagnostic.getDynamicObject()->setProperty("bypassed", controller.project.plugin(pluginId)->bypass);
    folder.getChildFile("wrapper-click-failure.json").replaceWithText(juce::JSON::toString(diagnostic, true));
    return fail("Actual wrapper accessibility Bypass reaches canonical document completion");
}
if (controller.revision != revisionBefore + 1)
    return fail("Wrapper bypass creates one history edge");
auto command = [&](const char *id)
{
    auto args = object();
    args.getDynamicObject()->setProperty("command", id);
    return mcpCall("vibera_command", args)["structuredContent"];
};
command("edit.undo");
if (!documentBefore.isEquivalentTo(controller.project.state()))
    return fail("Wrapper bypass exact canonical Undo");
command("edit.redo");
if (!controller.project.plugin(pluginId)->bypass)
    return fail("Wrapper bypass canonical Redo");
command("edit.undo");
const auto wetBefore = controller.project.plugin(pluginId)->wet;
const auto mixRevision = controller.revision;
// Undo changes the document synchronously, while the wrapper still owns its
// previous native ACK. Wait for that real prerequisite before a new UI action.
if (!await([&] { return !pluginWrapperPending && !host->wrapperState().pending; }))
    return fail("Previous wrapper operation drains before independent wet/dry action");
const bool mixInvoked = accessibleAction(L"Effect wet/dry", false, .37);
if (!mixInvoked ||
    !await(
        [&]
        { return controller.project.plugin(pluginId)->wet != wetBefore && !host->wrapperState().pending; }))
{
    auto diagnostic = pluginEditorState(pluginId);
    auto *details = diagnostic.getDynamicObject();
    details->setProperty("accessibilityInvoked", mixInvoked);
    details->setProperty("wetBefore", wetBefore);
    details->setProperty("wetAfter", controller.project.plugin(pluginId)->wet);
    details->setProperty("applicationPending", bool(pluginWrapperPending));
    details->setProperty("status", status);
    folder.getChildFile("wrapper-mix-failure.json").replaceWithText(juce::JSON::toString(diagnostic, true));
    return fail("Actual wrapper accessibility wet/dry reaches canonical effect target");
}
if (controller.revision != mixRevision + 1)
    return fail("Wrapper effect mix one history edge");
command("edit.undo");
if (!documentBefore.isEquivalentTo(controller.project.state()))
    return fail("Wrapper mix exact Undo");
PluginParameterCatalogue catalogue;
if (!host->readParameterCatalogue(catalogue) || catalogue.error)
    return fail("Wrapper native catalogue");
auto selection = object();
selection.getDynamicObject()->setProperty("action", "wrapper");
selection.getDynamicObject()->setProperty("view", "parameters");
selection.getDynamicObject()->setProperty("plugin", juce::int64(pluginId));
selection.getDynamicObject()->setProperty("instance", host->runtimeIdentity());
selection.getDynamicObject()->setProperty("parameter", juce::int64(0));
selection.getDynamicObject()->setProperty("generation", juce::int64(catalogue.generation));
if (!terminal(mcpCall("vibera_plugin", selection)["structuredContent"]))
    return fail("Parameters exact zero-ID selection");
if (!await([&] { return host->wrapperSelection() == (std::uint64_t(catalogue.generation) << 32); }) ||
    !capture("wrapper-parameters"))
    return fail("Parameters actual readback/render");
const auto selectedBefore = host->wrapperSelection();
// The ordinary UI entry point selects this same worker-owned Parameters view;
// it does not instantiate the retained compatibility inspector.
openPluginControlsUI(pluginId);
if (!await([&] { return !host->editorBusy() && pluginEditorState(pluginId)["wrapperView"].toString() == "parameters"; }) ||
    parameterWindow || !ownedWindow() || host->wrapperSelection() != selectedBefore)
    return fail("Ordinary Open Controls reuses actual worker wrapper and current parameter selection");
selection.getDynamicObject()->setProperty("generation", juce::int64(catalogue.generation + 1));
bool staleRefused = false;
try
{
    (void)mcpCall("vibera_plugin", selection);
}
catch (const std::exception &)
{
    staleRefused = true;
}
if (!staleRefused || host->wrapperSelection() != selectedBefore)
    return fail("Wrapper stale selection refused atomically");
selection.getDynamicObject()->setProperty("generation", juce::int64(catalogue.generation));
auto refuseSelection = [&](const char *key, const juce::var &bad)
{
    auto invalid = selection.clone();
    invalid.getDynamicObject()->setProperty(key, bad);
    try
    {
        (void)mcpCall("vibera_plugin", invalid);
    }
    catch (const std::exception &)
    {
        return host->wrapperSelection() == selectedBefore;
    }
    return false;
};
if (!refuseSelection("view", "unknown") || !refuseSelection("parameter", -1) ||
    !refuseSelection("parameter", 4294967296.) || !refuseSelection("parameter", .5) ||
    !refuseSelection("generation", 0) || !refuseSelection("generation", 4294967296.))
    return fail("Wrapper direct canonical view/uint32 validation before any target mutation");
// Exercise actual worker host controls through accessibility and observe their
// retained canonical jobs; these are neither simulated toolbar pointers nor
// admissions mistaken for native completion.
const auto advancedBefore = controller.project.state();
const auto advancedRevision = controller.revision;
if (!terminal(pluginCall("wrapper", "settings")) ||
    !accessibleAction(L"Native parameter notifications", true, 0) ||
    !await([&] { return !host->parameterNotificationsEnabled() && !pluginWrapperPending && !host->wrapperState().pending; }) ||
    !accessibleAction(L"Native parameter notifications", true, 0) ||
    !await([&] { return host->parameterNotificationsEnabled() && !pluginWrapperPending && !host->wrapperState().pending; }) ||
    controller.revision != advancedRevision || !advancedBefore.isEquivalentTo(controller.project.state()))
    return fail("Wrapper session notifications use acknowledged state without document/history mutation");
if (!terminal(mcpCall("vibera_plugin", selection)["structuredContent"]))
    return fail("Return to exact parameter after session controls");
auto wrapperControl = [&](const wchar_t *caption, const juce::String &kind, bool allowExpectedClose = false)
{
    juce::String previous, token;
    for (auto it = pluginOperations.rbegin(); it != pluginOperations.rend(); ++it)
        if (it->plugin == pluginId && it->kind == kind) { previous = it->id; break; }
    if (!accessibleAction(caption, false, 0, true, allowExpectedClose))
        return juce::var{};
    if (!await([&]
        {
            if (token.isEmpty())
                for (auto it = pluginOperations.rbegin(); it != pluginOperations.rend(); ++it)
                    if (it->plugin == pluginId && it->kind == kind && it->id != previous)
                    { token = it->id; break; }
            const auto job = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                [&](const auto &entry) { return entry.id == token; });
            return job != pluginOperations.end() && job->state != "pending" && !pluginWrapperPending &&
                   !host->wrapperState().pending;
        }))
        return juce::var{};
    auto args = object();
    args.getDynamicObject()->setProperty("action", "job");
    args.getDynamicObject()->setProperty("operation", token);
    return mcpCall("vibera_plugin", args)["structuredContent"];
};
const auto refreshed = wrapperControl(L"Refresh parameters", "refresh");
if (refreshed["completionState"].toString() != "completed" ||
    std::uint32_t(juce::int64(refreshed["catalogueSerial"])) <= catalogue.serial ||
    controller.revision != advancedRevision || !advancedBefore.isEquivalentTo(controller.project.state()))
    return fail("Wrapper refresh waits for actual fresh native catalogue and leaves project unchanged");
const auto refusedInit = wrapperControl(L"Remove init", "automate");
if (refusedInit["completionState"].toString() != "failed" ||
    refusedInit["error"]["code"].toString() != "validation_failed" ||
    controller.revision != advancedRevision || !advancedBefore.isEquivalentTo(controller.project.state()))
    return fail("Wrapper missing initialization is a real asynchronous validation failure without mutation");
const auto initialized = wrapperControl(L"Initialize", "automate");
if (initialized["completionState"].toString() != "completed" ||
    controller.project.automation.initialValues.size() != 1 || controller.revision != advancedRevision + 1)
    return fail("Wrapper Initialize creates the exact native binding and one history edge");
const auto initializedProject = controller.project.state();
const auto removedInit = wrapperControl(L"Remove init", "automate");
if (removedInit["completionState"].toString() != "completed" ||
    !controller.project.automation.initialValues.empty())
    return fail("Wrapper Remove init updates the current native binding");
command("edit.undo");
if (!initializedProject.isEquivalentTo(controller.project.state()))
    return fail("Wrapper Remove init exact one-step Undo");
command("edit.undo");
if (!advancedBefore.isEquivalentTo(controller.project.state()))
    return fail("Wrapper Initialize exact one-step Undo");
const auto clipCount = controller.project.automation.placements.size();
const auto created = wrapperControl(L"Create automation clip", "automate");
if (created["completionState"].toString() != "completed" ||
    controller.project.automation.placements.size() != clipCount + 1)
    return fail("Wrapper Create automation clip produces a real Playlist automation placement");
command("edit.undo");
if (!advancedBefore.isEquivalentTo(controller.project.state()) || !ownedWindow() ||
    audio.parameterHost(pluginId) != host || !audio.playing())
    return fail("Wrapper automation Undo preserves exact project, same native wrapper, worker and playback");
if (!await([&] { return !pluginWrapperPending && !host->wrapperState().pending; }) ||
    !capture("wrapper-advanced-parameters"))
    return fail("Capture actual advanced wrapper controls");

const auto scalarRevision = controller.revision;
if (!accessibleAction(L"Parameter value", false, .47) ||
    !await([&] { return controller.revision > scalarRevision && !pluginWrapperPending && !nativeScalar; },
           15000u))
    return fail("Actual wrapper accessibility parameter reaches real ACK and fresh-state history");
const auto scalarJob = std::find_if(pluginOperations.rbegin(), pluginOperations.rend(),
                                    [](const auto &job) { return job.kind == "set"; });
if (scalarJob == pluginOperations.rend() || scalarJob->state != "completed" || !scalarJob->nativeApplied ||
    !scalarJob->historyCommitted || scalarJob->parameter != 0 || controller.revision != scalarRevision + 1)
    return fail("Wrapper parameter actual terminal native/history receipt");

// The semantic gesture uses the same owner as the wrapper slider. An ACK for
// an earlier desired value is aggregate evidence, not completion of a new value.
const auto gestureBefore = controller.project;
const auto gestureRevision = controller.revision;
auto scalar = object();
scalar.getDynamicObject()->setProperty("action", "set");
scalar.getDynamicObject()->setProperty("plugin", juce::int64(pluginId));
scalar.getDynamicObject()->setProperty("instance", host->runtimeIdentity());
scalar.getDynamicObject()->setProperty("parameter", juce::int64(0));
scalar.getDynamicObject()->setProperty("generation", juce::int64(catalogue.generation));
scalar.getDynamicObject()->setProperty("value", .61);
scalar.getDynamicObject()->setProperty("gestureOpen", true);
auto admitted = mcpCall("vibera_plugin", scalar)["structuredContent"];
const auto gestureToken = admitted["operation"].toString();
auto gestureJob = [&]
{
    auto query = object();
    query.getDynamicObject()->setProperty("action", "job");
    query.getDynamicObject()->setProperty("operation", gestureToken);
    return mcpCall("vibera_plugin", query)["structuredContent"];
};
if (gestureToken.isEmpty() || admitted["completionState"].toString() != "pending" ||
    bool(admitted["nativeApplied"]) || !await([&] { return bool(gestureJob()["nativeApplied"]); }))
    return fail("Semantic open gesture has actual first native ACK and remains owned");
scalar.getDynamicObject()->setProperty("operation", gestureToken);
auto refuseGesture = [&](const char *key, const juce::var &bad)
{
    auto invalid = scalar.clone();
    invalid.getDynamicObject()->setProperty(key, bad);
    const auto queued = nativeScalar ? nativeScalar->queued : -1.f;
    try
    {
        (void)mcpCall("vibera_plugin", invalid);
    }
    catch (const std::exception &)
    {
        return nativeScalar && nativeScalar->operation == gestureToken && nativeScalar->queued == queued &&
               controller.revision == gestureRevision;
    }
    return false;
};
if (!refuseGesture("operation", "foreign-token") || !refuseGesture("parameter", juce::int64(99)) ||
    !refuseGesture("generation", juce::int64(catalogue.generation + 1)) ||
    !refuseGesture("instance", "foreign-runtime"))
    return fail("Semantic gesture refuses changed owner/target/generation without mutation");
for (const double desired : {.68, .73, .79})
{
    scalar.getDynamicObject()->setProperty("value", desired);
    admitted = mcpCall("vibera_plugin", scalar)["structuredContent"];
    if (admitted["operation"].toString() != gestureToken ||
        admitted["completionState"].toString() != "pending" || !bool(admitted["nativeApplied"]) ||
        !bool(admitted["nativeDispatched"]) || bool(admitted["historyCommitted"]) ||
        controller.revision != gestureRevision)
        return fail("Coalesced gesture reports exact aggregate ACK while history remains pending");
}
scalar.getDynamicObject()->setProperty("gestureOpen", false);
admitted = mcpCall("vibera_plugin", scalar)["structuredContent"];
bool closedRefused = false;
try
{
    (void)mcpCall("vibera_plugin", scalar);
}
catch (const std::exception &)
{
    closedRefused = true;
}
if (!closedRefused || !terminal(admitted))
    return fail("Closed gesture cannot reopen before actual capture completion");
const auto completed = gestureJob();
if (!bool(completed["nativeApplied"]) || !bool(completed["historyCommitted"]) ||
    std::abs(double(completed["after"]) - .79) > .0001 || controller.revision != gestureRevision + 1)
    return fail("Continuous gesture ends at last desired value with exactly one committed history edge");
closedRefused = false;
try
{
    (void)mcpCall("vibera_plugin", scalar);
}
catch (const std::exception &)
{
    closedRefused = true;
}
if (!closedRefused)
    return fail("Terminal gesture token cannot reopen or create another edit");
if (controlsOnly)
{
    if (!terminal(pluginCall("wrapper", "plugin"), "failed") || !ownedWindow() ||
        pluginEditorState(pluginId)["availability"].toString() != "controls_only" ||
        pluginEditorState(pluginId)["wrapperView"].toString() != "parameters")
        return fail("Native Open refusal retains the actual editorless Parameters window");
}
else if (!terminal(pluginCall("wrapper", "plugin")) ||
         GetAncestor(test::gestureFixtureWindow(originalWorker), GA_ROOT) != window)
    return fail("Plugin view restores same actual native editor without duplicates");
if (!audio.playing() || host->processId() != originalWorker ||
    audio.projectionDiagnostics().replacements != originalProjection.replacements)
    return fail("Wrapper navigation/ordinary bypass retains transport/worker/graph");
// Exercise the real worker producer before structural history replaces its Host.
// Copy deliberately closes this editor; it must still target its containing
// chain rather than the unrelated strip selected in the main application.
{
    int containingInsert = -1, containingSlot = -1;
    for (const auto &insert : controller.project.mixer)
        for (size_t i = 0; i < insert.effects.size(); ++i)
            if (insert.effects[i].id == pluginId)
            {
                containingInsert = insert.index;
                containingSlot = int(i);
            }
    const auto other = std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                                    [&](const auto &insert) { return insert.index != containingInsert; });
    if (containingInsert < 0 || other == controller.project.mixer.end() ||
        !terminal(pluginCall("wrapper", "settings")) ||
        !await([&] { return !pluginWrapperPending && !host->wrapperState().pending; }))
        return fail("Actual Settings containing-chain source and independent selection prerequisite");
    const int independentInsert = other->index;
    const juce::ScopedValueSetter<int> independentSelection(selectedInsert, independentInsert);
    const auto sourceState = controller.project.state();
    const auto sourceProject = controller.projectIdentity;
    const auto sourceRevision = controller.revision;
    const auto sourceDirty = controller.dirty;
    const auto sourceUndo = controller.undo.getUndoDescriptions();
    const auto sourceRedo = controller.undo.getRedoDescriptions();
    const auto sourceActions = controller.undo.getNumActionsInCurrentTransaction();
    const auto sourceProjection = audio.projectionDiagnostics();
    const auto sourceInstance = host->runtimeIdentity();
    const auto sourceReset = host->midiResetGeneration();
    const auto sourceNativeRevision = host->parameterRevision();
    auto unchanged = [&]
    {
        const auto current = audio.projectionDiagnostics();
        return sourceState.isEquivalentTo(controller.project.state()) &&
               controller.projectIdentity == sourceProject && controller.revision == sourceRevision &&
               controller.dirty == sourceDirty && controller.undo.getUndoDescriptions() == sourceUndo &&
               controller.undo.getRedoDescriptions() == sourceRedo &&
               controller.undo.getNumActionsInCurrentTransaction() == sourceActions &&
               selectedInsert == independentInsert && audio.playing() && current.headPlaying &&
               audio.parameterHost(pluginId) == host && host->alive() &&
               host->runtimeIdentity() == sourceInstance && host->processId() == originalWorker &&
               host->midiResetGeneration() == sourceReset &&
               host->parameterRevision() == sourceNativeRevision && current.edit == sourceProjection.edit &&
               current.playbackContext == sourceProjection.playbackContext &&
               current.replacements == sourceProjection.replacements &&
               current.samplerCreations == sourceProjection.samplerCreations &&
               current.pluginDeviceEpoch == sourceProjection.pluginDeviceEpoch &&
               manager.deviceManager.getCurrentAudioDevice() == device;
    };
    const auto closedWindow = window;
    const auto copied = wrapperControl(L"Copy chain", "chain", true);
    auto evidence = object();
    evidence.getDynamicObject()->setProperty("copy", copied);
    evidence.getDynamicObject()->setProperty("containingInsert", containingInsert);
    evidence.getDynamicObject()->setProperty("selectedInsert", selectedInsert);
    evidence.getDynamicObject()->setProperty("globalNativeInput", false);
    const auto evidenceFile =
        folder.getChildFile(juce::String(pattern ? "pattern-" : "song-") + "wrapper-chain-ui.json");
    if (!evidenceFile.replaceWithText(juce::JSON::toString(evidence, true)))
        return fail("Retain actual worker Copy-chain outcome before assertions");
    bool ownClose = false, nativeCapture = false;
    if (const auto *closures = copied["editorClosures"].getArray())
        for (const auto &close : *closures)
            if (juce::int64(close["plugin"]) == pluginId && close["instance"].toString() == sourceInstance &&
                close["outcome"].toString() == "closed" && bool(close["nativeCompletionKnown"]) &&
                bool(close["dispatched"]) && !bool(close["windowExists"]))
                ownClose = true;
    if (const auto *provenance = copied["structuralOperation"]["provenance"].getArray())
        for (const auto &entry : *provenance)
            if (juce::int64(entry["plugin"]) == pluginId && entry["instance"].toString() == sourceInstance &&
                entry["stateSource"].toString() == "nativeAcknowledgement" &&
                juce::int64(entry["revision"]) == sourceNativeRevision)
                nativeCapture = true;
    if (copied["completionState"].toString() != "completed" || copied["kind"].toString() != "chain" ||
        copied["chainAction"].toString() != "copy" || int(copied["mixer"]) != containingInsert ||
        int(copied["slot"]) != containingSlot || copied["canonicalOperation"].toString().isEmpty() ||
        copied["structuralOperation"]["completionState"].toString() != "completed" ||
        bool(copied["documentChanged"]) || bool(copied["historyCommitted"]) || bool(copied["undoable"]) ||
        !ownClose || !nativeCapture || IsWindow(closedWindow) || host->editorWindowState().exists ||
        int(mixerChainReadback()["state"]["clipboard"]["sourceInsert"]) != containingInsert || !unchanged())
        return fail("Actual worker Copy chain closes owned editor, captures current native state and "
                    "preserves independent selection/history/transport/Host");

    // The same worker creates one new Settings peer after the acknowledged close.
    if (!terminal(pluginCall("wrapper", "settings")))
        return fail("Reopen actual Settings after containing-chain Copy");
    struct FindSettings
    {
        DWORD pid;
        HWND found = nullptr;
        unsigned count = 0;
    } settings{originalWorker};
    EnumWindows(
        [](HWND candidate, LPARAM context) -> BOOL
        {
            auto &target = *reinterpret_cast<FindSettings *>(context);
            DWORD pid = 0;
            GetWindowThreadProcessId(candidate, &pid);
            wchar_t title[256]{};
            if (pid == target.pid && IsWindowVisible(candidate) && !GetParent(candidate) &&
                GetWindowTextW(candidate, title, 256) && juce::String(title).startsWith("VibeRa | "))
            {
                target.found = candidate;
                ++target.count;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&settings));
    window = settings.found;
    const auto nativeChild = test::gestureFixtureWindow(originalWorker);
    if (settings.count != 1 || !ownedWindow() ||
        (controlsOnly ? nativeChild != nullptr
                      : !nativeChild || GetAncestor(nativeChild, GA_ROOT) != window) ||
        pluginEditorState(pluginId)["wrapperView"].toString() != "settings" || !unchanged())
        return fail("Copy reopens one actual same-Host Settings peer with truthful native capability");

    const auto directoryBefore = mixerChainReadback()["state"]["directory"].toString();
    const auto clipboardBefore = juce::JSON::toString(mixerChainReadback()["state"]["clipboard"]);
    if (!accessibleAction(L"Chain directory", false, 0, true))
    {
        capture("chain-directory-failure");
        return fail("Actual worker Chain directory button invokes retained interactive parent (" +
                    juce::String(accessibleFailure) + ")");
    }
    juce::String choosing;
    if (!await(
            [&]
            {
                for (const auto &job : pluginOperations)
                    if (job.plugin == pluginId && job.kind == "chain" && job.action == "configureDirectory" &&
                        job.state == "pending" && job.choosing && job.mixer == containingInsert &&
                        job.slot == containingSlot && job.childOperation.isEmpty() &&
                        pluginChainUiOperation == job.id && chooser)
                    {
                        choosing = job.id;
                        return true;
                    }
                return false;
            }))
        return fail("Chain directory retains real choosing/chooser ownership before cancellation");
    auto cancelChoice = object();
    cancelChoice.getDynamicObject()->setProperty("action", "cancel");
    cancelChoice.getDynamicObject()->setProperty("operation", choosing);
    const auto cancellation = mcpCall("vibera_plugin", cancelChoice)["structuredContent"];
    cancelChoice.getDynamicObject()->setProperty("action", "job");
    juce::var cancelled;
    if (!await(
            [&]
            {
                cancelled = mcpCall("vibera_plugin", cancelChoice)["structuredContent"];
                return cancelled["completionState"].toString() != "pending" && !pluginWrapperPending &&
                       !host->wrapperState().pending;
            }))
        return fail("Actual interactive directory cancellation drains parent and wrapper ownership");
    evidence.getDynamicObject()->setProperty("directoryCancellation", cancellation);
    evidence.getDynamicObject()->setProperty("directoryTerminal", cancelled);
    if (!evidenceFile.replaceWithText(juce::JSON::toString(evidence, true)))
        return fail("Retain actual interactive directory cancellation receipt");
    if (cancelled["completionState"].toString() != "cancelled" || cancelled["kind"].toString() != "chain" ||
        cancelled["chainAction"].toString() != "configureDirectory" ||
        cancelled["canonicalOperation"].toString().isNotEmpty() || bool(cancelled["nativeDispatched"]) ||
        bool(cancelled["documentChanged"]) || bool(cancelled["historyCommitted"]) ||
        bool(cancelled["undoable"]) || chooser || pluginChainConfirmation ||
        pluginChainUiOperation.isNotEmpty() || !ownedWindow() ||
        mixerChainReadback()["state"]["directory"].toString() != directoryBefore ||
        juce::JSON::toString(mixerChainReadback()["state"]["clipboard"]) != clipboardBefore || !unchanged())
        return fail("Directory Cancel acknowledges closed chooser without "
                    "file/clipboard/project/history/worker changes");
}
if (!terminal(pluginCall("closeEditor")) || host->editorWindowState().exists)
    return fail("Wrapper actual close acknowledgement");

// Native state restoration is structural and may replace the worker. Its
// original-window assertions above deliberately finish before history begins.
const auto gestureAfter = controller.project;
auto history = [&](const char *id)
{
    const auto admission = command(id);
    const auto operation = lastMixerChainHistoryOperation;
    auto diagnostic = object();
    diagnostic.getDynamicObject()->setProperty("command", id);
    diagnostic.getDynamicObject()->setProperty("admission", admission);
    diagnostic.getDynamicObject()->setProperty("operation", operation);
    if (operation.isEmpty() ||
        !await([&] { return mixerChainReadback(operation)["completionState"].toString() != "pending"; },
               15000u))
        return false;
    const auto state = mixerChainReadback(operation);
    diagnostic.getDynamicObject()->setProperty("terminal", state);
    folder.getChildFile(juce::String(pattern ? "pattern-" : "song-") + id + "-history.json")
        .replaceWithText(juce::JSON::toString(diagnostic, true));
    const auto current = audio.parameterHost(pluginId);
    return state["completionState"].toString() == "completed" && state["audioState"].toString() == "active" &&
           current && current->alive();
};
if (!history("edit.undo"))
    return fail("Continuous gesture Undo actual native restoration terminal");
auto sameMusicalModel = [&](const Project &expected)
{
    auto actual = controller.project, model = expected;
    actual.plugin(pluginId)->state.clear();
    model.plugin(pluginId)->state.clear();
    return actual.state().isEquivalentTo(model.state());
};
auto stateXml = [&](const Project &project)
{
    juce::MemoryBlock bytes;
    return decodePluginState(project.plugin(pluginId)->state, bytes)
               ? juce::AudioProcessor::getXmlFromBinary(bytes.getData(), int(bytes.getSize()))
               : std::unique_ptr<juce::XmlElement>{};
};
auto scalarReadback = [&](double expected)
{
    PluginParameterCatalogue observed;
    const auto current = audio.parameterHost(pluginId);
    if (!current || !current->readParameterCatalogue(observed) || observed.error)
        return false;
    const auto parameter = std::find_if(observed.parameters.begin(), observed.parameters.end(),
                                        [](const auto &item) { return item.id == 0; });
    return parameter != observed.parameters.end() && (parameter->availability & 1u) &&
           std::abs(parameter->value - expected) < .000001;
};
const auto scalarBefore = double(completed["before"]);
if (!sameMusicalModel(gestureBefore) || !scalarReadback(scalarBefore))
    return fail("Continuous gesture Undo restores actual scalar and exact unrelated musical model");
auto freshAfterXml = stateXml(gestureAfter), undoXml = stateXml(controller.project);
if (!freshAfterXml || !undoXml)
    return fail("Continuous gesture state envelopes decode");
// Independent oracle: current opaque chunks remain byte-identical. Only the
// selected scalar overlay changes; restoring historical native chunks is unsafe.
auto *afterValues = freshAfterXml->getChildByName("VibeRaParameterValues");
auto *undoValues = undoXml->getChildByName("VibeRaParameterValues");
if (!afterValues || !undoValues)
    return fail("Continuous gesture captured scalar overlays");
int afterTargets = 0, undoTargets = 0;
for (auto *values : {afterValues, undoValues})
    for (auto *entry = values->getFirstChildElement(); entry; entry = entry->getNextElement())
        if (entry->getIntAttribute("id", -1) == 0)
        {
            const auto expected = values == afterValues ? .79 : scalarBefore;
            if (std::abs(entry->getDoubleAttribute("value", -1) - expected) > .000001)
                return fail("Continuous gesture exact owned overlay values");
            (values == afterValues ? afterTargets : undoTargets)++;
            entry->setAttribute("value", 0.); // Normalize only the witnessed owned scalar.
        }
if (afterTargets != 1 || undoTargets != 1 || freshAfterXml->toString() != undoXml->toString())
    return fail("Undo preserves fresh opaque chunks and every unrelated scalar state field");
if (!history("edit.redo") || !sameMusicalModel(gestureAfter) || !scalarReadback(.79))
    return fail("Continuous gesture Redo restores actual scalar and exact unrelated musical model");
audio.stop();
const auto saved = folder.getChildFile(pattern ? "parameter-pattern.vibra" : "parameter-song.vibra");
auto saveArgs = object();
saveArgs.getDynamicObject()->setProperty("action", "save");
saveArgs.getDynamicObject()->setProperty("path", saved.getFullPathName());
saveArgs.getDynamicObject()->setProperty("overwrite", false);
auto saveJob = mcpCall("vibera_project", saveArgs)["structuredContent"];
const auto saveToken = saveJob["operation"].toString();
if (!await(
        [&]
        {
            if (saveJob["completionState"].toString() != "pending")
                return true;
            auto query = object();
            query.getDynamicObject()->setProperty("action", "job");
            query.getDynamicObject()->setProperty("operation", saveToken);
            saveJob = mcpCall("vibera_project", query)["structuredContent"];
            return saveJob["completionState"].toString() != "pending";
        },
        15000u) ||
    saveJob["completionState"].toString() != "completed" || !saved.existsAsFile())
    return fail("Wrapper edits persist only after the actual canonical save completes");
Project reopened;
if (loadProject(saved, reopened).failed() || !sameMusicalModel(reopened) || !reopened.plugin(pluginId) ||
    reopened.plugin(pluginId)->state.isEmpty())
    return fail("Wrapper saved project round trip retains musical data and current plugin state");
report += juce::String(pattern ? "Pattern" : "Song") +
          (controlsOnly ? ": actual original editorless host-control peer; no native attachment claimed; "
                        : ": actual original native editor in one worker wrapper; ") +
          "Plugin/Settings/Parameters typed async navigation; zero-ID/stale selection; owned-wrapper "
          "accessibility Bypass; "
          "effect mix and parameter actual ACK/history; actual refresh/notifications/automation/init/remove-init, failure and exact history; continuous semantic coalescing/owner "
          "refusal/terminal single history; "
          "exact bypass and scalar Undo/Redo; canonical save/round trip; transport/worker/graph retained; "
          "actual Settings Copy chain with independent selection/native close+capture; interactive directory Cancel drained; "
          "actual close; callbacks=" +
          juce::String(wrapperCallbacks) + "; maxMs=" + juce::String(wrapperMaximumMs, 4) +
          "; globalNativeInput=false; listening=false PASS.\n";
