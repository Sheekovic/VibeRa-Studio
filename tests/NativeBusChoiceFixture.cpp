// SPDX-License-Identifier: AGPL-3.0-or-later
// Original SDK bus-choice effect. Its independent weighted PCM identifies native buses.
// No JUCE wrapper, worker scheduling, dynamic allocation or locks in process().
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
DECLARE_UID(classID, 0x56494245, 0x52414255, 0x5343484F, 0x49434531)
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
    std::array<SpeakerArrangement, 3> inputs{SpeakerArr::kStereo, SpeakerArr::kStereo, SpeakerArr::kStereo};
    std::array<SpeakerArrangement, 3> outputs{SpeakerArr::kStereo, SpeakerArr::kStereo, SpeakerArr::k51};
    std::array<bool, 3> inputActive{true, false, false}, outputActive{true, false, false};

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
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override
    {
        return type == kAudio ? 3 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo &out) override
    {
        if (type != kAudio || index < 0 || index >= 3)
            return kInvalidArgument;
        out = {};
        out.mediaType = type;
        out.direction = direction;
        out.channelCount =
            SpeakerArr::getChannelCount((direction == kInput ? inputs : outputs)[size_t(index)]);
        out.busType = index == 0 ? kMain : kAux;
        out.flags = index == 0 ? BusInfo::kDefaultActive : 0;
        text(out.name, direction == kInput ? (index == 0   ? "Input zero"
                                              : index == 1 ? "Input one"
                                                           : "Input two")
                                           : (index == 0   ? "Output zero"
                                              : index == 1 ? "Output one"
                                                           : "Unsupported surround output"));
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection direction, int32 index,
                                   TBool enabled) override
    {
        if (type != kAudio || index < 0 || index >= 3)
            return kInvalidArgument;
        (direction == kInput ? inputActive : outputActive)[size_t(index)] = enabled != 0;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        const std::array<uint32, 2> state{0x42555331, 1};
        int32 bytes = 0;
        return stream &&
                       stream->write(const_cast<uint32 *>(state.data()), sizeof(state), &bytes) ==
                           kResultOk &&
                       bytes == sizeof(state)
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        std::array<uint32, 2> state{};
        int32 bytes = 0;
        return stream && stream->read(state.data(), sizeof(state), &bytes) == kResultOk &&
                       bytes == sizeof(state) && state[0] == 0x42555331 && state[1] == 1
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override
    {
        return setState(stream);
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *in, int32 inCount, SpeakerArrangement *out,
                                          int32 outCount) override
    {
        if (!in || !out || inCount != 3 || outCount != 3)
            return kResultFalse;
        for (int i = 0; i < 3; ++i)
            if ((in[i] != SpeakerArr::kEmpty && in[i] != SpeakerArr::kMono && in[i] != SpeakerArr::kStereo) ||
                (out[i] != SpeakerArr::kEmpty && out[i] != (i == 2 ? SpeakerArr::k51 : SpeakerArr::kStereo)))
                return kResultFalse;
        for (int i = 0; i < 3; ++i)
        {
            if (in[i] != SpeakerArr::kEmpty)
                inputs[size_t(i)] = in[i];
            if (out[i] != SpeakerArr::kEmpty)
                outputs[size_t(i)] = out[i];
        }
        return kResultOk;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 index,
                                         SpeakerArrangement &out) override
    {
        if (index < 0 || index >= 3)
            return kInvalidArgument;
        out = (direction == kInput ? inputs : outputs)[size_t(index)];
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
        if (data.numSamples == 0)
            return kResultOk;
        if (data.symbolicSampleSize != kSample32 || data.numSamples < 0 || data.numInputs != 3 ||
            data.numOutputs != 3)
            return kResultFalse;
        constexpr std::array<float, 3> weight{1.f, 3.f, 7.f};
        for (int n = 0; n < data.numSamples; ++n)
        {
            // Read BOTH independent channel sums before any in-place output.
            // Mono inputs duplicate by their genuine one native channel.
            std::array<float, 2> sum{};
            for (int b = 0; b < 3; ++b)
                if (inputActive[size_t(b)] && data.inputs[b].numChannels > 0 &&
                    data.inputs[b].channelBuffers32)
                    for (int c = 0; c < 2; ++c)
                        sum[size_t(c)] +=
                            weight[size_t(b)] *
                            data.inputs[b].channelBuffers32[std::min(c, data.inputs[b].numChannels - 1)][n];
            for (int b = 0; b < 3; ++b)
                if (outputActive[size_t(b)] && data.outputs[b].numChannels > 0 &&
                    data.outputs[b].channelBuffers32)
                    for (int c = 0; c < data.outputs[b].numChannels; ++c)
                        data.outputs[b].channelBuffers32[c][n] = float(b + 1) * sum[size_t(c % 2)];
        }
        for (int b = 0; b < 3; ++b)
            if (outputActive[size_t(b)])
                data.outputs[b].silenceFlags = 0;
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
class Factory final : public IPluginFactory
{
  public:
    REFS();
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
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
        *info = PFactoryInfo("VibeRa original bus-choice fixture", "", "", 0);
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
                           "VibeRa Native Bus Choice Fixture");
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
