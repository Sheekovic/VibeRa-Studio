// SPDX-License-Identifier: AGPL-3.0-or-later
// Original release-velocity VST3 fixture, derived from our InstrumentFixture.
// A real NoteOffEvent on a held key adds one frame L=velocity/4,R=-velocity/8.
// No third-party implementation/assets/UI; the legacy fixture remains separate.
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
DECLARE_UID(classID, 0x56494245, 0x52415245, 0x4C454153, 0x45303031)
constexpr ParamID gainId = 77, onId = 90, offId = 91, panicId = 95;
void text(String128 out, const char *in)
{
    std::fill_n(out, 128, TChar{});
    for (int i = 0; in[i] && i < 127; ++i)
        out[i] = TChar(in[i]);
}
#define REFS()                                                                                               \
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
#define QUERY(T)                                                                                             \
    if (!std::memcmp(iid, T##_iid, 16))                                                                      \
    {                                                                                                        \
        *out = static_cast<T *>(this);                                                                       \
        addRef();                                                                                            \
        return kResultOk;                                                                                    \
    }
class Instrument final : public IComponent,
                         public IAudioProcessor,
                         public IEditController,
                         public IMidiMapping
{
    std::array<float, 16 * 128> keys{};
    std::atomic<double> controllerGain{.5};
    std::atomic<unsigned> observedOns{0}, observedOffs{0};
    double gain = .5, left = 0, right = 0;
    unsigned ons = 0, offs = 0, heldKeys = 0;
    void clear() noexcept
    {
        keys.fill(0);
        left = right = 0;
        heldKeys = 0;
    }
    void note(int channel, int pitch, float velocity, bool on) noexcept
    {
        if (channel < 0 || channel >= 16 || pitch < 0 || pitch >= 128)
            return;
        auto &key = keys[std::size_t(channel * 128 + pitch)];
        const double before = key;
        key = on ? std::clamp(velocity, 0.f, 1.f) : 0.f;
        if (before == 0 && key > 0)
            ++heldKeys;
        if (before > 0 && key == 0)
            --heldKeys;
        left += (key - before) * (pitch + 1) / 512.;
        right += (key - before) * (128 - pitch) / 512.;
        if (heldKeys == 0)
            left = right = 0;
        if (on)
            observedOns.store(++ons, std::memory_order_relaxed);
        else
            observedOffs.store(++offs, std::memory_order_relaxed);
    }

  public:
    REFS();
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        QUERY(IComponent)
        QUERY(IAudioProcessor)
        QUERY(IEditController)
        QUERY(IMidiMapping) if (!std::memcmp(iid, FUnknown_iid, 16) || !std::memcmp(iid, IPluginBase_iid, 16))
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
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo &info) override
    {
        if (index != 0 || getBusCount(type, dir) == 0)
            return kInvalidArgument;
        info = {};
        info.mediaType = type;
        info.direction = dir;
        info.channelCount = type == kAudio ? 2 : 16;
        info.busType = kMain;
        info.flags = BusInfo::kDefaultActive;
        text(info.name, type == kAudio ? "Diagnostic stereo" : "MIDI input");
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool active) override
    {
        if (active)
            clear();
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        double value = 0;
        int32 bytes = 0;
        if (!stream || stream->read(&value, sizeof(value), &bytes) != kResultOk || bytes != sizeof(value) ||
            !std::isfinite(value) || value < 0 || value > 1)
            return kResultFalse;
        gain = value;
        controllerGain.store(value);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        int32 bytes = 0;
        return stream ? stream->write(&gain, sizeof(gain), &bytes) : kResultFalse;
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
    tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 bus, SpeakerArrangement &out) override
    {
        if (direction != kOutput || bus != 0)
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
        return 0;
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return 0;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup &) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData &data) override
    {
        IParamValueQueue *gainQueue = nullptr, *panicQueue = nullptr;
        if (auto *changes = data.inputParameterChanges)
            for (int q = 0; q < changes->getParameterCount(); ++q)
                if (auto *queue = changes->getParameterData(q))
                {
                    if (queue->getParameterId() == gainId)
                        gainQueue = queue;
                    if (queue->getParameterId() == panicId)
                        panicQueue = queue;
                }
        int nextGain = 0, nextPanic = 0, nextEvent = 0;
        const int totalEvents = data.inputEvents ? data.inputEvents->getEventCount() : 0;
        // The supported parameter-only flush is control work, not audio/time.
        if (data.numSamples == 0)
        {
            int32 offset = 0;
            ParamValue value = 0;
            if (gainQueue && gainQueue->getPointCount() > 0 &&
                gainQueue->getPoint(gainQueue->getPointCount() - 1, offset, value) == kResultOk)
                gain = value;
            if (panicQueue && panicQueue->getPointCount() > 0)
                clear();
            return kResultOk;
        }
        if (data.numOutputs != 1 || data.outputs[0].numChannels != 2)
            return kResultFalse;
        for (int n = 0; n < data.numSamples; ++n)
        {
            double releaseImpulse = 0;
            const auto points = [&](IParamValueQueue *queue, int &next, bool panic)
            {
                while (queue && next < queue->getPointCount())
                {
                    int32 offset = 0;
                    ParamValue value = 0;
                    if (queue->getPoint(next, offset, value) != kResultOk || offset > n)
                        break;
                    if (panic)
                        clear();
                    else
                        gain = value;
                    ++next;
                }
            };
            points(gainQueue, nextGain, false);
            points(panicQueue, nextPanic, true);
            while (nextEvent < totalEvents)
            {
                Event event{};
                if (data.inputEvents->getEvent(nextEvent, event) != kResultOk)
                    return kResultFalse;
                if (event.sampleOffset > n)
                    break;
                if (event.type == Event::kNoteOnEvent)
                    note(event.noteOn.channel, event.noteOn.pitch, event.noteOn.velocity,
                         event.noteOn.velocity > 0);
                if (event.type == Event::kNoteOffEvent)
                {
                    const auto channel = event.noteOff.channel, pitch = event.noteOff.pitch;
                    if (channel >= 0 && channel < 16 && pitch >= 0 && pitch < 128 &&
                        keys[std::size_t(channel * 128 + pitch)] > 0 && std::isfinite(event.noteOff.velocity))
                        releaseImpulse += std::clamp(event.noteOff.velocity, 0.f, 1.f);
                    note(event.noteOff.channel, event.noteOff.pitch, 0, false);
                }
                ++nextEvent;
            }
            const bool sounding = heldKeys != 0;
            // Retrigger stamp makes duplicate native note-ons observable while
            // a key stays held. This is diagnostic PCM, not a synth promise.
            data.outputs[0].channelBuffers32[0][n] =
                float((sounding ? gain * (left + ons / 4096.) : 0.) + releaseImpulse / 4.);
            data.outputs[0].channelBuffers32[1][n] =
                float((sounding ? -gain * (right + ons / 8192.) : 0.) - releaseImpulse / 8.);
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return 4;
    }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo &info) override
    {
        if (index < 0 || index > 3)
            return kInvalidArgument;
        info = {};
        info.id = std::array<ParamID, 4>{gainId, onId, offId, panicId}[std::size_t(index)];
        info.defaultNormalizedValue = index == 0 ? .5 : 0;
        info.unitId = 0;
        info.flags = index == 0   ? ParameterInfo::kCanAutomate
                     : index == 3 ? ParameterInfo::kIsHidden
                                  : ParameterInfo::kIsReadOnly;
        text(info.title, index == 0   ? "Diagnostic gain"
                         : index == 1 ? "Note-on count /1024"
                         : index == 2 ? "Note-off count /1024"
                                      : "Panic mapping");
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue value, String128 out) override
    {
        char s[48]{};
        std::snprintf(s, sizeof(s), "%.6f", value);
        text(out, s);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override
    {
        return kNotImplemented;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue value) override
    {
        return value;
    }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue value) override
    {
        return value;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override
    {
        return id == gainId  ? controllerGain.load()
               : id == onId  ? std::min(1., observedOns.load() / 1024.)
               : id == offId ? std::min(1., observedOffs.load() / 1024.)
                             : 0;
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
    {
        if (id != gainId && id != panicId)
            return kResultFalse;
        if (!std::isfinite(value) || value < 0 || value > 1)
            return kInvalidArgument;
        if (id == gainId)
            controllerGain.store(value);
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *) override
    {
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
        id = panicId;
        return kResultOk;
    }
};
class Factory final : public IPluginFactory2
{
  public:
    REFS();
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        QUERY(IPluginFactory2) QUERY(IPluginFactory) if (!std::memcmp(iid, FUnknown_iid, 16))
        {
            *out = static_cast<IPluginFactory2 *>(this);
            addRef();
            return kResultOk;
        }
        return kNoInterface;
    }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override
    {
        if (!info)
            return kInvalidArgument;
        *info = PFactoryInfo("VibeRa original diagnostic", "", "", 0);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override
    {
        return 1;
    }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo *info) override
    {
        if (index != 0 || !info)
            return kInvalidArgument;
        *info = PClassInfo(classID, PClassInfo::kManyInstances, kVstAudioEffectClass,
                           "VibeRa Note Release Fixture");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2 *info) override
    {
        if (index != 0 || !info)
            return kInvalidArgument;
        *info = PClassInfo2(classID, PClassInfo::kManyInstances, kVstAudioEffectClass,
                            "VibeRa Note Release Fixture", 0, "Instrument|Synth",
                            "VibeRa original diagnostic", "1.0.0", "VST 3");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (std::memcmp(cid, classID, 16))
            return kResultFalse;
        auto *p = new Instrument;
        const auto result = p->queryInterface(iid, out);
        p->release();
        return result;
    }
};
} // namespace
extern "C" __declspec(dllexport) IPluginFactory *PLUGIN_API GetPluginFactory()
{
    return new Factory;
}
extern "C" __declspec(dllexport) bool InitDll()
{
    return true;
}
extern "C" __declspec(dllexport) bool ExitDll()
{
    return true;
}
