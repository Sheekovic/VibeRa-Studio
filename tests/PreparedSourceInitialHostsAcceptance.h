// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedSourceHosts.h"
#include "LocalSourceInitialAcceptance.h"

// Additive actual native/source + TWO original SDK workers. No normal app
// prepared-source activation or production policy change. Reuses the existing
// literal MusicalSignal / K3+31=799 / complete PCM + exact deadline oracles.
namespace prepared_source_initial_hosts_acceptance
{
using Session = te::SourceBurstSession;
using Membership = vibera::source_membership::PreparedSourceHosts;
using Receipt = vibera::source_membership::Receipt;
using Phase = vibera::source_membership::Phase;
using Status = vibera::source_membership::Status;
using Graph = local_source_initial_acceptance::Graph;
using Fixture = local_source_initial_acceptance::Fixture;
using Capture = local_source_initial_acceptance::Capture;

inline Session::Configuration configuration(const PreparedPluginTransport &plan, std::uint64_t generation)
{
    const auto &b = plan.budget();
    return {b.deviceEpoch,    generation,     plan.sampleRate(),      b.configuredFrames,
            b.physicalFrames, b.sourceFrames, b.maximumSourceScalePpm};
}

// Actual peer end refusal through the native Observer contract. It injects no
// worker state or physical metadata and owns no Host. Failure is an intentional
// failed callback, never a normal playback continuity claim.
struct EndRefusal final : Session::Observer
{
    unsigned begins = 0, ends = 0, aborts = 0;
    bool begin(const Session::Outcome &) noexcept override
    {
        ++begins;
        return true;
    }
    bool end(const Session::Outcome &) noexcept override
    {
        ++ends;
        return false;
    }
    void abort(const Session::Outcome &) noexcept override
    {
        ++aborts;
    }
};

inline void append(juce::String &csv, const char *phase, const Session::Ptr &session,
                   const Membership::Ptr &membership, PluginHost &active, PluginHost &unused)
{
    Session::Outcome native;
    Receipt receipt;
    const bool nativeKnown = session && session->read(native),
               known = membership && membership->read(receipt);
    const auto a = active.statistics(), u = unused.statistics();
    const auto consumed = active.consumedLatency();
    csv += juce::String(phase) + "," + juce::String(int(nativeKnown)) + "," + juce::String(int(known)) + "," +
           juce::String(juce::int64(native.generation)) + "," +
           juce::String(juce::int64(native.deviceEpoch)) + "," +
           juce::String(juce::int64(native.physicalSerial)) + "," + juce::String(int(native.status)) + "," +
           juce::String(int(native.failed)) + "," + juce::String(int(native.retired)) + "," +
           juce::String(int(native.scopeOpen)) + "," + juce::String(native.sourceQuota) + "," +
           juce::String(int(receipt.phase)) + "," + juce::String(int(receipt.status)) + "," +
           juce::String(juce::int64(receipt.membershipGeneration)) + "," +
           juce::String(juce::int64(receipt.serial)) + "," +
           juce::String(juce::int64(receipt.lastClosedSerial)) + "," +
           juce::String(juce::int64(receipt.lastAbortedSerial)) + "," + juce::String(int(receipt.host)) +
           "," + juce::String(int(receipt.source)) + "," + juce::String(active.packetFrame()) + "," +
           juce::String(juce::int64(a.submitted)) + "," + juce::String(juce::int64(a.missed)) + "," +
           juce::String(juce::int64(a.dropped)) + "," + juce::String(unused.packetFrame()) + "," +
           juce::String(juce::int64(u.submitted)) + "," + juce::String(juce::int64(u.missed)) + "," +
           juce::String(juce::int64(u.dropped)) + "," + juce::String(int(consumed.has_value())) + "," +
           juce::String(consumed ? juce::int64(consumed->sequence) : -1) + "," +
           juce::String(consumed ? consumed->stamp.nativeSamples : -1) + "," +
           juce::String(consumed ? consumed->stamp.generation : 0) + "\n";
}

// Called after callback exclusion, INCLUDING on callback/PCM assertion failure.
// Every failure's native/membership/worker observations survive before rethrow.
inline void drive(Capture &capture, Fixture &fixture, Graph &peer, Graph &local, const Session::Ptr &session,
                  const Membership::Ptr &membership, PluginHost &active, PluginHost &unused,
                  const juce::File &folder, juce::String &csv, const char *phase, int frames,
                  bool localProcesses)
{
    std::exception_ptr failure;
    try
    {
        capture.drive(fixture, peer, local, session, folder, phase, frames, localProcesses, true);
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    append(csv, phase, session, membership, active, unused);
    require(folder.getChildFile("membership.csv").replaceWithText(csv),
            "Retain actual source/Host scope observations");
    if (failure)
        std::rethrow_exception(failure);
}

inline void peerRefusal(Fixture &fixture, Graph &peer, Graph &local, const PreparedPluginTransport::Ptr &plan,
                        const Session::Ptr &session, const Membership::Ptr &membership, PluginHost &active,
                        PluginHost &unused, const juce::File &folder, juce::String &csv)
{
    auto &manager = fixture.audio->engine.getDeviceManager();
    auto observer = std::make_shared<EndRefusal>();
    Session::Ptr peerSession;
    require(Session::prepare(manager.deviceManager, configuration(*plan, 903), observer, peerSession) ==
                    Session::Status::prepared &&
                manager.installSourceBurstSession(*peer.context, peerSession) == Session::Status::prepared,
            "Actual second native Session with independent deliberate end refusal");
    juce::AudioBuffer<float> output(2, 256);
    const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
    const auto before = local.context->getNodePlayHead()->getReferenceSampleRange();
    const int oldFrames = local.musical->frames;
    const auto started = juce::Time::getHighResolutionTicks();
    {
        struct Probe
        {
            bool previous = callbackAllocationProbe;
            Probe()
            {
                callbackAllocationProbe = true;
            }
            ~Probe()
            {
                callbackAllocationProbe = previous;
            }
        } probe;
        fixture.device->process(output);
    }
    const auto elapsed = SyntheticCallbackTiming::milliseconds(started, juce::Time::getHighResolutionTicks());
    append(csv, "actual-peer-end-refusal", session, membership, active, unused);
    require(folder.getChildFile("membership.csv").replaceWithText(csv),
            "Retain peer/native ACK divergence before assertion");
    juce::String pcm = "frame,left,right,expectedLeft,expectedRight\n";
    double error = 0;
    for (int frame = 0; frame != 256; ++frame)
    {
        for (int channel = 0; channel != 2; ++channel)
        {
            const auto sample = output.getSample(channel, frame);
            error = std::isfinite(sample) ? std::max(error, std::abs(double(sample)))
                                          : std::numeric_limits<double>::infinity();
        }
        pcm += juce::String(frame) + "," + juce::String(output.getSample(0, frame), 9) + "," +
               juce::String(output.getSample(1, frame), 9) + ",0,0\n";
    }
    require(folder.getChildFile("peer-failed-pcm.csv").replaceWithText(pcm),
            "Retain all deliberately failed callback output; silence is fault behavior, not musical success");
    const auto native = local_source_initial_acceptance::read(session);
    Receipt receipt;
    require(membership->read(receipt), "Actual observer receipt after real peer failure");
    require(error == 0 && callbackAllocations.load() == allocated &&
                callbackDestructions.load() == destroyed && elapsed <= 1000. * 256 / 48000. &&
                observer->begins == 1 && observer->ends == 1 && observer->aborts == 1 && native.failed &&
                native.status == Session::Status::peerRefused && !native.scopeOpen &&
                receipt.phase == Phase::closed && receipt.status == Status::ready &&
                receipt.lastClosedSerial == native.physicalSerial &&
                !Membership::closedWith(receipt, native) && local.musical->frames == oldFrames + 256 &&
                local.context->getNodePlayHead()->getReferenceSampleRange().getEnd() == before.getEnd() + 256,
            "Actual peer failure invalidates just-closed ACK; no fake rollback/continuity after processed "
            "failed callback");
}

inline void scenario(const juce::File &folder, const PluginSpec &fixtureSpec, bool partialBeginFailure)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned two-Host initial-source scenario");
    Fixture fixture(folder.getChildFile("fixture"));
    for (int count = 0; count != 4; ++count)
        fixture.silentCallback(
            "Actual existing app no-note graph prepared before independent native source experiment");
    auto peer = std::make_unique<Graph>(fixture, 0);
    auto &manager = fixture.audio->engine.getDeviceManager();
    PreparedPluginTransport::Ptr plan;
    require(PreparedPluginTransport::prepare(manager.deviceManager, 256, {31, 1}, 1000000, 512, plan)
                    .succeeded() &&
                plan->budget().packets == 3 && plan->budget().transportSamples == 768,
            "Literal actual manager512 budget gives K3 transport768; no normal app policy change");
    PluginSpec first = fixtureSpec, second = fixtureSpec;
    first.id = 1;
    second.id = 2;
    auto active = std::make_shared<PluginHost>(plan), unused = std::make_shared<PluginHost>(plan);
    require(active->start(first, 48000., 256, false).wasOk() &&
                unused->start(second, 48000., 256, false).wasOk(),
            "Actual separate original SDK workers using the SAME retained prepared plan");
    require(active->runtimeIdentity() != unused->runtimeIdentity() &&
                active->processId() != unused->processId(),
            "Distinct real worker runtime/owned-process identities, same original fixture class");
    require(active->declaredLatency() == PluginLatencyStamp{31, 1} &&
                unused->declaredLatency() == PluginLatencyStamp{31, 1},
            "Genuine original SDK initial native31 generation1; no metadata-derived PCM delay fit");
    const auto activeInstance = active->runtimeIdentity(), unusedInstance = unused->runtimeIdentity();
    const auto activePid = active->processId(), unusedPid = unused->processId();
    require(folder.getChildFile("runtime-identities.txt")
                .replaceWithText(
                    "active plugin1 instance=" + activeInstance + " pid=" + juce::String(activePid) +
                    "\nunused plugin2 instance=" + unusedInstance + " pid=" + juce::String(unusedPid) +
                    "\nQ=256 K=3 transport=768 native=31 expectedTotal=799\n"),
            "Retain exact actual runtime identity and literal latency policy separately from binary hashes");
    const auto activeReset = active->midiResetGeneration(), unusedReset = unused->midiResetGeneration();
    auto local = std::make_unique<Graph>(fixture, 901, active); // Only first Host belongs to graph.
    const auto reference = local->context->getNodePlayHead()->getReferenceSampleRange();
    Capture capture;
    for (int count = 0; count != 5; ++count)
        capture.drive(fixture, *peer, *local, {}, folder, "pending-before-membership", 256, false, true);
    require(local->musical->frames == 0 &&
                local->context->getNodePlayHead()->getReferenceSampleRange() == reference,
            "Actual pending local graph untouched while independent counterpart plays every sample");
    std::array<Membership::Binding, 2> bindings{{{&first, active, {}}, {&second, unused, {}}}};
    Membership::Ptr membership;
    const auto config = configuration(*plan, 901);
    require(Membership::prepare(plan, config, 902, bindings, 2, membership).succeeded(),
            "Actual fixed two-worker membership validates real descriptor/instance/plan/bus/config identity");
    bindings = {}; // Observer, graph and explicit CONTROL owners now define all lifetime pins.
    Session::Ptr session;
    require(Session::prepare(manager.deviceManager, config, membership, session) ==
                    Session::Status::prepared &&
                manager.installInitialSourceBurstSession(*local->context, session) ==
                    Session::Status::pendingFirstSerial,
            "Actual initial LOCAL session owns fixed Host Observer; install remains pending");
    const auto installed = local_source_initial_acceptance::read(session);
    Receipt initial;
    require(membership->read(initial) && initial.phase == Phase::prepared && initial.serial == 0,
            "No scope/native completion fabricated at session acceptance");
    if (partialBeginFailure)
        unused->beginRetirement(); // Genuine owned Host unavailable flag, NOT a worker-crash injection.
    local->edit->getTransport().setPosition(te::TimePosition::fromSeconds(0));
    local->edit->getTransport().play(false);
    dispatchMessages(10); // CONTROL only, no device callback in flight.
    const auto starts = fixture.device->starts, stops = fixture.device->stops;
    juce::String csv =
        "phase,nativeKnown,observerKnown,generation,epoch,serial,nativeStatus,failed,retired,open,quota,"
        "memberPhase,memberStatus,membership,memberSerial,closedSerial,abortedSerial,hostStatus,sourceStatus,"
        "activeCursor,activeSubmitted,activeMissed,activeDropped,unusedCursor,unusedSubmitted,unusedMissed,"
        "unusedDropped,"
        "consumedKnown,consumedSequence,consumedNative,consumedGeneration\n";
    juce::int64 expectedEnd = reference.getEnd();
    std::uint64_t previousSourceSerial = installed.activationAfterSerial;
    for (int block = 0; block != (partialBeginFailure ? 3 : 28); ++block)
    {
        const int frames = std::array<int, 7>{127, 64, 64, 65, 256, 512, 128}[std::size_t(block % 7)];
        drive(capture, fixture, *peer, *local, session, membership, *active, *unused, folder, csv,
              partialBeginFailure ? "fixed-required-begin-refusal" : "two-host-active", frames,
              !partialBeginFailure);
        const auto native = local_source_initial_acceptance::read(session);
        Receipt receipt;
        require(membership->read(receipt), "Actual fixed membership scalar result after native callback");
        if (partialBeginFailure)
        {
            require(native.failed && native.status == Session::Status::observerRefused && !native.scopeOpen &&
                        receipt.phase == Phase::aborted && receipt.status == Status::beginRefused &&
                        receipt.failedMember == 1 &&
                        receipt.host == PluginTransportStatus::workerUnavailable &&
                        !Membership::closedWith(receipt, native) && local->musical->frames == 0 &&
                        active->packetFrame() == 0 && active->statistics().submitted == 0 &&
                        local->context->getNodePlayHead()->getReferenceSampleRange() == reference,
                    "Required partial begin refusal aborts first Host metadata before source/clock/worker "
                    "packets; not incumbent guarantee");
        }
        else
        {
            expectedEnd += frames;
            const auto actual = local->context->getNodePlayHead()->getReferenceSampleRange();
            const auto unusedStats = unused->statistics();
            const auto consumed = active->consumedLatency();
            const bool exactConsumed =
                capture.localFrames <= 768
                    ? !consumed.has_value()
                    : consumed && consumed->stamp == PluginLatencyStamp{31, 1} &&
                          consumed->sequence == std::uint64_t((capture.localFrames - 1) / 256 - 3);
            require(Membership::closedWith(receipt, native) && receipt.membershipGeneration == 902 &&
                        receipt.quota == native.sourceQuota && native.physicalSerial > previousSourceSerial &&
                        actual.getEnd() == expectedEnd &&
                        actual.getStart() == expectedEnd - std::min(256, frames) &&
                        local->musical->firstMusicalSample == 0 && active->transportOutcome().succeeded() &&
                        active->statistics().submitted == std::uint64_t(capture.localFrames / 256) &&
                        exactConsumed && active->packetFrame() == capture.localFrames % 256 &&
                        active->statistics().missed == 0 && active->statistics().dropped == 0 &&
                        unused->transportOutcome().succeeded() && unused->packetFrame() == 0 &&
                        unusedStats.submitted == 0 && unusedStats.processed == 0 && unusedStats.missed == 0 &&
                        unusedStats.dropped == 0 && !unused->consumedLatency() &&
                        active->latencySamples() == 799 && unused->latencySamples() == 799 &&
                        fixture.device->starts == starts && fixture.device->stops == stops &&
                        active->runtimeIdentity() == activeInstance &&
                        unused->runtimeIdentity() == unusedInstance && active->processId() == activePid &&
                        unused->processId() == unusedPid && active->midiResetGeneration() == activeReset &&
                        unused->midiResetGeneration() == unusedReset,
                    "Actual native closed source + fixed membership + full799 musical PCM, unused zero-work "
                    "Host and exact owners retained");
            previousSourceSerial = native.physicalSerial;
        }
        dispatchMessages(6); // Outside the ENTIRE physical callback; never wait/service on DSP.
        active->service();
        if (!partialBeginFailure)
            unused->service();
    }
    if (!partialBeginFailure)
        peerRefusal(fixture, *peer, *local, plan, session, membership, *active, *unused, folder, csv);
    else
    {
        // Sole processing owner on CONTROL after actual callback exclusion.
        // The Host API itself proves first worker's partially opened scope was closed.
        const auto closed = active->endPhysicalBurst();
        require(closed.status == PluginTransportStatus::burstMissing && active->packetFrame() == 0 &&
                    active->statistics().submitted == 0,
                "Actual begin-abort cleanup leaves no first-Host open scope; explicit negative query retains "
                "failure");
        active->abortPhysicalBurst();
    }
    std::weak_ptr<PluginHost> weakActive = active, weakUnused = unused;
    std::weak_ptr<Membership> weakMembership = membership;
    active.reset();
    unused.reset();
    membership.reset();
    require(!weakActive.expired() && !weakUnused.expired() && !weakMembership.expired(),
            "Native session/graph retain exact members after external CONTROL handles released");
    local.reset(); // Native context destruction/retire joins/excludes before releasing Observer on CONTROL.
    require(local_source_initial_acceptance::read(session).retired && weakMembership.expired() &&
                weakActive.expired() && weakUnused.expired(),
            "Actual native context retirement releases all immutable observer/worker pins on CONTROL, no "
            "predecessor chain");
}

inline void tests(const juce::File &folder, const juce::File &originalLatencyFixture)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned two-worker initial source attempt");
    vibera::test::routing_lifetime::Storage storage(folder.getChildFile("storage"));
    const auto spec = parameterSpec(originalLatencyFixture);
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Real original SDK native31 effect, no installed-provider compatibility claim");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), originalLatencyFixture})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Retain actual executable/worker/fixture hashes");
    std::exception_ptr firstFailure;
    juce::Array<juce::var> outcomes;
    for (const bool refusal : {false, true})
    {
        const char *name = refusal ? "partial-required-begin-refusal" : "fixed-initial-plus-peer-refusal";
        auto item = std::make_unique<juce::DynamicObject>();
        item->setProperty("scenario", name);
        try
        {
            scenario(folder.getChildFile(name), spec, refusal);
            item->setProperty("passed", true);
        }
        catch (const std::exception &error)
        {
            if (!firstFailure)
                firstFailure = std::current_exception();
            item->setProperty("passed", false);
            item->setProperty("failure", juce::String(error.what()));
        }
        outcomes.add(juce::var(item.release()));
        require(
            folder.getChildFile("scenarios.json").replaceWithText(juce::JSON::toString(juce::var(outcomes))),
            "Retain both independent scenarios and every failure without retrying until green");
    }
    if (firstFailure)
        std::rethrow_exception(firstFailure);
}
} // namespace prepared_source_initial_hosts_acceptance
