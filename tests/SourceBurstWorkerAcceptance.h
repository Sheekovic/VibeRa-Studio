// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RemoteEffect.h"
namespace source_burst_acceptance
{
struct HostObserver final : Session::Observer
{
    const std::shared_ptr<PluginHost> host;
    unsigned begins = 0, ends = 0, aborts = 0;
    explicit HostObserver(std::shared_ptr<PluginHost> h) : host(std::move(h)) {}
    bool begin(const Session::Outcome &o) noexcept override
    {
        ++begins;
        return host->beginPhysicalBurst(o.sourceQuota).succeeded();
    }
    bool end(const Session::Outcome &) noexcept override
    {
        ++ends;
        return host->endPhysicalBurst().succeeded();
    }
    void abort(const Session::Outcome &) noexcept override
    {
        ++aborts;
        host->abortPhysicalBurst();
    }
};
__declspec(noinline) inline void workerTests(const juce::File &folder, const juce::File &binary)
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
    te::Edit::Options options{f.audio->engine, te::createEmptyEdit(f.audio->engine), {}};
    options.numAudioTracks = 1;
    auto edit = te::Edit::createEdit(options);
    require(bool(edit), "Actual independent native graph Edit");
    auto tracks = te::getAudioTracks(*edit);
    require(tracks.size() == 1, "Exact closed source/effect native graph");
    tracks[0]->getOutput().setOutputToDefaultDevice(false); // false = native AUDIO, not MIDI.
    f.audio->engine.getPluginManager().createBuiltInType<Signal>();
    auto source = edit->getPluginCache().createNewPlugin(Signal::xmlTypeName, {});
    auto effect = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
    auto *generator = dynamic_cast<Signal *>(source.get());
    auto *remote = dynamic_cast<RemoteEffect *>(effect.get());
    require(generator && remote, "Actual registered cached source and effect types");
    auto timeline = std::make_shared<LiveTimeline>();
    struct Capture
    {
        std::array<std::array<float, 2>, 16384> samples{};
        int frames = 0;
        static void append(void *opaque, const te::PluginRenderContext &fc) noexcept
        {
            auto &self = *static_cast<Capture *>(opaque);
            for (int n = 0; n < fc.bufferNumSamples && self.frames < int(self.samples.size()); ++n)
            {
                for (int ch = 0; ch < 2; ++ch)
                    self.samples[std::size_t(self.frames)][std::size_t(ch)] =
                        fc.destBuffer->getSample(ch, fc.bufferStartSample + n);
                ++self.frames;
            }
        }
    };
    auto input = std::make_unique<Capture>(), processedOutput = std::make_unique<Capture>();
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
    auto observer = std::make_shared<HostObserver>(host);
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
    bool everyRequiredPhysicalDeadlineMet = true;
    // This fixture is configured256 and advertises256/512. Its irregular
    // outer1 call remains a REAL outer callback and a strict PCM/lifetime test,
    // but does not qualify advertised device cadence. User policy2026-10-18
    // accepts its failed stress timing, never a different/floored deadline.
    const bool singleFrameStressOnly =
        f.device->getCurrentBufferSizeSamples() == 256 && !f.device->getAvailableBufferSizes().contains(1);
    int singleFrameDeadlineMisses = 0;
    juce::String csv =
        "outer,physical,source,submitted,processed,missed,dropped,maxError,new,delete,callbackMs,status,"
        "failed,retired,scope,begins,ends,aborts,sameContext,playing,graph\n";
    juce::String pcm = "frame,left,right,expectedLeft,expectedRight,effectInputLeft,effectOutputLeft\n";
    for (int outer = 0; outer < 28; ++outer)
    {
        const int count = std::array<int, 7>{127, 64, 65, 512, 256, 1, 255}[std::size_t(outer % 7)];
        juce::AudioBuffer<float> output(2, count);
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
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const bool metLiteralDeadline = ms <= 1000. * count / 48000.;
        everyPhysicalDeadlineMet &= metLiteralDeadline;
        if (count == 1 && singleFrameStressOnly)
            singleFrameDeadlineMisses += !metLiteralDeadline;
        else
            everyRequiredPhysicalDeadlineMet &= metLiteralDeadline;
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
    require(folder.getChildFile("physical-timing-scope.txt")
                .replaceWithText(
                    "policy=user2026-10-18\nconfiguredFrames=256\n"
                    "outerOneFrameIsInternalSlice=false\n"
                    "allLiteralPhysicalDeadlinesMet=" +
                    juce::String(int(everyPhysicalDeadlineMet)) +
                    "\nrequiredPhysicalDeadlinesMet=" + juce::String(int(everyRequiredPhysicalDeadlineMet)) +
                    "\nunadvertisedSyntheticOneFrameStress=" + juce::String(int(singleFrameStressOnly)) +
                    "\noneFrameLiteralDeadlineMisses=" + juce::String(singleFrameDeadlineMisses) +
                    "\nOne-frame PCM/scopes/heap/packets remain required; stress timing is not fixed.\n"),
            "Retain literal outer-callback timing and explicit accepted stress limitation");
    require(everyRequiredPhysicalDeadlineMet,
            "Required actual outer callbacks meet unchanged frames/rate deadlines; see full CSV and scope");
}
} // namespace source_burst_acceptance
