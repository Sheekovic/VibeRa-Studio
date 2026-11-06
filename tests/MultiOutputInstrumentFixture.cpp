// SPDX-License-Identifier: AGPL-3.0-or-later
// Original SDK multi-output instrument fixture: four stereo output buses, each writing a
// constant level of (bus + 1) * 0.1 so a render shows which Mixer track received which bus.
// No dynamic allocation or locks in process().
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
DECLARE_UID(classID, 0x56494245, 0x52414D55, 0x4C54494F, 0x55543031)
constexpr int buses = 4;
void text(String128 destination, const char *source)
{
    std::fill_n(destination, 128, TChar{});
    for (int i = 0; source[i] && i < 127; ++i)
        destination[i] = TChar(source[i]);
}
#define REFS()                                                                                               \
    uint32 PLUGIN_API addRef() override                                                                      \
    {                                                                                                        \
        return ++references;                                                                                 \
    }                                                                                                        \
    uint32 PLUGIN_API release() override                                                                     \
    {                                                                                                        \
        const auto count = --references;                                                                     \
        if (!count)                                                                                          \
            delete this;                                                                                     \
        return count;                                                                                        \
    }                                                                                                        \
    std::atomic<uint32> references{1}
#define QUERY(Type)                                                                                          \
    if (std::memcmp(iid, Type##_iid, 16) == 0)                                                               \
    {                                                                                                        \
        *out = static_cast<Type *>(this);                                                                    \
        addRef();                                                                                            \
        return kResultOk;                                                                                    \
    }
class Fixture final : public IComponent, public IAudioProcessor, public IEditController
{
    std::array<bool, buses> outputActive{true, false, false, false};

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
        if (!std::memcmp(iid, FUnknown_iid, 16) || !std::memcmp(iid, IPluginBase_iid, 16))
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
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection direction) override
    {
        if (type == kAudio)
            return direction == kOutput ? buses : 0;
        return direction == kInput ? 1 : 0; // One MIDI event input.
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo &out) override
    {
        out = {};
        out.mediaType = type;
        out.direction = direction;
        if (type == kEvent && direction == kInput && index == 0)
        {
            out.channelCount = 16;
            out.busType = kMain;
            out.flags = BusInfo::kDefaultActive;
            text(out.name, "MIDI in");
            return kResultOk;
        }
        if (type != kAudio || direction != kOutput || index < 0 || index >= buses)
            return kInvalidArgument;
        out.channelCount = 2;
        out.busType = index == 0 ? kMain : kAux;
        out.flags = index == 0 ? BusInfo::kDefaultActive : 0;
        static const char *names[]{"Main out", "Out 2", "Out 3", "Out 4"};
        text(out.name, names[index]);
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection direction, int32 index, TBool enabled) override
    {
        if (type == kEvent)
            return direction == kInput && index == 0 ? kResultOk : kInvalidArgument;
        if (type != kAudio || direction != kOutput || index < 0 || index >= buses)
            return kInvalidArgument;
        outputActive[size_t(index)] = enabled != 0;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        const std::array<uint32, 2> state{0x4D4F5554, 1};
        int32 bytes = 0;
        return stream && stream->write(const_cast<uint32 *>(state.data()), sizeof(state), &bytes) == kResultOk &&
                       bytes == sizeof(state)
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        std::array<uint32, 2> state{};
        int32 bytes = 0;
        return stream && stream->read(state.data(), sizeof(state), &bytes) == kResultOk &&
                       bytes == sizeof(state) && state[0] == 0x4D4F5554 && state[1] == 1
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override
    {
        return setState(stream);
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *, int32 inCount, SpeakerArrangement *out,
                                          int32 outCount) override
    {
        if (inCount != 0 || !out || outCount < 1 || outCount > buses)
            return kResultFalse;
        for (int i = 0; i < outCount; ++i)
            if (out[i] != SpeakerArr::kStereo && out[i] != SpeakerArr::kEmpty)
                return kResultFalse;
        return kResultOk;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 index, SpeakerArrangement &out) override
    {
        if (direction != kOutput || index < 0 || index >= buses)
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
    tresult PLUGIN_API setupProcessing(ProcessSetup &) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override
    {
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return 0;
    }
    tresult PLUGIN_API process(ProcessData &data) override
    {
        if (data.numSamples <= 0)
            return kResultOk;
        if (data.symbolicSampleSize != kSample32 || data.numOutputs < 0 || data.numOutputs > buses)
            return kResultFalse;
        for (int b = 0; b < data.numOutputs; ++b)
        {
            // Only active buses own delivered buffers; a disabled bus keeps its old arrangement.
            auto &bus = data.outputs[b];
            if (!outputActive[size_t(b)] || !bus.channelBuffers32)
                continue;
            for (int c = 0; c < bus.numChannels; ++c)
                if (bus.channelBuffers32[c])
                    std::fill_n(bus.channelBuffers32[c], data.numSamples, .1f * float(b + 1));
            bus.silenceFlags = 0;
        }
        return kResultOk;
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return 0;
    }
    tresult PLUGIN_API getParameterInfo(int32, ParameterInfo &) override
    {
        return kInvalidArgument;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override
    {
        return kNotImplemented;
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
    ParamValue PLUGIN_API getParamNormalized(ParamID) override
    {
        return 0;
    }
    tresult PLUGIN_API setParamNormalized(ParamID, ParamValue) override
    {
        return kInvalidArgument;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *) override
    {
        return kResultOk;
    }
    IPlugView *PLUGIN_API createView(FIDString) override
    {
        return nullptr;
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
        QUERY(IPluginFactory2)
        QUERY(IPluginFactory)
        if (!std::memcmp(iid, FUnknown_iid, 16))
        {
            *out = static_cast<IPluginFactory *>(this);
            addRef();
            return kResultOk;
        }
        return kNoInterface;
    }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override
    {
        if (!info)
            return kInvalidArgument;
        *info = PFactoryInfo("VibeRa original multi-output fixture", "", "", 0);
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
                           "VibeRa Multi-output Instrument Fixture");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2 *info) override
    {
        if (index != 0 || !info)
            return kInvalidArgument;
        *info = PClassInfo2(classID, PClassInfo::kManyInstances, kVstAudioEffectClass,
                            "VibeRa Multi-output Instrument Fixture", 0, "Instrument|Synth", "VibeRa", "1.0.0",
                            "VST 3.7");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (std::memcmp(cid, classID, 16))
            return kResultFalse;
        auto *instance = new Fixture;
        const auto result = instance->queryInterface(iid, out);
        instance->release();
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
