// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedPluginTransport.h"
// Actual closed app sampler/native DeviceManager experiment, not production policy.
namespace source_burst_acceptance
{
using Session = te::SourceBurstSession;
class Signal final : public te::Plugin
{
  public:
    static constexpr const char *xmlTypeName = "vibera_source_burst_signal";
    int frames = 0;
    const bool silent;
    explicit Signal(te::PluginCreationInfo info)
        : Plugin(info), silent(bool(info.state.getProperty("silent", false)))
    {
    }
    ~Signal() override
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
        if (!context.destBuffer)
            return;
        for (int c = 0; c < context.destBuffer->getNumChannels(); ++c)
            for (int n = 0; n < context.bufferNumSamples; ++n)
                context.destBuffer->setSample(c, context.bufferStartSample + n,
                                              silent ? 0.f : sample(c, frames + n));
        frames += context.bufferNumSamples;
    }
};
inline void prepareClosedNativeEdit(te::Edit &edit)
{
    // Same own-meter/disabled native VCA contract used by AudioEngine::buildEdit.
    // Generic upstream metering is not claimed real-time safe by this fixture.
    edit.state.setProperty("viberaOwnMetering", true, nullptr);
    for (auto *track : te::getAudioTracks(edit))
    {
        const auto plugins = track->pluginList.getPlugins();
        for (auto *plugin : plugins)
            if (dynamic_cast<te::LevelMeterPlugin *>(plugin))
                plugin->deleteFromParent();
            else if (dynamic_cast<te::VolumeAndPanPlugin *>(plugin))
                plugin->setEnabled(false);
    }
    edit.getMasterVolumePlugin()->setEnabled(false);
}
struct Observer final : Session::Observer
{
    unsigned begins = 0, ends = 0, aborts = 0;
    bool accept = true;
    std::array<Session::Outcome, 16> completed{};
    bool begin(const Session::Outcome &) noexcept override
    {
        ++begins;
        return accept;
    }
    bool end(const Session::Outcome &o) noexcept override
    {
        if (ends >= completed.size())
            return false;
        completed[ends++] = o;
        return true;
    }
    void abort(const Session::Outcome &) noexcept override
    {
        ++aborts;
    }
};
inline Session::Ptr install(AudioEngine &audio, te::EditPlaybackContext &context,
                            const std::shared_ptr<Observer> &observer, int scale, std::uint64_t generation)
{
    auto &manager = audio.engine.getDeviceManager();
    PreparedPluginTransport::Ptr plan;
    require(
        PreparedPluginTransport::prepare(manager.deviceManager, 256, {0, 1}, scale, 768, plan).succeeded(),
        "Exact actual manager-derived source/transport plan");
    const auto &b = plan->budget();
    Session::Configuration c;
    c.deviceEpoch = b.deviceEpoch;
    c.generation = generation;
    c.sampleRate = plan->sampleRate();
    c.deviceFrames = b.configuredFrames;
    c.maximumPhysicalFrames = b.physicalFrames;
    c.maximumSourceFrames = b.sourceFrames;
    c.maximumSourceScalePpm = b.maximumSourceScalePpm;
    Session::Ptr session;
    require(Session::prepare(manager.deviceManager, c, observer, session) == Session::Status::prepared &&
                session,
            "CONTROL prepares native session from same exact plan bounds");
    require(manager.installSourceBurstSession(context, session) == Session::Status::prepared,
            "Actual native context installs prepared session");
    return session;
}
inline Session::Outcome read(const Session::Ptr &session)
{
    Session::Outcome o;
    require(session->read(o), "Coherent actual native source outcome");
    return o;
}
inline bool drive(vibera::test::routing_lifetime::Fixture &f, int frames, juce::String &csv,
                  const char *phase)
{
    juce::AudioBuffer<float> buffer(2, frames);
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
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
        f.device->process(buffer);
    }
    const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
    bool silence = true;
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < frames; ++n)
            silence &= buffer.getSample(c, n) == 0.f;
    const auto allocated = callbackAllocations.load() - allocations;
    const auto destroyed = callbackDestructions.load() - destructions;
    csv += juce::String(phase) + "," + juce::String(frames) + "," + juce::String(ms, 6) + "," +
           juce::String(allocated) + "," + juce::String(destroyed) + "," + juce::String(silence ? 1 : 0) +
           "\n";
    return silence && allocated == 0 && destroyed == 0 && ms <= 1000. * frames / 48000.;
}
__declspec(noinline) inline void tests(const juce::File &folder)
{
    using namespace vibera::test::routing_lifetime;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh native source acceptance folder");
    Storage storage(folder.getChildFile("storage"));
    juce::String csv = "phase,physicalFrames,callbackMs,new,delete,exactSilence\n";
    juce::String sources = "generation,speed,reference,physical,quota,source,offset,status,failed,scopeOpen,"
                           "expectedEnd,actualEnd\n";
    std::uint64_t generation = 0;
    for (const double speed : {0., 1., 10., -10.})
        for (const double reference : {.5, 1., 1.5})
        {
            Fixture f(folder.getChildFile("conversion-" + juce::String(++generation)));
            for (int n = 0; n < 4; ++n)
                f.silentCallback("Actual initial graph selected before bounded session");
            auto *context = f.nativeEdit().getTransport().getCurrentPlaybackContext();
            require(context && context->getNodePlayHead(), "Actual allocated source context/head");
            auto observer = std::make_shared<Observer>();
            auto session = install(*f.audio, *context, observer, 1100000, generation);
            context->setSpeedCompensation(speed);
            context->setTempoAdjustment(reference - 1.);
            const auto starts = f.device->starts, stops = f.device->stops;
            juce::int64 referenceHalfUnits =
                2 * context->getNodePlayHead()->getReferenceSampleRange().getEnd();
            for (const int frames : {127, 256, 512, 768})
            {
                const auto callbackOk = drive(f, frames, csv, "conversion");
                const auto result = read(session);
                const int whole = frames / 256, remainder = frames % 256;
                // Independent literal small fixtures, not the production quota helper.
                const int converted = whole * int(std::floor(256 * (1. + speed * .01) + .5)) +
                                      (remainder ? int(std::floor(remainder * (1. + speed * .01) + .5)) : 0);
                const int quota = whole * 282 + (remainder ? (remainder * 11 + 9) / 10 : 0);
                referenceHalfUnits += juce::int64(converted) * juce::int64(reference * 2);
                const auto expectedEnd =
                    (referenceHalfUnits + 1) / 2; // Independent exact half-unit clock, rounded once.
                const auto actualEnd = context->getNodePlayHead()->getReferenceSampleRange().getEnd();
                sources += juce::String(juce::int64(generation)) + "," + juce::String(speed) + "," +
                           juce::String(reference) + "," + juce::String(frames) + "," +
                           juce::String(result.sourceQuota) + "," + juce::String(result.sourceProcessed) +
                           "," + juce::String(result.deviceOffset) + "," + juce::String(int(result.status)) +
                           "," + juce::String(int(result.failed)) + "," +
                           juce::String(int(result.scopeOpen)) + "," + juce::String(expectedEnd) + "," +
                           juce::String(actualEnd) + "\n";
                require(folder.getChildFile("callbacks.csv").replaceWithText(csv) &&
                            folder.getChildFile("source-states.csv").replaceWithText(sources),
                        "Retain native actual and expected observations before assertion");
                require(callbackOk && !result.failed && !result.scopeOpen &&
                            result.status == Session::Status::ready && result.sourceProcessed == converted &&
                            result.sourceQuota == quota && result.deviceOffset == frames &&
                            result.physicalFrames == frames && actualEnd == expectedEnd &&
                            observer->begins == observer->ends && observer->aborts == 0 &&
                            f.device->starts == starts && f.device->stops == stops,
                        "Actual physical native splitting/latching/reference and observer completion counts");
            }
            f.audio.reset(); // Genuine CONTROL context exclusion and session retirement.
            require(read(session).retired && observer->aborts == 0,
                    "Retained native session outlives context safely");
        }
    // All-context preflight must precede even the first observer/time mutation.
    for (const bool failObserver : {false, true})
    {
        Fixture f(folder.getChildFile(failObserver ? "observer-refusal" : "peer-preflight"));
        te::Edit::Options options{f.audio->engine, te::createEmptyEdit(f.audio->engine), {}};
        options.numAudioTracks = 1;
        auto secondEdit = te::Edit::createEdit(options);
        require(bool(secondEdit), "Actual independent second native Edit/context");
        prepareClosedNativeEdit(*secondEdit);
        f.audio->engine.getPluginManager().createBuiltInType<Signal>();
        juce::ValueTree silentState(te::IDs::PLUGIN);
        silentState.setProperty(te::IDs::type, Signal::xmlTypeName, nullptr);
        silentState.setProperty("silent", true, nullptr);
        te::EditItemID::readOrCreateNewID(*secondEdit, silentState);
        auto silentSource = secondEdit->getPluginCache().getOrCreatePluginFor(silentState);
        require(dynamic_cast<Signal *>(silentSource.get()) != nullptr,
                "Actual registered and cached silent graph source");
        te::getAudioTracks(*secondEdit)[0]->pluginList.insertPlugin(silentSource, 0, nullptr);
        require(te::getAudioTracks(*secondEdit)[0]->pluginList.getPlugins().contains(silentSource.get()),
                "Inserted graph retains exact cached silent source");
        secondEdit->getTransport().ensureContextAllocated();
        auto *first = f.nativeEdit().getTransport().getCurrentPlaybackContext();
        auto *second = secondEdit->getTransport().getCurrentPlaybackContext();
        require(first && second && first != second, "Two actual contexts share exact native DeviceManager");
        for (int n = 0; n < 4; ++n)
        {
            const auto prepared = drive(f, 256, csv, "two-context-pre-admission");
            require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                    "Retain pre-admission callback diagnostics before original oracle");
            require(prepared, "Both actual graphs prepared before negative admission");
        }
        auto a = std::make_shared<Observer>(), b = std::make_shared<Observer>();
        auto sa = install(*f.audio, *first, a, 1000000, ++generation);
        auto sb = install(*f.audio, *second, b, 1000000, ++generation);
        if (failObserver)
            b->accept = false;
        else
            second->setSpeedCompensation(10.); // Deliberate unsupported conversion, not malformed config.
        const auto sourceFrames = static_cast<Signal *>(silentSource.get())->frames;
        const auto firstClock = first->getNodePlayHead()->getReferenceSampleRange();
        const auto secondClock = second->getNodePlayHead()->getReferenceSampleRange();
        const auto callbackOk = drive(f, 256, csv, failObserver ? "observer-refusal" : "peer-preflight");
        require(folder.getChildFile("callbacks.csv").replaceWithText(csv), "Retain peer failure evidence");
        require(callbackOk && static_cast<Signal *>(silentSource.get())->frames == sourceFrames &&
                    first->getNodePlayHead()->getReferenceSampleRange() == firstClock &&
                    second->getNodePlayHead()->getReferenceSampleRange() == secondClock && a->ends == 0 &&
                    b->ends == 0 && read(sa).failed && read(sb).failed && !read(sa).scopeOpen &&
                    !read(sb).scopeOpen,
                "Refused peer prevents ALL native reference work and closes every scope");
        require(failObserver ? (a->begins == 1 && b->begins == 1 && a->aborts == 1 && b->aborts == 1)
                             : (a->begins == 0 && b->begins == 0 && a->aborts == 0 && b->aborts == 0),
                "Two-phase native admission distinguishes preflight failure from partial observer begin");
        require(folder.getChildFile("callbacks.csv").replaceWithText(csv), "Retain peer failure evidence");
        silentSource = nullptr;
        secondEdit.reset();
        require(read(sb).retired, "Second context retirement is CONTROL-owned after failed callback");
    }
}
} // namespace source_burst_acceptance
