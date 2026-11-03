// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ProductionMembershipSdkFixture.h"
#include "RoutingLifetimeAcceptance.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <exception>
#include <vector>

// Include after actual original parameter helpers, readWav and callback probes.
namespace vibera::test::production_membership_sdk
{
using Broker = source_membership::SourceMembershipBroker;
constexpr int quantum = 256, transportFrames = 768, maxCallbacks = 48;
constexpr double rate = 48000.;
constexpr std::array<float, 2> sourcePcm{.125f, -.0625f};
inline juce::String canonical(const Controller &c)
{
    return c.project.state().toXmlString() + "\n" + c.projectIdentity + "/" +
           juce::String(juce::int64(c.revision)) + "/" + juce::String(int(c.dirty)) + "/" +
           juce::String(c.undo.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
           c.undo.getUndoDescriptions().joinIntoString("|") + "/" +
           c.undo.getRedoDescriptions().joinIntoString("|");
}
inline juce::String capture(PluginHost &host)
{
    const auto serial = host.requestState();
    const auto start = juce::Time::getMillisecondCounter();
    juce::String state;
    while (serial && !host.readState(serial, state) && juce::Time::getMillisecondCounter() - start < 10000)
    {
        host.service();
        dispatchMessages(2);
    }
    require(serial && state.isNotEmpty(), "Actual original worker state capture, not cached spec bytes");
    return state;
}
struct Trace
{
    routing_lifetime::Fixture &f;
    const juce::File folder;
    const std::shared_ptr<PluginHost> incumbent;
    const AudioEngine::ProjectionDiagnostics baseline;
    const PluginLatencyStamp native;
    const juce::String instance;
    const juce::uint32 pid;
    const unsigned resets, revision;
    const int starts, stops;
    juce::AudioBuffer<float> block{2, quantum};
    std::array<std::vector<float>, 2> actual, expected;
    int callbacks = 0, releaseFrame = -1;
    std::uint64_t priorProcessed = 0;
    ProductionMembershipSdkFixture *bridge = nullptr; // Only synchronous CONTROL body.
    juce::String csv =
        "phase,callback,sourceFrame,maxError,ms,new,delete,nativeSerial,nativeClosed,nativeFailed,"
        "memberGeneration,memberClosed,memberStatus,brokerState,brokerError,bankSize,submitted,"
        "processed,consumedSequence,freshSubmitted,freshProcessed,freshCursor,held\n";
    Trace(routing_lifetime::Fixture &fixture, const juce::File &output, std::shared_ptr<PluginHost> host)
        : f(fixture), folder(output), incumbent(std::move(host)), baseline(f.audio->projectionDiagnostics()),
          native(incumbent->declaredLatency()), instance(incumbent->runtimeIdentity()),
          pid(incumbent->processId()), resets(incumbent->midiResetGeneration()),
          revision(incumbent->parameterRevision()), starts(f.device->starts), stops(f.device->stops)
    {
        for (auto &c : actual)
            c.reserve(maxCallbacks * quantum);
        for (auto &c : expected)
            c.reserve(maxCallbacks * quantum);
    }
    void retain()
    {
        require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                "Retain every actual callback before assertion");
        for (int c = 0; c < 2; ++c)
            for (bool reference : {false, true})
            {
                auto file = folder.getChildFile(juce::String(reference ? "expected-" : "actual-") +
                                                juce::String(c) + ".f32");
                auto stream = file.createOutputStream();
                require(stream && stream->setPosition(0) && stream->truncate().wasOk(),
                        "Owned PCM stream truncate/status");
                const auto &samples = reference ? expected[std::size_t(c)] : actual[std::size_t(c)];
                require(stream->write(samples.data(), samples.size() * sizeof(float)),
                        "All startup/transition/tail PCM retained");
                stream->flush();
                require(stream->getStatus().wasOk(), "Actual/analytical PCM flush/status verified");
            }
    }
    void tick(const char *phase)
    {
        require(callbacks < maxCallbacks, "Predeclared bounded callback/PCM capacity");
        const auto before = f.audio->projectionDiagnostics();
        const auto a = callbackAllocations.load(), d = callbackDestructions.load();
        const auto start = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            f.device->process(block);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
        const auto allocated = callbackAllocations.load() - a, deleted = callbackDestructions.load() - d;
        double error = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < quantum; ++n)
            {
                const int frame = callbacks * quantum + n;
                const bool sounding =
                    frame >= transportFrames && (releaseFrame < 0 || frame < releaseFrame + transportFrames);
                const float wanted = sounding ? sourcePcm[std::size_t(c)] * .5f : 0.f;
                const float value = block.getSample(c, n);
                actual[std::size_t(c)].push_back(value);
                expected[std::size_t(c)].push_back(wanted);
                error = std::isfinite(value) ? std::max(error, std::abs(double(value) - wanted))
                                             : std::numeric_limits<double>::infinity();
            }
        ++callbacks;
        dispatchMessages(6); // Test pacing/service outside measured callback, no DSP wait.
        incumbent->service();
        if (bridge)
            bridge->fresh->service();
        const auto now = f.audio->projectionDiagnostics();
        const auto packets = incumbent->statistics();
        const auto consumed = incumbent->consumedLatency();
        source_membership::Receipt members;
        te::SourceBurstSession::Outcome nativeScope;
        Broker::Outcome outcome;
        const bool scopes = bridge && bridge->scopes(members, nativeScope);
        if (bridge)
            outcome = bridge->outcome();
        const auto fresh = bridge ? bridge->fresh->statistics() : PluginHost::Statistics{};
        csv += juce::String(phase) + "," + juce::String(callbacks - 1) + "," +
               juce::String((callbacks - 1) * quantum) + "," + juce::String(error, 12) + "," +
               juce::String(ms, 6) + "," + juce::String(allocated) + "," + juce::String(deleted) + "," +
               juce::String(juce::int64(nativeScope.physicalSerial)) + "," +
               juce::String(int(!nativeScope.scopeOpen)) + "," + juce::String(int(nativeScope.failed)) + "," +
               juce::String(juce::int64(members.membershipGeneration)) + "," +
               juce::String(juce::int64(members.lastClosedSerial)) + "," +
               juce::String(unsigned(members.status)) + "," + juce::String(unsigned(outcome.state)) + "," +
               juce::String(unsigned(outcome.error)) + "," + juce::String(bridge ? int(bridge->size()) : 1) +
               "," + juce::String(juce::int64(packets.submitted)) + "," +
               juce::String(juce::int64(packets.processed)) + "," +
               juce::String(consumed ? juce::int64(consumed->sequence) : -1) + "," +
               juce::String(juce::int64(fresh.submitted)) + "," + juce::String(juce::int64(fresh.processed)) +
               "," + juce::String(bridge ? bridge->fresh->packetFrame() : 0) + "," +
               juce::String(now.channels.empty() ? -1 : now.channels.front().heldInputKeys) + "\n";
        retain(); // Actual and analytical samples/readbacks exist BEFORE all oracle assertions.
        require(error < 2.e-7 && allocated == 0 && deleted == 0 && ms <= 256000. / 48000.,
                "Independent literal768 sample phase/half-gain PCM, unchanged fullQ deadline/heap gate");
        require(now.edit == baseline.edit && now.playbackContext == baseline.playbackContext &&
                    now.replacements == baseline.replacements &&
                    now.samplerCreations == baseline.samplerCreations &&
                    now.cache.preparations == baseline.cache.preparations && now.channels.size() == 1 &&
                    now.channels[0].sampler == baseline.channels[0].sampler &&
                    now.channels[0].sample == baseline.channels[0].sample &&
                    now.channels[0].track == baseline.channels[0].track &&
                    now.channels[0].graphInitialisations == baseline.channels[0].graphInitialisations &&
                    now.channels[0].renderCalls == before.channels[0].renderCalls + 1 &&
                    now.channels[0].heldInputKeys == (releaseFrame < 0 ? 1 : 0) && !f.audio->playing() &&
                    now.headSeconds == baseline.headSeconds && f.device->starts == starts &&
                    f.device->stops == stops,
                "Real stopped held sample/clock/context/device stays owned and source processes once");
        require(incumbent->alive() && incumbent->runtimeIdentity() == instance &&
                    incumbent->processId() == pid && incumbent->midiResetGeneration() == resets &&
                    incumbent->parameterRevision() == revision &&
                    packets.submitted == std::uint64_t(callbacks) && packets.processed <= packets.submitted &&
                    packets.processed >= priorProcessed &&
                    packets.processed >= std::uint64_t(std::max(0, callbacks - 3)) && packets.missed == 0 &&
                    packets.dropped == 0 && incumbent->packetFrame() == 0,
                "Warm original SDK packet phase and owner never reset or duplicate");
        priorProcessed = packets.processed;
        if (callbacks >= 4)
            require(
                consumed && consumed->sequence == std::uint64_t(callbacks - 4) && consumed->stamp == native,
                "Exact genuine consumed packet sequence/stamp remains continuous through membership switch");
        if (bridge)
            require(
                scopes && source_membership::PreparedSourceHosts::closedWith(members, nativeScope) &&
                    !nativeScope.failed && fresh.submitted == 0 && fresh.processed == 0 &&
                    bridge->fresh->packetFrame() == 0 && !bridge->fresh->consumedLatency() &&
                    bridge->fresh->transportOutcome().succeeded(),
                "Actual installed broker/native close; private unselected SDK worker processes zero samples");
    }
    void release()
    {
        releaseFrame = callbacks * quantum;
        f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        for (int n = 0; n < 4; ++n)
            tick("literal_noteoff_plus768_full_tail");
    }
};
inline void scenario(const juce::File &folder, const juce::File &source, const PluginSpec &original,
                     bool patternMode, bool cancelBeforeBegin)
{
    Id incumbentId = 0;
    routing_lifetime::Fixture f(
        folder, false, patternMode,
        [&](Project &p)
        {
            auto &c = p.channels.front();
            c.source = source.getFullPathName();
            c.gain = 1;
            c.pan = 0;
            c.sampler = {};
            c.sampler.root = 60;
            c.sampler.loop = true;
            c.sampler.envelope = true;
            c.sampler.attack = c.sampler.decay = c.sampler.release = 0;
            c.sampler.sustain = 1;
            for (auto &m : p.mixer)
            {
                m.gain = 1;
                m.pan = 0;
                m.muted = false;
                m.stereoSeparation = 0;
                m.phaseInverted = false;
                m.stereoSwapped = false;
            }
            auto spec = original;
            spec.id = p.allocate();
            incumbentId = spec.id;
            p.mixer[1].effects = {spec};
        },
        false, 256);
    f.audio->stop(); // Before the first real callback; held input works while transport is stopped.
    const auto incumbent = f.audio->parameterHost(incumbentId);
    require(incumbent && incumbent->alive() &&
                incumbent->transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                incumbent->transportPacketCount() == 3 && incumbent->transportLatencySamples() == 768 &&
                incumbent->declaredLatency().nativeSamples == 0,
            "Actual ordinary initial production plan is3Q/native0");
    const auto catalogue = parameterCatalogue(*incumbent);
    require(nativeParameter(catalogue, 0).value == .5f && nativeParameter(catalogue, 0xfedcba98u).value == 0,
            "Actual original SDK half-gain and zero-bias, no fitted coefficient");
    const auto nativeState = capture(*incumbent);
    const auto document = canonical(f.controller);
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    Trace trace(f, folder, incumbent);
    for (int n = 0; n < 8; ++n)
        trace.tick("all_startup_and_warm_incumbent_samples");
    auto next = f.controller.project;
    auto added = original;
    added.id = next.allocate();
    next.mixer[2].effects = {added};
    std::unique_ptr<PreparedControllerChange> model;
    require(f.controller
                    .prepareSnapshotChange(next, f.controller.project, "Original membership cancellation",
                                           true, false, true, model)
                    .wasOk() &&
                model,
            "Genuine canonical destination/history prepared only");
    ProductionMembershipSdkFixture::with(
        *f.audio, f.controller, *model, added,
        [&](ProductionMembershipSdkFixture &bridge)
        {
            trace.bridge = &bridge;
            source_membership::Receipt sourceBank;
            te::SourceBurstSession::Outcome native;
            require(bridge.scopes(sourceBank, native) &&
                        source_membership::PreparedSourceHosts::closedWith(sourceBank, native) &&
                        bridge.size() == 1 && bridge.fresh && bridge.fresh != incumbent &&
                        bridge.fresh->alive(),
                    "Actual already-warm installed broker, exact native close and separate fresh worker");
            juce::String controls = "operation,error,state,outcomeError,bankSize,memberGeneration,"
                                    "nativeSerial,nativeClosed,nativeFailed\n";
            const auto control = [&](const char *operation, Broker::Error error)
            {
                source_membership::Receipt m;
                te::SourceBurstSession::Outcome n;
                const bool read = bridge.scopes(m, n);
                const auto o = bridge.outcome();
                controls += juce::String(operation) + "," + juce::String(unsigned(error)) + "," +
                            juce::String(unsigned(o.state)) + "," + juce::String(unsigned(o.error)) + "," +
                            juce::String(int(bridge.size())) + "," +
                            juce::String(juce::int64(m.membershipGeneration)) + "," +
                            juce::String(juce::int64(n.physicalSerial)) + "," +
                            juce::String(int(read && !n.scopeOpen)) + "," + juce::String(int(n.failed)) +
                            "\n";
                require(folder.getChildFile("controls.csv").replaceWithText(controls),
                        "Retain actual admission/cancellation facts before assertions");
            };
            const auto firstMembership = sourceBank.membershipGeneration;
            const auto prepare = bridge.prepare();
            control("prepare", prepare);
            trace.tick("control_prepare_does_not_publish_bank_or_graph");
            require(prepare == Broker::Error::none && bridge.size() == 1 &&
                        canonical(f.controller) == document,
                    "CONTROL preparation retains original membership and canonical history");
            const auto requested = bridge.request();
            control("request", requested);
            require(requested == Broker::Error::none && bridge.outcome().state == Broker::State::pending &&
                        bridge.size() == 1,
                    "Actual request admission is pending until a real begin/end callback");
            if (!cancelBeforeBegin)
            {
                for (int n = 0; n < 16 && bridge.outcome().state == Broker::State::pending; ++n)
                    trace.tick("real_union_native_begin_end_ACK");
                require(bridge.outcome().state == Broker::State::expanded && bridge.size() == 2 &&
                            bridge.scopes(sourceBank, native) &&
                            sourceBank.membershipGeneration != firstMembership,
                        "Genuine union bank closed once, fresh worker zero-work; no graph adoption claimed");
            }
            const auto cancelled = bridge.cancel();
            control("cancel", cancelled);
            require(cancelled == Broker::Error::none && bridge.outcome().state == Broker::State::pending,
                    "Cancellation queues retained-source return; it never aborts/resets from CONTROL");
            for (int n = 0; n < 16 && bridge.outcome().state == Broker::State::pending; ++n)
                trace.tick("real_reverse_native_begin_end_ACK");
            const auto result = bridge.outcome();
            control("reverse_ack_observed", Broker::Error::none);
            require(
                result.state == Broker::State::cancelled && bridge.size() == 1 &&
                    bridge.scopes(sourceBank, native) && sourceBank.membershipGeneration == firstMembership &&
                    bridge.fresh->statistics().submitted == 0 && bridge.fresh->statistics().processed == 0 &&
                    canonical(f.controller) == document && !model->wasCommitted() &&
                    model->destinationBeforeCommit(),
                "Real reverse ACK settles same source bank without native graph/model/history publication");
            trace.release();
            require(bridge.acknowledge() == Broker::Error::none,
                    "CONTROL acknowledgement follows retained reverse close and full release evidence");
            trace.bridge = nullptr;
        });
    require(capture(*incumbent) == nativeState && canonical(f.controller) == document &&
                f.audio->parameterHost(incumbentId) == incumbent && !f.audio->parameterHost(added.id),
            "Original native state/owner retained; cancelled fresh worker never enters public mapping");
    require(
        folder.getChildFile("result.txt")
            .replaceWithText(
                "PASS actual ordinary SDK broker cancellation; "
                "all startup/switch/reverse/release PCM retained; graph post/FX/PDC/retirement unqualified; "
                "programmatic MIDI/synthetic device only, no physical hardware/listening claim.\n"),
        "Retain bounded scenario outcome");
}
} // namespace vibera::test::production_membership_sdk

__declspec(noinline) static void productionMembershipSdkTests(const juce::File &folder,
                                                              const juce::File &binary)
{
    using namespace vibera;
    using namespace vibera::test::production_membership_sdk;
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned actual membership SDK attempt");
    const auto original = parameterSpec(binary);
    require(!original.instrument && original.name == "VibeRa Parameter Fixture",
            "Actual original native SDK provider");
    test::routing_lifetime::Storage storage(folder.getChildFile("storage"));
    const auto source = folder.getChildFile("original-constant-signed.wav");
    {
        juce::AudioBuffer<float> pcm(2, 16384);
        for (int c = 0; c < 2; ++c)
            std::fill_n(pcm.getWritePointer(c), pcm.getNumSamples(), sourcePcm[std::size_t(c)]);
        auto stream = source.createOutputStream();
        require(bool(stream), "Owned original sample output");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 32, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "Original exact binary-fraction source");
    }
    double sourceRate = 0;
    const auto pcm = readWav(source, sourceRate);
    require(sourceRate == rate && pcm.getNumChannels() == 2 && pcm.getNumSamples() == 16384,
            "Independent source roundtrip shape");
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < pcm.getNumSamples(); ++n)
            require(pcm.getSample(c, n) == sourcePcm[std::size_t(c)],
                    "Independent exact source bytes, no gain/phase fitting");
    std::exception_ptr first;
    for (bool pattern : {true, false})
        for (bool early : {true, false})
        {
            const auto output = folder.getChildFile(juce::String(pattern ? "pattern-" : "song-") +
                                                    (early ? "cancel-unread" : "cancel-expanded"));
            try
            {
                scenario(output, source, original, pattern, early);
            }
            catch (const std::exception &e)
            {
                if (!first)
                    first = std::current_exception();
                output.createDirectory();
                require(output.getChildFile("failure.txt").replaceWithText(e.what()),
                        "Retain original scenario failure");
            }
            catch (...)
            {
                if (!first)
                    first = std::current_exception();
                output.createDirectory();
                require(output.getChildFile("failure.txt").replaceWithText("Unknown exception"),
                        "Retain unknown scenario failure");
            }
        }
    if (first)
        std::rethrow_exception(first);
}
