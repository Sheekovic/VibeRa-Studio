// SPDX-License-Identifier: AGPL-3.0-or-later
// Original native SDK source fixture. No JUCE wrapper or app test hook.
#include "ClockWaveform.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
DECLARE_UID(classID, 0x56494245, 0x5241434C, 0x4F434B57, 0x41564531)
void text(String128 out, const char *value)
{
    std::fill_n(out, 128, TChar{});
    for (int n = 0; value[n] && n < 127; ++n)
        out[n] = TChar(value[n]);
}
#define REFS()                                                                                               \
    uint32 PLUGIN_API addRef() override { return ++references; }                                              \
    uint32 PLUGIN_API release() override                                                                      \
    { const auto count = --references; if (!count) delete this; return count; }                               \
    std::atomic<uint32> references{1}
#define QUERY(Type)                                                                                          \
    if (!std::memcmp(iid, Type##_iid, 16))                                                                     \
    { *out = static_cast<Type *>(this); addRef(); return kResultOk; }
class ClockSource final : public IComponent, public IAudioProcessor, public IEditController
{
    // Sole native process() owner. No control API reads/writes/resets this clock.
    std::int64_t sampleClock = 0;

  public:
    REFS();
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
    {
        if (!out) return kInvalidArgument;
        *out = nullptr;
        QUERY(IComponent) QUERY(IAudioProcessor) QUERY(IEditController)
        if (!std::memcmp(iid, FUnknown_iid, 16) || !std::memcmp(iid, IPluginBase_iid, 16))
        { *out = static_cast<IComponent *>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    tresult PLUGIN_API initialize(FUnknown *) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }
    tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override
    { return type == kAudio ? 1 : 0; }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo &bus) override
    {
        if (type != kAudio || index != 0) return kInvalidArgument;
        bus = {}; bus.mediaType = type; bus.direction = direction;
        bus.channelCount = 2; bus.busType = kMain; bus.flags = BusInfo::kDefaultActive;
        text(bus.name, "Original clock source stereo"); return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection, int32 index, TBool) override
    { return type == kAudio && index == 0 ? kResultOk : kInvalidArgument; }
    tresult PLUGIN_API setActive(TBool) override { return kResultOk; }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        uint32 magic = 0; int32 count = 0;
        return stream && stream->read(&magic, sizeof(magic), &count) == kResultOk &&
                       count == sizeof(magic) && magic == 0x43574f31 ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        uint32 magic = 0x43574f31; int32 count = 0;
        return stream && stream->write(&magic, sizeof(magic), &count) == kResultOk &&
                       count == sizeof(magic) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override { return setState(stream); }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 inputCount,
                                         SpeakerArrangement *outputs, int32 outputCount) override
    {
        return inputs && outputs && inputCount == 1 && outputCount == 1 &&
                       inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo
                   ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32 index, SpeakerArrangement &out) override
    { if (index != 0) return kInvalidArgument; out = SpeakerArr::kStereo; return kResultOk; }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    { return size == kSample32 ? kResultOk : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup &) override { return kResultOk; }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }
    tresult PLUGIN_API process(ProcessData &data) override
    {
        if (data.numSamples == 0) return kResultOk; // Controller flushes do not advance time.
        if (data.symbolicSampleSize != kSample32 || data.numSamples < 0 || data.numOutputs != 1 ||
            !data.outputs || data.outputs[0].numChannels != 2 ||
            !data.outputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32[0] ||
            !data.outputs[0].channelBuffers32[1] ||
            sampleClock > std::numeric_limits<std::int64_t>::max() - data.numSamples)
            return kResultFalse;
        for (int n = 0; n < data.numSamples; ++n, ++sampleClock)
            for (int channel = 0; channel < 2; ++channel)
                data.outputs[0].channelBuffers32[channel][n] =
                    mixer_clock_waveform::sample(sampleClock, channel);
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    tresult PLUGIN_API getParameterInfo(int32, ParameterInfo &) override { return kInvalidArgument; }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override { return kInvalidArgument; }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override { return kInvalidArgument; }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue value) override { return value; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue value) override { return value; }
    ParamValue PLUGIN_API getParamNormalized(ParamID) override { return 0; }
    tresult PLUGIN_API setParamNormalized(ParamID, ParamValue) override { return kInvalidArgument; }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *) override { return kResultOk; }
    IPlugView *PLUGIN_API createView(FIDString) override { return nullptr; }
};
class Factory final : public IPluginFactory
{
  public:
    REFS();
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
    {
        if (!out) return kInvalidArgument;
        *out = nullptr; QUERY(IPluginFactory)
        if (!std::memcmp(iid, FUnknown_iid, 16))
        { *out = static_cast<IPluginFactory *>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *out) override
    { if (!out) return kInvalidArgument; *out = PFactoryInfo("VibeRa original clock fixture", "", "", 0); return kResultOk; }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo *out) override
    {
        if (index != 0 || !out) return kInvalidArgument;
        *out = PClassInfo(classID, PClassInfo::kManyInstances, kVstAudioEffectClass, "VibeRa Clock Waveform Fixture");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **out) override
    {
        if (!out) return kInvalidArgument;
        *out = nullptr;
        if (std::memcmp(cid, classID, 16)) return kResultFalse;
        auto *instance = new ClockSource;
        const auto result = instance->queryInterface(iid, out); instance->release(); return result;
    }
};
} // namespace
extern "C" __declspec(dllexport) IPluginFactory *PLUGIN_API GetPluginFactory() { return new Factory; }
extern "C" __declspec(dllexport) bool InitDll() { return true; }
extern "C" __declspec(dllexport) bool ExitDll() { return true; }
