// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "EffectChain.h"
#include "AudioEngine.h"
#include "ChannelRoutingCoordinator.h"
#include "Customization.h"
#include "MtsEsp.h"
#include "MidiController.h"
#include "AudioClipImporter.h"
#include "Theme.h"
#include "ToolbarButton.h"
#include "PluginCatalog.h"
#include "TypingKeyboard.h"
#include "PerformanceInput.h"
#include "MixerAutomationUiIntent.h"
#include "SamplerAutomationUiIntent.h"
#include "SamplerControllerLearn.h"
#include "MixerSelection.h"
#include "MixerOrganisationReferences.h"
#include "MacroConfiguration.h"
#include "MixerAutomationTake.h"
#include "ai/AiSettings.h"
#include <deque>
#include "Performance.h"
#include "RecentSources.h"
#include "MissingMedia.h"
#include <thread>
#include "ai/MusicEngines.h"
#include "ai/ProducerAgent.h"
#include "ai/ClaudeCli.h"
#include "ai/CodexAgent.h"
#include "ai/McpLoopback.h"
#include "Updater.h"
#include <atomic>

namespace vibera
{
struct MidiScore;
class CoProducerDock;
class Studio final : public juce::Component, private juce::Timer, public juce::DragAndDropContainer
{
    friend class AiPage;          // Settings > AI edits aiSettings/keys and drives engine jobs.
    friend class CoProducerChat;  // Docked chat over the shared aiAgent/aiMusic state.
    friend class CoProducerDock;  // Right-side CoProducer panel (chat + MIDI ideas).
    friend class CoProducerPanel; // Coordinates its temporary projection with Studio's publication scheduler.
    friend class PluginControls;
    friend class RackVelocityGraph; // Original Rack UI, canonical control-thread command dispatch only.
    friend class RackSwingControl;

  public:
    explicit Studio(bool enableDevice = true, int explicitProviderFrames = 0);
    ~Studio() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress &) override;
    bool performAction(const juce::String &, const juce::KeyPress *typingKey = nullptr);
    void invokeController(const ControllerInvocation &);
    std::unique_ptr<MixerAutomationTake> automationTouch;
    std::shared_ptr<MidiLane> automationTouchSource;
    const juce::AudioIODevice *automationTouchDevice = nullptr;
    juce::var automationTouchTerminal;
    unsigned nextAutomationTouchSession = 0, automationTouchInputSession = 0;
    double automationTouchInputSeconds = 0;
    bool automationTouchInputValid = false, servicingAutomationTouch = false;
    std::vector<PluginRuntimeState> automationTouchNativeBefore;
    bool automationTouchCancel = false, automationTouchDraining = false;
    bool automationRecordingActive() const noexcept;
    juce::Result automationRecordingAdmission() const;
    juce::Result startAutomationRecording(int insert, int lane);
    std::optional<juce::Result> automationRecordingCommand(const juce::String &, const CommandArgs &);
    juce::Result automationRecordingSourceCurrent() const;
    void serviceAutomationRecording();
    juce::Result finishAutomationRecording(bool cancel);
    juce::var automationRecordingOperation(const juce::var &);
    juce::var automationRecordingState(int offset = 0, int limit = 128) const;
    juce::Result verifyAutomationTouch(const juce::File &);
    juce::Result verifyControllers(const juce::File &);
    juce::Result verifySamplerControllerWorkflow(const juce::File &);
    juce::var pianoNoteModes(const juce::var &);
    juce::Result verifyNoteModesWorkflow(const juce::File &);
    juce::Result verifyMidiTakeRecovery(const juce::File &);
    juce::Result verifyAudioTake(const juce::File &);
    juce::Result verifyPerformance(const juce::File &);
    // Every visible control has a name for screen readers; editors describe their state.
    juce::Result verifyAccessibility(const juce::File &);
    // Playlist time selection in beats (Ctrl-drag the Playlist ruler). Workspace state,
    // not musical history; it is also the punch range for audio takes.
    std::optional<std::pair<double, double>> playlistRange;
    juce::Result verifyUiHistoryRecovery(const juce::File &);
    bool keyStateChanged(bool) override;
    void releaseTypingNotes();
    void previewNote(int pitch);
    Controller controller;
    AudioEngine audio;
    ChannelRoutingCoordinator routingCoordinator{controller, audio}; // retires before AudioEngine
    std::uint64_t lastRoutingOperation = 0, lastRoutingDispatchOperation = 0;
    bool routingApplicationPreparing = false;
    bool effectCaptureDispatch = false; // Exact owned routing job may install its capture callback.
    std::uint64_t effectCaptureCallbackOperation = 0; // Physically installed afterPluginState slot only.
    juce::String routingPendingStatus{"Routing pending actual audio selection..."};
    juce::String routingReadyStatus{"Ready"};
    juce::String routingInterruptedStatus{"Routing interrupted; inspect audio state"};
    struct DeferredDevicePreparation;
    std::unique_ptr<DeferredDevicePreparation> devicePreparation;
    std::uint64_t devicePreparationSerial = 0;
    std::uint64_t deviceCaptureCallbackOperation = 0; // Exact CONTROL-owned afterPluginState slot stamp.
    bool devicePreparationServicing = false, devicePreparationDispatch = false;
    juce::String devicePreparationPendingStatus{"Preparing audio device; current plugin state retained..."};
    juce::String devicePreparationStoppedStatus{"Audio preparation stopped; retained state is recoverable"};
    bool devicePreparationBusy() const noexcept;
    bool pluginControlsPending() const;
    juce::Result closePluginParameters(Id, const juce::String &instance);
    juce::Result devicePreparationSourceCurrent(const DeferredDevicePreparation&, bool finalNative) const;
    juce::Result beginDevicePreparation(bool requestPlay);
    void serviceDevicePreparation();
    juce::Result cancelDevicePreparation(std::uint64_t);
    juce::Result retryDevicePreparation(std::uint64_t, bool requestPlay = false);
    juce::var devicePreparationState(std::uint64_t operation = 0) const;
    static juce::var devicePreparationSchema();
    bool routingPending();
    juce::Result routingSourceAdmission();
    juce::Result routingResult(const ChannelRoutingCoordinator::Result&);
    juce::Result captureRoutingEffect(const ChannelRoutingCoordinator::Result &);
    bool routingStructuralCommand(const juce::String&, const CommandArgs&) const;
    juce::Result restoreRoutingBeforeStructural();
    std::optional<juce::Result> admitControllerCommand(const juce::String&, const CommandArgs&,
        const juce::String&);
    std::optional<juce::Result> admitControllerHistory(HistoryDirection);
    juce::Result applyRoutingSequence(const std::vector<CommandArgs>&, const juce::String& label);
    juce::var channelRoutingState(std::uint64_t operation = 0);
    static juce::var channelRoutingSchema();
    juce::Result verifyStudioChannelRouting(const juce::File &, const PluginSpec *originalEffect = nullptr,
                                            const juce::File &detectorFixture = {});
    juce::Result verifyMixerTargetChoices(const juce::File&);
    // Declared before plugins so its exact temporary file outlives the scanner.
    std::unique_ptr<juce::TemporaryFile> pluginTestSettings;
    PluginCatalog plugins;
    Customization customization = Customization::familiarDefaults();
    std::unique_ptr<juce::TemporaryFile> workflowTestSettings;
    MidiController midiController;
    bool controllerTestMode = false;
    bool controllerMcpInjection = false, controllerMcpRejected = false;
    struct ControllerPickup
    {
        juce::String target;
        double previous = -1, applied = -1;
        bool caught = false;
    };
    std::array<ControllerPickup, 1024> controllerPickup{};
    std::optional<SamplerControllerLearn> samplerControllerLearn;
    std::uint64_t nextSamplerControllerLearn = 0;
    bool samplerControllerLearnCommitting = false;
    juce::String samplerControllerProjectionError, samplerControllerLearnWindowToken;
    juce::Component::SafePointer<juce::AlertWindow> samplerControllerLearnWindow,
        samplerControllerPropertiesWindow;
    void syncSamplerControllerBindings();
    juce::var samplerControllerState() const;
    juce::var samplerControllerLearnState() const;
    juce::var samplerControllerOperation(const juce::var &);
    juce::Result startSamplerControllerLearn(Id, std::uint32_t);
    juce::Result cancelSamplerControllerLearn(const juce::String &);
    juce::Result acceptSamplerControllerLearn(const juce::String &, double, double, bool,
                                              const std::vector<Id> &);
    void showSamplerControllerLearn(Id, std::uint32_t);
    void samplerControllerLinksMenu();
    void samplerControllerLinkDialog(Id);
    bool recordingNotes = false;
    bool quantizeRecordingStart = false, quantizeRecordingEnd = false;
    juce::Result finishMidiTake(bool cancel = false);
    juce::Result retryMidiTake(std::uint64_t expectedSerial = 0);
    juce::Result cancelPendingMidiTake(std::uint64_t expectedSerial = 0);
    bool hasPendingMidiTake() const noexcept;
    std::uint64_t midiTakeSerial() const noexcept;
    juce::var midiTakeState() const;
    const std::vector<Note> &liveTakeNotes() const
    {
        return midiPreview;
    }
    Id liveTakePattern() const
    {
        return midiTaking ? takePattern : 0;
    }
    double liveTakeSongStart() const
    {
        return takeSongBeat;
    }
    double liveTakeSourceOffset() const
    {
        return takeSourceOffset;
    }
    bool liveTakeInSong() const
    {
        return midiTaking && !takePatternMode;
    }
    void guideMenu(bool countIn);
    void toolsMenu();
    void patternMenu();
    void selectPattern(Id);
    void renamePattern();
    void channelPropertiesDialog(const std::vector<Id> &);
    void transposePattern();
    void deletePattern();
    void patternBulkAction(int action, const std::vector<Id> &ids);
    void findEmptyPattern(bool first, bool naming = true);
    void midiFileMenu();
    void chooseMidiImport();
    void importMidiFile(const juce::File &, Id pattern = 0, Id channel = 0, bool showOptions = true);
    void selectPianoTool(bool paint);
    void chooseMidiExport();
    juce::Result commitMidiImport(const MidiScore &, bool blend, const juce::String &identity,
                                  std::uint64_t revision, Id pattern, Id channel, bool useTempo = false,
                                  bool split = false);
    juce::Result verifyMidiInterchange(const juce::File &);
    juce::Result verifyBrowserLive(const juce::File &, const juce::File &fixture = {});
    juce::Result verifyAudioPlaylist(const juce::File &);
    juce::Result verifyAudioPlaylistLive(const juce::File &, const juce::File &fixture = {});
    juce::Result verifyPatternWorkflow(const juce::File &, const juce::File &fixture = {});
    juce::Result finishRoutingForTest(const char *phase);
    juce::Result verifyClipPicker(const juce::File &);
    void dumpScoreLog(double seconds = 180);
    void clearScoreLog();
    std::vector<MacroStep> recordedSteps;
    Id selectedPattern = 0, selectedChannel = 0;
    Scope focus = Scope::rack;
    bool patternMode = true;
    bool followPlayhead = true; // Playlist page-scrolls to keep the Song playhead in view.
    double snap = .25;
    std::vector<Id> selectedNotes, selectedClips;
    std::vector<Id> selectedChannels; // Bulk Rack targets; active/MIDI target stays selectedChannel.
    juce::String rackFilter = "all", rackProjectIdentity;
    Id rackGroup = 0, rackObservedActive = 0;
    std::vector<Id> visibleRackChannels() const;
    Channel *rackChannelAt(int visibleRow);
    juce::Result selectRackChannels(const std::vector<Id> &, const juce::String &mode = "replace",
                                    Id active = 0);
    juce::Result setRackFilter(const juce::String &kind, Id group = 0);
    void reconcileRack();
    void rackGroupDialog(const juce::String &operation);
    void rackGroupMenu(juce::Component *anchor);
    juce::var rackState() const;
    juce::var mcpRackOperation(const juce::var &);
    bool rackGraphVisible = false;
    juce::String rackGraphProperty = "velocity";
    juce::Result setRackGraphProperty(const juce::String &);
    void setRackGraphVisible(bool);
    void addRackGraphItem(juce::PopupMenu &, Id channel);
    juce::Result finishRackVelocity(bool commit = true);
    juce::Result finishRackSwing();
    juce::Result finishRackEditsChecked();
    void addSwingItems(juce::PopupMenu &, const std::vector<Id> &);
    void swingDialog(const juce::String &, const std::vector<Id> & = {});
    juce::Result verifyRackSwing(const juce::File &, const juce::File &fixture = {});
    juce::var rackGraphState() const;
    juce::Result stepVelocityAdmission() const;
    juce::Result verifyRackVelocity(const juce::File &, const juce::File &fixture = {});
    juce::Result verifyRackGroups(const juce::File &);
    juce::Result verifyClipGroups(const juce::File &);
    juce::Result verifyDropTargets(const juce::File &);
    void verifyAutosave(const juce::File &folder, std::function<void(juce::Result)> completion);
    juce::var updateStatus() const;
    juce::Result requestUpdateCheck() { return checkForUpdates(true, false); }
    juce::Result verifyNoteMute(const juce::File &);
    juce::Result verifyNoteSlice(const juce::File &);
    juce::Result verifyNoteStretch(const juce::File &);
    juce::Result selectMixerInsert(int);
    juce::Result admitMixerEffectMove(juce::String &errorCode) const;
    juce::Result admitMixerChainControl(juce::String &errorCode, const juce::String &owner = {}) const;
    void mixerRouteMenu(int source, int target);
    void promptMixerSend(int source, int target);
    bool performMixerRoutingAction(const juce::String &);
    juce::var mixerRoutingState() const;
    juce::Result verifyMixerRouting(const juce::File &);
    void mixerNameDialog(int);
    void mixerControlMenu(int);
    void mixerStereoDialog(int);
    void mixerValueDialog(int, MixerAutomationControl, std::optional<int> sendTarget = {});
    void mixerAutomationMenu(int, MixerAutomationControl, std::optional<int> sendTarget = {});
    juce::Result admitMixerAutomation(const juce::String &owner = {});
    juce::Result performMixerAutomationIntent(const MixerAutomationUiIntent &, MixerAutomationUiAction, Id source = 0);
    juce::Result selectAutomationSource(Id);
    void automationSourceDialog(Id);
    juce::Result verifyMixerAutomationUi(const juce::File &);
    juce::Result verifyMixerStereoUi(const juce::File &);
    juce::Result admitMixerInsertOrder(juce::String &errorCode, const juce::String &owner = {});
    juce::Result verifyMixerInsertOrder(const juce::File &);
    bool performMixerControlAction(const juce::String &);
    juce::var mixerMeterState(int) const;
    juce::Result resetMixerOverload(int, std::uint64_t &clearedThroughBlock);
    juce::Result verifyMixerControls(const juce::File &);
    struct NoteStretchSession
    {
        juce::String id, project, state = "pending", reason;
        CommandArgs args;
        NoteStretchPlan plan;
        std::uint64_t revision = 0;
        double lastActivity = 0;
        int leaseMs = 30000;
        bool remote = false;
    };
    std::optional<NoteStretchSession> noteStretchSession;
    bool committingNoteStretch = false;
    bool noteStretchPending() const;
    juce::Result beginNoteStretch(const std::vector<Id> &, bool remote, int leaseMs = 30000);
    juce::Result updateNoteStretch(double ratio);
    juce::Result finishNoteStretch(bool commit, const juce::String &reason = {});
    void pollNoteStretch();
    juce::var noteStretchState(int offset = 0, int limit = 64) const;

    struct NoteSliceSession
    {
        juce::String id, project, state = "pending", reason;
        CommandArgs args;
        std::vector<NoteSliceCut> cuts;
        std::uint64_t revision = 0;
        double lastActivity = 0;
        int leaseMs = 30000;
        bool remote = false;
    };
    std::optional<NoteSliceSession> noteSliceSession;
    bool committingNoteSlice = false;
    bool noteSlicePending() const;
    juce::Result beginNoteSlice(bool remote, int leaseMs = 30000);
    juce::Result updateNoteSlice(const NoteSliceLine &, const std::vector<Id> &ids = {});
    juce::Result finishNoteSlice(bool commit, const juce::String &reason = {});
    void pollNoteSlice();
    juce::var noteSliceState(int offset = 0, int limit = 64) const;

    void addStepFillItems(juce::PopupMenu &, const std::vector<Id> &, Id pattern);
    struct StepComposeSession
    {
        juce::String id, owner, project, state = "pending", reason;
        Id pattern = 0, activeChannel = 0;
        std::vector<StepComposeRecipe> original, recipes;
        std::unique_ptr<Project> baseline;
        std::uint64_t revision = 0, serial = 0;
        double lastActivity = 0;
        int leaseMs = 30000;
        bool remote = false, preview = false, solo = false;
    };
    std::optional<StepComposeSession> stepComposeSession;
    juce::Component::SafePointer<juce::DocumentWindow> stepComposeWindow;
    bool stepComposePending() const;
    void pollStepCompose();
    juce::var stepComposeState() const;
    juce::Result stepComposeAction(const juce::var &, bool remote = true);
    void openStepComposer(Id, const std::vector<Id> &);
    juce::Result verifyStepCompose(const juce::File &);
    void addChannelLoopItems(juce::PopupMenu &, Id channel, Id pattern);
    void channelLoopLengthDialog(Id channel, Id pattern);
    void patternMeterDialog(Id pattern);
    juce::Result verifyStepFill(const juce::File &);
    void addChannelMuteItems(juce::PopupMenu &, Id);
    juce::Result setSoloRestorePreference(bool);
    juce::Result setStepMemoryPreference(bool);
    juce::Result verifyChannelSolo(const juce::File &);
    struct ChannelEditJob
    {
        juce::String id, project, command, state = "pending", error, message;
        std::uint64_t revision = 0;
        std::vector<Id> targets, created;
        std::vector<std::pair<Id, juce::String>> runtimes;
    };
    std::vector<ChannelEditJob> channelEditJobs;
    juce::Result beginChannelEdit(const juce::String &, const std::vector<Id> &, juce::String &operation);
    juce::var channelEditState(const juce::String &) const;
    juce::Result cancelChannelEdit(const juce::String &);
    void addChannelManagementItems(juce::PopupMenu &, const std::vector<Id> &);
    // Instrument picker (Rack + button, empty-area right-click, channel Insert/Replace):
    // the native Synth, then scanned VST3 instruments grouped by category, Load sample and
    // More plugins. `replace` swaps that Channel's source instead of adding a Channel.
    void addInstrumentItems(juce::PopupMenu &, Id replace = 0);
    void showInstrumentMenu(Id replace = 0);
    void addInstrument(const juce::PluginDescription &, Id replace = 0);
    std::optional<juce::PluginDescription> instrumentByIdentifier(const juce::String &) const;
    void addChannelPresetItems(juce::PopupMenu &, Id channel);
    void addChannelTuningItems(juce::PopupMenu &, Id channel);
    // Detects onsets in a sampler Channel's file and slices it into Channels + a Pattern.
    juce::Result sliceChannel(Id channel, double sensitivity);
    void saveChannelPreset(Id channel);
    void chooseChannelPresetFile(Id channel, const juce::String &xml);
    // Shared UI/MCP preset services; capture is asynchronous for instruments.
    void captureChannelPreset(Id channel, std::function<void(juce::Result, juce::String)> done);
    juce::Result applyChannelPresetFile(Id channel, const juce::File &);
    void loadChannelPreset(Id channel);
    juce::Result verifyChannelManagement(const juce::File &, const juce::File &fixture = {});
    juce::Result verifyLiveBurn(const juce::File &);
    juce::Result verifyAiUi(const juce::File &); // Renders Settings > AI and the Producer window to PNG.
    std::vector<Id> effectiveClipSelection() const;
    juce::Result setPlaylistGrouping(bool);
    juce::Result selectItems(bool clips, const std::vector<Id> &, const juce::String &mode = "replace");
    int selectedInsert = 1;
    MixerSelection mixerSelection;
    void reconcileMixerSelection();
    juce::Result selectMixerMany(const std::vector<int> &, const juce::String &mode = "replace",
                                  std::optional<int> active = {});
    juce::Result selectMixerGroup(const juce::String &);
    juce::var mixerOrganisationState() const;
    void mixerOrganisationDialog(const juce::String &, const juce::String &group = {});
    void mixerOrganisationMenu(int);
    juce::Result verifyMixerOrganisation(const juce::File &);
    juce::String status;
    void command(const juce::String &, CommandArgs = {}, const juce::String &owner = {});
    void openQuantizer();
    juce::Result verifyQuantizer(const juce::File &);
    juce::Component::SafePointer<juce::Component> quantizerWindow;
    juce::var quantizerPreviewState;
    std::optional<GrooveTemplate> quantizerGuide;
    Id quantizerGuidePattern = 0;
    std::uint64_t quantizerGuideSerial = 0;
    void showEditor(int);
    void toggleEditor(int);
    void maximiseEditor(int);
    void moveWorkspaceWindow(int, juce::Rectangle<int>);
    void dockWorkspaceWindow(int);
    void detachWorkspaceWindow(int);
    void tileWorkspaceWindows(int);
    void tileDetachedWindows();
    void leaveAutoTiling();
    juce::Result saveWorkspaceLayout(const juce::File &) const;
    juce::Result loadWorkspaceLayout(const juce::File &);
    void restoreDockedLayout();
    void refresh();
    void loadSample(const juce::String &, bool replace, Id channel = 0);
    void importAudioClip(const juce::File &, double beat, int lane);
    void pollAudioImports();
    void openInstrument(Id);
    bool selectSamplerModulationTarget(Id, int);
    void samplerAutomationMenu(Id, std::uint32_t);
    juce::Result performSamplerAutomationIntent(const SamplerAutomationUiIntent &, SamplerAutomationUiAction,
                                                Id source = 0);
    void samplerAutomationRangeDialog(const SamplerAutomationUiIntent &, Id source);
    void requestClose(std::function<void()>);
    juce::Result runSmokeTest(const juce::File &);
    juce::Result runCoProducerIntegrationTest(const juce::File &, const juce::File &instrumentFixture = {},
                                              bool anticipation = false);
    juce::Result runPianoNavigationTest(const juce::File &, const juce::File &);
    static juce::var mcpTools();
    juce::var mcpCall(const juce::String &, const juce::var &,
                      juce::String *admittedPluginOperation = nullptr,
                      const PluginGesture *lastTweakWitness = nullptr);
    juce::Result verifySamplerGestures(const juce::File &);
    juce::Result verifyAutomation(const juce::File &);
    juce::Result runInstrumentOpeningTest(const juce::File &project, const juce::File &output);
    juce::Result openProjectFile(const juce::File &, bool discardChanges = false);
    juce::Result newProject(bool demo = false, bool discardChanges = false);
    void runMacro(const juce::String &);
    juce::File profileFile() const;
    void saveProfile();
    juce::String macroConfigurationGeneration() const;
    juce::Result changeMacro(MacroConfigurationChange, const Macro &, const juce::String &id,
                              const juce::String &generation);
    juce::Result runSavedMixerMacro(const juce::String &, const juce::String &generation);
    juce::var mcpMacroOperation(const juce::var &);
    void applyAppearance();
    void closeSettings();
    void showPlugins();
    juce::Result browseMixerEffects(int insert, juce::String &errorCode);
    juce::var mixerEffectBrowserState() const;
    juce::Result verifyMixerEffectSlots(const juce::File &output);
    juce::Result verifyMixerChainControlsUi();
    void pluginMenu(Id);
    void effectMenu(int insert);
    juce::var mixerChainOperation(const juce::var &, juce::String *admittedOperation = nullptr);
    juce::var mixerChainReadback(const juce::String &operation = {}) const;
    const effect_chain::Clipboard* mixerChainClipboardData() const noexcept; // CONTROL test/readback data, never a native handle.
    juce::Result verifyMixerChainWorkflow(const juce::File&, const juce::File&);
    bool performMixerChainAction(const juce::String&);
    bool mixerChainBusy() const noexcept;
    bool mixerChainApplying() const noexcept;
    bool mixerChainRecovering() const noexcept;
    bool mixerChainCaptureDispatch = false;
    std::optional<HistoryDirection> mixerChainHistoryDispatch;
    std::optional<HistoryDirection> nativeScalarHistoryDispatch;
    std::optional<HistoryDirection> pluginIoHistoryDispatch;
    std::optional<HistoryDirection> structuralHistoryDispatch;
    juce::Result verifyPluginIo(const juce::File &, const juce::File &);
    juce::var pluginIoState(Id) const;
    void addPluginIoMenu(juce::PopupMenu &, Id, int &nextItem);
    const CommandArgs *mixerInsertLifecycleDispatch =
        nullptr; // Borrowed only during synchronous private admission.
    std::optional<HistoryDirection> mixerInsertHistoryDispatch;
    juce::String lastLifecycleOperation;
    const CommandArgs *mixerStructuralDispatch = nullptr; // Borrowed synchronous private admission only.
    juce::String mixerStructuralCommandDispatch;
    std::optional<HistoryDirection> mixerRoutingHistoryDispatch;
    juce::String lastStructuralOperation;
    juce::String *canonicalStructuralAdmissionOperation = nullptr; // Borrowed synchronous admission output.
    bool requiresBoundedMixerStructural(const juce::String &, const CommandArgs &) const;
    void confirmMixerInsertDeletion(int, std::optional<int> replacement);
    const CommandArgs *mixerEffectSidechainDispatch = nullptr; // Synchronous private adapter only.
    juce::String lastMixerEffectSidechainOperation;
    bool mixerEffectReplacementDispatch = false;      // Private canonical command adapter only.
    juce::String lastMixerEffectReplacementOperation; // Exact shared Apply job, not model-only success.
    juce::String
        lastMixerChainHistoryOperation; // Exact asynchronous history receipt, even if already terminal.
    juce::Result beginMixerChainApplyCapture();
    void finishMixerChainApplyCapture(const juce::String &, juce::Result, Project) noexcept;
    juce::Result mixerChainApplySourceCurrent(bool finalNative) const;
    void serviceMixerChainApply() noexcept;
    bool mixerChainCaptureException() noexcept;
    juce::Result admitMixerChainCapture();
    void serviceMixerChainFiles() noexcept;
    void finishMixerChainCapture(const juce::String&, juce::Result, Project) noexcept;
    void chooseMixerChainDirectory();
    void chooseMixerChainFile(const juce::String&, int);
    struct MixerChainWorkflow;
    std::shared_ptr<MixerChainWorkflow> mixerChains; // CONTROL-owned; defined in StudioMixerChains.inc.

    void showPluginParameters(Id);
    void openPluginControlsUI(Id);
    juce::Result recoverPlugin(Id, bool reopenControls = false);
    juce::Result selectPluginParameter(Id, std::optional<std::uint32_t>, bool lastTweaked);
    juce::var pluginControlSnapshot() const;
    unsigned refreshPluginCatalogue(Id, const juce::String &instance);
    bool readPluginCatalogue(Id, const juce::String &instance, PluginParameterCatalogue &,
                             bool initial = false);
    juce::Result setPluginNotifications(Id, const juce::String &instance, bool enabled);
    juce::Result verifyPluginParameters(const juce::File &, const juce::File &, bool wrapperOnly = false,
                                       bool controlsOnly = false);
    juce::Result verifyNativeScalarHistory(const juce::File &, const juce::File &);
    juce::Result verifyPluginSaveCapture(const juce::File &, const juce::File &);
    juce::Result verifyPluginCatalogueControls(const juce::File &, const juce::File &);
    juce::Result verifyMixerEffectOpening(const juce::File &, const juce::File &, bool originalFixture,
                                          bool expectNoEditor = false);

  private:
    struct LastPluginTweak
    {
        Id plugin = 0;
        juce::String instance, project;
        PluginGesture gesture;
    };
    std::optional<LastPluginTweak> lastPluginTweak();
    juce::String lastPluginTweakProject, lastPluginTweakInstance;
    std::uint64_t lastPluginTweakTicks = 0;
    bool lastPluginTweakAmbiguous = false;
    juce::PopupMenu lastPluginTweakMenu(const std::optional<LastPluginTweak> &);
    juce::Result requestLastPluginTweak(const LastPluginTweak &, const juce::String &,
                                        juce::String &operation, juce::String &errorCode);
    juce::Result applyNativeAutomation(Id, const PluginBinding &, const juce::String &);
    struct PluginOperation
    {
        juce::String id, kind, project, instance, state = "pending", code, message, action;
        Id plugin = 0;
        std::uint32_t parameter = 0;
        unsigned serial = 0, generation = 0, completedSerial = 0, completedGeneration = 0;
        juce::uint32 started = 0;
        std::optional<std::uint64_t> revision;
        std::uint64_t completedRevision = 0;
        std::shared_ptr<PluginHost> host;
        std::optional<PluginEditorResult> editorResult;
        std::optional<PluginGesture> lastTweakWitness;
        juce::String childOperation;
        PluginMainIo io{};
        bool nativeApplied = false, nativeDispatched = false, historyCommitted = false;
        float scalarBefore = 0, scalarAfter = 0;
        bool scalarBeforeKnown = false;
        std::vector<NativeScalarEdit> scalarEdits;
        // CONTROL-only containing-chain ownership. No worker wait occurs in DSP.
        juce::String phase, descriptor;
        int mixer = -1, slot = -1;
        juce::var chainOptions;
        bool choosing = false, cancelRequested = false, closesPrepared = false;
        struct ChainClose
        {
            Id plugin = 0;
            std::shared_ptr<PluginHost> host;
            juce::String instance;
            unsigned serial = 0;
            std::optional<PluginEditorResult> result;
        };
        std::vector<ChainClose> closes;
    };
    std::vector<PluginOperation> pluginOperations;
    struct PluginWrapperPending
    {
        Id plugin = 0;
        std::shared_ptr<PluginHost> host;
        juce::String project, instance, operation, failure;
        PluginWrapperRequest request;
        juce::uint32 lastActivity = 0;
        float value = 0;
        bool gestureOpen = false, closing = false;
    };
    std::optional<PluginWrapperPending> pluginWrapperPending;
    bool servicingPluginWrapper = false;
    void admitPluginWrapperRequest(Id, const std::shared_ptr<PluginHost> &, const PluginWrapperRequest &);
    void servicePluginWrapperRequests();
    juce::Result requestPluginAudioIo(Id, const juce::String &, const PluginMainIo &, juce::String &operation,
                                      juce::String &errorCode);
    juce::Result requestPluginChain(Id, const juce::String &, const juce::String &, const juce::var &,
                                    bool interactive, juce::String &operation, juce::String &errorCode);
    juce::Result pluginChainContext(const PluginOperation &) const;
    void servicePluginChain(PluginOperation &);
    void choosePluginChainPath(const juce::String &);
    void choosePluginDetector(const juce::String &);
    void finishPluginChainChoice(const juce::String &, const juce::File &, bool overwriteApproved = false);
    void cancelPluginChainUi(const juce::String &);
    juce::String pluginChainUiOperation, pluginChainAdmissionOperation;
    juce::Component::SafePointer<juce::AlertWindow> pluginChainConfirmation;
    struct NativeScalarWork
    {
        juce::String operation, project, instance, failure;
        Project source;
        Id plugin = 0;
        std::shared_ptr<PluginHost> host;
        unsigned generation = 0, inFlight = 0, epoch = 0;
        std::uint64_t revision = 0, losses = 0, captureSession = 0;
        std::optional<float> queued;
        std::vector<NativeScalarEdit> edits;
        // A prepared native patch reuses scalar ACK/capture/history ownership.
        std::vector<std::pair<std::uint32_t, float>> batch;
        std::size_t batchCursor = 0;
        std::vector<Id> captureIds;
        juce::uint32 gestureActivity = 0;
        bool gestureOpen = false, dispatched = false, applied = false, capturing = false;
        // During a genuine MIDI take this is a live native write, never a state
        // capture/history owner. Stop and ordinary document edits must still work.
        bool recordingWriteOnly = false;
    };
    std::unique_ptr<NativeScalarWork> nativeScalar;
    bool servicingNativeScalar = false, nativeScalarCaptureDispatch = false;
    juce::String lastNativeScalarOperation;
    std::map<Id, std::pair<juce::String, std::uint64_t>> nativeScalarGestureLosses;
    bool nativeScalarBusy() const noexcept
    {
        return nativeScalar && !nativeScalar->recordingWriteOnly;
    }
    bool nativeScalarPending(const juce::String &) const;
    juce::Result requestNativeScalar(Id, std::uint32_t, unsigned, float, const juce::String &,
                                     bool gestureOpen, juce::String &operation);
    juce::Result requestNativePatch(Id, unsigned, const juce::String &,
                                   std::span<const std::pair<std::uint32_t,float>>, juce::String &operation);
    void finishNativeScalarGesture(const juce::String &);
    juce::Result cancelNativeScalar(const juce::String &);
    void pollNativeScalarOperations();
    void finishNativeScalarCapture(const juce::String &, juce::Result, Project);
    void completeNativeScalar(const char *, const char *, const juce::String &);
    juce::Result nativeScalarAdmission(bool recordingWriteOnly = false);
    juce::Result requestPluginEditor(Id, bool open, const juce::String &instance, juce::String &operation,
                                     juce::String &errorCode, PluginWrapperView view = PluginWrapperView::plugin,
                                     std::uint64_t selection = 0);
    void openPluginEditorUI(Id, bool open = true);
    juce::var pluginEditorState(Id) const;
    void pollPluginOperations();
    juce::var mcpPluginOperation(const juce::var &, juce::String *admittedOperation = nullptr,
                                 const PluginGesture *lastTweakWitness = nullptr);
    juce::var mcpCatalogueOperation(const juce::var &);
    juce::var mcpSynthOperation(const juce::var &);
    juce::Component::SafePointer<juce::Component> mcpPointerTarget;
    juce::Point<float> mcpPointerOrigin;
    int mcpPointerEditor = -1, mcpPointerFlags = 0;
    juce::String mcpPointerProject;
    struct McpGesture
    {
        juce::String id, project, label, state = "pending", reason;
        double lastActivity = 0;
        int leaseMs = 30000;
        juce::String domain = "document";
        Id pattern = 0, channel = 0;
        int sourceInsert = 0, targetInsert = 0;
    };
    std::optional<McpGesture> mcpGesture;
    bool mcpGestureDispatch = false;
    juce::String mcpBatchOwner; // CONTROL scoped canonical batch token, never a UI implicit owner.
    void pollMcpGesture();
    class Editor;
    class EditorWindow;
    class Browser;
    class Splitter;
    class InstrumentWindow;
    class DetachedWindow;
    std::array<std::unique_ptr<DetachedWindow>, 5> detachedWindows;
    std::map<Id, std::unique_ptr<InstrumentWindow>> instrumentWindows;
    struct PendingInstrumentOpening
    {
        juce::String projectIdentity, descriptor;
        Id channel = 0, plugin = 0;
        bool matches(const juce::String &identity, const Project &project) const
        {
            const auto *target = project.channel(channel);
            return identity == projectIdentity && target && target->instrument &&
                   target->instrument->id == plugin && target->instrument->descriptor == descriptor;
        }
    };
    std::optional<PendingInstrumentOpening> pendingInstrument;
    std::unique_ptr<Browser> browser;
    std::array<std::unique_ptr<EditorWindow>, 4> windows;
    std::unique_ptr<Splitter> browserSplit, arrangementSplit, rackSplit, mixerSplit;
    juce::Rectangle<int> workspace;
    int maximisedEditor = -1;
    int autoTileMode = 0, tilePrimaryEditor = 2;
    std::array<bool, 5> floatingWindows{};
    std::array<juce::Rectangle<int>, 5> floatingBounds;
    float browserWidth = 228, rackWidth = 450, mixerHeight = 280, playlistRatio = .52f;
    bool browserShown = true;
    WorkspaceLook look;
    std::unique_ptr<juce::Drawable> brand;
    ToolbarButton play{"Play"}, mode{"Pattern"}, newPattern{"+ Pattern"}, clonePattern{"Clone"}, save{"Save"},
        open{"Open"}, exportWav{"Export WAV"}, devices{"Settings"}, demo{"Demo"};
    ToolbarButton stop{"Stop"}, undoButton{"Undo"}, redoButton{"Redo"}, browserButton{"Browser"};
    ToolbarButton record{"Record"}, loopButton{"Loop"};
    class GuideButton final : public ToolbarButton
    {
      public:
        std::function<void()> menu;
        explicit GuideButton(const juce::String &name) : ToolbarButton(name) {}
        void mouseDown(const juce::MouseEvent &event) override
        {
            if (event.mods.isRightButtonDown() && menu)
                menu();
            else
                ToolbarButton::mouseDown(event);
        }
    };
    GuideButton click{"Click"}, countIn{"Count-in"}, typingButton{"Typing keys"};
    // Arms audio-input recording (right-click: input, monitoring); shows the input level.
    class AudioInputButton final : public ToolbarButton
    {
      public:
        std::function<void()> menu;
        std::array<float, 2> level{};
        int channels = 2;
        using ToolbarButton::ToolbarButton;
        void mouseDown(const juce::MouseEvent &event) override
        {
            if (event.mods.isRightButtonDown() && menu)
                menu();
            else
                ToolbarButton::mouseDown(event);
        }
        void paintOverChildren(juce::Graphics &g) override
        {
            if (!getToggleState())
                return;
            for (int c = 0; c < channels; ++c)
            {
                const auto db = juce::Decibels::gainToDecibels(level[size_t(c)], -60.f);
                const float fill = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
                const float y = float(getHeight() - 5 + c * 2 - (channels - 1));
                g.setColour(juce::Colours::white.withAlpha(.12f));
                g.fillRect(5.f, y, float(getWidth() - 10), 1.5f);
                g.setColour(db > -1.f ? juce::Colour(0xffef6678) : db > -9.f ? juce::Colour(0xffe8a33d)
                                                                             : juce::Colour(0xff35c9b0));
                g.fillRect(5.f, y, float(getWidth() - 10) * fill, 1.5f);
            }
        }
    };
    AudioInputButton audioInputButton{"Audio input"};
    ToolbarButton toolsButton{"Tools"};
    std::unique_ptr<juce::TooltipWindow> tooltips; // Hover hints for every window of the app.
    ScoreLog scoreLog;
    void drainScoreLog();
    // Step record: with the transport stopped, played notes (MIDI or typing keyboard) are
    // written at a cursor in the selected Pattern; a chord commits when every key is up and
    // the cursor then advances by the snap (one Undo per chord).
    bool midiRecordReplace = false; // MIDI takes replace notes in the recorded span (else overdub).
    bool stepRecord = false;
    double stepCursor = 0;
    std::map<int, float> stepHeld;
    std::vector<std::pair<int, float>> stepChord;
    void stepRecordEvent(const MidiCapture::Event &);
    double stepLength() const { return snap > 0 ? snap : .25; }
    juce::Result verifyScoreLog(const juce::File &);
    TypingKeyboard typingKeys;
    PerformanceInput performanceInput;
    int previewPitch = -1;
    juce::Result verifyPianoNavigation(const juce::File &, const Project *fixture = nullptr);
    juce::Result verifyPianoLoopPreview(const juce::File &, const Project *fixture);
    juce::Result verifyScoreTools(const juce::File &);
    Id typingTarget = 0;
    void typingMenu();
    bool persistentTyping = false;
    void saveTypingPreferences();
    juce::Result verifyTypingKeyboard(const juce::File &);
    bool recordArmed = false, midiTaking = false;
    // Audio-input recording: user preferences (persisted) and the current/last take.
    struct AudioInputSettings
    {
        bool enabled = false, monitor = false;
        int input = 0, channels = 2; // First device input channel (0-based), mono 1 / stereo 2.
        float monitorGain = 1.f;
        // Separate tracks recorded at once from consecutive inputs, each `channels` wide
        // (a drum kit on several mics): one file, Channel and Playlist track per input.
        int tracks = 1;
        // Mixer insert the recorded Channels feed (0 = Master); several tracks feed
        // consecutive inserts from here.
        int insert = 0;
        int width() const noexcept { return channels * tracks; }
    };
    struct AudioTakeOutcome
    {
        std::uint64_t serial = 0;
        juce::String phase = "idle", code, message;
        juce::File file;
        double beat = 0, seconds = 0, latencyMs = 0, droppedSeconds = 0;
        int lane = 0;
        Id clip = 0, channel = 0, group = 0;
        std::vector<Id> clips; // One per loop pass, in recording order; the last one plays.
        std::vector<juce::File> files; // One per input track (several-track takes), `file` is the first.
        int passes = 0;
        bool interrupted = false;
    };
    std::optional<std::pair<double, double>> audioTakeLoop; // Loop range at take start.
    // Crash safety: every capturing take has a JSON sidecar next to its WAV and an entry
    // in the active-take index; entries left at start are interrupted takes to recover.
    struct RecoveredTake
    {
        juce::File file;
        double beat = 0, seconds = 0;
        bool mapped = false;
        juce::String project;
    };
    std::vector<RecoveredTake> recoveredTakes;
    // Audio Clip stretching: rendered on a worker, applied by audio.stretchApply.
    struct StretchJob
    {
        std::thread thread;
        std::atomic<bool> done{false};
        juce::Result result = juce::Result::ok();
        Id clip = 0;
        juce::File target;
        double seconds = 0;
        bool bakedFades = false;
        double warpBpm = 0; // A tempo warp: recorded on the clip so it can be re-warped.
        juce::String project, operation;
        ~StretchJob()
        {
            if (thread.joinable())
                thread.join();
        }
    };
    std::unique_ptr<StretchJob> stretchJob;
    MtsEspMaster mtsEsp; // Broadcasts the mtsEsp Channel tuning (Studio timer).
    // Warped clips follow tempo edits: the tempo/position signature each clip was last
    // warped for, and when a pending change was first seen (debounce).
    std::map<Id, juce::String> warpSignatures;
    double rewarpPendingSince = 0;
    void followTempoWarps();
    // Video player following the transport (Tools > Video player).
    std::unique_ptr<class VideoPlayerWindow> videoWindow;
    // Performance mode: launched Patterns are written into the Song at bar boundaries.
    std::map<Id, performance::Pad> performancePads;
    std::unique_ptr<juce::DocumentWindow> performanceWindow;
    juce::Result launchPattern(Id pattern, bool on);
    void pollPerformance();
    void showPerformance();
    std::unique_ptr<juce::DocumentWindow> drumPadsWindow;
    // Learns the samples/instruments the producer loads most (Browser "Recent" section).
    RecentSources recentSources;
    // Autosave, backups and crash recovery (StudioAutosave.cpp). Autosave writes the dirty
    // project every `autosaveSeconds` while stopped; a session marker without a clean exit
    // offers the autosave at the next start; Save keeps `backupCount` previous versions.
    bool autosaveEnabled = false, autosaveRunning = false;
    double autosaveSeconds = 120, lastAutosaveTime = 0;
    std::uint64_t autosaveRevision = 0;
    static constexpr int backupCount = 5;
    juce::File lastAutosaveFile;
    juce::File autosaveDirectory() const;
    juce::File autosaveFile() const;
    juce::File sessionMarkerFile() const;
    void writeSessionMarker();
    void startAutosaveSession();
    void endAutosaveSession();
    void serviceAutosave();
    void autosaveNow(std::function<void(juce::Result)> completion);
    juce::Result recoverAutosave(const juce::File &autosave, const juce::File &original);
    void rotateBackups(const juce::File &project);
    struct CrashedSession
    {
        juce::File marker, autosave, project;
        juce::Time time;
    };
    // In-app updates from GitHub Releases (StudioUpdater.cpp, logic in Updater.h). Checks
    // run on a worker thread; the user sees what changed and approves every download and
    // install. Phases: idle, checking, upToDate, available, downloading, ready, failed.
    struct UpdateJob;
    std::shared_ptr<UpdateJob> updateJob;
    juce::String updatePhase = "idle", updateMessage, updateSkippedTag;
    double updateProgress = 0;
    bool updateOfferShown = false;
    juce::File updateDownloaded;
    std::optional<updates::Release> availableUpdate;
    juce::File updateSettingsFile() const;
    juce::var loadUpdateSettings() const;
    void saveUpdateSettings(const juce::var &);
    void scheduleStartupUpdateCheck();
    juce::Result checkForUpdates(bool manual, bool interactive = true);
    void cancelUpdateWork();
    void pollUpdates();
    void showUpdateOffer();
    juce::Result downloadUpdate();
    void offerUpdateInstall();
    void installDownloadedUpdate();
    void updateSettingsMenu();
    // Newest autosave left by a VibeRa that did not exit cleanly; older markers are removed.
    std::optional<CrashedSession> findCrashedSession();
    void showDrumPads();
    std::unique_ptr<juce::DocumentWindow> chordWindow;
    void showChordProgression();
    juce::var performanceState() const;
    bool togglePerformancePad(int index);
    // Piano Roll scale/maqam guide (view only): name empty = off.
    struct PianoGuide
    {
        juce::String name;
        int root = 0;
        bool maqam = false;
    } pianoGuide;
    juce::Result relinkMissingMedia(const juce::File &folder);
    // Song/Pattern/stem export format (export menu, MCP sampleRate/bitDepth).
    double exportRate = 48000;
    int exportBits = 24;
    int exportTail = 0; // 0 cut, 1 leave the remainder, 2 wrap it onto the start.
    bool exportCancelRequested = false;
    // Stops the running export render (and any stems still queued); false when none runs.
    bool cancelExport();
    // Moves an Audio Clip (with its take group) so its first transient lands on the nearest
    // multiple of `grid` beats; one clip.move Undo step. Reports the shift in the status.
    juce::Result alignClipToGrid(Id clip, double grid = .25);
    // Song looping follows the Playlist time selection (whole Song without one).
    void syncSongLoop()
    {
        if (patternMode || !playlistRange)
            return audio.setSongLoopBeats(std::nullopt);
        audio.setSongLoopBeats(std::pair{std::min(playlistRange->first, playlistRange->second),
                                         std::max(playlistRange->first, playlistRange->second)});
    }
    juce::Result openVideo(const juce::File &);
    void closeVideo();
    juce::var videoState() const;
    juce::Result stretchAudioClip(Id clip, double ratio, const juce::String &operation = {});
    juce::Result warpAudioClip(Id clip, double sourceBpm, const juce::String &operation = {});
    // Tempo of an audio file: rhythm analysis for longer audio, otherwise the whole-beat
    // length (1, 2, 4 ... 64 beats) giving a tempo in 70..180 BPM closest to the Song tempo.
    // `how` says which method answered.
    double detectFileTempo(const juce::File &, juce::String &how) const;
    // Fits an Audio Clip to the Song tempo (pitch kept) from its source tempo; 0 detects it.
    juce::Result fitAudioClipToTempo(Id clip, double sourceBpm = 0);
    // reverse | normalize (value dBFS) | gain (value dB) | consolidate, as a new source.
    juce::Result editAudioClip(Id clip, const juce::String &edit, double value, const juce::String &operation = {});
    void pollStretchJob();
    juce::File audioTakeIndex;
    bool audioTakeSidecarMapped = false;
    juce::File audioTakeSidecar() const { return audioTake.file.withFileExtension("take.json"); }
    void writeAudioTakeSidecar(std::optional<double> startEdit);
    void forgetAudioTakeSidecar(const juce::File &take);
    void scanRecoveredTakes();
    juce::Result placeRecoveredTake(const juce::File &);
    std::vector<TakePass> audioTakePasses;
    int audioTakeTracks = 1; // Input tracks of the capturing take (settings may change after).
    // Splits a several-track take into one WAV per track (`<name>-in<k>.wav`).
    static juce::Result splitAudioTake(const juce::File &, int tracks, std::vector<juce::File> &parts);
    AudioInputSettings audioInput;
    // Mixer record arm: route the recording input into `insert` (again disarms it).
    juce::Result toggleInsertRecordArm(int insert);
    bool insertRecordArmed(int insert) const { return audioInput.enabled && audioInput.insert == insert && insert > 0; }
    AudioTakeOutcome audioTake;
    std::uint64_t audioTakeSerial = 0;
    bool audioTaking = false, audioTakePlacementPending = false, persistentAudioInput = false;
    std::array<float, 2> audioInputLevel{};
    void loadAudioInputSettings(bool persistent);
    void saveAudioInputSettings() const;
    juce::Result applyAudioInput(const AudioInputSettings &);
    juce::String audioInputReadiness() const;
    int audioInputCompactIndex() const;
    void beginAudioTake();
    void finishAudioTake();
    void cancelAudioTake();
    void placeAudioTake();
    void pollAudioTake();
    void audioInputMenu();
    juce::File nextAudioTakeFile() const;
    juce::var audioRecordingState() const;
    juce::var audioRecordingOperation(const juce::var &);
    static juce::var audioRecordingToolDefinition();
    MidiTake midiTake;
    struct PendingMidiTake
    {
        enum class Phase
        {
            capturing,
            sealing,
            ready,
            applying,
            invalidCapture
        };
        enum class Error
        {
            none,
            preparation,
            captureInvalid,
            sourceChanged,
            targetMissing,
            placementChanged,
            busy,
            commandRejected
        };
        juce::String identity;
        std::uint64_t serial = 0;
        CommandArgs args;
        double placementBeat = 0, placementOffset = 0, stopSeconds = 0;
        Phase phase = Phase::capturing;
        Error error = Error::none;
    };
    std::unique_ptr<PendingMidiTake> pendingMidiTake;
    std::uint64_t nextMidiTakeSerial = 0, lastMidiTakeSerial = 0, lastMidiTakeRevision = 0;
    int lastMidiTakeNoteCount = 0;
    bool lastMidiTakeCompleted = false, midiTakeUiPending = false;
    juce::String retainedMidiTakeStatus{"MIDI take retained; Stop/Record retries, Escape cancels"};
    juce::String cancelledMidiTakeStatus{"MIDI take cancelled; project unchanged"};
    juce::String committedMidiTakeStatus{"MIDI take applied (one Undo)"};
    juce::String emptyMidiTakeStatus{"No MIDI notes recorded"};
    std::optional<AudioClipImporter::Job> audioImportAfterTake;
    enum class DeferredAudioImportPhase
    {
        pending,
        applying,
        committed
    };
    DeferredAudioImportPhase audioImportPhase = DeferredAudioImportPhase::pending;
    bool pollingAudioImports = false;
    Id audioImportClip = 0, audioImportChannel = 0;
    juce::String committedAudioImportStatus{"Audio clip imported"};
    juce::String retainedAudioImportStatus{"Audio import retained; waiting for take or UI recovery"};
    void stopMidiCaptureForRecovery();
    std::vector<Note> midiPreview;
    Id takePattern = 0, takePlacement = 0;
    bool takePatternMode = true;
    double takeSongBeat = 0;
    double takeSourceOffset = 0;
    void beginMidiTake(double leadSeconds = 0);
    void drainMidiTake();
    juce::Result verifyMidiRecording(const juce::File &);
    juce::Result verifyCountInRecording(const juce::File &);
    std::array<ToolbarButton, 4> editorButtons;
    class PatternSelector final : public juce::ComboBox
    {
      public:
        std::function<void()> menu;
        std::function<void()> focusSelector;
        std::function<bool(const juce::KeyPress &)> shortcut;
        void mouseDown(const juce::MouseEvent &event) override
        {
            if (focusSelector)
                focusSelector();
            if (event.mods.isRightButtonDown() && menu)
                menu();
            else
                juce::ComboBox::mouseDown(event);
        }
        void focusGained(FocusChangeType) override
        {
            if (focusSelector)
                focusSelector();
        }
        bool keyPressed(const juce::KeyPress &key) override
        {
            return (shortcut && shortcut(key)) || juce::ComboBox::keyPressed(key);
        }
    };
    PatternSelector patterns;
    juce::ComboBox snapChoice;
    ToolbarButton patternOptions{"Pattern menu"};
    juce::Slider tempo;
    juce::Label title, deviceLabel;
    juce::ThreadPool midiFileJobs{1};
    // CONTROL owns sessions; bounded worker jobs own immutable prepared PCM only.
    // No job captures Studio, a widget or a live document reference.
    struct SamplerTempoJob
    {
        juce::String id, project, source;
        Id channel = 0;
        SamplerSettings settings;
        std::shared_ptr<const Sample> sample;
        std::atomic<bool> cancel{false}, finished{false};
        double estimate = 0; // Published by finished.release, read after acquire.
        bool analyzed = false, analysisFailed = false, applied = false, documentChanged = false;
        double appliedBpm = 0;
        bool appliedFit = false;
    };
    std::vector<std::shared_ptr<SamplerTempoJob>> samplerTempoJobs;
    juce::var samplerTempoOperation(const juce::var &);
    bool midiFileBusy = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::AlertWindow> patternPropertiesWindow;
    juce::Component::SafePointer<juce::AlertWindow> patternMeterWindow;
    juce::Component::SafePointer<juce::AlertWindow> rackGroupWindow;
    juce::Component::SafePointer<juce::AlertWindow> channelPropertiesWindow;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    // CoProducer side panel: docked at the right of the workspace, never modal.
    std::unique_ptr<CoProducerDock> coProducerDock;
    std::unique_ptr<Splitter> coProducerSplit;
    float coProducerWidth = 420;
    bool coProducerShown = false;
    int coProducerTab = 0; // 0 chat, 1 MIDI ideas
    ToolbarButton coProducerButton{"CoProducer"};
    void showCoProducer(int tab = -1); // -1 keeps the current tab.
    void hideCoProducer();
    // CoProducer chat: settings, encrypted keys, local music engines and the chat agent.
    // The agent is declared last so it is destroyed (cancelled and joined) first.
    ai::AiSettings aiSettings;
    ai::MusicEngines aiMusic;
    struct AiPlacement
    {
        juce::String project;
        double beat = 0;
        int lane = -1; // -1: first free lane.
        bool pending = false;
        juce::File placed;
    } aiPlacement;
    ai::McpLoopbackHost aiBridge; // Live tools for local agent CLIs (destroyed after the agents).
    ai::CliAgent aiCli;          // Codex or Claude Code CLI turns (subscription providers).
    juce::String aiCliPreset;    // Which CLI owns aiCli's thread; another one starts fresh.
    ai::ProducerAgent aiAgent;
    std::atomic<bool> aiDetecting{false}; // Declared first: outlives the detection thread below.
    struct JoiningThread
    {
        std::thread thread;
        ~JoiningThread()
        {
            if (thread.joinable())
                thread.join();
        }
    } aiDetect;
    // Whichever agent the selected provider uses.
    bool aiUsesCodex() const
    {
        return aiSettings.dialect() == ai::Dialect::codexCli || aiSettings.dialect() == ai::Dialect::claudeCli;
    }
    std::vector<ai::AgentEvent> aiEvents() const
    {
        return aiUsesCodex() ? aiCli.events() : aiAgent.events();
    }
    bool aiBusy() const
    {
        return aiCli.busy() || aiAgent.busy();
    }
    void aiCancel()
    {
        aiCli.cancel();
        aiAgent.cancel();
    }
    void aiReset()
    {
        aiCli.reset();
        aiAgent.reset();
    }
    void aiDetectAgents(); // Background CLI detection; may switch an unconfigured provider to Codex.
    juce::File aiSettingsFile() const;
    ai::SecretStore aiSecrets() const;
    ai::LlmConfig aiLlmConfig() const;
    void saveAiSettings();
    std::vector<ai::ToolSpec> aiTools() const;
    ai::ToolOutcome aiExecuteTool(const juce::String &, const juce::var &, const ai::CancelCheck &, bool mayEdit);
    juce::Result aiSend(const juce::String &);
    juce::Result aiStartMusic(const ai::MusicRequest &, double beat, int lane);
    juce::File aiOutputFolder() const;
    void pollAiMusic();
    juce::var mcpAiOperation(const juce::var &);
    static juce::var aiToolDefinition();
    struct AiConnectionTest
    {
        std::thread worker;
        std::atomic<bool> running{false};
        std::mutex mutex;
        juce::String message;
        juce::StringArray models;
        bool ok = false;
        ~AiConnectionTest()
        {
            if (worker.joinable())
                worker.join();
        }
    } aiConnection;
    void aiTestConnection();
    void pinAiEngineRoot();
    juce::Component::SafePointer<juce::DocumentWindow> parameterWindow;
    juce::File projectFile;
    std::uint64_t engineRevision = 0;
    AudioClipImporter audioImports;
    bool needsRebuild = true, renderWasActive = false;
    struct PluginRecovery
    {
        Id id;
        juce::String project;
        bool reopenControls;
    };
    std::optional<PluginRecovery> pluginRecovery;
    juce::String projectionError;
    // Shared CONTROL final admission; never takes ownership of a pending Save.
    juce::Result refreshProjectTransitionState(juce::String &errorCode);
    juce::Result admitProjectTransition(bool discardChanges, juce::String &errorCode);
    void requestProjectTransition(std::function<void(bool discardChanges)>);
    std::function<void(juce::Result, Project)> afterPluginState;
    std::optional<Project> pendingExport;
    juce::File pendingExportFile;
    // Pattern render that becomes a Playlist audio clip once the WAV is complete.
    struct RenderClipRequest
    {
        juce::File file;
        juce::String project;
        double beat = 0;
        int lane = 0;
        std::vector<Id> muteAfter; // Render selected clips: the originals are muted once placed.
    };
    std::optional<RenderClipRequest> renderClip;
    juce::Result renderPatternToClip(Id pattern);
    juce::Result renderClipsToAudio(std::vector<Id> clips);
    void completeRenderClip(const juce::Result &rendered); // Places (or drops) a pending renderClip.
    void withPluginState(std::function<void(juce::Result, Project)>, const std::vector<Id> *only = nullptr);
    void saveTo(const juce::File &, std::function<void(juce::Result)> completion = {});
    void exportTo(const juce::File &, Id patternOnly = 0, bool stems = false, std::vector<Id> clipsOnly = {});
    // Export queue: each step renders a WAV through the shared render worker, then (for
    // FLAC/OGG targets) encodes it on a worker thread. Stems queue one step per Mixer insert
    // that a Channel feeds, rendered with that insert soloed with its connections.
    struct ExportStep
    {
        Project snapshot;
        juce::File render, target;
    };
    struct ExportEncoding
    {
        std::thread thread;
        std::atomic<bool> done{false};
        juce::Result result = juce::Result::ok();
        ~ExportEncoding()
        {
            if (thread.joinable())
                thread.join();
        }
    };
    std::deque<ExportStep> exportQueue;
    std::optional<ExportStep> exportCurrent;
    std::unique_ptr<ExportEncoding> exportEncoding;
    juce::StringArray exportOutputs;
    bool exportBusy() const { return exportCurrent.has_value() || !exportQueue.empty() || bool(exportEncoding); }
    std::vector<std::pair<int, juce::File>> stemTargets(const Project &, const juce::File &folder) const;
    juce::Result queueExport(const Project &snapshot, const juce::File &target, bool stems);
    void startNextExport();
    void finishExportStep(juce::Result rendered);
    void pollExportEncoding();
    void exportMenu();
    struct McpOperation
    {
        juce::String id, kind, path, project, state = "pending", message;
        std::uint64_t revision = 0;
        bool documentChanged = false; // Exact transition witness, never inferred from later sessions.
        int expressionNotesOmitted = -1; // MIDI export: unknown until successful encoding.
        int midiExpressionNotesOmitted = -1, midiSlideNotesOmitted = -1, midiPortamentoNotesOmitted = -1;
    };
    std::vector<McpOperation> mcpOperations;
    struct MidiExportSnapshot
    {
        Pattern pattern;
        Id channel;
        double tempo;
        int numerator, denominator;
        juce::String project;
        std::uint64_t revision;
    };
    std::optional<MidiExportSnapshot> midiExportSnapshot() const;
    juce::Result startMidiExport(const juce::File &, MidiExportSnapshot, bool overwrite,
                                 juce::String &operation);
    juce::String mcpExportOperation;
    juce::var mcpProjectOperation(const juce::var &);
    void completeMcpOperation(const juce::String &, juce::Result);

    Id clipboardPattern = 0, clipboardChannel = 0;
    std::shared_ptr<const ClipClipboard> clipClipboard; // Playlist Ctrl+C / Ctrl+X.
    void timerCallback() override;
    void chooseSave();
    void chooseOpen();
    void chooseExport();
    void audioSettings();
};
} // namespace vibera
