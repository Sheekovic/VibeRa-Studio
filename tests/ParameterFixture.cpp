// SPDX-License-Identifier: AGPL-3.0-or-later
// Original test-only VST3. No JUCE plugin wrapper: consumes the native queues at their sample offsets.
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/gui/iplugview.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
DECLARE_UID(classID, 0x56494245, 0x52415041, 0x52414D45, 0x54455231)
constexpr std::array<ParamID, 6> ids{0, 0xfedcba98u, 99, 777, 42, 4000001234u};
constexpr std::array<double, 6> defaults{.5, 0, 0, .25, 0, -1};
constexpr std::array<const char *, 6> names{"Gain zero ID", "Sparse bias", "Read-only meter",
                                            "Hidden",       "Schema mode", "Unavailable MIDI helper"};
int indexFor(ParamID id)
{
    const auto it = std::find(ids.begin(), ids.end(), id);
    return it == ids.end() ? -1 : int(it - ids.begin());
}
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
        const auto n = --references;                                                                         \
        if (!n)                                                                                              \
            delete this;                                                                                     \
        return n;                                                                                            \
    }                                                                                                        \
    std::atomic<uint32> references{1}
#define QUERY(Type)                                                                                          \
    if (std::memcmp(iid, Type##_iid, 16) == 0)                                                               \
    {                                                                                                        \
        *out = static_cast<Type *>(this);                                                                    \
        addRef();                                                                                            \
        return kResultOk;                                                                                    \
    }

class Fixture final : public IComponent,
                      public IAudioProcessor,
                      public IEditController,
                      public IMidiMapping,
                      public IUnitInfo
{
    std::array<std::atomic<double>, 6> values;
    std::array<double, 6> dsp = defaults;
    IComponentHandler *handler = nullptr;
    unsigned editorViewProbes = 0; // Original editor-fixture control thread only.
    bool startupEditorProbeDelayed = false;

  public:
    Fixture()
    {
        for (size_t i = 0; i < values.size(); ++i)
            values[i] = defaults[i];
    }
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
        QUERY(IMidiMapping)
        QUERY(IUnitInfo) if (!std::memcmp(iid, FUnknown_iid, 16) || !std::memcmp(iid, IPluginBase_iid, 16))
        {
            *out = static_cast<IComponent *>(this);
            addRef();
            return kResultOk;
        }
        return kNoInterface;
    }
    tresult PLUGIN_API initialize(FUnknown *) override
    {
        if (std::getenv("VIBERA_FIXTURE_INIT_FAIL"))
            return kInternalError; // Original fixture only: exercise recovery to a missing placeholder.
        // Test-only initialization stall; no application/third-party DSP injection.
        if (const auto *value = std::getenv("VIBERA_FIXTURE_INIT_DELAY_MS"))
            std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(std::atoi(value), 0, 6000)));
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
        return type == kAudio || (type == kEvent && dir == kInput) ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 i, BusInfo &bus) override
    {
        if (i != 0 || !getBusCount(type, dir))
            return kInvalidArgument;
        bus = {};
        bus.mediaType = type;
        bus.direction = dir;
        bus.channelCount = type == kAudio ? 2 : 16;
        bus.busType = kMain;
        bus.flags = BusInfo::kDefaultActive;
        text(bus.name, "Fixture bus");
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
    tresult PLUGIN_API setActive(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        std::array<double, 6> data{};
        int32 read = 0;
        if (!stream || stream->read(data.data(), sizeof(data), &read) != kResultOk || read != sizeof(data))
            return kResultFalse;
        for (size_t i = 0; i < data.size(); ++i)
            if (!std::isfinite(data[i]) || (i == 5 ? data[i] != -1 : data[i] < 0 || data[i] > 1))
                return kResultFalse;
        dsp = data;
        for (size_t i = 0; i < data.size(); ++i)
            values[i] = data[i];
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        int32 written = 0;
        return stream ? stream->write(dsp.data(), sizeof(dsp), &written) : kResultFalse;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override
    {
        return setState(stream);
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *in, int32 ni, SpeakerArrangement *out,
                                          int32 no) override
    {
        return ni == 1 && no == 1 && in[0] == SpeakerArr::kStereo && out[0] == SpeakerArr::kStereo
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32 i, SpeakerArrangement &out) override
    {
        out = SpeakerArr::kStereo;
        return i == 0 ? kResultOk : kInvalidArgument;
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
        {
            if (auto *changes = data.inputParameterChanges)
                for (int q = 0; q < changes->getParameterCount(); ++q)
                {
                    auto *queue = changes->getParameterData(q);
                    const auto i = queue ? indexFor(queue->getParameterId()) : -1;
                    if (i < 0 || queue->getPointCount() == 0)
                        continue;
                    int32 offset = 0;
                    ParamValue value = 0;
                    if (queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
                        dsp[size_t(i)] = value;
                }
            return kResultOk;
        }
        if (data.numOutputs < 1)
            return kResultOk;
        // Reject malformed ordering visibly; independently read every queue's exact sample offset.
        if (auto *changes = data.inputParameterChanges)
            for (int q = 0; q < changes->getParameterCount(); ++q)
            {
                auto *queue = changes->getParameterData(q);
                int previous = -1;
                for (int p = 0; queue && p < queue->getPointCount(); ++p)
                {
                    int32 offset = -1;
                    ParamValue value = 0;
                    if (queue->getPoint(p, offset, value) != kResultOk || offset <= previous ||
                        offset >= data.numSamples)
                    {
                        for (int c = 0; c < data.outputs[0].numChannels; ++c)
                            std::fill_n(data.outputs[0].channelBuffers32[c], data.numSamples, -100.f);
                        return kResultOk;
                    }
                    previous = offset;
                }
            }
        std::array<int, 6> next{};
        for (int n = 0; n < data.numSamples; ++n)
        {
            if (auto *changes = data.inputParameterChanges)
                for (int q = 0; q < changes->getParameterCount(); ++q)
                {
                    auto *queue = changes->getParameterData(q);
                    const int i = queue ? indexFor(queue->getParameterId()) : -1;
                    if (i < 0)
                        continue;
                    while (next[size_t(i)] < queue->getPointCount())
                    {
                        int32 offset = -1;
                        ParamValue value = 0;
                        queue->getPoint(next[size_t(i)], offset, value);
                        if (offset > n)
                            break;
                        dsp[size_t(i)] = value;
                        ++next[size_t(i)];
                    }
                }
            for (int c = 0; c < data.outputs[0].numChannels; ++c)
            {
                const float input = data.numInputs && c < data.inputs[0].numChannels
                                        ? data.inputs[0].channelBuffers32[c][n]
                                        : 0;
                data.outputs[0].channelBuffers32[c][n] = float(input * dsp[0] + dsp[1]);
            }
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return int32(ids.size());
    }
    tresult PLUGIN_API getParameterInfo(int32 i, ParameterInfo &info) override
    {
        if (i < 0 || i >= int32(ids.size()))
            return kInvalidArgument;
        info = {};
        info.id = ids[size_t(i)];
        text(info.title, names[size_t(i)]);
        text(info.units, "norm");
        info.defaultNormalizedValue = defaults[size_t(i)];
        info.stepCount = i == 4 ? 3 : 0;
        if (i == 1 && values[4].load() > .6 && values[4].load() < .75)
            info.stepCount = -1; // Original malformed-metadata mode; no upstream step-count overflow.
        info.unitId = i == 1 || i == 4 ? 7 : 0;
        info.flags = i == 2   ? ParameterInfo::kIsReadOnly
                     : i == 3 ? ParameterInfo::kIsHidden
                              : ParameterInfo::kCanAutomate;
        if (i == 5)
            info.flags = 1024; // Original nonconforming sentinel fixture, matching the observed API category.
        if (i == 1 && values[4].load() > .9)
            info.flags |= ParameterInfo::kIsReadOnly;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue value, String128 out) override
    {
        char buffer[40]{};
        std::snprintf(buffer, sizeof(buffer), "%.4f", value);
        text(out, buffer);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override
    {
        return kNotImplemented;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override
    {
        return v;
    }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override
    {
        return v;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override
    {
        int i = indexFor(id);
        return i < 0 ? 0 : values[size_t(i)].load();
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override
    {
        const int i = indexFor(id);
        if (i < 0 || !std::isfinite(v) || v < 0 || v > 1)
            return kInvalidArgument;
        // Reserved exact, representable test inputs. Only this original fixture
        // stalls/exits; these are control calls, not processor automation events.
        if (id == 0 && v == double(.27182818f))
            Sleep(10000);
        if (id == 0 && v == double(.31415927f))
            std::_Exit(73);
        const auto previous = values[size_t(i)].exchange(v);
        if (i == 4 && previous != v && handler)
            handler->restartComponent(kParamTitlesChanged);
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
    void editorGesture(int zone)
    {
        values[2] = double(zone) / 3.; // Test-only read-only acknowledgement of actual editor handler.
        if (!handler)
            return;
        const auto id = zone == 1 ? ids[1] : ids[0];
        if (zone != 2)
            handler->beginEdit(id);
        const auto target = getParamNormalized(id) > .5 ? .2 : .8;
        const int count = zone >= 2 ? 1000 : 2;
        for (int i = 0; i < count; ++i)
        {
            const auto value = i + 1 == count ? target : .3;
            setParamNormalized(id, value);
            handler->performEdit(id, value);
        }
        if (zone != 2)
            handler->endEdit(id);
    }
    void editorMetadataToggle()
    {
        if (!handler)
            return;
        const auto previous = values[4].load();
        const auto next = previous > .6 && previous < .75 ? 0. : 2. / 3.;
        handler->beginEdit(ids[4]);
        setParamNormalized(ids[4], next);
        handler->performEdit(ids[4], next);
        handler->endEdit(ids[4]);
    }
    class View final : public IPlugView
    {
        Fixture &owner;
        HWND window = nullptr;
        static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l)
        {
            auto *view = reinterpret_cast<View *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                view = static_cast<View *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
            }
            if (view && message == WM_LBUTTONDOWN)
            {
                RECT client{};
                GetClientRect(window, &client);
                view->owner.editorGesture(
                    std::clamp(4 * int(LOWORD(l)) / std::max(1L, client.right), 0L, 3L));
                InvalidateRect(window, nullptr, TRUE);
                return 0;
            }
            if (view && message == WM_RBUTTONDOWN)
            {
                view->owner.editorMetadataToggle();
                return 0;
            }
            if (view && message == WM_PAINT)
            {
                PAINTSTRUCT paint;
                auto dc = BeginPaint(window, &paint);
                RECT client{};
                GetClientRect(window, &client);
                // Dark flat buttons in the host's palette; the four equal click zones are unchanged.
                auto background = CreateSolidBrush(RGB(0x1b, 0x21, 0x28));
                FillRect(dc, &client, background);
                DeleteObject(background);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0xe6, 0xec, 0xf1));
                auto font = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
                auto oldFont = SelectObject(dc, font);
                auto face = CreateSolidBrush(RGB(0x27, 0x30, 0x3a));
                auto edge = CreatePen(PS_SOLID, 1, RGB(0x3f, 0x8f, 0x86));
                auto oldBrush = SelectObject(dc, face);
                auto oldPen = SelectObject(dc, edge);
                const wchar_t *labels[]{L"Gain gesture", L"Sparse ID gesture", L"Telemetry flood",
                                        L"Gesture overflow"};
                for (int i = 0; i < 4; ++i)
                {
                    RECT rect{i * client.right / 4 + 6, 10, (i + 1) * client.right / 4 - 6,
                              client.bottom - 10};
                    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 10, 10);
                    DrawTextW(dc, labels[i], -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                SelectObject(dc, oldFont);
                DeleteObject(face);
                DeleteObject(edge);
                DeleteObject(font);
                EndPaint(window, &paint);
                return 0;
            }
            return DefWindowProcW(window, message, w, l);
        }

      public:
        explicit View(Fixture &fixture) : owner(fixture)
        {
            owner.addRef();
        }
        ~View()
        {
            removed();
            owner.release();
        }
        REFS();
        tresult PLUGIN_API queryInterface(const TUID iid, void **out) override
        {
            if (!out)
                return kInvalidArgument;
            *out = nullptr;
            QUERY(IPlugView)
            if (std::memcmp(iid, FUnknown_iid, 16) == 0)
            {
                *out = static_cast<IPlugView *>(this);
                addRef();
                return kResultOk;
            }
            return kNoInterface;
        }
        tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
        {
            return type && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultOk : kResultFalse;
        }
        tresult PLUGIN_API attached(void *parent, FIDString type) override
        {
            if (!parent || isPlatformTypeSupported(type) != kResultOk)
                return kResultFalse;
            // Original fixture only. Put native-work delay here: JUCE hasEditor
            // creates/releases probe views before an editor request is accepted.
            if (const auto* delay = std::getenv("VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS"))
                std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(std::atoi(delay), 0, 6000)));
            if (std::getenv("VIBERA_FIXTURE_EDITOR_ATTACH_FAIL"))
                return kResultFalse; // No child is created; a blank wrapper must not report success.
            if (std::getenv("VIBERA_FIXTURE_EDITOR_ATTACH_CRASH"))
                std::_Exit(87); // Only this explicitly armed original fixture's owned worker.
            WNDCLASSW klass{};
            klass.lpfnWndProc = procedure;
            klass.hInstance = GetModuleHandleW(nullptr);
            klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            klass.lpszClassName = L"VibeRaOriginalGestureFixture";
            RegisterClassW(&klass);
            window =
                CreateWindowExW(0, klass.lpszClassName, L"Original gesture fixture", WS_CHILD | WS_VISIBLE, 0,
                                0, 600, 100, static_cast<HWND>(parent), nullptr, klass.hInstance, this);
            return window ? kResultOk : kResultFalse;
        }
        tresult PLUGIN_API removed() override
        {
            if (window)
            {
                DestroyWindow(window);
                window = nullptr;
            }
            return kResultOk;
        }
        tresult PLUGIN_API onWheel(float) override
        {
            return kResultFalse;
        }
        tresult PLUGIN_API onKeyDown(char16, int16, int16) override
        {
            return kResultFalse;
        }
        tresult PLUGIN_API onKeyUp(char16, int16, int16) override
        {
            return kResultFalse;
        }
        tresult PLUGIN_API getSize(ViewRect *size) override
        {
            if (!size)
                return kInvalidArgument;
            *size = {0, 0, 600, 100};
            return kResultOk;
        }
        tresult PLUGIN_API onSize(ViewRect *) override
        {
            return kResultOk;
        }
        tresult PLUGIN_API onFocus(TBool) override
        {
            return kResultOk;
        }
        tresult PLUGIN_API setFrame(IPlugFrame *) override
        {
            return kResultOk;
        }
        tresult PLUGIN_API canResize() override
        {
            return kResultFalse;
        }
        tresult PLUGIN_API checkSizeConstraint(ViewRect *) override
        {
            return kResultFalse;
        }
    };
    IPlugView *PLUGIN_API createView(FIDString name) override
    {
        if (!name || std::strcmp(name, ViewType::kEditor) != 0 ||
            std::getenv("VIBERA_FIXTURE_EDITOR_UNSUPPORTED"))
            return nullptr;
        if (!startupEditorProbeDelayed)
            if (const auto *delay = std::getenv("VIBERA_FIXTURE_EDITOR_PROBE_DELAY_MS"))
            {
                startupEditorProbeDelayed = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(std::atoi(delay), 0, 6000)));
            }
        // Pinned JUCE probes hasEditor during startup and request admission.
        // Setting2 permits those two probes but returns a genuine null native
        // editor on the subsequent creation call, including fallback lookups.
        if (const auto *count = std::getenv("VIBERA_FIXTURE_EDITOR_NULL_AFTER_PROBES"))
            if (++editorViewProbes > unsigned(std::clamp(std::atoi(count), 0, 8)))
                return nullptr;
        return new View(*this);
    }
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber number,
                                                   ParamID &id) override
    {
        id = ids[0];
        return bus == 0 && channel == 0 && number == 7 ? kResultOk : kResultFalse;
    }
    int32 PLUGIN_API getUnitCount() override
    {
        return 2;
    }
    tresult PLUGIN_API getUnitInfo(int32 i, UnitInfo &info) override
    {
        if (i < 0 || i > 1)
            return kInvalidArgument;
        info = {};
        info.id = i ? 7 : 0;
        info.parentUnitId = i ? 0 : -1;
        info.programListId = -1;
        text(info.name, i ? "Grouped" : "Root");
        return kResultOk;
    }
    int32 PLUGIN_API getProgramListCount() override
    {
        return 0;
    }
    tresult PLUGIN_API getProgramListInfo(int32, ProgramListInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API getProgramName(ProgramListID, int32, String128) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API getProgramInfo(ProgramListID, int32, Steinberg::Vst::CString, String128) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API hasProgramPitchNames(ProgramListID, int32) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API getProgramPitchName(ProgramListID, int32, int16, String128) override
    {
        return kNotImplemented;
    }
    UnitID PLUGIN_API getSelectedUnit() override
    {
        return 0;
    }
    tresult PLUGIN_API selectUnit(UnitID) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API getUnitByBus(MediaType, BusDirection, int32, int32, UnitID &) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API setUnitProgramData(int32, int32, IBStream *) override
    {
        return kNotImplemented;
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
        QUERY(IPluginFactory) if (!std::memcmp(iid, FUnknown_iid, 16))
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
        *info = PFactoryInfo("VibeRa test fixture", "", "", 0);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override
    {
        return 1;
    }
    tresult PLUGIN_API getClassInfo(int32 i, PClassInfo *info) override
    {
        if (i != 0 || !info)
            return kInvalidArgument;
        *info =
            PClassInfo(classID, PClassInfo::kManyInstances, kVstAudioEffectClass, "VibeRa Parameter Fixture");
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
