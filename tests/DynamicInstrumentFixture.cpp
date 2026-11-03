// SPDX-License-Identifier: AGPL-3.0-or-later
// Original SDK instrument: deterministic MIDI synthesis with a mutable native
// delay. No third-party code/assets, JUCE wrapper, editor or DSP host callbacks.
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
DECLARE_UID(dynamicInstrumentID, 0x56494245, 0x52414459, 0x4E494E53, 0x54523031)
constexpr ParamID latencyID = 17, gainID = 77, onID = 90, offID = 91, heldID = 92, panicID = 95;
constexpr std::array<unsigned, 4> latencyModes{0, 31, 257, 1024};
constexpr unsigned ringSize = 4096;
static_assert(std::atomic<unsigned>::is_always_lock_free && std::atomic<double>::is_always_lock_free);
void string128(String128 out, const char *in)
{
    std::fill_n(out, 128, TChar{});
    for (int n = 0; n < 127 && in[n]; ++n)
        out[n] = TChar(in[n]);
}
unsigned modeFrom(double value)
{
    return unsigned(std::lround(std::clamp(value, 0., 1.) * 3));
}
bool knownParam(ParamID id)
{
    return id == latencyID || id == gainID || id == onID || id == offID || id == heldID || id == panicID;
}
#define DYNAMIC_REFS()                                                                                       \
    uint32 PLUGIN_API addRef() override                                                                      \
    {                                                                                                        \
        return ++references;                                                                                 \
    }                                                                                                        \
    uint32 PLUGIN_API release() override                                                                     \
    {                                                                                                        \
        auto n = --references;                                                                               \
        if (!n)                                                                                              \
            delete this;                                                                                     \
        return n;                                                                                            \
    }                                                                                                        \
    std::atomic<uint32> references{1}
#define DYNAMIC_QUERY(T)                                                                                     \
    if (!std::memcmp(iid, T##_iid, 16))                                                                      \
    {                                                                                                        \
        *out = static_cast<T *>(this);                                                                       \
        addRef();                                                                                            \
        return kResultOk;                                                                                    \
    }

class DynamicInstrument final : public IComponent,
                                public IAudioProcessor,
                                public IEditController,
                                public IMidiMapping
{
    std::atomic<unsigned> mode{0}, observedOns{0}, observedOffs{0}, observedHeld{0};
    std::atomic<double> controllerGain{.5};
    IComponentHandler *handler = nullptr; // CONTROL-only registration/notification.
    std::array<float, 16 * 128> keys{};
    std::array<std::array<float, ringSize>, 2> history{};
    unsigned cursor = 0, ons = 0, offs = 0, held = 0;
    std::uint64_t sourceFrame = 0; // DSP-owned; never reset by native mode/state/control flush.
    double leftWeight = 0, rightWeight = 0, dspGain = .5;
    void clearKeys() noexcept
    {
        keys.fill(0);
        leftWeight = rightWeight = 0;
        held = 0;
        observedHeld.store(0, std::memory_order_relaxed);
    }
    void key(int channel, int pitch, float velocity, bool pressed) noexcept
    {
        if (channel < 0 || channel >= 16 || pitch < 0 || pitch >= 128 || !std::isfinite(velocity))
            return;
        auto &value = keys[std::size_t(channel * 128 + pitch)];
        const double before = value;
        value = pressed ? std::clamp(velocity, 0.f, 1.f) : 0.f;
        held += unsigned(before == 0 && value > 0);
        held -= unsigned(before > 0 && value == 0);
        leftWeight += (double(value) - before) * (pitch + 1);
        rightWeight += (double(value) - before) * (128 - pitch);
        if (held == 0)
            leftWeight = rightWeight = 0;
        if (pressed)
            observedOns.store(++ons, std::memory_order_relaxed);
        else
            observedOffs.store(++offs, std::memory_order_relaxed);
        observedHeld.store(held, std::memory_order_relaxed);
    }
    void changeMode(unsigned value)
    {
        const auto prior = mode.exchange(value, std::memory_order_acq_rel);
        if (prior != value && handler)
            handler->restartComponent(kLatencyChanged);
    }
    // Fixed24-byte little-endian revision1 state, independent of compiler padding.
    // MIDI voices, counters, source time and queued PCM are deliberately transient.
    static void put(std::uint8_t *out, std::uint64_t value, unsigned count)
    {
        for (unsigned n = 0; n < count; ++n)
            out[n] = std::uint8_t(value >> (8 * n));
    }
    static std::uint64_t get(const std::uint8_t *in, unsigned count)
    {
        std::uint64_t value = 0;
        for (unsigned n = 0; n < count; ++n)
            value |= std::uint64_t(in[n]) << (8 * n);
        return value;
    }

  public:
    DYNAMIC_REFS();
    ~DynamicInstrument()
    {
        if (handler)
            handler->release();
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        DYNAMIC_QUERY(IComponent)
        DYNAMIC_QUERY(IAudioProcessor) DYNAMIC_QUERY(IEditController) DYNAMIC_QUERY(
            IMidiMapping) if (!std::memcmp(iid, FUnknown_iid, 16) || !std::memcmp(iid, IPluginBase_iid, 16))
        {
            *out = static_cast<IComponent *>(this);
            addRef();
            return kResultOk;
        }
        return kNoInterface;
    }
    tresult PLUGIN_API initialize(FUnknown *) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override
    {
        return kResultOk;
    }
    tresult PLUGIN_API getControllerClassId(TUID) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API setIoMode(IoMode) override
    {
        return kResultOk;
    }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override
    {
        return (type == kAudio && dir == kOutput) || (type == kEvent && dir == kInput) ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo &out) override
    {
        if (index != 0 || getBusCount(type, dir) == 0)
            return kInvalidArgument;
        out = {};
        out.mediaType = type;
        out.direction = dir;
        out.channelCount = type == kAudio ? 2 : 16;
        out.busType = kMain;
        out.flags = BusInfo::kDefaultActive;
        string128(out.name, type == kAudio ? "Dynamic instrument stereo" : "Owned MIDI input");
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool) override
    {
        return index == 0 && getBusCount(type, dir) == 1 ? kResultOk : kInvalidArgument;
    }
    tresult PLUGIN_API setActive(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        std::array<std::uint8_t, 24> state{};
        int32 read = 0;
        if (!stream || stream->read(state.data(), int32(state.size()), &read) != kResultOk ||
            read != int32(state.size()) || get(state.data(), 4) != 0x314e5944 ||
            get(state.data() + 4, 4) != 1 || get(state.data() + 8, 4) > 3 || get(state.data() + 12, 4) != 0)
            return kResultFalse;
        const double gain = std::bit_cast<double>(get(state.data() + 16, 8));
        if (!std::isfinite(gain) || gain < 0 || gain > 1)
            return kResultFalse;
        // Validate all bytes first. No partial state on an invalid revision/value.
        controllerGain.store(gain, std::memory_order_release);
        dspGain = gain; // Host state capture/restore is callback-excluded.
        changeMode(unsigned(get(state.data() + 8, 4)));
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        std::array<std::uint8_t, 24> state{};
        put(state.data(), 0x314e5944, 4);
        put(state.data() + 4, 1, 4);
        put(state.data() + 8, mode.load(std::memory_order_acquire), 4);
        put(state.data() + 16, std::bit_cast<std::uint64_t>(controllerGain.load(std::memory_order_acquire)),
            8);
        int32 written = 0;
        return stream && stream->write(state.data(), int32(state.size()), &written) == kResultOk &&
                       written == int32(state.size())
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override
    {
        return setState(stream);
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *, int32 inputs, SpeakerArrangement *out,
                                          int32 outputs) override
    {
        return inputs == 0 && outputs == 1 && out && out[0] == SpeakerArr::kStereo ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 bus, SpeakerArrangement &out) override
    {
        if (dir != kOutput || bus != 0)
            return kInvalidArgument;
        out = SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultOk : kResultFalse;
    }
    uint32 PLUGIN_API getLatencySamples() override
    {
        return latencyModes[mode.load(std::memory_order_acquire)];
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return getLatencySamples();
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) override
    {
        return setup.symbolicSampleSize == kSample32 && setup.maxSamplesPerBlock > 0 ? kResultOk
                                                                                     : kResultFalse;
    }
    tresult PLUGIN_API setProcessing(TBool active) override
    {
        if (active)
        {
            clearKeys();
            for (auto &channel : history)
                channel.fill(0);
            cursor = ons = offs = 0;
            sourceFrame = 0;
            observedOns.store(0);
            observedOffs.store(0);
            dspGain = controllerGain.load(std::memory_order_acquire);
        }
        return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData &data) override
    {
        IParamValueQueue *gainQueue = nullptr, *panicQueue = nullptr;
        if (auto *changes = data.inputParameterChanges)
            for (int n = 0; n < changes->getParameterCount(); ++n)
                if (auto *queue = changes->getParameterData(n))
                {
                    if (queue->getParameterId() == gainID)
                        gainQueue = queue;
                    if (queue->getParameterId() == panicID)
                        panicQueue = queue;
                }
        // Parameter-only control flush changes no sample clock/history/key ownership.
        // Panic is a real MIDI ownership operation, not a latency-mode side effect.
        if (data.numSamples == 0)
        {
            int32 at = 0;
            ParamValue value = 0;
            if (gainQueue && gainQueue->getPointCount() &&
                gainQueue->getPoint(gainQueue->getPointCount() - 1, at, value) == kResultOk &&
                std::isfinite(value) && value >= 0 && value <= 1)
            {
                dspGain = value;
                controllerGain.store(value, std::memory_order_release);
            }
            if (panicQueue && panicQueue->getPointCount())
                clearKeys();
            return kResultOk;
        }
        if (data.numSamples < 0 || data.symbolicSampleSize != kSample32 || data.numOutputs != 1 ||
            !data.outputs || data.outputs[0].numChannels != 2 || !data.outputs[0].channelBuffers32 ||
            !data.outputs[0].channelBuffers32[0] || !data.outputs[0].channelBuffers32[1])
            return kResultFalse;
        const auto delay = latencyModes[mode.load(std::memory_order_acquire)];
        int gainPoint = 0, panicPoint = 0, eventIndex = 0;
        const int events = data.inputEvents ? data.inputEvents->getEventCount() : 0;
        for (int n = 0; n < data.numSamples; ++n)
        {
            const auto apply = [&](IParamValueQueue *queue, int &next, bool panic)
            {
                while (queue && next < queue->getPointCount())
                {
                    int32 at = 0;
                    ParamValue value = 0;
                    if (queue->getPoint(next, at, value) != kResultOk)
                        return false;
                    if (at > n)
                        break;
                    if (!std::isfinite(value) || value < 0 || value > 1)
                        return false;
                    if (panic)
                        clearKeys();
                    else
                    {
                        dspGain = value;
                        controllerGain.store(value, std::memory_order_relaxed);
                    }
                    ++next;
                }
                return true;
            };
            if (!apply(gainQueue, gainPoint, false) || !apply(panicQueue, panicPoint, true))
                return kResultFalse;
            while (eventIndex < events)
            {
                Event event{};
                if (data.inputEvents->getEvent(eventIndex, event) != kResultOk)
                    return kResultFalse;
                if (event.sampleOffset > n)
                    break;
                if (event.type == Event::kNoteOnEvent)
                    key(event.noteOn.channel, event.noteOn.pitch, event.noteOn.velocity,
                        event.noteOn.velocity > 0);
                else if (event.type == Event::kNoteOffEvent)
                    key(event.noteOff.channel, event.noteOff.pitch, 0, false);
                ++eventIndex;
            }
            const int phaseL = int((sourceFrame % 251 * 17 + 5) % 251) - 125;
            const int phaseR = int((sourceFrame % 241 * 23 + 11) % 241) - 120;
            history[0][cursor] = float(dspGain * (leftWeight / 512.) * (phaseL / 128.));
            history[1][cursor] = float(-dspGain * (rightWeight / 512.) * (phaseR / 128.));
            const auto tap = (cursor + ringSize - delay) % ringSize;
            data.outputs[0].channelBuffers32[0][n] = history[0][tap];
            data.outputs[0].channelBuffers32[1][n] = history[1][tap];
            cursor = (cursor + 1) % ringSize;
            ++sourceFrame;
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return 6;
    }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo &out) override
    {
        if (index < 0 || index >= 6)
            return kInvalidArgument;
        constexpr std::array<ParamID, 6> ids{latencyID, gainID, onID, offID, heldID, panicID};
        constexpr std::array<const char *, 6> names{"Native latency mode",    "Synthesis gain",
                                                    "Native note-ons /1024",  "Native note-offs /1024",
                                                    "Native held keys /1024", "MIDI panic"};
        out = {};
        out.id = ids[std::size_t(index)];
        out.unitId = 0;
        out.stepCount = index == 0 ? 3 : 0;
        out.defaultNormalizedValue = index == 1 ? .5 : 0;
        out.flags = index == 1                 ? ParameterInfo::kCanAutomate
                    : index >= 2 && index <= 4 ? ParameterInfo::kIsReadOnly
                    : index == 5               ? ParameterInfo::kIsHidden
                                               : 0;
        string128(out.title, names[std::size_t(index)]);
        string128(out.units, index == 0 ? "samples" : "linear");
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 out) override
    {
        if (!knownParam(id) || !std::isfinite(value) || value < 0 || value > 1)
            return kInvalidArgument;
        char text[48]{};
        if (id == latencyID)
            std::snprintf(text, sizeof(text), "%u", latencyModes[modeFrom(value)]);
        else
            std::snprintf(text, sizeof(text), "%.8f", value);
        string128(out, text);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override
    {
        return kNotImplemented;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue value) override
    {
        return id == latencyID && std::isfinite(value) ? latencyModes[modeFrom(value)] : value;
    }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue value) override
    {
        if (id != latencyID || !std::isfinite(value))
            return value;
        unsigned nearest = 0;
        for (unsigned n = 1; n < latencyModes.size(); ++n)
            if (std::abs(value - latencyModes[n]) < std::abs(value - latencyModes[nearest]))
                nearest = n;
        return nearest / 3.;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override
    {
        if (id == latencyID)
            return mode.load(std::memory_order_acquire) / 3.;
        if (id == gainID)
            return controllerGain.load(std::memory_order_acquire);
        if (id == onID)
            return std::min(1., observedOns.load(std::memory_order_relaxed) / 1024.);
        if (id == offID)
            return std::min(1., observedOffs.load(std::memory_order_relaxed) / 1024.);
        if (id == heldID)
            return std::min(1., observedHeld.load(std::memory_order_relaxed) / 1024.);
        return 0;
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
    {
        if (!std::isfinite(value) || value < 0 || value > 1)
            return kInvalidArgument;
        if (id == latencyID)
        {
            changeMode(modeFrom(value));
            return kResultOk;
        }
        if (id == gainID)
        {
            controllerGain.store(value, std::memory_order_release);
            return kResultOk;
        }
        if (id == panicID)
            return kResultOk; // Actual ownership mutation arrives in process parameter queue.
        return kResultFalse;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *next) override
    {
        if (next)
            next->addRef();
        if (handler)
            handler->release();
        handler = next;
        return kResultOk;
    }
    IPlugView *PLUGIN_API createView(FIDString) override
    {
        return nullptr;
    }
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber number,
                                                   ParamID &id) override
    {
        if (bus != 0 || channel < 0 || channel >= 16 || (number != 120 && number != 123))
            return kResultFalse;
        id = panicID;
        return kResultOk;
    }
};
class DynamicFactory final : public IPluginFactory2
{
  public:
    DYNAMIC_REFS();
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        DYNAMIC_QUERY(IPluginFactory2) DYNAMIC_QUERY(IPluginFactory) if (!std::memcmp(iid, FUnknown_iid, 16))
        {
            *out = static_cast<IPluginFactory2 *>(this);
            addRef();
            return kResultOk;
        }
        return kNoInterface;
    }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = PFactoryInfo("VibeRa original diagnostic", "", "", 0);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override
    {
        return 1;
    }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo *out) override
    {
        if (index != 0 || !out)
            return kInvalidArgument;
        *out = PClassInfo(dynamicInstrumentID, PClassInfo::kManyInstances, kVstAudioEffectClass,
                          "VibeRa Dynamic Instrument Fixture");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2 *out) override
    {
        if (index != 0 || !out)
            return kInvalidArgument;
        *out = PClassInfo2(dynamicInstrumentID, PClassInfo::kManyInstances, kVstAudioEffectClass,
                           "VibeRa Dynamic Instrument Fixture", 0, "Instrument|Synth",
                           "VibeRa original diagnostic", "1.0.0", "VST 3");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (std::memcmp(cid, dynamicInstrumentID, 16))
            return kResultFalse;
        auto *instrument = new DynamicInstrument;
        const auto result = instrument->queryInterface(iid, out);
        instrument->release();
        return result;
    }
};
} // namespace
extern "C" __declspec(dllexport) IPluginFactory *PLUGIN_API GetPluginFactory()
{
    return new DynamicFactory;
}
extern "C" __declspec(dllexport) bool InitDll()
{
    return true;
}
extern "C" __declspec(dllexport) bool ExitDll()
{
    return true;
}
