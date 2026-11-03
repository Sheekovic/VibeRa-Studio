// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RemoteEffect.h"
#include "PreparedPluginTransport.h"
#include <array>
#include <cmath>
#include <limits>
#include <atomic>
#include <thread>
#include <exception>

// Additive original experiment. Include after ParameterAcceptance.h,
// RoutingLifetimeAcceptance.h and SourceBurstAcceptance.h; do not replace those
// tests or their oracles. Actual native graph/SDK worker, serialized virtual
// callback owner. No physical input/listening/concurrent CONTROL claim.
namespace local_source_initial_acceptance
{
using Session = te::SourceBurstSession;
using Status = Session::Status;
using Capability = te::Edit::NativeCapability;
using Refusal = te::Edit::NativeCapabilityStatus;
using Fixture = vibera::test::routing_lifetime::Fixture;

// A literal musical-time source, not a measured output-fit or callback counter.
// It emits the independent signed bed during [0,3072) musical samples only.
class MusicalSignal final : public te::Plugin
{
  public:
    static constexpr const char *xmlTypeName = "vibera_local_initial_signal";
    int frames = 0, firstMusicalSample = -1;
    explicit MusicalSignal(te::PluginCreationInfo info) : Plugin(info) {}
    ~MusicalSignal() override { notifyListenersOfDeletion(); }
    juce::String getName() const override { return "Original local initial musical signal"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
    bool isSynth() override { return true; }
    bool takesAudioInput() override { return false; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    void initialise(const te::PluginInitialisationInfo &) override {}
    void deinitialise() override {}
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    static float sample(int channel, int at) noexcept
    {
        if (at < 0 || at >= 3072) return 0;
        return (channel ? -.03125f : .0625f) +
               (at % (channel ? 263 : 251) == (channel ? 17 : 0)
                    ? (channel ? -.125f : .25f) : 0.f);
    }
    void applyToBuffer(const te::PluginRenderContext &fc) override
    {
        if (!fc.destBuffer) return;
        const auto at = int(std::llround(fc.editTime.getStart().inSeconds() * sampleRate));
        if (firstMusicalSample < 0) firstMusicalSample = at;
        for (int c = 0; c < fc.destBuffer->getNumChannels(); ++c)
            for (int n = 0; n < fc.bufferNumSamples; ++n)
                fc.destBuffer->setSample(c, fc.bufferStartSample + n,
                                         fc.isPlaying ? sample(c, at + n) : 0.f);
        frames += fc.bufferNumSamples;
    }
};

struct Observer final : Session::Observer
{
    std::shared_ptr<PluginHost> host; // Prepared CONTROL owner, never copied by DSP.
    unsigned begins = 0, ends = 0, aborts = 0;
    bool accept = true;
    explicit Observer(std::shared_ptr<PluginHost> h = {}) : host(std::move(h)) {}
    bool begin(const Session::Outcome &o) noexcept override
    {
        ++begins;
        if (!accept) return false;
        return !host || host->beginPhysicalBurst(o.sourceQuota).succeeded();
    }
    bool end(const Session::Outcome &) noexcept override
    {
        ++ends;
        return !host || host->endPhysicalBurst().succeeded();
    }
    void abort(const Session::Outcome &) noexcept override
    {
        ++aborts;
        if (host) host->abortPhysicalBurst();
    }
};

struct Graph
{
    std::unique_ptr<te::Edit> edit;
    te::Plugin::Ptr source, effect;
    MusicalSignal *musical = nullptr;
    source_burst_acceptance::Signal *peer = nullptr;
    te::EditPlaybackContext *context = nullptr;
    Graph(Fixture &f, std::uint64_t generation, const std::shared_ptr<PluginHost> &host = {})
    {
        auto &engine = f.audio->engine;
        te::Edit::Options options{engine, te::createEmptyEdit(engine), {}};
        options.numAudioTracks = 1;
        options.initialSourceGeneration = generation;
        options.closedNativeIO = generation != 0;
        edit = te::Edit::createEdit(options);
        require(bool(edit), "Actual independent native local/counterpart Edit");
        auto tracks = te::getAudioTracks(*edit);
        require(tracks.size() == 1, "One exact original source track");
        tracks[0]->getOutput().setOutputToDefaultDevice(false);
        if (generation)
        {
            engine.getPluginManager().createBuiltInType<MusicalSignal>();
            source = edit->getPluginCache().createNewPlugin(MusicalSignal::xmlTypeName, {});
            musical = dynamic_cast<MusicalSignal *>(source.get());
            require(musical != nullptr, "Actual cached musical source owner");
        }
        else
        {
            engine.getPluginManager().createBuiltInType<source_burst_acceptance::Signal>();
            source = edit->getPluginCache().createNewPlugin(source_burst_acceptance::Signal::xmlTypeName, {});
            peer = dynamic_cast<source_burst_acceptance::Signal *>(source.get());
            require(peer != nullptr, "Actual cached independent counterpart owner");
        }
        tracks[0]->pluginList.insertPlugin(source, 0, nullptr);
        if (host)
        {
            effect = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
            auto *remote = dynamic_cast<RemoteEffect *>(effect.get());
            require(remote != nullptr, "Actual native RemoteEffect owner");
            auto timeline = std::make_shared<LiveTimeline>();
            auto document = std::make_unique<LiveDocument>();
            document->generation = 1;
            require(timeline->publish(document), "Actual prepared empty automation document");
            remote->configure(host, false, timeline, 1);
            tracks[0]->pluginList.insertPlugin(effect, 1, nullptr);
        }
        require(tracks[0]->pluginList.getPlugins().contains(source.get()),
                "Exact cached original source is in the real native plugin list");
        source_burst_acceptance::prepareClosedNativeEdit(*edit);
        edit->getTransport().ensureContextAllocated();
        context = edit->getTransport().getCurrentPlaybackContext();
        require(context && context->getNodePlayHead(), "Actual native context and local PlayHead");
    }
    ~Graph()
    {
        source = nullptr;
        effect = nullptr;
        edit.reset(); // Context graph reclaims on CONTROL while engine still lives.
    }
};

inline Session::Ptr prepare(Fixture &f, const PreparedPluginTransport::Ptr &plan,
                             const std::shared_ptr<Observer> &observer, std::uint64_t generation)
{
    const auto &b = plan->budget();
    Session::Configuration c;
    c.generation = generation; c.deviceEpoch = b.deviceEpoch;
    c.sampleRate = plan->sampleRate(); c.deviceFrames = b.configuredFrames;
    c.maximumPhysicalFrames = b.physicalFrames; c.maximumSourceFrames = b.sourceFrames;
    c.maximumSourceScalePpm = b.maximumSourceScalePpm;
    Session::Ptr session;
    require(Session::prepare(f.audio->engine.getDeviceManager().deviceManager, c, observer, session) ==
                Status::prepared && session, "Actual manager-derived initial source preparation");
    return session;
}
inline Session::Outcome read(const Session::Ptr &session)
{
    Session::Outcome outcome;
    require(session->read(outcome), "Actual coherent initial source receipt");
    return outcome;
}

struct Capture
{
    juce::AudioBuffer<float> output{2, 768};
    juce::String csv = "phase,physical,callbackMs,new,delete,maxError,peerExpected,localExpected,"
                       "referenceStart,referenceEnd,phaseState,sourceStatus,sourceFailed,sourceOpen\n";
    juce::String pcm = "phase,frame,left,right,expectedLeft,expectedRight\n";
    int peerFrames = 0, localFrames = 0;
    void drive(Fixture &f, Graph &peer, Graph &local, const Session::Ptr &session,
               const juce::File &folder, const char *phase, int frames, bool processLocal,
               bool worker)
    {
        output.setSize(2, frames, false, false, true); // CONTROL, before callback heap probe.
        const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                bool old = callbackAllocationProbe;
                Probe() { callbackAllocationProbe = true; }
                ~Probe() { callbackAllocationProbe = old; }
            } probe;
            f.device->process(output);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        double error = 0;
        for (int n = 0; n < frames; ++n)
        {
            std::array<float, 2> expected{};
            for (int c = 0; c < 2; ++c)
            {
                const int at = localFrames + n - (worker ? 799 : 0);
                expected[std::size_t(c)] = source_burst_acceptance::Signal::sample(c, peerFrames + n) +
                    (processLocal ? MusicalSignal::sample(c, at) : 0.f);
                const float actual = output.getSample(c, n);
                error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected[std::size_t(c)]))
                                              : std::numeric_limits<double>::infinity();
            }
            pcm += juce::String(phase) + "," + juce::String(n) + "," +
                   juce::String(output.getSample(0, n), 9) + "," + juce::String(output.getSample(1, n), 9) + "," +
                   juce::String(expected[0], 9) + "," + juce::String(expected[1], 9) + "\n";
        }
        const auto reference = local.context->getNodePlayHead()->getReferenceSampleRange();
        const auto outcome = session ? read(session) : Session::Outcome{};
        const auto a = callbackAllocations.load() - allocated, d = callbackDestructions.load() - destroyed;
        csv += juce::String(phase) + "," + juce::String(frames) + "," + juce::String(ms, 6) + "," +
               juce::String(a) + "," + juce::String(d) + "," + juce::String(error, 12) + "," +
               juce::String(peerFrames) + "," + juce::String(localFrames) + "," +
               juce::String(reference.getStart()) + "," + juce::String(reference.getEnd()) + "," +
               juce::String(int(local.context->initialSourcePhase())) + "," +
               juce::String(int(outcome.status)) + "," + juce::String(int(outcome.failed)) + "," +
               juce::String(int(outcome.scopeOpen)) + "\n";
        require(folder.getChildFile("callbacks.csv").replaceWithText(csv) &&
                    folder.getChildFile("pcm.csv").replaceWithText(pcm),
                "Retain all startup/failed PCM and callback observations before assertion");
        require(error == 0 && a == 0 && d == 0 && ms <= 1000. * frames / 48000.,
                "Actual local/counterpart exact complete PCM, unchanged physical deadline, no observed callback heap");
        peerFrames += frames;
        if (processLocal) localFrames += frames;
        require(peer.peer->frames == peerFrames && local.musical->frames == localFrames,
                "Native source ownership counts, no pending input/graph consumption or counterpart loss");
    }
};

// Serialized actual native calls. No callback is running while snapshots and
// null requests are made. Async worker processed counts are intentionally NOT
// compared: an already submitted packet may legitimately finish independently.
inline void nullSyncTests(Graph &local, Graph &peer, const std::shared_ptr<Observer> &observer,
                          const Session::Ptr &session, te::EditPlaybackContext::InitialSourcePhase expectedPhase)
{
    const auto localRange = local.context->getNodePlayHead()->getReferenceSampleRange();
    const auto peerRange = peer.context->getNodePlayHead()->getReferenceSampleRange();
    const auto position = local.context->getNodePlayHead()->getPosition();
    const auto unlooped = local.context->getNodePlayHead()->getUnloopedPosition();
    const int localFrames = local.musical->frames, peerFrames = peer.peer->frames;
    const auto begins = observer->begins, ends = observer->ends, aborts = observer->aborts;
    const auto refusal = local.edit->lastNativeCapabilityRefusal();
    const auto state = local.edit->state.createXml()->toString();
    const auto before = session ? read(session) : Session::Outcome{};
    require(local.context->initialSourcePhase() == expectedPhase,
            "Null-sync case begins in exact actual pending/active phase");
    require(local.context->trySyncToContext(nullptr, te::TimePosition::fromSeconds(37.25),
                                           te::TimeDuration::fromSeconds(.75)) == Refusal::allowed,
            "Actual LOCAL-CLOSED null sync returns typed already-satisfied result");
    local.context->syncToContext(nullptr, te::TimePosition::fromSeconds(99.5),
                                te::TimeDuration::fromSeconds(2.5));
    require(local.edit->getTransport().trySyncToEdit(nullptr, true) == Refusal::allowed,
            "Actual transport null-sync companion returns typed already-satisfied result");
    local.edit->getTransport().syncToEdit(nullptr, false);
    const auto after = session ? read(session) : Session::Outcome{};
    require(local.context->getNodePlayHead()->getReferenceSampleRange() == localRange &&
                peer.context->getNodePlayHead()->getReferenceSampleRange() == peerRange &&
                local.context->getNodePlayHead()->getPosition() == position &&
                local.context->getNodePlayHead()->getUnloopedPosition() == unlooped &&
                local.musical->frames == localFrames && peer.peer->frames == peerFrames &&
                observer->begins == begins && observer->ends == ends && observer->aborts == aborts &&
                local.context->initialSourcePhase() == expectedPhase &&
                local.edit->lastNativeCapabilityRefusal() == refusal && local.edit->state.createXml()->toString() == state,
            "Null sync preserves actual local/counterpart reference, musical/source/observer ownership and exact state");
    require(!session || (after.status == before.status && after.generation == before.generation &&
                after.physicalSerial == before.physicalSerial && after.sourceProcessed == before.sourceProcessed &&
                after.deviceOffset == before.deviceOffset && after.scopeOpen == before.scopeOpen &&
                after.failed == before.failed && after.retired == before.retired &&
                after.activationAfterSerial == before.activationAfterSerial),
            "Null sync neither admits/cancels/retires a physical source scope nor changes real async outcome");
}

inline void capabilityTests(Graph &local, Graph &peer, Fixture &f)
{
    auto &e = *local.edit;
    const auto originalState = e.state.createXml()->toString();
    require(e.sourceReferenceDomain() == te::Edit::SourceReferenceDomain::localClosed &&
                e.nativeCapability(Capability::inputOutput) == Refusal::nativeIOForbidden,
            "Immutable LOCAL-CLOSED source capability");
    const auto head = local.context->getNodePlayHead()->getReferenceSampleRange();
    te::TimePosition target = te::TimePosition::fromSeconds(123.25);
    require(local.context->tryGlobalStreamTimeToEditTime(42., false, target) ==
                Refusal::globalTimestampForbidden && target.inSeconds() == 123.25 &&
                std::isnan(local.context->globalStreamTimeToEditTime(42.).inSeconds()) &&
                std::isnan(local.context->globalStreamTimeToEditTimeUnlooped(42.).inSeconds()),
            "Checked conversion preserves output; unsupported legacy call is invalid, never plausible zero");
    require(local.context->trySyncToContext(peer.context, {}, te::TimeDuration::fromSeconds(1)) ==
                Refusal::contextSyncForbidden &&
                peer.context->trySyncToContext(local.context, {}, te::TimeDuration::fromSeconds(1)) ==
                Refusal::contextSyncForbidden &&
                e.getTransport().trySyncToEdit(peer.edit.get(), false) == Refusal::contextSyncForbidden &&
                peer.edit->getTransport().trySyncToEdit(&e, false) == Refusal::contextSyncForbidden,
            "Both direct and transport sync directions refuse exact local context persistently");
    auto input = std::make_shared<te::VirtualMidiInputDevice>(f.audio->engine, "Original local refusal input",
        te::InputDevice::virtualMidiDevice, "vibera-original-local-refusal", false);
    te::MidiOutputDevice output(f.audio->engine, {"Original local refusal output", "vibera-original-local-output"});
    require(input->createInstance(*local.context) == nullptr && output.createInstance(*local.context) == nullptr &&
                local.context->getAllInputs().isEmpty() && local.context->nativeIOAttachmentRefused(),
            "Real built-in virtual input/output factories refuse before instance registration");
    e.enableTimecodeSync(true);
    e.setCurrentMidiTimecodeSource(input);
    e.setCurrentMidiMachineControlSource(input);
    e.setCurrentMidiMachineControlDest(&output);
    e.enableTimecodeSync(false);
    e.setCurrentMidiTimecodeSource({});
    e.setCurrentMidiMachineControlSource({});
    e.setCurrentMidiMachineControlDest(nullptr);
    e.setTimecodeOffset(te::TimeDuration::fromSeconds(7));
    e.setMidiTimecodeIgnoringHours(true);
    require(!e.isTimecodeSyncEnabled() && !e.getCurrentMidiTimecodeSource() &&
                !e.getCurrentMidiMachineControlSource() && !e.getCurrentMidiMachineControlDest() &&
                e.lastNativeCapabilityRefusal() == Refusal::externalClockForbidden &&
                local.context->getNodePlayHead()->getReferenceSampleRange() == head,
            "Native external-clock/MMC changes refused before config/restart/local-clock mutation");
    require(local.context->applyRetrospectiveRecord().failed(), "Native retrospective recording refused");
    e.getTransport().record(false, true);
    require(!e.getTransport().isRecording() && e.state.createXml()->toString() == originalState,
            "Forbidden native recording/settings leave exact model state unchanged, including disable/null requests");
}

// Actual concurrent fixture interleaving only. The callback has NO injected wait,
// notification, CONTROL operation or allocation. Failure to obtain a genuine
// suffix observation is a retained fixture limitation, not fabricated coverage.
inline void suffixTests(const juce::File &folder)
{
    require(folder.createDirectory().wasOk(), "Fresh owned physical-prefix/suffix attempt");
    Fixture f(folder.getChildFile("fixture"));
    for (int n = 0; n < 4; ++n) f.silentCallback("Initial no-note application graph prepared");
    Graph peer(f, 0), local(f, 201);
    auto &manager = f.audio->engine.getDeviceManager();
    auto reader = manager.deviceManager.getPhysicalCallbackReader();
    juce::AudioDeviceManager::PhysicalCallbackSnapshot before;
    require(reader.read(before), "Actual pre-burst retained physical metadata");
    Session::Configuration c;
    c.generation = 201; c.deviceEpoch = before.deviceEpoch; c.sampleRate = 48000.;
    c.deviceFrames = 256; c.maximumPhysicalFrames = c.maximumSourceFrames = 8192;
    c.maximumSourceScalePpm = 1000000;
    auto observer = std::make_shared<Observer>();
    Session::Ptr session;
    require(Session::prepare(manager.deviceManager, c, observer, session) == Status::prepared,
            "Native-only explicit8192 scope prepared before physical burst, no fabricated Host K");
    const auto localReference = local.context->getNodePlayHead()->getReferenceSampleRange();
    juce::AudioBuffer<float> longOutput(2, 8192); // All allocations on CONTROL before launch.
    std::atomic<bool> done{false}, callbackException{false};
    double ms = 0;
    juce::int64 callbackBeganTicks = 0, callbackEndedTicks = 0;
    unsigned allocations = 0, destructions = 0;
    std::jthread driver([&]
    {
        const auto a = callbackAllocations.load(), d = callbackDestructions.load();
        const auto start = juce::Time::getHighResolutionTicks();
        callbackBeganTicks = start;
        try
        {
            struct Probe
            {
                bool old = callbackAllocationProbe;
                Probe() { callbackAllocationProbe = true; }
                ~Probe() { callbackAllocationProbe = old; }
            } probe;
            f.device->process(longOutput);
        }
        catch (...) { callbackException.store(true); }
        callbackEndedTicks = juce::Time::getHighResolutionTicks();
        ms = SyntheticCallbackTiming::milliseconds(start, callbackEndedTicks);
        allocations = callbackAllocations.load() - a;
        destructions = callbackDestructions.load() - d;
        done.store(true, std::memory_order_release);
    });
    juce::AudioDeviceManager::PhysicalCallbackSnapshot during, afterInstall;
    bool sawRealBurst = false;
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (!done.load(std::memory_order_acquire) && juce::Time::getMillisecondCounter() < deadline)
    {
        if (reader.read(during) && during.callbackSerial > before.callbackSerial && during.outerFrames == 8192)
        {
            sawRealBurst = true;
            break;
        }
        // Bounded CONTROL polling: an OS yield may surrender its whole quantum
        // until the short physical burst finishes. No device-thread wait/hook.
    }
    auto installed = Status::contextUnavailable;
    const bool doneAtAdmission = done.load(std::memory_order_acquire);
    const auto installationBeganTicks = juce::Time::getHighResolutionTicks();
    if (sawRealBurst) installed = manager.installInitialSourceBurstSession(*local.context, session);
    const auto installationEndedTicks = juce::Time::getHighResolutionTicks();
    const bool afterKnown = reader.read(afterInstall);
    driver.join();
    const auto outcome = read(session);
    juce::String receipt =
        "beforeSerial,duringSerial,afterSerial,floor,observedSerial,observedOffset,observedPhysical,status,failed,"
        "begins,ends,aborts,peerFrames,localFrames,callbackMs,new,delete,doneAtAdmission,"
        "callbackBeganQpc,installationBeganQpc,installationEndedQpc,callbackEndedQpc\n";
    receipt += juce::String(juce::int64(before.callbackSerial)) + "," +
               juce::String(juce::int64(during.callbackSerial)) + "," +
               juce::String(juce::int64(afterInstall.callbackSerial)) + "," +
               juce::String(juce::int64(outcome.activationAfterSerial)) + "," +
               juce::String(juce::int64(outcome.observedSerial)) + "," + juce::String(outcome.observedOffset) + "," +
               juce::String(outcome.observedPhysicalFrames) + "," + juce::String(int(outcome.status)) + "," +
               juce::String(int(outcome.failed)) + "," + juce::String(observer->begins) + "," +
               juce::String(observer->ends) + "," + juce::String(observer->aborts) + "," +
               juce::String(peer.peer->frames) + "," + juce::String(local.musical->frames) + "," +
               juce::String(ms, 6) + "," + juce::String(allocations) + "," + juce::String(destructions) + "," +
               juce::String(int(doneAtAdmission)) + "," + juce::String(callbackBeganTicks) + "," +
               juce::String(installationBeganTicks) + "," + juce::String(installationEndedTicks) + "," +
               juce::String(callbackEndedTicks) + "\n";
    require(folder.getChildFile("suffix.csv").replaceWithText(receipt), "Retain actual suffix fixture result before assertion");
    juce::String pcm = "frame,left,right,expectedLeft,expectedRight\n";
    double error = 0;
    for (int n = 0; n < 8192; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
            error = std::max(error, std::abs(double(longOutput.getSample(ch, n)) -
                                            source_burst_acceptance::Signal::sample(ch, n)));
        pcm += juce::String(n) + "," + juce::String(longOutput.getSample(0, n), 9) + "," +
               juce::String(longOutput.getSample(1, n), 9) + "," +
               juce::String(source_burst_acceptance::Signal::sample(0, n), 9) + "," +
               juce::String(source_burst_acceptance::Signal::sample(1, n), 9) + "\n";
    }
    require(folder.getChildFile("suffix-pcm.csv").replaceWithText(pcm), "Retain every actual8192 counterpart sample");
    require(!callbackException && error == 0 && allocations == 0 && destructions == 0 &&
                ms <= 1000. * 8192 / 48000. && peer.peer->frames == 8192 && local.musical->frames == 0 &&
                local.context->getNodePlayHead()->getReferenceSampleRange() == localReference,
            "Actual whole8192 burst leaves pending source untouched and every counterpart sample/clock exact");
    require(sawRealBurst && afterKnown && installed == Status::pendingFirstSerial &&
                during.callbackSerial == afterInstall.callbackSerial &&
                outcome.activationAfterSerial == during.callbackSerial &&
                outcome.observedSerial == outcome.activationAfterSerial && outcome.observedOffset > 0 &&
                outcome.observedPhysicalFrames == 8192 && outcome.status == Status::pendingFirstSerial &&
                !outcome.failed && !outcome.scopeOpen && outcome.sourceProcessed == 0 &&
                observer->begins == 0 && observer->ends == 0 && observer->aborts == 0,
            "Fixture limitation if no real mid-physical install/suffix: require actual same-floor suffix pending, never fake it");
    local.edit->getTransport().setPosition(te::TimePosition::fromSeconds(0));
    local.edit->getTransport().play(false);
    dispatchMessages(10);
    Capture capture;
    capture.peerFrames = 8192;
    capture.drive(f, peer, local, session, folder, "next-full-serial", 512, true, false);
    const auto resumed = read(session);
    require(resumed.status == Status::ready && !resumed.failed && !resumed.scopeOpen &&
                resumed.physicalSerial > outcome.activationAfterSerial && observer->begins == 1 && observer->ends == 1 &&
                local.context->getNodePlayHead()->getReferenceSampleRange().getEnd() == localReference.getEnd() + 512 &&
                local.musical->firstMusicalSample == 0,
            "Only the next genuine full physical serial activates with literal local musical0 and prior-END continuity");
}

inline void deviceLifecycleTests(const juce::File &folder)
{
    require(folder.createDirectory().wasOk(), "Fresh owned virtual device lifecycle attempt");
    Fixture f(folder.getChildFile("fixture"));
    for (int n = 0; n < 4; ++n) f.silentCallback("Original application context prepared before lifecycle case");
    Graph peer(f, 0), local(f, 301);
    auto &manager = f.audio->engine.getDeviceManager();
    PreparedPluginTransport::Ptr plan;
    require(PreparedPluginTransport::prepare(manager.deviceManager, 256, {0, 1}, 1000000, 512, plan).succeeded(),
            "Actual virtual device lifecycle prepared source identity");
    auto observer = std::make_shared<Observer>();
    auto session = prepare(f, plan, observer, 301);
    require(manager.installInitialSourceBurstSession(*local.context, session) == Status::pendingFirstSerial,
            "Actual initial lifecycle session pending");
    local.edit->getTransport().setPosition(te::TimePosition::fromSeconds(0));
    local.edit->getTransport().play(false);
    dispatchMessages(10);
    Capture capture;
    capture.drive(f, peer, local, session, folder, "before-actual-device-stop", 256, true, false);
    const auto oldReference = local.context->getNodePlayHead()->getReferenceSampleRange();
    const auto oldSourceFrames = local.musical->frames;
    const auto oldBegins = observer->begins;
    auto retainedReader = manager.deviceManager.getPhysicalCallbackReader();
    juce::AudioDeviceManager::PhysicalCallbackSnapshot oldDevice;
    require(retainedReader.read(oldDevice), "Actual prior virtual device epoch");
    manager.deviceManager.closeAudioDevice();
    manager.deviceManager.restartLastAudioDevice();
    dispatchMessages(40); // CONTROL preparation only; no device callbacks in flight.
    f.device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(f.device && f.device->getCurrentSampleRate() == 48000. && f.device->getCurrentBufferSizeSamples() == 256,
            "Real JUCE virtual-device close/restart completed, no physical hardware opened");
    juce::AudioDeviceManager::PhysicalCallbackSnapshot newDevice;
    require(retainedReader.read(newDevice) && newDevice.deviceEpoch != oldDevice.deviceEpoch,
            "Actual native device lifecycle changed retained-reader epoch");
    auto *current = local.edit->getTransport().getCurrentPlaybackContext();
    const bool retainedContext = current == local.context;
    // The contract explicitly permits native lifecycle retirement OR retaining
    // an invalidated context. Never dereference the old context after retirement.
    if (retainedContext)
        require(current->sourceLifecycleInvalidated() && current->getNodePlayHead()->getReferenceSampleRange() == oldReference &&
                    manager.installInitialSourceBurstSession(*current, session) == Status::localLifecycleInvalidated,
                "Actual native device preparation invalidates retained local context without rebasing");
    else
        require(read(session).retired, "A genuinely replaced context retires its owned source observer/session");
    juce::AudioBuffer<float> output(2, 256);
    const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
    const auto start = juce::Time::getHighResolutionTicks();
    {
        struct Probe
        {
            bool old = callbackAllocationProbe;
            Probe() { callbackAllocationProbe = true; }
            ~Probe() { callbackAllocationProbe = old; }
        } probe;
        f.device->process(output);
    }
    const auto ms = SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
    const auto outcome = read(session);
    double error = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 256; ++n)
        {
            const float expected = retainedContext ? 0.f : source_burst_acceptance::Signal::sample(ch, capture.peerFrames + n);
            const float actual = output.getSample(ch, n);
            error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                          : std::numeric_limits<double>::infinity();
        }
    const auto a = callbackAllocations.load() - allocated, d = callbackDestructions.load() - destroyed;
    const juce::String receipt = "retainedContext,oldEpoch,newEpoch,status,failed,retired,beginsBefore,beginsAfter,"
                                "sourceBefore,sourceAfter,maxError,new,delete,callbackMs\n" +
        juce::String(int(retainedContext)) + "," + juce::String(juce::int64(oldDevice.deviceEpoch)) + "," +
        juce::String(juce::int64(newDevice.deviceEpoch)) + "," + juce::String(int(outcome.status)) + "," +
        juce::String(int(outcome.failed)) + "," + juce::String(int(outcome.retired)) + "," +
        juce::String(oldBegins) + "," + juce::String(observer->begins) + "," + juce::String(oldSourceFrames) + "," +
        juce::String(local.musical->frames) + "," + juce::String(error, 12) + "," + juce::String(a) + "," +
        juce::String(d) + "," + juce::String(ms, 6) + "\n";
    require(folder.getChildFile("actual-device-restart.csv").replaceWithText(receipt),
            "Retain exact lifecycle disposition/counters/failure result before assertion");
    require(error == 0 && a == 0 && d == 0 && ms <= 1000. * 256 / 48000. &&
                local.musical->frames == oldSourceFrames && observer->begins == oldBegins &&
                (retainedContext ? (outcome.failed && outcome.status == Status::localLifecycleInvalidated)
                                 : outcome.retired),
            "Actual new-epoch callback never resumes old source/observer contract; typed failure/retirement is not recovery");
}

__declspec(noinline) inline void tests(const juce::File &folder, const juce::File &latencyBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned local source experiment");
    vibera::test::routing_lifetime::Storage storage(folder.getChildFile("storage"));
    const auto spec = parameterSpec(latencyBinary);
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Actual scanned original native31 SDK effect fixture");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), latencyBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Retain exact actual executable/worker/SDK fixture identities");

    for (int scenario = 0; scenario < 3; ++scenario)
    {
        const bool worker = scenario == 1, refused = scenario == 2;
        const auto attempt = folder.getChildFile(worker ? "local-sdk" : refused ? "begin-refusal" : "local-native");
        require(attempt.createDirectory().wasOk(), "Owned additive local scenario");
        Fixture f(attempt.getChildFile("fixture"));
        for (int n = 0; n < 4; ++n) f.silentCallback("Actual existing no-note app context prepared");
        Graph peer(f, 0);
        auto &manager = f.audio->engine.getDeviceManager();
        PreparedPluginTransport::Ptr plan;
        require(PreparedPluginTransport::prepare(manager.deviceManager, 256, {worker ? 31 : 0, 1},
            1000000, 512, plan).succeeded() && plan->budget().packets == 3,
            "Literal native whole-physical512 / transport768 contract, no production policy selection");
        std::shared_ptr<PluginHost> host;
        if (worker)
        {
            host = std::make_shared<PluginHost>(plan);
            const auto started = host->start(spec, 48000., 256, false);
            require(started.wasOk(), "Actual LOCAL-CLOSED SDK worker: " + started.getErrorMessage().toStdString());
        }
        Graph local(f, std::uint64_t(101 + scenario), host);
        require(local.context->initialSourcePhase() == te::EditPlaybackContext::InitialSourcePhase::awaitingSession,
                "Initial context is pending before publication/first callback, never first unbound DSP");
        const auto priorReference = local.context->getNodePlayHead()->getReferenceSampleRange();
        Capture capture;
        for (int n = 0; n < 5; ++n)
        {
            capture.drive(f, peer, local, {}, attempt, "awaiting-session", 256, false, worker);
            require(local.context->getNodePlayHead()->getReferenceSampleRange() == priorReference,
                    "Actual pending context local clock remains unchanged while counterpart advances");
        }
        capabilityTests(local, peer, f);
        auto observer = std::make_shared<Observer>(host);
        observer->accept = !refused;
        nullSyncTests(local, peer, observer, {}, te::EditPlaybackContext::InitialSourcePhase::awaitingSession);
        auto session = prepare(f, plan, observer, std::uint64_t(101 + scenario));
        require(manager.installSourceBurstSession(*local.context, session) == Status::initialContextRequired &&
                    manager.installInitialSourceBurstSession(*local.context, session) == Status::pendingFirstSerial &&
                    manager.installInitialSourceBurstSession(*local.context, session) == Status::pendingFirstSerial,
                "Strict API cannot bypass reservation; initial/repeated install truthfully stays pending");
        const auto installed = read(session);
        require(!installed.failed && !installed.scopeOpen && observer->begins == 0,
                "Initial install never pretends observer/DSP completion");
        local.edit->getTransport().setPosition(te::TimePosition::fromSeconds(0));
        local.edit->getTransport().play(false);
        dispatchMessages(10); // CONTROL only; no callbacks are in flight.
        const auto starts = f.device->starts, stops = f.device->stops;
        juce::int64 expectedReferenceEnd = priorReference.getEnd();
        for (int block = 0; block < (refused ? 3 : 28); ++block)
        {
            const int frames = std::array<int, 7>{127, 64, 64, 65, 256, 512, 128}[std::size_t(block % 7)];
            capture.drive(f, peer, local, session, attempt, refused ? "begin-refusal" : "local-active",
                          frames, !refused, worker);
            const auto outcome = read(session);
            if (refused)
            {
                require(outcome.failed && outcome.status == Status::observerRefused &&
                            !outcome.scopeOpen && observer->begins == 1 && observer->ends == 0 && observer->aborts == 1 &&
                            local.context->initialSourcePhase() == te::EditPlaybackContext::InitialSourcePhase::failed &&
                            local.context->getNodePlayHead()->getReferenceSampleRange() == priorReference &&
                            manager.installInitialSourceBurstSession(*local.context, session) == Status::observerRefused,
                        "Initial begin refusal aborts only its own partial scope, sticky failure and counterpart continuity");
            }
            else
            {
                expectedReferenceEnd += frames;
                const auto actual = local.context->getNodePlayHead()->getReferenceSampleRange();
                const int finalSlice = frames > 256 ? 256 : frames;
                require(actual.getEnd() == expectedReferenceEnd && actual.getStart() == expectedReferenceEnd - finalSlice &&
                            local.musical->firstMusicalSample == 0 &&
                            !outcome.failed && !outcome.scopeOpen && outcome.status == Status::ready &&
                            outcome.physicalSerial > installed.activationAfterSerial &&
                            observer->begins == unsigned(block + 1) && observer->ends == observer->begins &&
                            observer->aborts == 0 && manager.installInitialSourceBurstSession(*local.context, session) == Status::ready &&
                            f.device->starts == starts && f.device->stops == stops,
                        "Literal prior-END local continuity, musical sample0, true physical floor, exactly-once admission and readiness");
                if (block == 0)
                    nullSyncTests(local, peer, observer, session, te::EditPlaybackContext::InitialSourcePhase::active);
                if (host)
                    require(host->statistics().missed == 0 && host->statistics().dropped == 0 &&
                                host->packetFrame() == capture.localFrames % 256 && host->transportOutcome().succeeded(),
                            "Actual original SDK partial-packet owner/counters retained");
            }
            dispatchMessages(6); // Serialized paced provider dispatch AFTER the entire physical callback.
            if (host) host->service();
        }
        if (!refused)
        {
            const auto clock = local.context->getNodePlayHead()->getReferenceSampleRange();
            // Invoke the exact native CONTROL device-resync entry as a bounded
            // lifecycle diagnostic; this is NOT an actual physical restart test.
            local.context->resyncToGlobalStreamTime({1000., 1000. + 256. / 48000.}, 48000.);
            require(local.context->sourceLifecycleInvalidated() &&
                        local.context->getNodePlayHead()->getReferenceSampleRange() == clock &&
                        manager.installInitialSourceBurstSession(*local.context, session) == Status::localLifecycleInvalidated,
                    "CONTROL lifecycle invalidates local session without resetting its musical/reference cursor");
        }
    }
    // Independent late scenarios must both run: a scheduling-fixture failure
    // must not conceal an actual device-lifecycle defect. The aggregate still
    // rethrows its first failure, without skipping or weakening either oracle.
    std::exception_ptr firstFailure;
    juce::Array<juce::var> outcomes;
    for (const auto &[name, operation] :
         std::array<std::pair<const char *, void (*)(const juce::File &)>, 2>{{
             {"actual-suffix", suffixTests}, {"actual-device-restart", deviceLifecycleTests}}})
    {
        auto result = std::make_unique<juce::DynamicObject>();
        result->setProperty("case", name);
        try
        {
            operation(folder.getChildFile(name));
            result->setProperty("passed", true);
        }
        catch (const std::exception &error)
        {
            result->setProperty("passed", false);
            result->setProperty("error", juce::String(error.what()));
            if (!firstFailure) firstFailure = std::current_exception();
        }
        catch (...)
        {
            result->setProperty("passed", false);
            result->setProperty("error", "Unknown native scenario failure");
            if (!firstFailure) firstFailure = std::current_exception();
        }
        outcomes.add(juce::var(result.release()));
        require(folder.getChildFile("independent-native-cases.json").replaceWithText(
                    juce::JSON::toString(outcomes)), "Retain independent actual scenario outcomes");
    }
    if (firstFailure) std::rethrow_exception(firstFailure);
}
} // namespace local_source_initial_acceptance
