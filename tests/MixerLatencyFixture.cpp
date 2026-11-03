// SPDX-License-Identifier: AGPL-3.0-or-later
// Original native SDK test effect. Its PCM delay is an independent latency oracle.
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
DECLARE_UID(classID, 0x56494245, 0x52414D58, 0x4C415445, 0x4E435931)
constexpr std::array<unsigned, 4> delays{0, 31, 257, 1024};
constexpr unsigned capacity = 4096;
constexpr ParamID latencyId = 17, sidechainId = 71, prepareMutationId = 113;
void text(String128 destination, const char *source)
{
    std::fill_n(destination, 128, TChar{});
    for (int i = 0; source[i] && i < 127; ++i)
        destination[i] = TChar(source[i]);
}
unsigned modeFor(double normalized)
{
    return unsigned(std::clamp(std::lround(normalized * 3), 0L, 3L));
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
    std::array<std::array<float, capacity>, 2> ring{};
    unsigned cursor = 0;
    std::atomic<unsigned> mode{1};
    std::atomic<double> sidechainAmount{0};
    std::atomic<bool> mutateLayoutOnPrepare{false};
    bool preparationMutation = false;
    SpeakerArrangement mainLayout = SpeakerArr::kStereo;
    bool sidechainActive = false;
    SpeakerArrangement sidechainLayout = SpeakerArr::kEmpty;
    IComponentHandler *handler = nullptr;

  public:
    ~Fixture()
    {
        if (handler)
            handler->release();
    }
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
        return type == kAudio ? (direction == kInput ? 2 : 1) : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo &bus) override
    {
        if (index < 0 || index >= getBusCount(type, direction))
            return kInvalidArgument;
        bus = {};
        bus.mediaType = type;
        bus.direction = direction;
        bus.channelCount = index == 0 ? (mainLayout == SpeakerArr::kMono ? 1 : 2) : 1;
        bus.busType = index == 0 ? kMain : kAux;
        bus.flags = index == 0 ? BusInfo::kDefaultActive : 0;
        text(bus.name, index == 0 ? "Stereo main" : "Mono/stereo sidechain");
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection direction, int32 index, TBool active) override
    {
        if (index < 0 || index >= getBusCount(type, direction))
            return kInvalidArgument;
        if (direction == kInput && index == 1)
            sidechainActive = active != 0;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        struct State
        {
            uint32 magic, mode;
            double amount;
        } state{};
        int32 read = 0;
        if (!stream || stream->read(&state, sizeof(state), &read) != kResultOk || read != sizeof(state) ||
            state.magic != 0x4d584c31 || state.mode >= delays.size() || !std::isfinite(state.amount) ||
            state.amount < 0 || state.amount > 1)
            return kResultFalse;
        const auto previous = mode.exchange(state.mode);
        sidechainAmount = state.amount;
        // Genuine old16-byte fixture states remain valid. Only the explicit
        // preparation-mutation scenario appends this independently versioned tag.
        uint32 extension = 0;
        read = 0;
        stream->read(&extension, sizeof(extension), &read);
        if (read != 0 && (read != sizeof(extension) || extension != 0x50524531))
            return kResultFalse;
        mutateLayoutOnPrepare = read == sizeof(extension);
        if (previous != state.mode && handler)
            handler->restartComponent(kLatencyChanged);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        struct State
        {
            uint32 magic, mode;
            double amount;
        } state{0x4d584c31, mode.load(), sidechainAmount.load()};
        int32 written = 0;
        if (!stream || stream->write(&state, sizeof(state), &written) != kResultOk ||
            written != sizeof(state))
            return kResultFalse;
        if (mutateLayoutOnPrepare.load())
        {
            uint32 extension = 0x50524531;
            written = 0;
            return stream->write(&extension, sizeof(extension), &written) == kResultOk &&
                           written == sizeof(extension)
                       ? kResultOk
                       : kResultFalse;
        }
        return kResultOk;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override
    {
        return setState(stream);
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 inputCount,
                                          SpeakerArrangement *outputs, int32 outputCount) override
    {
        if (preparationMutation)
        {
            // The native processor genuinely changes its requested main buses
            // during setupProcessing and refuses a subsequent stereo request.
            // No recursive host callback or application-specific fault hook.
            mainLayout = SpeakerArr::kMono;
            sidechainLayout = SpeakerArr::kEmpty;
            return kResultFalse;
        }
        if (!inputs || !outputs || inputCount != 2 || outputCount != 1 || inputs[0] != SpeakerArr::kStereo ||
            outputs[0] != SpeakerArr::kStereo ||
            (inputs[1] != SpeakerArr::kEmpty && inputs[1] != SpeakerArr::kMono &&
             inputs[1] != SpeakerArr::kStereo))
            return kResultFalse;
        sidechainLayout = inputs[1];
        return kResultOk;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 index,
                                         SpeakerArrangement &out) override
    {
        if (index < 0 || index >= getBusCount(kAudio, direction))
            return kInvalidArgument;
        out = index == 0 ? mainLayout : sidechainLayout;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultOk : kResultFalse;
    }
    uint32 PLUGIN_API getLatencySamples() override
    {
        return delays[mode.load()];
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup &) override
    {
        preparationMutation = mutateLayoutOnPrepare.load();
        if (preparationMutation)
        {
            mainLayout = SpeakerArr::kMono;
            sidechainLayout = SpeakerArr::kEmpty;
            sidechainActive = false;
        }
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool active) override
    {
        if (active)
        {
            for (auto &channel : ring)
                channel.fill(0);
            cursor = 0;
        }
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return delays[mode.load()];
    }
    tresult PLUGIN_API process(ProcessData &data) override
    {
        // Control latency is deliberately not automatable. Its kLatencyChanged
        // notification occurs on the controller thread; sample queues cannot
        // invoke host callbacks from DSP. Sidechain amount can be automated.
        if (data.numSamples == 0)
            return kResultOk;
        auto amount = sidechainAmount.load();
        IParamValueQueue *amountQueue = nullptr;
        if (auto *changes = data.inputParameterChanges)
            for (int q = 0; q < changes->getParameterCount(); ++q)
                if (auto *queue = changes->getParameterData(q);
                    queue && queue->getParameterId() == sidechainId)
                    amountQueue = queue;
        int next = 0;
        const auto delay = delays[mode.load()];
        if (data.numOutputs < 1 || data.outputs[0].numChannels < 2)
            return kResultFalse;
        for (int n = 0; n < data.numSamples; ++n)
        {
            while (amountQueue && next < amountQueue->getPointCount())
            {
                int32 frame = 0;
                ParamValue value = 0;
                if (amountQueue->getPoint(next, frame, value) != kResultOk)
                    return kResultFalse;
                if (frame > n)
                    break;
                amount = value;
                ++next;
            }
            for (int channel = 0; channel < 2; ++channel)
            {
                // Mono detector feeds both main channels; a genuinely negotiated
                // stereo auxiliary bus preserves its independent L/R samples.
                const auto side =
                    sidechainActive && data.numInputs > 1 && data.inputs[1].numChannels > 0
                        ? data.inputs[1]
                              .channelBuffers32[std::min(channel, data.inputs[1].numChannels - 1)][n]
                        : 0.f;
                const auto input = data.numInputs > 0 && data.inputs[0].numChannels > channel
                                       ? data.inputs[0].channelBuffers32[channel][n]
                                       : 0.f;
                ring[channel][cursor] = input + float(amount * side);
                data.outputs[0].channelBuffers32[channel][n] =
                    ring[channel][(cursor + capacity - delay) % capacity];
            }
            cursor = (cursor + 1) % capacity;
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return 3;
    }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo &info) override
    {
        if (index < 0 || index > 2)
            return kInvalidArgument;
        info = {};
        info.id = index == 0 ? latencyId : index == 1 ? sidechainId : prepareMutationId;
        text(info.title, index == 0   ? "Declared latency mode"
                         : index == 1 ? "Sidechain amount"
                                      : "Mutate buses at next preparation");
        text(info.units, index == 0 ? "samples" : index == 1 ? "linear" : "boolean");
        info.stepCount = index == 0 ? 3 : index == 1 ? 0 : 1;
        info.defaultNormalizedValue = index == 0 ? 1. / 3. : 0;
        info.flags = index == 1 ? ParameterInfo::kCanAutomate : 0;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 out) override
    {
        char buffer[40]{};
        if (id == latencyId)
            std::snprintf(buffer, sizeof(buffer), "%u", delays[modeFor(value)]);
        else if (id == sidechainId)
            std::snprintf(buffer, sizeof(buffer), "%.4f", value);
        else if (id == prepareMutationId)
            std::snprintf(buffer, sizeof(buffer), "%s", value >= .5 ? "On next preparation" : "Normal");
        else
            return kInvalidArgument;
        text(out, buffer);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override
    {
        return kNotImplemented;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue value) override
    {
        return id == latencyId ? delays[modeFor(value)] : value;
    }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue value) override
    {
        if (id != latencyId)
            return value;
        unsigned best = 0;
        for (unsigned i = 1; i < delays.size(); ++i)
            if (std::abs(value - delays[i]) < std::abs(value - delays[best]))
                best = i;
        return best / 3.;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override
    {
        return id == latencyId                                           ? mode.load() / 3.
               : id == sidechainId                                       ? sidechainAmount.load()
               : id == prepareMutationId && mutateLayoutOnPrepare.load() ? 1.
                                                                         : 0.;
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
    {
        if (!std::isfinite(value) || value < 0 || value > 1)
            return kInvalidArgument;
        if (id == sidechainId)
        {
            sidechainAmount = value;
            return kResultOk;
        }
        if (id == prepareMutationId)
        {
            mutateLayoutOnPrepare = value >= .5;
            return kResultOk;
        }
        if (id != latencyId)
            return kInvalidArgument;
        const auto next = modeFor(value), previous = mode.exchange(next);
        if (previous != next && handler)
            handler->restartComponent(kLatencyChanged);
        return kResultOk;
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
        *info = PFactoryInfo("VibeRa original Mixer test fixture", "", "", 0);
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
                           "VibeRa Mixer Latency Fixture");
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
