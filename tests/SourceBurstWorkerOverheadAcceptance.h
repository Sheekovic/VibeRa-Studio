// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SourceBurstWorkerAcceptance.h"
#include "NativePluginCpuReadback.h"
#include <atomic>
namespace source_burst_acceptance
{
// Independent diagnostic variant ONLY. Original native_source_admission
// remains unchanged/failed. Added clock/probe work can worsen these deadlines.
struct OverheadTrace
{
    enum class Kind : unsigned
    {
        hostBegin,
        hostEnd,
        inputCapture,
        outputCapture,
        signal
    };
    struct Event
    {
        Kind kind{};
        juce::int64 begin = 0, end = 0;
        int frames = 0, offset = 0;
        std::uint64_t serial = 0;
        int quota = 0;
        unsigned thread = 0;
        std::uint64_t owner = 0; // Actual native Plugin item ID, not a pointer/guessed ordinal.
        int firstFrame = -1, channels = 0; // Absolute fixture source-stream frame, not transport time.
    };
    struct Slot
    {
        Event event;
        std::atomic<unsigned> generation{0};
    };
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static constexpr unsigned capacity = 64;
    std::array<Slot, capacity> slots{}; // Heap-owned, initialized once on CONTROL.
    std::atomic<unsigned> used{0}, generation{0};
    std::atomic<bool> overflow{false};
    std::uint64_t sourceOwner = 0, effectOwner = 0; // CONTROL once, before graph preparation.
    void reset(unsigned outer) noexcept // CONTROL, after previous callback/readback joins.
    {
        used.store(0);
        overflow.store(false);
        generation.store(outer + 1);
    }
    void append(Kind kind, juce::int64 begin, juce::int64 end, int frames = 0, int offset = 0,
                std::uint64_t serial = 0, int quota = 0, std::uint64_t owner = 0,
                int firstFrame = -1, int channels = 0) noexcept
    {
        const auto index = used.fetch_add(1, std::memory_order_relaxed);
        if (index >= capacity)
        {
            overflow.store(true);
            return;
        }
        unsigned thread = 0;
#if JUCE_WINDOWS
        thread = unsigned(GetCurrentThreadId());
#endif
        slots[index].event = {kind, begin, end, frames, offset, serial, quota, thread, owner, firstFrame, channels};
        slots[index].generation.store(generation.load(std::memory_order_relaxed), std::memory_order_release);
    }
    struct Summary
    {
        juce::int64 hostBegin = 0, hostEnd = 0, inputCapture = 0, outputCapture = 0, remoteBracket = 0;
        juce::int64 beforeFirstSignal = 0, signalBody = 0, signalToInput = 0;
        unsigned signalCalls = 0, signalInputPairs = 0;
        unsigned events = 0, paired = 0;
        bool valid = true, overflow = false;
    };
    Summary readControl(unsigned outer, juce::int64 callbackBegin, juce::int64 callbackEnd,
                        juce::String &csv) const
    {
        Summary result;
        const auto count = used.load(std::memory_order_acquire);
        result.overflow = overflow.load() || count > capacity;
        result.valid = !result.overflow && sourceOwner != 0 && effectOwner != 0 && sourceOwner != effectOwner;
        std::array<bool, capacity> matchedSignals{}; // CONTROL-only pairing; never DSP work.
        const Event *pendingInput = nullptr;
        int pendingInputEvent = -1, pendingSignalEvent = -1;
        for (unsigned i = 0; i < std::min(count, capacity); ++i)
        {
            if (slots[i].generation.load(std::memory_order_acquire) != outer + 1)
            {
                result.valid = false;
                continue;
            }
            const auto &e = slots[i].event;
            ++result.events;
            const auto duration = e.end - e.begin;
            int signalEvent = -1, inputEvent = -1;
            result.valid &= callbackBegin <= e.begin && e.begin <= e.end && e.end <= callbackEnd;
            switch (e.kind)
            {
            case Kind::hostBegin:
                result.hostBegin += duration;
                break;
            case Kind::hostEnd:
                result.hostEnd += duration;
                break;
            case Kind::signal:
                result.valid &= e.owner == sourceOwner && e.firstFrame >= 0 && e.channels == 2;
                if (result.signalCalls++ == 0)
                    result.beforeFirstSignal = e.begin - callbackBegin;
                result.signalBody += duration;
                signalEvent = int(i);
                break;
            case Kind::inputCapture:
            {
                result.valid &= e.owner == effectOwner && e.firstFrame >= 0 && e.channels == 2;
                // Dependencies may run on different native worker threads. Pair by
                // exact generated/captured stream interval and actual owner IDs,
                // never by thread coincidence or a fitted time/sample offset.
                for (unsigned source = 0; source < i; ++source)
                {
                    if (slots[source].generation.load(std::memory_order_acquire) != outer + 1)
                        continue;
                    const auto &s = slots[source].event;
                    if (s.kind == Kind::signal && s.owner == sourceOwner &&
                        s.firstFrame == e.firstFrame && s.frames == e.frames && s.channels == e.channels)
                    {
                        if (signalEvent != -1 || matchedSignals[source] || s.end > e.begin)
                            result.valid = false;
                        else
                        {
                            signalEvent = int(source);
                            matchedSignals[source] = true;
                            result.signalToInput += e.begin - s.end;
                            ++result.signalInputPairs;
                        }
                    }
                }
                result.valid &= signalEvent >= 0;
                inputEvent = int(i);
                pendingInputEvent = inputEvent;
                pendingSignalEvent = signalEvent;
                result.inputCapture += duration;
                if (pendingInput)
                    result.valid = false;
                pendingInput = &e;
                break;
            }
            case Kind::outputCapture:
                signalEvent = pendingSignalEvent;
                inputEvent = pendingInputEvent;
                result.valid &= e.owner == effectOwner && e.firstFrame >= 0 && e.channels == 2;
                result.outputCapture += duration;
                if (!pendingInput || pendingInput->thread != e.thread || pendingInput->frames != e.frames ||
                    pendingInput->offset != e.offset || pendingInput->end > e.begin ||
                    pendingInput->firstFrame != e.firstFrame || pendingInput->channels != e.channels ||
                    pendingInput->owner != e.owner)
                    result.valid = false;
                else
                {
                    result.remoteBracket += e.begin - pendingInput->end;
                    ++result.paired;
                }
                pendingInput = nullptr;
                pendingInputEvent = pendingSignalEvent = -1;
                break;
            }
            csv += juce::String(outer) + "," + juce::String(i) + "," + juce::String(unsigned(e.kind)) + "," +
                   juce::String(e.begin) + "," + juce::String(e.end) + "," + juce::String(duration) + "," +
                   juce::String(e.frames) + "," + juce::String(e.offset) + "," +
                   juce::String(juce::uint64(e.serial)) + "," + juce::String(e.quota) + "," +
                   juce::String(e.thread) + "," + juce::String(juce::uint64(e.owner)) + "," +
                   juce::String(e.firstFrame) + "," + juce::String(e.channels) + "," +
                   juce::String(signalEvent) + "," + juce::String(inputEvent) + "\n";
        }
        result.valid &= pendingInput == nullptr && result.signalCalls > 0 &&
                        result.signalInputPairs == result.signalCalls && result.signalInputPairs == result.paired;
        return result;
    }
};
// Same original Signal algorithm/lifecycle, replacement diagnostic source only.
class OverheadSignal final : public te::Plugin
{
  public:
    static constexpr const char *xmlTypeName = "vibera_source_burst_signal_qpc";
    int frames = 0;
    OverheadTrace *trace = nullptr; // Preowned event bank, bound on CONTROL.
    std::uint64_t owner = 0;
    const bool silent;
    explicit OverheadSignal(te::PluginCreationInfo info)
        : Plugin(info), silent(bool(info.state.getProperty("silent", false)))
    {
    }
    ~OverheadSignal() override
    {
        notifyListenersOfDeletion();
    }
    juce::String getName() const override
    {
        return "Original physical-source signal";
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool isSynth() override
    {
        return true;
    }
    bool takesAudioInput() override
    {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return true;
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    void initialise(const te::PluginInitialisationInfo &) override {}
    void deinitialise() override {}
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    static float sample(int channel, int frame) noexcept
    {
        return (channel ? -.0625f : .125f) +
               (frame % (channel ? 257 : 251) == (channel ? 23 : 11) ? (channel ? -.125f : .25f) : 0.f);
    }
    void applyToBuffer(const te::PluginRenderContext &context) override
    {
        const auto began = juce::Time::getHighResolutionTicks();
        const auto first = frames;
        if (!context.destBuffer)
        {
            const auto ended = juce::Time::getHighResolutionTicks();
            if (trace) trace->append(OverheadTrace::Kind::signal, began, ended, context.bufferNumSamples,
                context.bufferStartSample, 0, 0, owner, first, 0);
            return;
        }
        for (int c = 0; c < context.destBuffer->getNumChannels(); ++c)
            for (int n = 0; n < context.bufferNumSamples; ++n)
                context.destBuffer->setSample(c, context.bufferStartSample + n,
                                              silent ? 0.f : sample(c, frames + n));
        frames += context.bufferNumSamples;
        const auto ended = juce::Time::getHighResolutionTicks();
        if (trace) trace->append(OverheadTrace::Kind::signal, began, ended, context.bufferNumSamples,
            context.bufferStartSample, 0, 0, owner, first, context.destBuffer->getNumChannels());
    }
};
struct OverheadHostObserver final : Session::Observer
{
    const std::shared_ptr<PluginHost> host;
    OverheadTrace &trace;
    unsigned begins = 0, ends = 0, aborts = 0;
    OverheadHostObserver(std::shared_ptr<PluginHost> h, OverheadTrace &value)
        : host(std::move(h)), trace(value)
    {
    }
    bool begin(const Session::Outcome &o) noexcept override
    {
        ++begins;
        const auto began = juce::Time::getHighResolutionTicks();
        const auto result = host->beginPhysicalBurst(o.sourceQuota).succeeded();
        const auto ended = juce::Time::getHighResolutionTicks();
        trace.append(OverheadTrace::Kind::hostBegin, began, ended, 0, 0, o.physicalSerial, o.sourceQuota);
        return result;
    }
    bool end(const Session::Outcome &o) noexcept override
    {
        ++ends;
        const auto began = juce::Time::getHighResolutionTicks();
        const auto result = host->endPhysicalBurst().succeeded();
        const auto ended = juce::Time::getHighResolutionTicks();
        trace.append(OverheadTrace::Kind::hostEnd, began, ended, 0, 0, o.physicalSerial, o.sourceQuota);
        return result;
    }
    void abort(const Session::Outcome &) noexcept override
    {
        ++aborts;
        host->abortPhysicalBurst();
    }
};
__declspec(noinline) inline void workerOverheadTests(const juce::File &folder, const juce::File &binary)
{
    using namespace vibera::test::routing_lifetime;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh native source/SDK evidence folder");
    Storage storage(folder.getChildFile("storage"));
    Fixture f(folder.getChildFile("fixture"));
    for (int n = 0; n < 4; ++n)
        f.silentCallback("Prepared original closed no-note app context");
    auto &manager = f.audio->engine.getDeviceManager();
    PreparedPluginTransport::Ptr plan;
    require(PreparedPluginTransport::prepare(manager.deviceManager, 256, {31, 1}, 1000000, 512, plan)
                    .succeeded() &&
                plan->budget().packets == 3,
            "Exact manager-derived literal K3/768 native31 plan");
    const auto spec = parameterSpec(binary);
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Genuine scanned original native31 SDK fixture");
    auto host = std::make_shared<PluginHost>(plan);
    const auto started = host->start(spec, 48000., 256, false);
    require(started.wasOk(), "Actual native-source worker: " + started.getErrorMessage().toStdString());
    auto overheadTrace = std::make_unique<OverheadTrace>(); // Outlives Edit and its observer.
    te::Edit::Options options{f.audio->engine, te::createEmptyEdit(f.audio->engine), {}};
    options.numAudioTracks = 1;
    auto edit = te::Edit::createEdit(options);
    require(bool(edit), "Actual independent native graph Edit");
    auto tracks = te::getAudioTracks(*edit);
    require(tracks.size() == 1, "Exact closed source/effect native graph");
    tracks[0]->getOutput().setOutputToDefaultDevice(false); // false = native AUDIO, not MIDI.
    f.audio->engine.getPluginManager().createBuiltInType<OverheadSignal>();
    auto source = edit->getPluginCache().createNewPlugin(OverheadSignal::xmlTypeName, {});
    auto effect = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
    auto *generator = dynamic_cast<OverheadSignal *>(source.get());
    auto *remote = dynamic_cast<RemoteEffect *>(effect.get());
    require(generator && remote, "Actual registered cached source and effect types");
    overheadTrace->sourceOwner = generator->owner = source->itemID.getRawID();
    overheadTrace->effectOwner = effect->itemID.getRawID();
    generator->trace = overheadTrace.get();
    require(folder.getChildFile("signal-identity.txt").replaceWithText(
        "diagnosticType=" + juce::String(OverheadSignal::xmlTypeName) + "\noriginalAlgorithmType=" +
        Signal::xmlTypeName + "\nsourceItemID=" + juce::String(juce::uint64(overheadTrace->sourceOwner)) +
        "\neffectItemID=" + juce::String(juce::uint64(overheadTrace->effectOwner)) +
        "\nclockHz=" + juce::String(juce::Time::getHighResolutionTicksPerSecond()) +
        "\nOne replacement source node; actual item IDs, fixture-stream frame IDs and raw clocks.\n"),
        "Record actual diagnostic source/effect identity before any measured callback");
    auto timeline = std::make_shared<LiveTimeline>();
    struct Capture
    {
        OverheadTrace *trace = nullptr;
        OverheadTrace::Kind kind{};
        std::uint64_t owner = 0;
        std::array<std::array<float, 2>, 16384> samples{};
        int frames = 0;
        static void append(void *opaque, const te::PluginRenderContext &fc) noexcept
        {
            const auto began = juce::Time::getHighResolutionTicks();
            auto &self = *static_cast<Capture *>(opaque);
            const auto first = self.frames;
            for (int n = 0; n < fc.bufferNumSamples && self.frames < int(self.samples.size()); ++n)
            {
                for (int ch = 0; ch < 2; ++ch)
                    self.samples[std::size_t(self.frames)][std::size_t(ch)] =
                        fc.destBuffer->getSample(ch, fc.bufferStartSample + n);
                ++self.frames;
            }
            const auto ended = juce::Time::getHighResolutionTicks();
            self.trace->append(self.kind, began, ended, fc.bufferNumSamples, fc.bufferStartSample,
                0, 0, self.owner, first, fc.destBuffer->getNumChannels());
        }
    };
    auto input = std::make_unique<Capture>(), processedOutput = std::make_unique<Capture>();
    input->trace = processedOutput->trace = overheadTrace.get();
    input->owner = processedOutput->owner = effect->itemID.getRawID();
    input->kind = OverheadTrace::Kind::inputCapture;
    processedOutput->kind = OverheadTrace::Kind::outputCapture;
    auto document = std::make_unique<LiveDocument>();
    document->generation = 1;
    require(timeline->publish(document), "CONTROL publishes genuine empty automation document");
    remote->configure(host, false, timeline, spec.id);
    remote->setInputProbe(input.get(), Capture::append);
    remote->setOutputProbe(processedOutput.get(), Capture::append);
    tracks[0]->pluginList.insertPlugin(source, 0, nullptr);
    tracks[0]->pluginList.insertPlugin(effect, 1, nullptr);
    require(tracks[0]->pluginList.getPlugins().contains(source.get()) &&
                tracks[0]->pluginList.getPlugins().contains(effect.get()),
            "Both exact configured cached owners are actually in the graph plugin list");
    prepareClosedNativeEdit(*edit);
    auto &transport = edit->getTransport();
    transport.ensureContextAllocated();
    auto *context = transport.getCurrentPlaybackContext();
    require(context && context->getNodePlayHead(), "Actual native worker processing context");
    auto observer = std::make_shared<OverheadHostObserver>(host, *overheadTrace);
    const auto &b = plan->budget();
    Session::Configuration c;
    c.deviceEpoch = b.deviceEpoch;
    c.generation = 100;
    c.sampleRate = plan->sampleRate();
    c.deviceFrames = b.configuredFrames;
    c.maximumPhysicalFrames = b.physicalFrames;
    c.maximumSourceFrames = b.sourceFrames;
    c.maximumSourceScalePpm = b.maximumSourceScalePpm;
    Session::Ptr session;
    require(Session::prepare(manager.deviceManager, c, observer, session) == Session::Status::prepared &&
                manager.installSourceBurstSession(*context, session) == Session::Status::prepared,
            "Same immutable worker plan admits whole native graph before processing");
    transport.play(false);
    const auto starts = f.device->starts, stops = f.device->stops;
    int frame = 0;
    bool everyPhysicalDeadlineMet = true;
    juce::String csv =
        "outer,physical,source,submitted,processed,missed,dropped,maxError,new,delete,callbackMs,status,"
        "failed,retired,scope,begins,ends,aborts,sameContext,playing,graph\n";
    juce::String pcm = "frame,left,right,expectedLeft,expectedRight,effectInputLeft,effectOutputLeft\n";
    juce::String eventCsv = "outer,event,kind,beginQpc,endQpc,durationTicks,nativeFrames,bufferOffset,"
                            "physicalSerial,quota,thread,nativeOwner,firstSourceFrame,channels,signalEvent,inputEvent\n";
    juce::String segmentCsv =
        "outer,physical,deadlineMs,metDeadline,wholeBeginQpc,wholeEndQpc,syntheticBeginQpc,dispatchBeginQpc,"
        "dispatchEndQpc,syntheticEndQpc,hostBeginTicks,hostEndTicks,inputCaptureTicks,outputCaptureTicks,"
        "remoteBracketTicks,matchedRemoteCalls,events,traceOverflow,traceValid,clockHz,"
        "beforeFirstSignalTicks,signalBodyTicks,signalToInputTicks,signalCalls,signalInputPairs\n";
    juce::String pluginCpuCsv = nativePluginCpuCsvHeader;
    for (int outer = 0; outer < 28; ++outer)
    {
        const int count = std::array<int, 7>{127, 64, 65, 512, 256, 1, 255}[std::size_t(outer % 7)];
        juce::AudioBuffer<float> output(2, count);
        overheadTrace->reset(unsigned(outer)); // CONTROL; no old callback/probe reader in flight.
        const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                bool old = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = old;
                }
            } probe;
            f.device->process(output);
        }
        const auto ended = juce::Time::getHighResolutionTicks();
        // CONTROL only, after the entire measured callback has returned. Native
        // smoothed telemetry is diagnostic, never raw deadline attribution.
        appendNativePluginCpuControl(f.audio->engine, edit.get(), outer, count, pluginCpuCsv);
        const auto ms = SyntheticCallbackTiming::milliseconds(began, ended);
        everyPhysicalDeadlineMet &= ms <= 1000. * count / 48000.;
        double error = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < count; ++n)
            {
                const int at = frame + n - 799;
                const float expected = at < 0 ? 0.f : Signal::sample(ch, at);
                const auto actual = output.getSample(ch, n);
                error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                              : std::numeric_limits<double>::infinity();
            }
        for (int n = 0; n < count; ++n)
        {
            const int at = frame + n - 799;
            pcm += juce::String(frame + n) + "," + juce::String(output.getSample(0, n), 9) + "," +
                   juce::String(output.getSample(1, n), 9) + "," +
                   juce::String(at < 0 ? 0.f : Signal::sample(0, at), 9) + "," +
                   juce::String(at < 0 ? 0.f : Signal::sample(1, at), 9) + "," +
                   juce::String(input->samples[std::size_t(frame + n)][0], 9) + "," +
                   juce::String(processedOutput->samples[std::size_t(frame + n)][0], 9) + "\n";
        }
        frame += count;
        const auto stats = host->statistics();
        const auto result = read(session);
        const auto a = callbackAllocations.load() - allocated, d = callbackDestructions.load() - destroyed;
        csv += juce::String(outer) + "," + juce::String(count) + "," + juce::String(generator->frames) + "," +
               juce::String(juce::int64(stats.submitted)) + "," + juce::String(juce::int64(stats.processed)) +
               "," + juce::String(juce::int64(stats.missed)) + "," +
               juce::String(juce::int64(stats.dropped)) + "," + juce::String(error, 12) + "," +
               juce::String(a) + "," + juce::String(d) + "," + juce::String(ms, 6) + "," +
               juce::String(int(result.status)) + "," + juce::String(int(result.failed)) + "," +
               juce::String(int(result.retired)) + "," + juce::String(int(result.scopeOpen)) + "," +
               juce::String(observer->begins) + "," + juce::String(observer->ends) + "," +
               juce::String(observer->aborts) + "," +
               juce::String(int(transport.getCurrentPlaybackContext() == context)) + "," +
               juce::String(int(transport.isPlaying())) + "," +
               juce::String(int(context->isPlaybackGraphAllocated())) + "\n";
        require(folder.getChildFile("native-worker.csv").replaceWithText(csv),
                "Retain complete failed/native worker observations");
        require(folder.getChildFile("native-worker-pcm.csv").replaceWithText(pcm),
                "Retain literal source/output PCM before timing oracle");
        const auto measured = overheadTrace->readControl(unsigned(outer), began, ended, eventCsv);
        const auto &driver = f.device->lastTiming();
        segmentCsv += juce::String(outer) + "," + juce::String(count) + "," +
                      juce::String(1000. * count / 48000., 9) + "," +
                      juce::String(int(ms <= 1000. * count / 48000.)) + "," + juce::String(began) + "," +
                      juce::String(ended) + "," + juce::String(driver.processBegan) + "," +
                      juce::String(driver.dispatchBegan) + "," + juce::String(driver.dispatchEnded) + "," +
                      juce::String(driver.processEnded) + "," + juce::String(measured.hostBegin) + "," +
                      juce::String(measured.hostEnd) + "," + juce::String(measured.inputCapture) + "," +
                      juce::String(measured.outputCapture) + "," + juce::String(measured.remoteBracket) +
                      "," + juce::String(measured.paired) + "," + juce::String(measured.events) + "," +
                      juce::String(int(measured.overflow)) + "," + juce::String(int(measured.valid)) + "," +
                      juce::String(juce::Time::getHighResolutionTicksPerSecond()) + "," +
                      juce::String(measured.beforeFirstSignal) + "," + juce::String(measured.signalBody) + "," +
                      juce::String(measured.signalToInput) + "," + juce::String(measured.signalCalls) + "," +
                      juce::String(measured.signalInputPairs) + "\n";
        require(folder.getChildFile("overhead-events.csv").replaceWithText(eventCsv) &&
                    folder.getChildFile("overhead-callbacks.csv").replaceWithText(segmentCsv),
                "Retain complete diagnostic clocks before original musical/deadline assertions");
        require(measured.valid && measured.paired > 0 && measured.signalCalls == measured.paired &&
                    measured.signalInputPairs == measured.paired,
                "Diagnostic event storage/pairing must be valid; never silently discard trace overflow");
        require(folder.getChildFile("native-plugin-cpu.csv").replaceWithText(pluginCpuCsv),
                "Retain CONTROL-only smoothed telemetry with actual context identities");
        const auto consumed = host->consumedLatency();
        const bool exactConsumed = frame <= 768
                                       ? !consumed.has_value()
                                       : consumed && consumed->stamp.nativeSamples == 31 &&
                                             consumed->sequence == std::uint64_t((frame - 1) / 256 - 3);
        require(error == 0 && a == 0 && d == 0 && !result.failed && !result.scopeOpen &&
                    host->transportOutcome().succeeded() && !host->deliveryFailed() && exactConsumed &&
                    stats.submitted == std::uint64_t(frame / 256) && host->packetFrame() == frame % 256 &&
                    generator->frames == frame && observer->begins == unsigned(outer + 1) &&
                    observer->ends == observer->begins && observer->aborts == 0 &&
                    f.device->starts == starts && f.device->stops == stops,
                "Actual native graph/source/SDK exact literal799 PCM and once-per-physical scope");
        dispatchMessages(6); // CONTROL scheduling only after the ENTIRE physical callback.
        host->service();
    }
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (host->alive() && host->statistics().processed != host->statistics().submitted &&
           juce::Time::getMillisecondCounter() < deadline)
    {
        dispatchMessages(1); // Sole CONTROL driver; no device callback is in flight.
        host->service();
    }
    require(host->alive() && host->statistics().processed == host->statistics().submitted,
            "Every submitted native graph packet actually completed before lifetime/refusal checks");
    const auto oldFrames = generator->frames, oldCursor = host->packetFrame();
    const auto oldStats = host->statistics();
    const auto oldReference = context->getNodePlayHead()->getReferenceSampleRange();
    juce::String refusedCsv = "phase,physicalFrames,callbackMs,new,delete,exactSilence\n";
    const auto refusedCallbackOk = drive(f, 768, refusedCsv, "physical-bound-refusal");
    require(folder.getChildFile("refusal.csv").replaceWithText(refusedCsv),
            "Retain native bound refusal evidence");
    require(refusedCallbackOk && read(session).status == Session::Status::physicalBurstExceeded &&
                generator->frames == oldFrames && host->packetFrame() == oldCursor &&
                host->statistics().submitted == oldStats.submitted &&
                context->getNodePlayHead()->getReferenceSampleRange() == oldReference &&
                observer->aborts == 0,
            "Oversized native physical callback refuses before source, clock, host scope or packets");
    require(folder.getChildFile("refusal.csv").replaceWithText(refusedCsv),
            "Retain native bound refusal evidence");
    source = nullptr;
    effect = nullptr;
    edit.reset();
    require(read(session).retired, "CONTROL context removal retires observer before last external handles");
    require(everyPhysicalDeadlineMet,
            "Every actual physical callback meets its unchanged frames/sample-rate deadline; see full CSV");
}
} // namespace source_burst_acceptance
