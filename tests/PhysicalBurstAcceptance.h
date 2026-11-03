// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PhysicalBurstContract.h"
#include <array>
#include <thread>

// Requires existing require/SyntheticDevice/CallbackAllocationProbe declarations.
// Original actual JUCE enforcer/lifecycle test, no audio engine or physical driver.
__declspec(noinline) static void physicalBurstTests(const juce::File &folder)
{
    using Manager = juce::AudioDeviceManager;
    using namespace vibera::transport;
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh physical-burst source fixture output");
    Manager manager;
    manager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    require(manager.initialise(0, 2, &setup, false).isEmpty(), "Actual standalone JUCE synthetic device");
    auto *device = dynamic_cast<SyntheticDevice *>(manager.getCurrentAudioDevice());
    require(device && device->getCurrentBufferSizeSamples() == 256 &&
                manager.getCurrentPhysicalCallbackBound() == Manager::PhysicalCallbackBound::unknown,
            "Custom factory remains unknown, not silently promoted by device type/name");
    Manager::PhysicalCallbackSnapshot initial;
    require(manager.readPhysicalCallbackSnapshot(initial) && initial.active && initial.deviceEpoch != 0 &&
                initial.configuredFrames == 256 && initial.outerFrames == 0 && initial.callbackSerial == 0,
            "Actual pre-first-callback prepared device epoch; no fabricated observation");
    PhysicalBurstBudget budget, untouched;
    untouched.quantum = 777;
    require(preparePhysicalBurstBudget(true, initial, manager.getCurrentPhysicalCallbackBound(), 256, {0, 1},
                                       1000000, 0, untouched)
                        .status == BurstStatus::unknownProviderBound &&
                untouched.quantum == 777,
            "Unknown maximum does not select2Q or mutate caller budget");
    require(preparePhysicalBurstBudget(true, initial, manager.getCurrentPhysicalCallbackBound(), 256, {0, 1},
                                       1000000, 512, budget)
                        .status == BurstStatus::ready &&
                budget.packets == 3 && budget.transportSamples == 768 && budget.sourceFrames == 512,
            "Explicit generic512 burst needs independent conservative3Q, not clamp-to2Q");
    struct Reader final : juce::AudioIODeviceCallback
    {
        Manager &manager;
        const PhysicalBurstBudget &budget;
        struct Record
        {
            Manager::PhysicalCallbackSnapshot physical;
            BurstOutcome admission;
            int frames = 0, offset = 0;
            bool known = false;
        };
        std::array<Record, 32> records{};
        int count = 0, position = 0;
        juce::uint64 outer = 0;
        bool overflow = false;
        Reader(Manager &m, const PhysicalBurstBudget &b) : manager(m), budget(b) {}
        void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
        void audioDeviceStopped() override {}
        void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *output, int channels,
                                              int frames, const juce::AudioIODeviceCallbackContext &) override
        {
            if (count >= int(records.size()))
            {
                overflow = true;
                return;
            }
            auto &record = records[std::size_t(count++)];
            record.known = manager.readPhysicalCallbackSnapshot(record.physical);
            if (record.physical.callbackSerial != outer)
            {
                outer = record.physical.callbackSerial;
                position = 0;
            }
            record.frames = frames;
            record.offset = position;
            record.admission = admitPhysicalBurst(record.known, record.physical, budget);
            // Preserve the generic larger-burst path even when its budget is exceeded.
            // This fixture callback is NOT a hosted effect fallback implementation.
            for (int c = 0; c < channels; ++c)
                for (int n = 0; n < frames; ++n)
                    output[c][n] = float((position + n) % 31 + 1) / (c == 0 ? 64.f : -128.f);
            position += frames;
        }
    } reader(manager, budget);
    manager.addAudioCallback(&reader);
    struct Remove
    {
        Manager &manager;
        Reader &reader;
        ~Remove()
        {
            manager.removeAudioCallback(&reader);
        }
    } remove{manager, reader};
    juce::String csv =
        "outer,inner,frames,offset,serial,epoch,originalFrames,maximumFrames,admission,new,delete\n";
    juce::uint64 serial = initial.callbackSerial;
    int maximum = 0;
    for (const int outerFrames : {256, 127, 512, 768})
    {
        juce::AudioBuffer<float> pcm(2, outerFrames);
        const int before = reader.count;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
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
            device->process(pcm); // Actual device->enforcer->manager->Reader chain.
        }
        const auto allocated = callbackAllocations.load() - allocations;
        const auto deleted = callbackDestructions.load() - destructions;
        ++serial;
        maximum = std::max(maximum, outerFrames);
        bool correct = !reader.overflow && reader.count - before == (outerFrames + 255) / 256 &&
                       allocated == 0 && deleted == 0;
        for (int i = before, offset = 0; i < reader.count; ++i)
        {
            const auto &r = reader.records[std::size_t(i)];
            const auto expectedStatus =
                outerFrames <= 512 ? BurstStatus::ready : BurstStatus::physicalBurstExceeded;
            correct &= r.known && r.frames == std::min(256, outerFrames - offset) && r.offset == offset &&
                       r.physical.callbackSerial == serial && r.physical.deviceEpoch == initial.deviceEpoch &&
                       r.physical.outerFrames == outerFrames && r.physical.maximumOuterFrames == maximum &&
                       r.admission.status == expectedStatus && r.admission.actualFrames == outerFrames;
            csv += juce::String(outerFrames) + "," + juce::String(i - before) + "," + juce::String(r.frames) +
                   "," + juce::String(r.offset) + "," + juce::String(juce::int64(r.physical.callbackSerial)) +
                   "," + juce::String(juce::int64(r.physical.deviceEpoch)) + "," +
                   juce::String(r.physical.outerFrames) + "," + juce::String(r.physical.maximumOuterFrames) +
                   "," + juce::String(int(r.admission.status)) + "," + juce::String(allocated) + "," +
                   juce::String(deleted) + "\n";
            offset += r.frames;
        }
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < outerFrames; ++n)
                correct &= pcm.getSample(c, n) == float(n % 31 + 1) / (c == 0 ? 64.f : -128.f);
        const auto raw = folder.getChildFile("physical-outer-" + juce::String(outerFrames) + ".raw");
        require(!raw.exists(), "Preserve previous actual generic PCM evidence");
        auto stream = raw.createOutputStream();
        require(bool(stream), "Actual generic callback PCM evidence");
        for (int c = 0; c < 2; ++c)
            require(stream->write(pcm.getReadPointer(c), std::size_t(outerFrames) * sizeof(float)),
                    "Retain real planar PCM before assertion");
        stream.reset();
        require(folder.getChildFile("physical-splits.csv").replaceWithText(csv),
                "Actual original burst evidence before assertion");
        require(
            correct,
            "Real pre-enforcer burst survives every chunk; generic PCM path preserved; callback owner heap0");
    }
    Manager::PhysicalCallbackSnapshot observed;
    require(manager.readPhysicalCallbackSnapshot(observed), "Actual final telemetry");
    const auto unchanged = budget;
    require(preparePhysicalBurstBudget(true, observed, manager.getCurrentPhysicalCallbackBound(), 256, {0, 1},
                                       1000000, 512, budget)
                        .status == BurstStatus::physicalBurstExceeded &&
                budget.transportSamples == unchanged.transportSamples,
            "Observed768 counterexample prevents smaller512 preparation; old immutable budget retained");
    require(preparePhysicalBurstBudget(true, observed, manager.getCurrentPhysicalCallbackBound(), 256, {0, 1},
                                       1000000, 768, budget)
                        .status == BurstStatus::ready &&
                budget.packets == 4 && budget.transportSamples == 1024,
            "CONTROL may prepare larger4Q candidate; not selected recovery or proof of future maximum");
    require(admitSourceBurst(true, observed, budget, 769).status == BurstStatus::sourceBurstExceeded &&
                admitSourceBurst(true, observed, budget, 768).status == BurstStatus::ready,
            "Actual total source burst must be admitted before graph slices, not per-Host count");
    manager.closeAudioDevice();
    Manager::PhysicalCallbackSnapshot stopped;
    require(manager.readPhysicalCallbackSnapshot(stopped) && !stopped.active &&
                admitPhysicalBurst(true, stopped, budget).status == BurstStatus::deviceStopped,
            "Native close acknowledgement stops admission; no fictional automatic recovery");
    manager.restartLastAudioDevice(); // Pinned API returns void; verify actual state below.
    Manager::PhysicalCallbackSnapshot restarted;
    require(manager.readPhysicalCallbackSnapshot(restarted) && restarted.active &&
                restarted.deviceEpoch > initial.deviceEpoch && restarted.configuredFrames == 256 &&
                admitPhysicalBurst(true, restarted, budget).status == BurstStatus::deviceChanged,
            "Same buffer/rate restart invalidates old epoch budget before a new callback");
    // Pure boundary oracles supplement actual device flow; not real backend tests.
    auto synthetic = restarted;
    synthetic.maximumOuterFrames = 0;
    PhysicalBurstBudget prepared;
    require(preparePhysicalBurstBudget(true, synthetic, Manager::PhysicalCallbackBound::currentBufferSize,
                                       256, {31, 1}, 1000000, 0, prepared)
                        .status == BurstStatus::ready &&
                prepared.packets == 2 && prepared.transportSamples == 512,
            "Pure pinned-bound normal-speed2Q arithmetic, not actual pinned physical-device evidence");
    require(preparePhysicalBurstBudget(true, synthetic, Manager::PhysicalCallbackBound::currentBufferSize,
                                       256, {31, 1}, 1100000, 0, prepared)
                        .status == BurstStatus::ready &&
                prepared.sourceFrames == 282 && prepared.packets == 3,
            "Actual-source expansion distinct from padded graph capacity, conservative3Q for10percent");
    require(preparePhysicalBurstBudget(true, synthetic, Manager::PhysicalCallbackBound::unknown, 256, {0, 1},
                                       1000000, 2048, prepared)
                    .status == BurstStatus::packetCapacityExceeded,
            "Current8-slot IPC cannot silently admit required9Q");
    auto collided = observed;
    collided.writerConflict = true; // Pure refusal oracle, not an injected native concurrency pass.
    require(admitPhysicalBurst(true, collided, budget).status == BurstStatus::writerConflict,
            "Sticky failed publication is unavailable, never a coherent/admitted active callback");
    synthetic.configuredFrames = 8192;
    require(preparePhysicalBurstBudget(true, synthetic, Manager::PhysicalCallbackBound::currentBufferSize,
                                       8192, {114688, 1}, 1000000, 0, prepared)
                    .status == BurstStatus::delayCapacityExceeded,
            "valid(Q) native declaration still refuses total2Q+native131072 without clamping");
    require(folder.getChildFile("result.txt")
                .replaceWithText("PASS actual JUCE custom-device physical counts/lifecycle/generic chunk PCM "
                                 "and pure budget boundaries.\n"
                                 "No real device, hosted worker, graph speed/PDC, production "
                                 "policy/fallback/recovery, listening or timing qualification.\n"),
            "Truthful completed source-seam acceptance receipt");
}

// Original successor-only tests. Existing physicalBurstTests above is unchanged.
// Separate callback reads prove no ownership mutation inside the processing path.
__declspec(noinline) static void physicalBurstReaderTests(const juce::File &folder)
{
    using namespace vibera::transport;
    using Manager = juce::AudioDeviceManager;
    using Snapshot = Manager::PhysicalCallbackSnapshot;
    using RetainedReader = Manager::PhysicalCallbackReader;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh reader lifetime evidence");
    const Snapshot sentinel{91, 92, 93, 94, 95, true, true, true, true};
    const auto equal = [](const Snapshot &a, const Snapshot &b) noexcept
    {
        return a.deviceEpoch == b.deviceEpoch && a.callbackSerial == b.callbackSerial &&
               a.configuredFrames == b.configuredFrames && a.outerFrames == b.outerFrames &&
               a.maximumOuterFrames == b.maximumOuterFrames && a.active == b.active &&
               a.countersExhausted == b.countersExhausted && a.writerConflict == b.writerConflict &&
               a.ownerAlive == b.ownerAlive;
    };
    RetainedReader empty;
    auto unchanged = sentinel;
    require(!empty.read(unchanged) && equal(unchanged, sentinel),
            "Default null reader refuses without partially changing any output field");
    auto manager = std::make_unique<Manager>();
    auto retained = manager->getPhysicalCallbackReader(); // CONTROL allocation/copy only.
    auto copied = retained;
    auto moved = std::move(copied);
    unchanged = sentinel;
    require(!copied.read(unchanged) && equal(unchanged, sentinel),
            "CONTROL moved-from reader remains a harmless default-null source");
    Snapshot initial;
    require(retained.read(initial) && initial.ownerAlive && !initial.active && initial.deviceEpoch == 0 &&
                initial.callbackSerial == 0 && initial.outerFrames == 0,
            "Retained fresh manager reader does not fabricate an opened device or callback");
    manager->addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    require(manager->initialise(0, 2, &setup, false).isEmpty(), "Actual retained-reader synthetic device");
    auto *device = dynamic_cast<SyntheticDevice *>(manager->getCurrentAudioDevice());
    Snapshot before;
    require(device && retained.read(before) && before.active && before.ownerAlive &&
                before.deviceEpoch != 0 && before.configuredFrames == 256 && before.callbackSerial == 0,
            "Reader observes actual prepared device before first callback");
    struct Capture final : juce::AudioIODeviceCallback
    {
        RetainedReader reader;
        std::array<Snapshot, 16> snapshots{};
        std::array<bool, 16> known{};
        int count = 0, position = 0;
        juce::uint64 serial = 0;
        bool overflow = false;
        explicit Capture(const RetainedReader &source) : reader(source) {} // CONTROL only.
        void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
        void audioDeviceStopped() override {}
        void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *outputs, int channels,
                                              int frames, const juce::AudioIODeviceCallbackContext &) override
        {
            if (count >= int(snapshots.size()))
            {
                overflow = true;
                return;
            }
            auto &snapshot = snapshots[std::size_t(count)];
            known[std::size_t(count++)] = reader.read(snapshot); // No shared_ptr copy/move/reset.
            if (snapshot.callbackSerial != serial)
            {
                serial = snapshot.callbackSerial;
                position = 0;
            }
            for (int c = 0; c < channels; ++c)
                for (int n = 0; n < frames; ++n)
                    outputs[c][n] = float((position + n) % 29 + 1) / (c == 0 ? 64.f : -128.f);
            position += frames;
        }
    } capture(retained);
    manager->addAudioCallback(&capture);
    // Explicit removal before manager retirement also keeps this fixture safe
    // when a later assertion throws; the callback does not retain a manager ref.
    struct Remove
    {
        Manager *manager;
        Capture &capture;
        ~Remove()
        {
            if (manager)
                manager->removeAudioCallback(&capture);
        }
    } remove{manager.get(), capture};
    juce::String csv = "outer,frames,inner,epoch,serial,originalFrames,known,ownerAlive,active,new,delete\n";
    juce::uint64 serial = 0;
    const auto drive = [&](int frames, juce::uint64 epoch)
    {
        juce::AudioBuffer<float> pcm(2, frames);
        const int start = capture.count;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const bool previousProbe = callbackAllocationProbe;
        callbackAllocationProbe = true;
        device->process(pcm); // Actual JUCE pre-enforcer and callback reader.
        callbackAllocationProbe = previousProbe;
        const auto allocated = callbackAllocations.load() - allocations;
        const auto deleted = callbackDestructions.load() - destructions;
        ++serial;
        bool correct = !capture.overflow && capture.count - start == (frames + 255) / 256 && allocated == 0 &&
                       deleted == 0;
        for (int n = 0; n < frames; ++n)
            for (int c = 0; c < 2; ++c)
                correct &= pcm.getSample(c, n) == float(n % 29 + 1) / (c == 0 ? 64.f : -128.f);
        for (int i = start; i < capture.count; ++i)
        {
            const auto &snapshot = capture.snapshots[std::size_t(i)];
            correct &= capture.known[std::size_t(i)] && snapshot.ownerAlive && snapshot.active &&
                       snapshot.callbackSerial == serial && snapshot.deviceEpoch == epoch &&
                       snapshot.outerFrames == frames;
            csv += juce::String(serial) + "," + juce::String(frames) + "," + juce::String(i - start) + "," +
                   juce::String(juce::int64(snapshot.deviceEpoch)) + "," +
                   juce::String(juce::int64(snapshot.callbackSerial)) + "," +
                   juce::String(snapshot.outerFrames) + "," +
                   juce::String(int(capture.known[std::size_t(i)])) + "," +
                   juce::String(int(snapshot.ownerAlive)) + "," + juce::String(int(snapshot.active)) + "," +
                   juce::String(allocated) + "," + juce::String(deleted) + "\n";
        }
        const auto file = folder.getChildFile("reader-pcm-" + juce::String(serial) + ".raw");
        require(!file.exists(), "Reader PCM preserves prior failed attempts");
        auto stream = file.createOutputStream();
        require(bool(stream), "Reader actual PCM capture");
        for (int c = 0; c < 2; ++c)
            require(stream->write(pcm.getReadPointer(c), std::size_t(frames) * sizeof(float)),
                    "Retain full unchanged signal");
        stream.reset();
        require(folder.getChildFile("reader-callbacks.csv").replaceWithText(csv),
                "Retain evidence before musical assertion");
        require(correct,
                "Stable reader source does not change pre-enforcer PCM or allocate/destroy in callback");
    };
    drive(127, before.deviceEpoch);
    drive(512, before.deviceEpoch); // Retained metadata, not hosted-worker3Q admission.
    manager->closeAudioDevice();
    Snapshot stopped;
    require(retained.read(stopped) && stopped.ownerAlive && !stopped.active && stopped.callbackSerial == 2 &&
                stopped.deviceEpoch == before.deviceEpoch,
            "Close keeps manager owner alive while native device becomes inactive");
    manager->restartLastAudioDevice();
    device = dynamic_cast<SyntheticDevice *>(manager->getCurrentAudioDevice());
    Snapshot restarted;
    require(device && retained.read(restarted) && restarted.ownerAlive && restarted.active &&
                restarted.deviceEpoch > stopped.deviceEpoch &&
                restarted.callbackSerial == stopped.callbackSerial && restarted.outerFrames == 0 &&
                restarted.maximumOuterFrames == 0,
            "Same retained source observes acknowledged restart with new epoch, not a new reader identity");
    drive(64, restarted.deviceEpoch);
    Snapshot finalLive;
    require(retained.read(finalLive) && finalLive.ownerAlive && finalLive.callbackSerial == 3,
            "Actual final live reader identity before controlled retirement");
    PhysicalBurstBudget liveBudget;
    require(preparePhysicalBurstBudget(true, finalLive, Manager::PhysicalCallbackBound::unknown, 256, {0, 1},
                                       1000000, 256, liveBudget)
                    .status == BurstStatus::ready,
            "Actual live immutable budget before owner destruction");
    manager->removeAudioCallback(&capture);
    remove.manager = nullptr;

    // Original concurrent lifetime case: retain the handle on CONTROL, then the
    // reader thread only calls read(). Manager shutdown never moves that handle.
    // All native callbacks have been detached, and the device's own destructor
    // still performs its genuine stop before ownerAlive becomes false.
    std::atomic<bool> reading{false}, destroyed{false};
    bool coherent = true, unchangedOnFailure = true, postDeathCorrect = true, bounded = true;
    unsigned successes = 0, unavailable = 0, allocatedReads = 0, destroyedReads = 0;
    std::jthread observer(
        [&](std::stop_token stop)
        {
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            const bool previousProbe = callbackAllocationProbe;
            callbackAllocationProbe = true;
            const auto observe = [&]
            {
                auto value = sentinel;
                if (retained.read(value))
                {
                    ++successes;
                    coherent &= value.ownerAlive || !value.active;
                    coherent &= value.deviceEpoch == finalLive.deviceEpoch &&
                                value.callbackSerial == finalLive.callbackSerial;
                }
                else
                {
                    ++unavailable;
                    unchangedOnFailure &= equal(value, sentinel);
                }
            };
            observe(); // Actual read precedes ready acknowledgement, never a zero-read success.
            reading.store(true, std::memory_order_release);
            for (unsigned i = 0; !destroyed.load(std::memory_order_acquire) && !stop.stop_requested(); ++i)
            {
                if (i >= 1000000)
                {
                    bounded = false;
                    break;
                }
                observe();
            }
            if (!destroyed.load(std::memory_order_acquire))
                postDeathCorrect = false;
            else
                for (int i = 0; i < 64; ++i)
                {
                    auto value = sentinel;
                    const bool known = retained.read(value);
                    postDeathCorrect &= known && !value.ownerAlive && !value.active &&
                                        value.deviceEpoch == finalLive.deviceEpoch &&
                                        value.callbackSerial == finalLive.callbackSerial &&
                                        value.configuredFrames == finalLive.configuredFrames &&
                                        value.outerFrames == finalLive.outerFrames &&
                                        value.maximumOuterFrames == finalLive.maximumOuterFrames;
                }
            callbackAllocationProbe = previousProbe;
            allocatedReads = unsigned(callbackAllocations.load() - allocations);
            destroyedReads = unsigned(callbackDestructions.load() - destructions);
        });
    const auto readyDeadline = juce::Time::getMillisecondCounterHiRes() + 1000.;
    while (!reading.load(std::memory_order_acquire) &&
           juce::Time::getMillisecondCounterHiRes() < readyDeadline)
        std::this_thread::yield(); // CONTROL only, bounded startup handshake.
    const bool observerReady = reading.load(std::memory_order_acquire);
    manager.reset(); // Actual destructor/device stop, not an artificial dead-owner field mutation.
    destroyed.store(true, std::memory_order_release);
    observer.join(); // Reader handle never moved/reset/destroyed by the observer.
    Snapshot dead, secondDead;
    const bool deadKnown = retained.read(dead), movedKnown = moved.read(secondDead);
    bool passed = observerReady && successes + unavailable > 0 && bounded && coherent && unchangedOnFailure &&
                  postDeathCorrect && allocatedReads == 0 && destroyedReads == 0 && deadKnown && movedKnown &&
                  equal(dead, secondDead) && !dead.ownerAlive && !dead.active &&
                  dead.callbackSerial == finalLive.callbackSerial;
    auto untouchedBudget = liveBudget;
    const auto refused = preparePhysicalBurstBudget(true, dead, Manager::PhysicalCallbackBound::unknown, 256,
                                                    {0, 1}, 1000000, 256, untouchedBudget);
    const bool unchangedBudget = untouchedBudget.deviceEpoch == liveBudget.deviceEpoch &&
                                 untouchedBudget.configuredFrames == liveBudget.configuredFrames &&
                                 untouchedBudget.physicalFrames == liveBudget.physicalFrames &&
                                 untouchedBudget.sourceFrames == liveBudget.sourceFrames &&
                                 untouchedBudget.quantum == liveBudget.quantum &&
                                 untouchedBudget.packets == liveBudget.packets &&
                                 untouchedBudget.transportSamples == liveBudget.transportSamples &&
                                 untouchedBudget.maximumSourceScalePpm == liveBudget.maximumSourceScalePpm &&
                                 untouchedBudget.proof == liveBudget.proof;
    passed &= refused.status == BurstStatus::managerReleased && unchangedBudget &&
              admitPhysicalBurst(true, dead, liveBudget).status == BurstStatus::managerReleased &&
              admitSourceBurst(true, dead, liveBudget, 64).status == BurstStatus::managerReleased;
    // A different manager must not revive/rebind the retained dead source.
    auto replacement = std::make_unique<Manager>();
    replacement->addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    require(replacement->initialise(0, 2, &setup, false).isEmpty(), "Independent replacement manager source");
    Snapshot replacementState, stillDead;
    auto replacementReader = replacement->getPhysicalCallbackReader();
    passed &= replacementReader.read(replacementState) && replacementState.ownerAlive &&
              replacementState.active && retained.read(stillDead) && equal(stillDead, dead);
    require(folder.getChildFile("reader-result.txt")
                .replaceWithText(
                    "pass=" + juce::String(int(passed)) + ";reads=" + juce::String(successes) +
                    ";unavailable=" + juce::String(unavailable) + ";coherent=" + juce::String(int(coherent)) +
                    ";unchangedOnFailure=" + juce::String(int(unchangedOnFailure)) +
                    ";managerReleased=" + juce::String(int(refused.status == BurstStatus::managerReleased)) +
                    ";unchangedBudget=" + juce::String(int(unchangedBudget)) +
                    ";postDeath=" + juce::String(int(postDeathCorrect)) +
                    ";new=" + juce::String(allocatedReads) + ";delete=" + juce::String(destroyedReads) +
                    "\nActual custom JUCE lifecycle and retained scalar reader; no plugin worker, physical "
                    "hardware, listening or callback deadline qualification.\n"),
            "Retain original reader lifetime outcomes before assertion");
    require(passed, "Retained readers survive concurrent native owner retirement without heap or dangling "
                    "manager access");
}
