// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "WindowChrome.h"
#include "McpServer.h"
#include "VibeRaVersion.h"
#include "NativeMixerMeterDriver.h"
#include "BuildAcceptanceOutput.h"
#include "../tests/InstalledStudioEffectTransactionAcceptance.h"
#include <juce_cryptography/juce_cryptography.h>
class VibeRaApplication final : public juce::JUCEApplication
{
    class Window final : public juce::DocumentWindow, private juce::Timer
    {
        vibera::WindowChrome chrome;
        bool nativeStyleRequested = false;
        void timerCallback() override
        {
            if (!nativeStyleRequested && getPeer())
            {
                vibera::roundNativeWindow(*this);
                nativeStyleRequested = true;
            }
            if (auto *studio = dynamic_cast<vibera::Studio *>(getContentComponent()))
                setName("VibeRa Studio  |  " + studio->controller.project.name +
                        (studio->controller.dirty ? " *" : ""));
        }

      public:
        explicit Window(bool smoke = false, int explicitProviderFrames = 0)
            : DocumentWindow("VibeRa Studio", vibera::themeColour(0xff111923), allButtons)
        {
            setLookAndFeel(&chrome);
            setUsingNativeTitleBar(false);
            setTitleBarHeight(36);
            setContentOwned(new vibera::Studio(!smoke, explicitProviderFrames), true);
            setResizable(true, true);
            setResizeLimits(1152, 640, 7680, 4320);
            centreWithSize(1600, 940);
            timerCallback();
            startTimer(250);
            setVisible(!smoke);
            if (!smoke)
                getContentComponent()->grabKeyboardFocus();
        }
        ~Window() override
        {
            stopTimer();
            setLookAndFeel(nullptr);
        }
        void activeWindowStatusChanged() override
        {
            if (!isActiveWindow())
                if (auto *studio = dynamic_cast<vibera::Studio *>(getContentComponent()))
                    studio->releaseTypingNotes();
        }
        void closeButtonPressed() override
        {
            if (auto *studio = dynamic_cast<vibera::Studio *>(getContentComponent()))
                studio->requestClose([] { juce::JUCEApplication::getInstance()->quit(); });
        }
    };
    std::unique_ptr<Window> window;
    std::unique_ptr<vibera::McpServer> mcp;
    std::unique_ptr<vibera::test::NativeMeterStorage> nativeMeterStorage;
    std::unique_ptr<vibera::test::NativeMixerMeterDriver> nativeMeterDriver;
    juce::File nativeMeterOutput;

  public:
    const juce::String getApplicationName() override
    {
        return "VibeRa Studio";
    }
    const juce::String getApplicationVersion() override
    {
        return vibera::applicationVersion;
    }
    void initialise(const juce::String &) override
    {
        auto args = getCommandLineParameterArray();
        // Windowless stdio relay for local agent CLIs: `--mcp-bridge <port> <token>`.
        if (args.size() == 3 && args[0] == "--mcp-bridge")
        {
            setApplicationReturnValue(vibera::ai::runMcpBridge(args[1].getIntValue(), args[2]));
            quit();
            return;
        }
        const bool effectOpeningRequested =
            std::any_of(args.begin(), args.end(),
                        [](const auto &argument)
                        {
                            return argument.startsWith("--mixer-effects-opening") ||
                                   argument.startsWith("--plugin-save-capture") ||
                                   argument.startsWith("--plugin-main-io");
                        });
        if (effectOpeningRequested)
        {
            const bool original =
                args.size() == 3 && (args[0] == "--mixer-effects-opening-check" ||
                                     args[0] == "--mixer-effects-opening-unsupported-check");
            const bool provider = args.size() == 3 && args[0] == "--mixer-effects-opening-provider-check";
            const bool saveCapture = args.size() == 3 && args[0] == "--plugin-save-capture-check";
            const bool pluginIo = args.size() == 3 && args[0] == "--plugin-main-io-check";
            juce::File attempt;
            const auto admission =
                (original || provider || saveCapture || pluginIo) && juce::File::isAbsolutePath(args[2]) &&
                        juce::File(args[2]).exists()
                    ? vibera::test::prepareBuildAcceptanceAttempt(args[1], attempt)
                    : juce::Result::fail("Effect opening requires exact flag, owned build output and "
                                         "absolute existing VST3");
            if (admission.failed())
            {
                juce::Logger::writeToLog(admission.getErrorMessage());
                setApplicationReturnValue(1);
                quit();
                return;
            }
            const auto receipt = attempt.getChildFile("verification.json");
            if (!receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"))
            {
                setApplicationReturnValue(1);
                quit();
                return;
            }
            try
            {
                nativeMeterStorage = std::make_unique<vibera::test::NativeMeterStorage>(
                    attempt.getChildFile("engine-storage"));
                window = std::make_unique<Window>(true, 256); // This owned provider dispatches fixed256.
                auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                if (!studio || !studio->controllerTestMode || !studio->pluginTestSettings ||
                    !studio->workflowTestSettings)
                    throw std::runtime_error("Effect opening requires isolated Studio(false)");
                auto &manager = studio->audio.engine.getDeviceManager();
                if (manager.deviceManager.getCurrentAudioDevice())
                    throw std::runtime_error("Effect opening refuses a pre-existing device");
                if (!studio->audio.engine.getPropertyStorage().getAppPrefsFolder().isAChildOf(
                        nativeMeterStorage->directory) ||
                    !studio->audio.engine.getPropertyStorage().getAppCacheFolder().isAChildOf(
                        nativeMeterStorage->directory))
                    throw std::runtime_error("Effect opening requires per-attempt engine storage");
                std::vector<juce::AudioIODeviceType *> physical;
                for (auto *type : manager.deviceManager.getAvailableDeviceTypes())
                    if (type->getTypeName() != "VibeRa synthetic")
                        physical.push_back(type);
                for (auto *type : physical)
                    manager.deviceManager.removeAudioDeviceType(type);
                studio->audio.engine.getPropertyStorage().setProperty(
                    vibera::te::SettingID::midiScanIntervalSeconds, 0);
                (void)manager.getHostedAudioDeviceInterface();
                studio->getProperties().set(saveCapture ? "viberaPluginSaveCaptureFixture" : "viberaMixerFxOpeningFixture", true);
                studio->getProperties().set(saveCapture ? "viberaPluginSaveCaptureOutput" : "viberaMixerFxOpeningOutput", attempt.getFullPathName());
                window->setVisible(!saveCapture); // Capture acceptance opens no editor or user window.
                juce::MessageManager::callAsync(
                    [this, args, attempt, receipt, original, saveCapture, pluginIo]
                    {
                        auto result = juce::Result::fail("Effect opening lost its owned Studio");
                        try
                        {
                            if (auto *owned = dynamic_cast<vibera::Studio *>(window->getContentComponent()))
                                result = pluginIo ? owned->verifyPluginIo(attempt, juce::File(args[2]))
                                         : saveCapture
                                             ? owned->verifyPluginSaveCapture(attempt, juce::File(args[2]))
                                             : owned->verifyMixerEffectOpening(
                                                   attempt, juce::File(args[2]), original,
                                                   args[0] == "--mixer-effects-opening-unsupported-check");
                        }
                        catch (const std::exception &error)
                        {
                            result = juce::Result::fail(error.what());
                        }
                        auto report = std::make_unique<juce::DynamicObject>();
                        report->setProperty("passed", result.wasOk());
                        report->setProperty("completionState", result.wasOk() ? "completed" : "failed");
                        report->setProperty("message", result.getErrorMessage());
                        report->setProperty(
                            "scope", pluginIo ? "Actual component menu/MCP/SDK negotiation/history/files and "
                                                "nonzero offline WAV; no live audio, OS input, hardware or listening"
                                     : saveCapture ? "Actual Studio/MCP close admission and original SDK "
                                                     "state; no OS input, audio, hardware or listening"
                                                   : "Actual component/native plugin editor and virtual PCM; "
                                                     "no OS input, hardware or listening");
                        const bool recorded =
                            receipt.replaceWithText(juce::JSON::toString(juce::var(report.release())));
                        if (result.failed())
                            juce::Logger::writeToLog(result.getErrorMessage());
                        setApplicationReturnValue(result.wasOk() && recorded ? 0 : 1);
                        quit();
                    });
            }
            catch (const std::exception &error)
            {
                receipt.replaceWithText("{\"passed\":false,\"phase\":\"setup-failed\"}\n");
                juce::Logger::writeToLog(error.what());
                setApplicationReturnValue(1);
                quit();
            }
            return;
        }
        const bool effectSlotsRequested = std::any_of(args.begin(), args.end(), [](const auto &argument)
                                                      { return argument.startsWith("--mixer-effects-ui"); });
        if (effectSlotsRequested)
        {
            juce::File attempt;
            const auto admission =
                args.size() == 2 && args[0] == "--mixer-effects-ui-check"
                    ? vibera::test::prepareBuildAcceptanceAttempt(args[1], attempt)
                    : juce::Result::fail(
                          "Effects component acceptance requires exact flag and owned build output");
            if (admission.failed())
            {
                juce::Logger::writeToLog(admission.getErrorMessage());
                setApplicationReturnValue(1);
                quit();
                return;
            }
            const auto receipt = attempt.getChildFile("verification.json");
            if (!receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"))
            {
                setApplicationReturnValue(1);
                quit();
                return;
            }
            window = std::make_unique<Window>(true); // Isolated Studio, no hardware initialization.
            juce::MessageManager::callAsync(
                [this, attempt, receipt]
                {
                    auto result = juce::Result::fail("Missing isolated effects acceptance view");
                    try
                    {
                        if (auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent()))
                            result = studio->verifyMixerEffectSlots(attempt);
                    }
                    catch (const std::exception &error)
                    {
                        result = juce::Result::fail(error.what());
                    }
                    auto output = std::make_unique<juce::DynamicObject>();
                    output->setProperty("passed", result.wasOk());
                    output->setProperty("completionState", result.wasOk() ? "completed" : "failed");
                    output->setProperty("message", result.getErrorMessage());
                    output->setProperty("scope", "Actual component and shared semantic handlers; no OS "
                                                 "input, provider audio, hardware or listening");
                    const bool recorded =
                        receipt.replaceWithText(juce::JSON::toString(juce::var(output.release())));
                    if (result.failed())
                        juce::Logger::writeToLog(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() && recorded ? 0 : 1);
                    quit();
                });
            return;
        }
        // Recognize reserved fixture prefixes before any Window/Studio or device
        // construction. Malformed known requests MUST NOT fall back to hardware.
        const bool recoveryRequested = std::any_of(
            args.begin(), args.end(),
            [](const auto &arg)
            {
                return arg.startsWith("--native-scalar-history") || arg.startsWith("--automation-touch") || arg.startsWith("--midi-take-recovery") || arg.startsWith("--history-ui-recovery") ||
                       arg.startsWith("--studio-channel-routing") ||
                       arg.startsWith("--studio-effect-transaction") ||
                       arg.startsWith("--mixer-targets-ui") || arg.startsWith("--mixer-insert-order-ui") ||
                       arg.startsWith("--mixer-stereo-ui") || arg.startsWith("--mixer-automation-ui") ||
                       arg.startsWith("--mixer-organisation-ui") || arg.startsWith("--mixer-chain-workflow");
            });
        if (recoveryRequested)
        {
            const bool mixerTargets = args.size() == 2 && args[0] == "--mixer-targets-ui-check";
            const bool mixerOrder = args.size() == 2 && args[0] == "--mixer-insert-order-ui-check";
            const bool mixerStereo = args.size() == 2 && args[0] == "--mixer-stereo-ui-check";
            const bool mixerAutomation = args.size() == 2 && args[0] == "--mixer-automation-ui-check";
            const bool automationTouch = args.size() == 2 && args[0] == "--automation-touch-check";
            const bool nativeScalarInstalled = args.size() == 3 && args[0] == "--native-scalar-history-installed-check";
            const bool nativeScalar = args.size() == 3 &&
                                      (args[0] == "--native-scalar-history-check" || nativeScalarInstalled) &&
                                      juce::File::isAbsolutePath(args[2]) &&
                                      (nativeScalarInstalled ? juce::File(args[2]).exists()
                                                             : juce::File(args[2]).existsAsFile());
            const bool mixerOrganisation = args.size() == 2 && args[0] == "--mixer-organisation-ui-check";
            const bool mixerChain = args.size() == 3 && args[0] == "--mixer-chain-workflow" &&
                                    juce::File::isAbsolutePath(args[2]) && juce::File(args[2]).existsAsFile();
            const bool midiRecovery = args.size() == 2 && args[0] == "--midi-take-recovery-ui-check";
            const bool historyRecovery = args.size() == 2 && args[0] == "--history-ui-recovery-check";
            const bool sameConfigRouting = args.size() == 3 &&
                                           args[0] == "--studio-channel-routing-same-config-check";
            const bool studioRouting =
                (args.size() == 2 || args.size() == 3 ||
                 (args.size() == 4 && args[0] == "--studio-channel-routing-check")) &&
                (args[0] == "--studio-channel-routing-check" || sameConfigRouting) &&
                (args.size() == 2 ||
                 (juce::File::isAbsolutePath(args[2]) && juce::File(args[2]).existsAsFile())) &&
                (args.size() != 4 ||
                 (juce::File::isAbsolutePath(args[3]) && juce::File(args[3]).existsAsFile()));
            const bool installedEffectTransaction =
                args.size() == 4 && args[0] == "--studio-effect-transaction-check" &&
                juce::File::isAbsolutePath(args[2]) && juce::File(args[2]).exists() &&
                juce::File::isAbsolutePath(args[3]) && juce::File(args[3]).existsAsFile();
            if (!nativeScalar && !automationTouch && !mixerTargets && !mixerOrder && !mixerStereo && !mixerAutomation && !mixerOrganisation &&
                !mixerChain && !midiRecovery && !historyRecovery && !studioRouting &&
                !installedEffectTransaction)
            {
                juce::Logger::writeToLog("Refused malformed explicit recovery fixture launch");
                setApplicationReturnValue(1);
                quit();
                return;
            }
            juce::File attempt;
            if (auto valid = vibera::test::prepareBuildAcceptanceAttempt(args[1], attempt); valid.failed())
            {
                juce::Logger::writeToLog(valid.getErrorMessage());
                setApplicationReturnValue(1);
                quit();
                return;
            }
            try
            {
                nativeMeterStorage = std::make_unique<vibera::test::NativeMeterStorage>(
                    attempt.getChildFile("engine-storage"));
                // Owned fixtures explicitly admit256-frame synthetic physical
                // calls; normal hardware uses its proven native driver bound.
                window = std::make_unique<Window>(true, 256); // No foreground/input/hardware.
                auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                if (!studio || !studio->controllerTestMode || !studio->pluginTestSettings ||
                    !studio->workflowTestSettings)
                    throw std::runtime_error("Recovery fixture requires fresh isolated Studio(false)");
                auto &manager = studio->audio.engine.getDeviceManager();
                if (manager.deviceManager.getCurrentAudioDevice())
                    throw std::runtime_error("Recovery fixture refuses a pre-existing audio device");
                if (!studio->audio.engine.getPropertyStorage().getAppPrefsFolder().isAChildOf(
                        nativeMeterStorage->directory) ||
                    !studio->audio.engine.getPropertyStorage().getAppCacheFolder().isAChildOf(
                        nativeMeterStorage->directory))
                    throw std::runtime_error("Recovery fixture requires per-attempt isolated engine storage");
                std::vector<juce::AudioIODeviceType *> physical;
                for (auto *type : manager.deviceManager.getAvailableDeviceTypes())
                    if (type->getTypeName() != "VibeRa synthetic")
                        physical.push_back(type);
                for (auto *type : physical)
                    manager.deviceManager.removeAudioDeviceType(type);
                studio->audio.engine.getPropertyStorage().setProperty(
                    vibera::te::SettingID::midiScanIntervalSeconds, 0);
                (void)manager.getHostedAudioDeviceInterface(); // Default useMidiDevices=false; no initialise.
                studio->getProperties().set(automationTouch ? "viberaAutomationTouchFixture" : mixerOrder ? "viberaMixerInsertOrderFixture" : mixerTargets ? "viberaMixerTargetsFixture" : studioRouting ? "viberaStudioRoutingFixture" :
                    midiRecovery ? "viberaMidiTakeRecoveryFixture" : "viberaUiHistoryRecoveryFixture", true);
                studio->getProperties().set(automationTouch ? "viberaAutomationTouchOutput" : mixerOrder ? "viberaMixerInsertOrderOutput" : mixerTargets ? "viberaMixerTargetsOutput" : studioRouting ? "viberaStudioRoutingOutput" :
                    midiRecovery ? "viberaMidiTakeRecoveryOutput" : "viberaUiHistoryRecoveryOutput",
                    attempt.getFullPathName());
                if (nativeScalarInstalled)
                    studio->getProperties().set("viberaNativeScalarInstalledFixture", true);
                if (studioRouting)
                    studio->getProperties().set("viberaStudioRoutingSameConfigFixture", sameConfigRouting);
                if (installedEffectTransaction)
                {
                    studio->getProperties().set("viberaInstalledEffectTransactionFixture", true);
                    studio->getProperties().set("viberaInstalledEffectTransactionOutput",
                                                attempt.getFullPathName());
                }
                if (mixerChain)
                {
                    studio->getProperties().set("viberaMixerChainWorkflowFixture", true);
                    studio->getProperties().set("viberaMixerChainWorkflowOutput", attempt.getFullPathName());
                }
                juce::MessageManager::callAsync(
                    [this, args, attempt, midiRecovery, studioRouting, mixerTargets, mixerOrder, mixerStereo,
                     mixerAutomation, automationTouch, nativeScalar, mixerOrganisation, mixerChain, installedEffectTransaction]
                    {
                        auto *owned = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                        auto result = juce::Result::fail("Recovery fixture lost its owned Studio");
                        try
                        {
                            if (owned && nativeScalar)
                                result = owned->verifyNativeScalarHistory(attempt, juce::File(args[2]));
                            else if (owned && automationTouch)
                                result = owned->verifyAutomationTouch(attempt);
                            else if (owned && installedEffectTransaction)
                                result = vibera::test::verifyInstalledStudioEffectTransaction(
                                    *owned, attempt, juce::File(args[2]), juce::File(args[3]));
                            else if (owned && mixerChain)
                                result = owned->verifyMixerChainWorkflow(attempt, juce::File(args[2]));
                            else if (owned && mixerOrganisation)
                                result = owned->verifyMixerOrganisation(attempt);
                            else if (owned && mixerAutomation)
                                result = owned->verifyMixerAutomationUi(attempt);
                            else if (owned && mixerStereo)
                                result = owned->verifyMixerStereoUi(attempt);
                            else if (owned && mixerOrder)
                                result = owned->verifyMixerInsertOrder(attempt);
                            else if (owned && mixerTargets)
                                result = owned->verifyMixerTargetChoices(attempt);
                            else if (owned && studioRouting)
                            {
                                std::optional<vibera::PluginSpec> fixture;
                                if (args.size() >= 3)
                                {
                                    std::vector<juce::PluginDescription> descriptions;
                                    const auto scanned = vibera::PluginCatalog::scanOne(juce::File(args[2]), descriptions);
                                    if (scanned.failed())
                                        throw std::runtime_error(scanned.getErrorMessage().toStdString());
                                    const auto selected = std::find_if(descriptions.begin(), descriptions.end(), [](const auto& d)
                                        { return d.name == "VibeRa Parameter Fixture" && !d.isInstrument &&
                                                 d.pluginFormatName == "VST3"; });
                                    if (selected == descriptions.end())
                                        throw std::runtime_error(
                                            "Exact original native Parameter Fixture required");
                                    fixture.emplace();
                                    fixture->name = selected->name;
                                    fixture->descriptor = selected->createXml()->toString();
                                    fixture->instrument = false;
                                }
                                result = owned->verifyStudioChannelRouting(
                                    attempt, fixture ? &*fixture : nullptr,
                                    args.size() == 4 ? juce::File(args[3]) : juce::File{});
                            }
                            else if (owned)
                                result = midiRecovery ? owned->verifyMidiTakeRecovery(attempt)
                                                      : owned->verifyUiHistoryRecovery(attempt);
                        }
                        catch (const std::exception &error)
                        {
                            result = juce::Result::fail(error.what());
                        }
                        catch (...)
                        {
                            result = juce::Result::fail("Unexpected recovery fixture failure");
                        }
                        if (result.failed())
                            juce::Logger::writeToLog(result.getErrorMessage());
                        setApplicationReturnValue(result.wasOk() ? 0 : 1);
                        quit();
                    });
            }
            catch (const std::exception &error)
            {
                juce::Logger::writeToLog(error.what());
                setApplicationReturnValue(1);
                quit();
            }
            return;
        }
        const bool nativeMeterFixture = args.contains("--native-mixer-meter-fixture");
        if (nativeMeterFixture)
        {
            // Exactly one explicit test launch; no user project/open/default
            // device flags, extra options or normal --mcp/settings lifetime.
            if (args.size() != 4 || args[0] != "--mcp-test" || args[1] != "--window-check-project" ||
                args[2] != "--native-mixer-meter-fixture" || !juce::File::isAbsolutePath(args[3]))
            {
                juce::Logger::writeToLog("Refused native meter fixture launch combination");
                setApplicationReturnValue(1);
                quit();
                return;
            }
            nativeMeterOutput = juce::File(args[3]);
            if (auto valid = vibera::test::validateNativeMeterOutput(juce::File::getCurrentWorkingDirectory(),
                                                                     nativeMeterOutput);
                valid.failed())
            {
                juce::Logger::writeToLog(valid.getErrorMessage());
                setApplicationReturnValue(1);
                quit();
                return;
            }
            try
            {
                nativeMeterStorage = std::make_unique<vibera::test::NativeMeterStorage>(
                    nativeMeterOutput.getChildFile("engine-" + juce::Uuid().toString()));
            }
            catch (const std::exception &error)
            {
                juce::Logger::writeToLog(error.what());
                setApplicationReturnValue(1);
                quit();
                return;
            }
        }
        bool smoke = args.size() == 2 && args[0] == "--smoke-test";
        bool pianoCheck = args.size() == 3 && args[0] == "--piano-navigation-check";
        bool samplerCheck = args.size() == 2 && args[0] == "--sampler-gesture-check";
        const bool noteModesCheck = args.size() == 2 && args[0] == "--piano-note-modes-check";
        bool noteStretchCheck = args.size() == 2 && args[0] == "--note-stretch-ui-check";
        bool mixerControlsCheck = args.size() == 2 && args[0] == "--mixer-controls-ui-check";
        bool mixerRoutingCheck = args.size() == 2 && args[0] == "--mixer-routing-ui-check";
        bool noteSliceCheck = args.size() == 2 && args[0] == "--note-slice-ui-check";
        bool noteMuteCheck = args.size() == 2 && args[0] == "--note-mute-ui-check";
        bool clipGroupsCheck = args.size() == 2 && args[0] == "--clip-groups-ui-check";
        bool controllerCheck = args.size() == 2 && args[0] == "--controller-check";
        bool automationCheck = args.size() == 2 && args[0] == "--automation-check";
        bool parameterCheck = args.size() == 3 && (args[0] == "--parameter-ui-check" || args[0] == "--wrapper-ui-check" ||
                                                  args[0] == "--wrapper-editorless-ui-check");
        bool catalogueCheck = args.size() == 3 && args[0] == "--catalogue-controls-check";
        bool midiFileCheck = args.size() == 2 && args[0] == "--midi-file-check";
        bool browserLiveCheck = (args.size() == 2 || args.size() == 3) && args[0] == "--browser-live-check";
        const bool audioTakeCheck = args.size() == 2 && args[0] == "--audio-take-check";
        const bool accessibilityCheck = args.size() == 2 && args[0] == "--accessibility-check";
        const bool performanceCheck = args.size() == 2 && args[0] == "--performance-check";
        const bool composeCheck = args.size() == 2 && args[0] == "--step-compose-ui-check";
        bool patternCheck = composeCheck || ((args.size() == 2 || args.size() == 3) && args[0] == "--pattern-workflow-check");
        bool audioPlaylistCheck = args.size() == 2 && args[0] == "--audio-playlist-check";
        bool audioPlaylistLive =
            (args.size() == 2 || args.size() == 3) && args[0] == "--audio-playlist-live-check";
        bool anticipationCheck =
            (args.size() == 2 || args.size() == 3) && args[0] == "--anticipation-panel-check";
        bool coProducerCheck =
            (args.size() == 2 || args.size() == 3) && (args[0] == "--coproducer-check" || anticipationCheck);
        bool instrumentCheck = args.size() == 3 && args[0] == "--instrument-check";
        bool channelManagementCheck =
            (args.size() == 2 || args.size() == 3) && args[0] == "--channel-management-check";
        const bool liveBurnCheck = args.size() == 2 && args[0] == "--live-burn-check";
        const bool aiUiCheck = args.size() == 2 && args[0] == "--ai-ui-check";
        bool stepVelocityCheck = (args.size() == 2 || args.size() == 3) &&
                                 (args[0] == "--step-velocity-check" || args[0] == "--swing-check");
        bool mcpMode = args.contains("--mcp") || args.contains("--mcp-test");
        window = std::make_unique<Window>(
            smoke || mixerControlsCheck || mixerRoutingCheck || noteStretchCheck || noteSliceCheck ||
                noteMuteCheck || clipGroupsCheck || samplerCheck || noteModesCheck || controllerCheck ||
                automationCheck || parameterCheck || catalogueCheck || instrumentCheck ||
                channelManagementCheck || liveBurnCheck || aiUiCheck || stepVelocityCheck || pianoCheck || coProducerCheck ||
                midiFileCheck || browserLiveCheck || audioTakeCheck || accessibilityCheck || performanceCheck || patternCheck || audioPlaylistCheck ||
                audioPlaylistLive || args.contains("--mcp-test") || args.contains("--window-check-project") ||
                args.contains("--detached-window-check-project"),
            (nativeMeterFixture || parameterCheck || instrumentCheck || pianoCheck ||
             channelManagementCheck || liveBurnCheck || stepVelocityCheck || audioPlaylistLive || browserLiveCheck || audioTakeCheck || performanceCheck ||
             patternCheck)
                ? 256
                : 0); // These owned acceptance drivers dispatch only fixed256-frame callbacks.
        if (args.contains("--window-check-project") || args.contains("--detached-window-check-project"))
            window->setVisible(true);
        if (nativeMeterFixture)
        {
            auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
            try
            {
                if (!studio || !studio->controllerTestMode || !studio->pluginTestSettings ||
                    !studio->workflowTestSettings)
                    throw std::runtime_error("Native meter fixture requires isolated fresh test Studio");
                if (int(studio->getProperties()["viberaNativeMixerMeterGuardsVersion"]) != 1)
                    throw std::runtime_error(
                        "Native meter fixture central configuration/project guards are missing");
                // Root integration adds central audioSettings/openProjectFile
                // refusals for this property. No reconfiguration is allowed
                // while the synthetic callback driver borrows its device.
                studio->getProperties().set(vibera::test::nativeMeterFixtureProperty, true);
                nativeMeterDriver = std::make_unique<vibera::test::NativeMixerMeterDriver>(
                    studio->audio, nativeMeterOutput, nativeMeterStorage->directory);
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("completionState", "failed");
                failure->setProperty("error", error.what());
                nativeMeterOutput.getChildFile("native-meter-driver.json")
                    .replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                juce::Logger::writeToLog(error.what());
                setApplicationReturnValue(1);
                quit();
                return;
            }
        }
        if (mixerControlsCheck)
        {
            juce::MessageManager::callAsync(
                [this, args]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto folder = juce::File(args[1]);
                    auto result = studio->verifyMixerControls(folder);
                    if (result.failed())
                        juce::Logger::writeToLog(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
            return;
        }
        if (mixerRoutingCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->verifyMixerRouting(folder);
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (noteStretchCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->verifyNoteStretch(folder);
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (noteSliceCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->verifyNoteSlice(folder);
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (noteMuteCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyNoteMute(folder);
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (clipGroupsCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyClipGroups(folder);
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (noteModesCheck)
        {
            juce::File folder;
            const auto admitted = vibera::test::prepareBuildAcceptanceAttempt(args[1], folder);
            if (admitted.failed())
            {
                juce::Logger::writeToLog(admitted.getErrorMessage());
                setApplicationReturnValue(1);
                quit();
                return;
            }
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyNoteModesWorkflow(folder);
                    if (result.failed())
                        juce::Logger::writeToLog(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
            return;
        }
        if (samplerCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifySamplerGestures(folder);
                    if (result.failed())
                        folder.getChildFile("sampler-gesture-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (controllerCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyControllers(folder);
                    if (result.failed())
                        folder.getChildFile("controller-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (automationCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyAutomation(folder);
                    if (result.failed())
                        folder.getChildFile("automation-gui-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (catalogueCheck)
        {
            const auto folder = juce::File(args[1]);
            const auto fixture = juce::File(args[2]);
            juce::MessageManager::callAsync(
                [this, folder, fixture]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyPluginCatalogueControls(folder, fixture);
                    if (result.failed())
                    {
                        folder.getChildFile("catalogue-ui-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                        auto *error = new juce::DynamicObject;
                        juce::var receipt(error);
                        error->setProperty("passed", false);
                        error->setProperty("completionState", "failed");
                        error->setProperty("error", result.getErrorMessage());
                        folder.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(receipt));
                    }
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (parameterCheck)
        {
            const auto fixture = juce::File(args[2]);
            juce::File folder;
            const auto admission = fixture.existsAsFile() && juce::File::isAbsolutePath(args[2])
                                       ? vibera::test::prepareBuildAcceptanceAttempt(args[1], folder)
                                       : juce::Result::fail("Parameter GUI requires an absolute existing fixture");
            if (admission.failed())
            {
                juce::Logger::writeToLog(admission.getErrorMessage());
                setApplicationReturnValue(1);
                quit();
                return;
            }
            const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
            const auto receipt = folder.getChildFile("verification.json");
            juce::var evidence(new juce::DynamicObject());
            auto *proof = evidence.getDynamicObject();
            proof->setProperty("finished", false);
            proof->setProperty("passed", false);
            proof->setProperty("executableSha256", juce::SHA256(executable).toHexString());
            proof->setProperty("fixtureSha256", juce::SHA256(fixture).toHexString());
            proof->setProperty("workerSha256", juce::SHA256(executable.getSiblingFile("VibeRa Plugin Worker.exe")).toHexString());
            proof->setProperty("project", folder.getChildFile("parameter-song.vibra").getFullPathName());
            proof->setProperty("nativeGlobalInput", false);
            proof->setProperty("listening", false);
            if (!receipt.replaceWithText(juce::JSON::toString(evidence, true)))
            {
                setApplicationReturnValue(1);
                quit();
                return;
            }
            juce::MessageManager::callAsync(
                [this, folder, fixture, receipt, evidence, wrapperOnly = args[0] != "--parameter-ui-check",
                 controlsOnly = args[0] == "--wrapper-editorless-ui-check"]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = juce::Result::fail("Parameter GUI lost its owned Studio");
                    try
                    {
                        if (studio)
                            result = studio->verifyPluginParameters(folder, fixture, wrapperOnly, controlsOnly);
                    }
                    catch (const std::exception &error)
                    {
                        result = juce::Result::fail(error.what());
                    }
                    const auto savedProject = folder.getChildFile("parameter-song.vibra");
                    if (result.wasOk() && !savedProject.existsAsFile())
                        result = juce::Result::fail("Parameter GUI passed without its actual saved project");
                    if (result.failed())
                        folder.getChildFile("parameter-gui-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    auto *proof = evidence.getDynamicObject();
                    proof->setProperty("finished", true);
                    proof->setProperty("passed", result.wasOk());
                    proof->setProperty("controlsOnly", controlsOnly);
                    proof->setProperty("error", result.getErrorMessage());
                    if (result.wasOk())
                        proof->setProperty("projectSha256", juce::SHA256(savedProject).toHexString());
                    if (!receipt.replaceWithText(juce::JSON::toString(evidence, true)))
                        result = juce::Result::fail("Parameter GUI terminal evidence could not be retained");
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (midiFileCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyMidiInterchange(folder);
                    if (result.failed())
                        folder.getChildFile("midi-dialog-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (performanceCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    folder.createDirectory();
                    const auto result = studio->verifyPerformance(folder);
                    if (result.failed())
                        folder.getChildFile("performance-failure.txt").replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (accessibilityCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    folder.createDirectory();
                    const auto result = studio->verifyAccessibility(folder);
                    if (result.failed())
                        folder.getChildFile("accessibility-failure.txt").replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (audioTakeCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    folder.createDirectory();
                    const auto result = studio->verifyAudioTake(folder);
                    if (result.failed())
                        folder.getChildFile("audio-take-failure.txt").replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (browserLiveCheck)
        {
            const auto folder = juce::File(args[1]);
            const auto fixture = args.size() == 3 ? juce::File(args[2]) : juce::File{};
            juce::MessageManager::callAsync(
                [this, folder, fixture]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyBrowserLive(folder, fixture);
                    if (result.failed())
                        folder.getChildFile("browser-live-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (patternCheck)
        {
            const auto folder = juce::File(args[1]);
            const auto fixture = args.size() == 3 ? juce::File(args[2]) : juce::File{};
            juce::MessageManager::callAsync(
                [this, folder, fixture, composeCheck]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = composeCheck ? studio->verifyStepCompose(folder) : studio->verifyPatternWorkflow(folder, fixture);
                    if (result.failed() && !composeCheck)
                    {
                        folder.createDirectory();
                        folder.getChildFile("pattern-workflow-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    }
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (audioPlaylistCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->verifyAudioPlaylist(folder);
                    if (result.failed())
                        folder.getChildFile("audio-playlist-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (audioPlaylistLive)
        {
            const auto folder = juce::File(args[1]);
            const auto fixture = args.size() == 3 ? juce::File(args[2]) : juce::File{};
            juce::MessageManager::callAsync(
                [this, folder, fixture]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->verifyAudioPlaylistLive(folder, fixture);
                    if (result.failed())
                        folder.getChildFile("audio-playlist-live-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (coProducerCheck)
        {
            auto folder = juce::File(args[1]);
            const auto instrumentFixture = args.size() == 3 ? juce::File(args[2]) : juce::File{};
            juce::MessageManager::callAsync(
                [this, folder, instrumentFixture, anticipationCheck]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result =
                        studio->runCoProducerIntegrationTest(folder, instrumentFixture, anticipationCheck);
                    if (result.failed())
                        folder.getChildFile("coproducer-integrated-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (stepVelocityCheck)
        {
            const auto folder = juce::File(args[1]);
            const auto fixture = args.size() == 3 ? juce::File(args[2]) : juce::File{};
            juce::MessageManager::callAsync(
                [this, folder, fixture]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->verifyRackVelocity(folder, fixture);
                    if (result.wasOk())
                        result = studio->verifyRackSwing(folder, fixture);
                    if (result.wasOk())
                        result = studio->verifyQuantizer(folder);
                    if (result.failed())
                        folder.getChildFile("step-velocity-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (aiUiCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyAiUi(folder);
                    if (result.failed())
                        juce::Logger::writeToLog(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (liveBurnCheck)
        {
            const auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyLiveBurn(folder);
                    folder.createDirectory();
                    folder.getChildFile("live-burn.txt")
                        .replaceWithText(result.wasOk() ? "PASS live Burn\n" : result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (channelManagementCheck)
        {
            const auto folder = juce::File(args[1]);
            const auto fixture = args.size() == 3 ? juce::File(args[2]) : juce::File{};
            juce::MessageManager::callAsync(
                [this, folder, fixture]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    const auto result = studio->verifyChannelManagement(folder, fixture);
                    if (result.failed())
                        folder.getChildFile("channel-management-failure.txt")
                            .replaceWithText(result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (mcpMode)
        {
            auto safe = juce::Component::SafePointer<vibera::Studio>(
                dynamic_cast<vibera::Studio *>(window->getContentComponent()));
            mcp = std::make_unique<vibera::McpServer>(
                vibera::Studio::mcpTools(),
                [this, safe](const juce::String &name, const juce::var &arguments)
                {
                    if (!safe)
                        throw std::runtime_error("Application closed");
                    if (nativeMeterDriver && name == "vibera_project")
                    {
                        const auto action = arguments["action"].toString();
                        if (action == "open" ||
                            (action == "new" && arguments["template"].toString() != "empty"))
                            throw std::runtime_error(
                                "Native meter fixture permits only fresh disposable projects");
                        if (arguments.hasProperty("path"))
                        {
                            const auto path = arguments["path"].toString();
                            if (!juce::File::isAbsolutePath(path) ||
                                !juce::File(path).isAChildOf(nativeMeterOutput))
                                throw std::runtime_error(
                                    "Native meter fixture file target must remain in its owned output");
                        }
                    }
                    return safe->mcpCall(name, arguments);
                });
            if (!mcp->start())
            {
                setApplicationReturnValue(1);
                quit();
            }
        }
        if (!smoke && (args.size() == 2 || (args.size() == 3 && args[2] == "--mcp-test")) &&
            (args[0] == "--open-project" || args[0] == "--window-check-project" ||
             args[0] == "--detached-window-check-project"))
            if (auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent()))
                studio->openProjectFile(juce::File(args[1]));
        if (args.contains("--detached-window-check-project"))
            juce::MessageManager::callAsync(
                [this]
                {
                    if (auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent()))
                        for (int index = 0; index < 5; ++index)
                            studio->detachWorkspaceWindow(index);
                });
        if (smoke)
        {
            auto folder = juce::File(args[1]);
            juce::MessageManager::callAsync(
                [this, folder]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->runSmokeTest(folder);
                    if (result.failed())
                    {
                        folder.createDirectory();
                        folder.getChildFile("gui-smoke.txt")
                            .replaceWithText("FAIL: " + result.getErrorMessage());
                    }
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (pianoCheck)
        {
            juce::MessageManager::callAsync(
                [this, args]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->runPianoNavigationTest(juce::File(args[1]), juce::File(args[2]));
                    if (result.failed())
                        juce::File(args[2])
                            .getChildFile("piano-vst3-gui.txt")
                            .replaceWithText("FAIL: " + result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
        if (instrumentCheck)
        {
            juce::MessageManager::callAsync(
                [this, args]
                {
                    auto *studio = dynamic_cast<vibera::Studio *>(window->getContentComponent());
                    auto result = studio->runInstrumentOpeningTest(juce::File(args[1]), juce::File(args[2]));
                    if (result.failed())
                        juce::File(args[2])
                            .getChildFile("rack-opening.txt")
                            .replaceWithText("FAIL: " + result.getErrorMessage());
                    setApplicationReturnValue(result.wasOk() ? 0 : 1);
                    quit();
                });
        }
    }
    void shutdown() override
    {
        mcp.reset();
        if (nativeMeterDriver)
        {
            try
            {
                const auto result =
                    nativeMeterDriver->finish(); // Join/capture BEFORE Studio/device destruction.
                if (result.failed())
                {
                    juce::Logger::writeToLog(result.getErrorMessage());
                    setApplicationReturnValue(1);
                }
            }
            catch (const std::exception &error)
            {
                // finish stops before writing; the destructor also joins if an
                // unexpected writer exception occurs. Never destroy audio first.
                juce::Logger::writeToLog(error.what());
                setApplicationReturnValue(1);
            }
            nativeMeterDriver.reset();
        }
        window.reset();
        nativeMeterStorage.reset(); // Keep isolated engine storage active through its destruction.
    }
};
START_JUCE_APPLICATION(VibeRaApplication)
