// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synth/SynthPlugin.h"
#include "drums/DrumSynthPlugin.h"
#include "NativeInstruments.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProtocol.h"
#include "PluginWrapperView.h"
#include "PluginEditorClock.h"
#include "PluginParameterSchema.h"
#include "NativeWindowStyle.h"
#include "Vst3ParameterBridge.h"
#include "Vst3BusValidation.h"
#include "Vst3EditorBridge.h"
#include "effects/NativeEffects.h"
#include "effects/NativeEffectsRegistry.h"
#include "PluginIpcResources.h"
#include <charconv>
#include <set>
#include <cmath>
#include <limits>
#include <optional>
#include <functional>

namespace
{
using namespace vibera;
thread_local bool hostParameterWrite = false;
class Position final : public juce::AudioPlayHead
{
  public:
    std::atomic<double> seconds{0}, tempo{120}, ppq{-1};
    std::atomic<bool> playing{false};
    std::atomic<int> numerator{4}, denominator{4};
    double rate = 48000;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        auto time = seconds.load();
        auto bpm = tempo.load();
        p.setTimeInSeconds(time);
        p.setTimeInSamples(juce::int64(time * rate));
        p.setBpm(bpm);
        const auto beat = ppq.load();
        p.setPpqPosition(beat >= 0 ? beat : time * bpm / 60.);
        p.setIsPlaying(playing.load());
        p.setTimeSignature(TimeSignature{numerator.load(), denominator.load()});
        return p;
    }
};
// JUCE's VST3PluginWindow sends its own editorBeingDeleted notification.
// Our directly owned native editors need the host wrapper to send that notice
// before deletion, including constructor-failure paths, or reopening is refused
// by AudioProcessor's stale active-editor pointer. All ownership is CONTROL/UI.
struct NativeEditorDeleter
{
    void operator()(juce::AudioProcessorEditor *component) const noexcept
    {
        if (component && dynamic_cast<NativeProcessor *>(&component->processor))
            component->processor.editorBeingDeleted(component);
        delete component;
    }
};
using NativeEditorOwner = std::unique_ptr<juce::AudioProcessorEditor, NativeEditorDeleter>;
// The worker has no Studio theme/settings owner. Keep its host chrome local,
// original and independent of the native editor or DSP resource ownership.
class PluginWrapperChrome final : public juce::LookAndFeel_V4
{
    class CaptionButton final : public juce::Button
    {
        void paintButton(juce::Graphics &g, bool over, bool down) override
        {
            if (over || down)
                g.fillAll(juce::Colour(0xffb5414b));
            g.setColour(juce::Colour(0xffd6dde3));
            const auto r = getLocalBounds().toFloat().withSizeKeepingCentre(10, 10);
            g.drawLine(r.getX(), r.getY(), r.getRight(), r.getBottom(), 1.3f);
            g.drawLine(r.getRight(), r.getY(), r.getX(), r.getBottom(), 1.3f);
        }

      public:
        CaptionButton() : Button("Close plugin window")
        {
            setTooltip(getName());
            setWantsKeyboardFocus(false);
        }
    };

  public:
    // Same palette and 13 px UI font as the main window, so the wrapper reads as part of VibeRa.
    PluginWrapperChrome()
    {
        const juce::Colour bg(0xff10191f), panel(0xff1a272f), grid(0xff2c3e48), ink(0xffe4eef2),
            accent(0xff29d7c5);
        setColour(juce::ResizableWindow::backgroundColourId, panel);
        setColour(juce::TextButton::buttonColourId, panel.brighter(.05f));
        setColour(juce::TextButton::buttonOnColourId, accent.darker(.55f));
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, ink);
        setColour(juce::ComboBox::backgroundColourId, bg);
        setColour(juce::ComboBox::outlineColourId, grid);
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff95a9b4));
        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::trackColourId, accent.withAlpha(.45f));
        setColour(juce::Slider::backgroundColourId, bg);
        setColour(juce::Slider::textBoxBackgroundColourId, bg);
        setColour(juce::Slider::textBoxOutlineColourId, grid);
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::ToggleButton::textColourId, ink);
        setColour(juce::ToggleButton::tickColourId, accent);
        setColour(juce::ToggleButton::tickDisabledColourId, grid);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, grid);
    }
    juce::Font getTextButtonFont(juce::TextButton &, int) override
    {
        return {"Segoe UI", 13.f, juce::Font::plain};
    }
    juce::Font getComboBoxFont(juce::ComboBox &) override
    {
        return {"Segoe UI", 13.f, juce::Font::plain};
    }
    juce::Font getPopupMenuFont() override
    {
        return {"Segoe UI", 14.f, juce::Font::plain};
    }
    juce::Font getLabelFont(juce::Label &label) override
    {
        const auto font = label.getFont();
        if (std::abs(font.getHeight() - 15.f) < .01f && !font.isBold() &&
            font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return {"Segoe UI", 13.f, juce::Font::plain};
        return font;
    }
    void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button, bool over, bool down) override
    {
        const float box = std::min(16.f, button.getHeight() - 4.f);
        drawTickBox(g, button, 3.f, (button.getHeight() - box) * .5f, box, box, button.getToggleState(),
                    button.isEnabled(), over, down);
        g.setColour(button.findColour(juce::ToggleButton::textColourId)
                        .withMultipliedAlpha(button.isEnabled() ? 1.f : .45f));
        g.setFont(juce::Font("Segoe UI", 13.f, juce::Font::plain));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(int(box) + 10),
                         juce::Justification::centredLeft, 2, 1.f);
    }
    juce::Button *createDocumentWindowButton(int type) override
    {
        return type == juce::DocumentWindow::closeButton ? new CaptionButton
                                                         : LookAndFeel_V4::createDocumentWindowButton(type);
    }
    void drawDocumentWindowTitleBar(juce::DocumentWindow &window, juce::Graphics &g, int width, int height,
                                    int titleX, int titleWidth, const juce::Image *, bool active) override
    {
        g.fillAll(juce::Colour(0xff182128));
        g.setColour(juce::Colour(active ? 0xff70c2b5 : 0xff78938f));
        g.fillRect(0, 0, 3, height);
        g.setColour(juce::Colour(active ? 0xffd6dde3 : 0xffa4afb7));
        g.setFont(juce::Font("Segoe UI", 14.f, juce::Font::plain));
        const int left = juce::jmax(12, titleX);
        g.drawFittedText(window.getName(), left, 0, juce::jmax(0, juce::jmin(titleWidth, width - left - 38)),
                         height, juce::Justification::centredLeft, 1);
    }
};
class EditorWindow final : public juce::DocumentWindow
{
    PluginWrapperChrome chrome;
    juce::TooltipWindow tooltips{nullptr, 600}; // Hover hints for the wrapper's buttons.
    NativeEditorOwner native;
    std::unique_ptr<PluginWrapperViewComponent> wrapper;
    PluginWrapperMailbox &mailbox;
    std::function<void()> changed;
    juce::String feedback;
    const bool builtIn;

  public:
    EditorWindow(juce::AudioProcessor &plugin, NativeEditorOwner component, PluginWrapperMailbox &control,
                 std::function<void()> onChanged)
        : DocumentWindow("VibeRa | " + plugin.getName(), juce::Colour(0xff182128), closeButton),
          native(std::move(component)), mailbox(control), changed(std::move(onChanged)),
          builtIn(dynamic_cast<NativeProcessor *>(&plugin) != nullptr)
    {
        setLookAndFeel(&chrome);
        setUsingNativeTitleBar(false);
        setTitleBarHeight(30);
        setColour(juce::DocumentWindow::textColourId, juce::Colour(0xffd6dde3));
        wrapper = std::make_unique<PluginWrapperViewComponent>(
            native.get(), [this](const PluginWrapperIntent &intent) { return mailbox.submit(intent); },
            native ? native->getConstrainer() : nullptr, native && native->isResizable());
        wrapper->onNativeSizeChanged = [this]
        {
            if (wrapper)
            {
                std::optional<juce::Rectangle<int>> workArea;
                if (const auto *display =
                        juce::Desktop::getInstance().getDisplays().getDisplayForRect(getBounds()))
                    workArea = display->userArea; // Copy before native reposition/DPI re-entry.
                setContentComponentSize(wrapper->getWidth(), wrapper->getHeight());
                updateResizeLimits();
                if (workArea && wrapper->selectedView() != PluginWrapperView::plugin)
                    setTopLeftPosition(
                        pluginWrapperBoundsWithinWorkArea(getBounds(), *workArea).getPosition());
            }
        };
        setContentNonOwned(wrapper.get(), true);
        // Keep peer style/attachment stable across tabs. Native fixed editors
        // are constrained below; host Settings/Parameters may still be resized.
        setResizable(true, false);
        updateResizeLimits();
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        // Owned acceptance captures may request JUCE's supported software peer.
        // Native plugin rendering remains its own implementation; normal runs
        // retain the default renderer. PrintWindow cannot capture Direct2D chrome.
        if (juce::SystemStats::getEnvironmentVariable("VIBERA_WRAPPER_SOFTWARE_RENDERER", {}) == "1")
            if (auto *peer = getPeer())
            {
                const auto engines = peer->getAvailableRenderingEngines();
                for (int i = 0; i < engines.size(); ++i)
                    if (engines[i].containsIgnoreCase("software"))
                        peer->setCurrentRenderingEngine(i);
            }
        roundNativeWindow(*this);
    }
    ~EditorWindow() override
    {
        clearContentComponent();
        wrapper.reset();
        setLookAndFeel(nullptr);
    }
    void updateResizeLimits()
    {
        if (!getContentComponent())
            return;
        if (!native || !wrapper || wrapper->selectedView() != PluginWrapperView::plugin)
        {
            const int minimumHeight =
                wrapper && wrapper->selectedView() == PluginWrapperView::settings ? 640 : 340;
            setResizeLimits(560 + getWidth() - getContentComponent()->getWidth(),
                            minimumHeight + getHeight() - getContentComponent()->getHeight(), 8192, 8192);
            return;
        }
        const int extraWidth = getWidth() - wrapper->getWidth();
        const int extraHeight = getHeight() - wrapper->getHeight() + 84;
        if (!native->isResizable())
        {
            const int width = juce::jmax(560, native->getWidth()) + extraWidth;
            const int height = native->getHeight() + extraHeight;
            setResizeLimits(width, height, width, height);
            return;
        }
        auto *limits = native->getConstrainer();
        setResizeLimits(juce::jmax(560, limits ? limits->getMinimumWidth() : native->getWidth()) + extraWidth,
                        (limits ? limits->getMinimumHeight() : native->getHeight()) + extraHeight,
                        juce::jmax(560, limits ? limits->getMaximumWidth() : native->getWidth()) + extraWidth,
                        (limits ? limits->getMaximumHeight() : native->getHeight()) + extraHeight);
    }
    bool selectView(PluginWrapperView view, std::uint64_t selection = 0)
    {
        if (selection && !wrapper->selectParameter(std::uint32_t(selection), unsigned(selection >> 32)))
            return false;
        wrapper->setView(view);
        mailbox.view.store(wrapper->selectedView(), std::memory_order_release);
        const auto selected = wrapper->selectedParameterIdentity();
        mailbox.selection.store(selected ? (std::uint64_t(selected->second) << 32) | selected->first : 0,
                                std::memory_order_release);
        return true;
    }
    PluginWrapperView selectedView() const
    {
        return wrapper->selectedView();
    }
    std::optional<std::pair<std::uint32_t, std::uint32_t>> selectedParameter() const
    {
        return wrapper->selectedParameterIdentity();
    }
    void updateWrapper(const std::vector<PluginWrapperParameterView> &parameters, bool instrument,
                       bool notifications, const PluginMainIo &io, const PluginBusCatalogue &buses)
    {
        const auto phase = mailbox.phase.load(std::memory_order_acquire);
        if (phase == PluginWrapperPhase::terminal)
        {
            wrapper->finishGesture(); // A terminal/expired drag cannot open another serial.
            feedback = mailbox.outcome == PluginWrapperOutcome::completed
                           ? "Applied"
                           : juce::String(mailbox.code) + ": " + juce::String(mailbox.message);
            mailbox.phase.store(PluginWrapperPhase::idle, std::memory_order_release);
        }
        PluginWrapperState state{mailbox.ready.load(std::memory_order_acquire),
                                 instrument,
                                 mailbox.bypassed.load(std::memory_order_acquire),
                                 phase != PluginWrapperPhase::idle && phase != PluginWrapperPhase::terminal,
                                 mailbox.mix.load(std::memory_order_acquire),
                                 wrapper->selectedView(),
                                 notifications,
                                 mailbox.lastTweakedSelection.load(std::memory_order_acquire)};
        state.channel = {mailbox.channelPresent.load(std::memory_order_acquire),
                         mailbox.channelOn.load(std::memory_order_acquire),
                         mailbox.channelGain.load(std::memory_order_acquire),
                         mailbox.channelPan.load(std::memory_order_acquire),
                         mailbox.channelTrack.load(std::memory_order_acquire),
                         mailbox.channelMaxTrack.load(std::memory_order_acquire),
                         mailbox.channelPitch.load(std::memory_order_acquire),
                         mailbox.channelRange.load(std::memory_order_acquire)};
        wrapper->update(state, parameters, feedback);
        wrapper->updateRouting(io, buses);
        mailbox.view.store(wrapper->selectedView(), std::memory_order_release);
        const auto selected = wrapper->selectedParameterIdentity();
        mailbox.selection.store(selected ? (std::uint64_t(selected->second) << 32) | selected->first : 0,
                                std::memory_order_release);
    }
    NativeVst3EditorAttachment attachmentState() const noexcept
    {
        if (!native)
            return NativeVst3EditorAttachment::failed; // Controls-only is a different proof below.
        if (builtIn)
            // Owned JUCE components have no third-party VST3 attachment call.
            // Observe their actual hierarchy, desktop peer and visible editor.
            return native->getParentComponent() == wrapper.get() && native->getPeer() &&
                           wrapper->isShowing() &&
                           (wrapper->selectedView() != PluginWrapperView::plugin || native->isShowing())
                       ? NativeVst3EditorAttachment::attached
                       : NativeVst3EditorAttachment::pending;
        return nativeVst3EditorAttachment(*native);
    }
    bool hasNativeEditor() const noexcept
    {
        return native != nullptr;
    }
    bool controlsPeerReady() const noexcept
    {
        return !native && getPeer() && isShowing() && wrapper && getContentComponent() == wrapper.get() &&
               wrapper->isShowing() && wrapper->getPeer() == getPeer();
    }
    void closeButtonPressed() override
    {
        wrapper->finishGesture();
        setVisible(false);
        if (changed)
            changed(); // User-close is hidden/existing, not a fabricated host-close acknowledgement.
    }
};
class WorkerApp final : public juce::JUCEApplication,
                        private juce::Timer,
                        private juce::AudioProcessorListener
{
    PluginIpcResources resources;
    ipc::Shared *shared = nullptr;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    // Immutable after CONTROL construction; cleared after the DSP thread stops.
    NativeProcessor *nativeEffect = nullptr;
    std::unique_ptr<EditorWindow> editor;
    PluginEditorAvailability editorAvailability = PluginEditorAvailability::unknown;
    std::optional<PluginEditorOutcome> pendingEditorAttachment;
    PluginWrapperView requestedWrapperView = PluginWrapperView::plugin;
    std::uint64_t requestedWrapperSelection = 0;
    unsigned wrapperGeneration = 0, wrapperUpdatedAt = 0;
    std::vector<PluginWrapperParameterView> wrapperParameters;
    Position position;
    juce::MidiBuffer midi;
    // Fixed preparation-owned packing storage for nondefault native input order.
    // Packet input[0:2] stays app stereo; input[2:4] stays independent detector.
    std::array<std::array<float, ipc::frames>, ipc::outputChannels> selectedBusAudio{};
    bool packSelectedBuses = false;
    int selectedMainOffset = 0, selectedAuxiliaryOffset = 0;
    // Output channel offsets inside the processing buffer: main stereo output, then each extra bus.
    int mainOutputOffset = 0, extraOutputs = 0;
    std::array<int, PluginBusRequest::maximumExtraOutputBuses> extraOutputOffsets{};
    int preparedInputBusCount = 0, preparedOutputBusCount = 0;
    std::array<int, PluginBusCatalogue::maximumPerDirection> preparedInputChannels{},
        preparedOutputChannels{};
    bool parameterBridgeAvailable() const noexcept
    {
        return nativeEffect || plugin->getVST3Client();
    }
    bool parameterInfo(std::uint32_t id, juce::AudioPluginExtensions::VST3Client::ParameterInfo &info) const
    {
        if (nativeEffect)
            return nativeEffect->parameterInfo(id, info);
        auto *bridge = plugin->getVST3Client();
        return bridge && bridge->getNativeParameterInfo(id, info);
    }
    bool parameterValue(std::uint32_t id, float &value) const
    {
        if (nativeEffect)
            return nativeEffect->parameterValue(id, value);
        auto *bridge = plugin->getVST3Client();
        return bridge && bridge->getNativeParameterValue(id, value);
    }
    bool setParameterEventsForNextBlock(
        juce::Span<const juce::AudioPluginExtensions::VST3Client::ParameterEvent> events, int frames) noexcept
    {
        if (nativeEffect)
            return nativeEffect->setParameterEventsForNextBlock(events, frames);
        auto *bridge = plugin->getVST3Client();
        return bridge && bridge->setParameterEventsForNextBlock(events, frames);
    }
    bool flushParameterChangesForState() noexcept
    {
        return nativeEffect ? nativeEffect->flushParameterChangesForState()
                            : juce::viberaFlushVST3Parameters(*plugin);
    }
    bool preparedLayoutMatches(const juce::AudioProcessor::BusesLayout &requested) const
    {
        if (!nativeEffect)
            return nativeVst3BusesMatch(*plugin, requested);
        // Validate the owned processor itself. A native instance has no SDK
        // component or cached external arrangement to consult.
        if (dynamic_cast<synth::SynthPlugin *>(plugin.get()) || dynamic_cast<drums::DrumSynthPlugin *>(plugin.get()))
            return requested.inputBuses.isEmpty() && requested.outputBuses.size() == 1 &&
                   requested.outputBuses[0] == juce::AudioChannelSet::stereo() &&
                   plugin->getBusesLayout() == requested;
        return requested.inputBuses.size() == 1 && requested.outputBuses.size() == 1 &&
               requested.inputBuses[0] == juce::AudioChannelSet::stereo() &&
               requested.outputBuses[0] == juce::AudioChannelSet::stereo() &&
               plugin->getBusCount(true) == 1 && plugin->getBusCount(false) == 1 &&
               plugin->getBusesLayout() == requested && plugin->getTotalNumInputChannels() == 2 &&
               plugin->getTotalNumOutputChannels() == 2;
    }
    bool stateEnvelopeMatches(const juce::XmlElement &xml) const
    {
        if (!nativeEffect)
            return xml.hasTagName("VST3PluginState");
        return nativeEffect->acceptsStateEnvelope(xml);
    }
    bool preparedBusShapeMatches() const noexcept
    {
        if (plugin->getBusCount(true) != preparedInputBusCount ||
            plugin->getBusCount(false) != preparedOutputBusCount)
            return false;
        for (int i = 0; i < preparedInputBusCount; ++i)
            if (plugin->getChannelCountOfBus(true, i) != preparedInputChannels[size_t(i)])
                return false;
        for (int i = 0; i < preparedOutputBusCount; ++i)
            if (plugin->getChannelCountOfBus(false, i) != preparedOutputChannels[size_t(i)])
                return false;
        return true;
    }
    unsigned stateSerial = 0;
    std::array<std::atomic<bool>, 8192> saveParameters;
    struct GestureMirror
    {
        // High word schema, low word native ID; zero schema is unavailable.
        std::atomic<std::uint64_t> identity{0};
        std::atomic<float> value{0};
        std::atomic<bool> active{false}, changed{false}, manualDirty{false};
    };
    std::array<GestureMirror, ipc::parameters> gestureMirrors;
    std::atomic<bool> parameterInfoDirty{true};
    unsigned catalogueSerial = 0;
    bool catalogueDirty = false;
    std::optional<PreparedParameterSchema> preparedParameters;
    const juce::String schemaTraceDirectory =
        juce::SystemStats::getEnvironmentVariable("VIBERA_SCHEMA_TRACE_DIRECTORY", {});
    const juce::String schemaTraceIdentity =
        juce::String(pluginProcessId()) + "-" + juce::Uuid().toString();
    unsigned schemaRefreshSerial = 0, tracedRefreshSerial = 0;
    std::vector<std::pair<unsigned, juce::String>> schemaTraces;
    void captureSchemaTrace()
    {
        if (!juce::File::isAbsolutePath(schemaTraceDirectory))
            return;
        const auto &catalogue = shared->catalogue;
        if (!catalogue.generation || tracedRefreshSerial == schemaRefreshSerial)
            return;
        juce::var document(new juce::DynamicObject);
        auto *object = document.getDynamicObject();
        object->setProperty("generation", int(catalogue.generation));
        object->setProperty("refresh", int(schemaRefreshSerial));
        object->setProperty("gestureEpoch", int(shared->gestureEpoch.load()));
        object->setProperty("reportedCount", catalogue.reportedCount);
        object->setProperty("error", catalogue.error);
        object->setProperty("diagnostic", juce::String::fromUTF8(catalogue.diagnostic));
        juce::Array<juce::var> entries;
        for (int i = 0; i < catalogue.count; ++i)
        {
            const auto &entry = catalogue.entries[size_t(i)];
            juce::var item(new juce::DynamicObject);
            auto *fields = item.getDynamicObject();
            fields->setProperty("id", juce::int64(entry.id));
            fields->setProperty("flags", juce::int64(entry.flags));
            fields->setProperty("availability", juce::int64(entry.availability));
            fields->setProperty("steps", entry.stepCount);
            fields->setProperty("unit", entry.unit);
            fields->setProperty("defaultValue", entry.defaultValue);
            fields->setProperty("name", juce::String::fromUTF8(entry.name));
            fields->setProperty("label", juce::String::fromUTF8(entry.label));
            fields->setProperty("category", int(plugin->getParameters()[i]->getCategory()));
            entries.add(std::move(item));
        }
        object->setProperty("parameters", entries);
        schemaTraces.emplace_back(schemaRefreshSerial, juce::JSON::toString(document));
        tracedRefreshSerial = schemaRefreshSerial;
    }
    void flushSchemaTraces()
    {
        // Opt-in diagnostics only. Never hold the DSP control pause across file I/O.
        if (schemaTraces.empty() || controlRequested.load())
            return;
        const juce::File folder(schemaTraceDirectory);
        for (const auto &[refresh, text] : schemaTraces)
            folder.getChildFile(schemaTraceIdentity + "-refresh-" + juce::String(refresh) + ".json")
                .replaceWithText(text);
        schemaTraces.clear();
    }
    using NativeValues = std::vector<std::pair<std::uint32_t, float>>;
    NativeValues captureValues;
    std::map<std::uint32_t, float> manualValues;
    bool snapshotNativeValues(NativeValues &values)
    {
        values.clear();
        if (!parameterBridgeAvailable() || plugin->getParameters().size() > ipc::parameters)
            return false;
        std::set<std::uint32_t> ids;
        for (const auto &[id, previous] : manualValues)
        {
            juce::AudioPluginExtensions::VST3Client::ParameterInfo info;
            float current = 0;
            if (!ids.insert(id).second || !parameterInfo(id, info))
                return false;
            if ((info.flags & ((1u << 1) | (1u << 4))) == 0 && parameterValue(id, current))
                values.emplace_back(id, current);
        }
        return true;
    }
    bool applyNativeValues(const NativeValues &values)
    {
        if (nativeEffect)
        {
            for (const auto &[id, value] : values)
            {
                auto *parameter = nativeEffect->parameterForId(id);
                juce::AudioPluginExtensions::VST3Client::ParameterInfo info;
                if (!parameter || !nativeEffect->parameterInfo(id, info) ||
                    (info.flags & ((1u << 1) | (1u << 4) | (1u << 15))) || !std::isfinite(value) ||
                    value < 0 || value > 1)
                    return false;
                parameter->setValue(value);
            }
            return true;
        }
        std::map<std::uint32_t, juce::HostedAudioProcessorParameter *> lookup;
        for (auto *p : plugin->getParameters())
            if (auto *hosted = dynamic_cast<juce::HostedAudioProcessorParameter *>(p))
            {
                const auto text = hosted->getParameterID().toStdString();
                std::uint32_t id = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
                if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size())
                    lookup.emplace(id, hosted);
            }
        auto *bridge = plugin->getVST3Client();
        if (!bridge)
            return false;
        // Program selectors first; subsequent controls restore the selected program's exact host values.
        for (int pass = 0; pass < 2; ++pass)
            for (const auto &[id, value] : values)
            {
                const auto parameter = lookup.find(id);
                juce::AudioPluginExtensions::VST3Client::ParameterInfo info;
                if (parameter == lookup.end() || !bridge->getNativeParameterInfo(id, info) ||
                    (info.flags & ((1u << 1) | (1u << 4))) != 0)
                    return false;
                const bool program = (info.flags & (1u << 15)) != 0;
                if (program != (pass == 0))
                    continue;
                float current = 0;
                if (program && bridge->getNativeParameterValue(id, current) && current == value)
                    continue;
                parameter->second->setValue(
                    value); // Rearm the next real packet; no synthetic audio/cursor advance.
            }
        return true;
    }
    static bool readNativeValues(const juce::XmlElement &node, NativeValues &values)
    {
        if (node.getIntAttribute("version") != 1 || node.getNumChildElements() > ipc::parameters)
            return false;
        std::set<std::uint32_t> ids;
        for (auto *entry : node.getChildIterator())
        {
            const auto text = entry->getStringAttribute("id").toStdString();
            std::uint32_t id = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
            const double value = entry->getDoubleAttribute("value", std::numeric_limits<double>::quiet_NaN());
            if (!entry->hasTagName("PARAM") || parsed.ec != std::errc{} ||
                parsed.ptr != text.data() + text.size() || !ids.insert(id).second || !std::isfinite(value) ||
                value < 0 || value > 1)
                return false;
            values.emplace_back(id, float(value));
        }
        return true;
    }
    std::array<juce::AudioPluginExtensions::VST3Client::ParameterEvent, ipc::parameterEvents> timedParameters;
    static_assert(ipc::parameterEvents == juce::AudioPluginExtensions::VST3Client::maximumParameterEvents);
    juce::AudioProcessorParameter *parameterForID(std::uint32_t id)
    {
        if (nativeEffect)
            return nativeEffect->parameterForId(id);
        for (auto *p : plugin->getParameters())
            if (auto *hosted = dynamic_cast<juce::HostedAudioProcessorParameter *>(p))
            {
                const auto text = hosted->getParameterID().toStdString();
                std::uint32_t native = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), native);
                if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && native == id)
                    return hosted;
            }
        return nullptr; // Message thread only; DSP uses the prepared native bridge.
    }
    PreparedParameterSchema readParameterSchema()
    {
        PreparedParameterSchema next;
        next.reportedCount = plugin->getParameters().size();
        if (!parameterBridgeAvailable())
            next.error = 1;
        else if (next.reportedCount > ipc::parameters)
            next.error = 2;
        else
        {
            next.parameters.reserve(size_t(next.reportedCount));
            std::set<std::uint32_t> ids;
            for (auto *p : plugin->getParameters())
            {
                auto *hosted = dynamic_cast<juce::HostedAudioProcessorParameter *>(p);
                if (!hosted)
                {
                    next.error = 3;
                    next.diagnostic = ("Non-native hosted parameter: " + p->getName(128)).toStdString();
                    break;
                }
                const auto text = hosted->getParameterID().toStdString();
                std::uint32_t id = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
                juce::AudioPluginExtensions::VST3Client::ParameterInfo info;
                float current = 0;
                juce::String reason;
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                    reason = "Non-numeric native ID";
                else if (!ids.insert(id).second)
                    reason = "Duplicate native ID";
                else if (!parameterInfo(id, info))
                    reason = "Native identity/info unavailable";
                else if (info.stepCount < 0 || info.stepCount == std::numeric_limits<int>::max())
                    reason = "Invalid native step count " + juce::String(info.stepCount);
                if (reason.isNotEmpty())
                {
                    next.error = 3;
                    next.diagnostic =
                        (reason + "; parameter=" + p->getName(128) + "; id=" + juce::String(juce::int64(id)) +
                         "; flags=" + juce::String(info.flags))
                            .toStdString();
                    break;
                }
                auto &item = next.parameters.emplace_back();
                item.category = int(p->getCategory());
                auto &entry = item.info;
                entry.id = id;
                entry.flags = info.flags;
                entry.stepCount = info.stepCount;
                entry.unit = info.unitId;
                entry.availability = 0;
                const bool hasDefault =
                    std::isfinite(info.defaultValue) && info.defaultValue >= 0 && info.defaultValue <= 1;
                const bool hasCurrent = parameterValue(id, current) &&
                                        std::isfinite(current) && current >= 0 && current <= 1;
                entry.defaultValue = hasDefault ? info.defaultValue : 0;
                entry.value = hasCurrent ? current : 0;
                if (hasCurrent)
                    entry.availability |= 1u;
                if (hasDefault)
                    entry.availability |= 2u;
                p->getName(128).copyToUTF8(entry.name, sizeof(entry.name));
                p->getLabel().copyToUTF8(entry.label, sizeof(entry.label));
            }
        }
        if (next.error)
            next.parameters.clear();
        return next;
    }
    void refreshParameterSchema()
    {
        ++schemaRefreshSerial;
        auto next = readParameterSchema();
        if (preparedParameters)
        {
            if (next.error || !preparedParameters->sameSchema(next))
                ++shared->catalogueGeneration;
            // Metadata notifications break gesture continuity even if automation
            // authority is unchanged. Do not resurrect an earlier Last tweaked.
            shared->gestureEpoch.fetch_add(1, std::memory_order_acq_rel);
        }
        preparedParameters = std::move(next);
        updateParameterFlags();
        catalogueDirty = true;
    }
    void publishParameters(bool freshlyPrepared = false)
    {
        auto &catalogue = shared->catalogue;
        int free = 0;
        if (!catalogue.status.compare_exchange_strong(free, 1, std::memory_order_acquire))
            return;
        std::optional<PreparedParameterSchema> fresh;
        if (!freshlyPrepared)
        {
            fresh = readParameterSchema();
            if (!preparedParameters || !preparedParameters->sameSchema(*fresh))
            {
                // A provider may change info before its notification is handled.
                // Reconcile under the existing control/DSP pause before publishing.
                parameterInfoDirty = true;
                catalogueDirty = true;
                catalogue.status.store(0, std::memory_order_release);
                return;
            }
        }
        const auto &snapshot = fresh ? *fresh : *preparedParameters;
        catalogue.serial = shared->catalogueRequest.load();
        catalogue.generation = shared->catalogueGeneration.load();
        catalogue.reportedCount = snapshot.reportedCount;
        catalogue.error = snapshot.error;
        catalogue.count = int(snapshot.parameters.size());
        juce::String(snapshot.diagnostic).copyToUTF8(catalogue.diagnostic, sizeof(catalogue.diagnostic));
        for (size_t i = 0; i < snapshot.parameters.size(); ++i)
            catalogue.entries[i] = snapshot.parameters[i].info;
        catalogueSerial = catalogue.serial;
        catalogueDirty = false;
        catalogue.status.store(2, std::memory_order_release);
        captureSchemaTrace(); // Message-thread capture; current values/opaque state are excluded.
    }
    void applyParameterControl()
    {
        auto &request = shared->parameterControl;
        if (request.status.load(std::memory_order_acquire) != 1)
            return;
        request.result = 0;
        request.actual = 0;
        request.previousValue = 0;
        request.beforeKnown = 0;
        if (request.patchCount != 0)
        {
            // Native owned patches only: every identity, value and BEFORE is
            // validated on CONTROL while the existing DSP pause is held.
            if(!nativeEffect || request.patchCount>request.patch.size()) request.result=2;
            else if(request.generation!=shared->catalogueGeneration.load()) request.result=1;
            std::array<juce::AudioProcessorParameter*,ipc::patchValues> targets{}; // One slot per patch value.
            for(unsigned i=0;request.result==0 && i<request.patchCount;++i)
            {
                auto &entry=request.patch[i];
                targets[i]=nativeEffect->parameterForId(entry.id);
                juce::AudioPluginExtensions::VST3Client::ParameterInfo info;
                if(!targets[i] || !nativeEffect->parameterInfo(entry.id,info) ||
                    (info.flags&((1u<<1)|(1u<<4)|(1u<<15))) || !std::isfinite(entry.value) ||
                    entry.value<0 || entry.value>1 || !nativeEffect->parameterValue(entry.id,entry.previousValue))
                    request.result=3;
                for(unsigned j=0;j<i;++j) if(request.patch[j].id==entry.id) request.result=3;
            }
            if(request.result==0)
                if(auto *ownedSynth=dynamic_cast<synth::SynthPlugin *>(plugin.get()))
                {
                    std::array<std::pair<std::uint32_t,float>,ipc::patchValues> values{};
                    for(unsigned i=0;i<request.patchCount;++i) values[i]={request.patch[i].id,request.patch[i].value};
                    if(!ownedSynth->validateParameterPatch({values.data(),request.patchCount})) request.result=3;
                }
            if(request.result==0)
            {
                const juce::ScopedValueSetter<bool> suppress(hostParameterWrite,true);
                nativeEffect->beginParameterPatch();
                for(unsigned i=0;i<request.patchCount;++i) targets[i]->beginChangeGesture();
                for(unsigned i=0;i<request.patchCount;++i) targets[i]->setValueNotifyingHost(request.patch[i].value);
                for(unsigned i=0;i<request.patchCount;++i)
                {
                    auto &entry=request.patch[i];
                    if(!nativeEffect->parameterValue(entry.id,entry.value)) request.result=3;
                    else manualValues[entry.id]=entry.value;
                    targets[i]->endChangeGesture();
                }
                nativeEffect->endParameterPatch();
                request.actual=request.patch[0].value;request.previousValue=request.patch[0].previousValue;
                request.beforeKnown=request.result==0?1:0;
            }
            request.status.store(2,std::memory_order_release);
            return;
        }
        auto *parameter = parameterForID(request.id);
        float previousValue = 0;
        juce::AudioPluginExtensions::VST3Client::ParameterInfo info;
        if (request.generation != shared->catalogueGeneration.load())
            request.result = 1;
        else if (!parameter || !parameterInfo(request.id, info) || (info.flags & ((1u << 1) | (1u << 4))) ||
                 !parameterValue(request.id, previousValue))
            request.result = 2;
        else if (!std::isfinite(request.requested) || request.requested < 0 || request.requested > 1)
            request.result = 3;
        else if(auto *ownedSynth=dynamic_cast<synth::SynthPlugin *>(plugin.get()); ownedSynth &&
            ![&]{const std::pair<std::uint32_t,float> value{request.id,request.requested};return ownedSynth->validateParameterPatch({&value,1});}())
            request.result=3;
        else
        {
            const juce::ScopedValueSetter<bool> suppress(hostParameterWrite, true);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(request.requested);
            if (!parameterValue(request.id, request.actual))
                request.result = 3;
            else if (request.actual != request.requested)
                parameter->setValueNotifyingHost(request.actual);
            parameter->endChangeGesture();
            if (request.result == 0)
            {
                manualValues[request.id] = request.actual;
                if (std::isfinite(previousValue) && previousValue >= 0 && previousValue <= 1)
                {
                    request.previousValue = previousValue;
                    request.beforeKnown = 1;
                }
            }
        }
        request.status.store(2, std::memory_order_release);
    }
    void updateParameterFlags()
    {
        auto &parameters = plugin->getParameters();
        for (size_t i = 0; i < saveParameters.size(); ++i)
        {
            saveParameters[i] =
                i >= size_t(parameters.size()) || (unsigned(parameters[int(i)]->getCategory()) >> 16) != 2;
            auto &mirror = gestureMirrors[i];
            mirror.identity = 0;
            mirror.active = false;
            mirror.changed = false;
            mirror.manualDirty = false;
            if (i >= size_t(parameters.size()))
                continue;
            auto *hosted = dynamic_cast<juce::HostedAudioProcessorParameter *>(parameters[int(i)]);
            if (!hosted || !parameterBridgeAvailable())
                continue;
            const auto text = hosted->getParameterID().toStdString();
            std::uint32_t id = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
            juce::AudioPluginExtensions::VST3Client::ParameterInfo info;
            float value = 0;
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                !parameterInfo(id, info) ||
                (info.flags & ((1u << 1) | (1u << 4) | (1u << 15))) ||
                !parameterValue(id, value))
                continue;
            mirror.value = value;
            mirror.identity.store((std::uint64_t(shared->catalogueGeneration.load()) << 32) | id,
                                  std::memory_order_release);
        }
    }
    void sendGesture(int index, PluginGesturePhase phase, float value) noexcept
    {
        if (!shared || hostParameterWrite || index < 0 || index >= ipc::parameters || !std::isfinite(value) ||
            value < 0 || value > 1 || !shared->gestureNotifications.load(std::memory_order_acquire))
            return;
        const auto identity = gestureMirrors[size_t(index)].identity.load(std::memory_order_acquire);
        const auto generation = unsigned(identity >> 32);
        if (!generation || generation != shared->catalogueGeneration.load(std::memory_order_acquire))
            return;
        shared->gestures.push(
            {std::uint32_t(identity), generation, shared->gestureEpoch.load(std::memory_order_acquire), value,
             phase,
             phase == PluginGesturePhase::end ? std::uint64_t(juce::Time::getHighResolutionTicks()) : 0});
    }
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor *, int index) override
    {
        if (hostParameterWrite || index < 0 || index >= ipc::parameters)
            return;
        auto &mirror = gestureMirrors[size_t(index)];
        mirror.active = true;
        mirror.changed = false;
        sendGesture(index, PluginGesturePhase::begin, mirror.value.load());
    }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor *, int index) override
    {
        if (hostParameterWrite || index < 0 || index >= ipc::parameters)
            return;
        auto &mirror = gestureMirrors[size_t(index)];
        sendGesture(index, PluginGesturePhase::end, mirror.value.load());
        if (mirror.active.exchange(false) && mirror.changed.load())
            mirror.manualDirty.store(true, std::memory_order_release);
    }
    void audioProcessorParameterChanged(juce::AudioProcessor *, int index, float value) override
    {
        if (index >= 0 && index < ipc::parameters && std::isfinite(value) && value >= 0 && value <= 1)
        {
            auto &mirror = gestureMirrors[size_t(index)];
            mirror.value = value;
            if (!hostParameterWrite && mirror.active.load())
            {
                mirror.changed = true;
                sendGesture(index, PluginGesturePhase::value, value);
            }
        }
        if (shared &&
            (index < 0 || index >= int(saveParameters.size()) || saveParameters[size_t(index)].load()))
            ++shared->parameterRevision;
    }
    void audioProcessorChanged(juce::AudioProcessor *, const ChangeDetails &details) override
    {
        if (!shared)
            return;
        if (details.parameterInfoChanged)
        {
            parameterInfoDirty = true;
            // Invalidate cached opaque state immediately; catalogue publication
            // occurs later on CONTROL and cannot be the first freshness signal.
            ++shared->parameterRevision;
        }
        if (details.programChanged || details.nonParameterStateChanged)
            ++shared->parameterRevision;
    }
    void collectManualGestures()
    {
        // Native manual values belong to state capture even with notifications
        // disabled. Drain before replacing schema mirrors; never mutate a map
        // in a processor callback.
        for (auto &mirror : gestureMirrors)
            if (mirror.manualDirty.exchange(false, std::memory_order_acquire))
            {
                const auto identity = mirror.identity.load(std::memory_order_acquire);
                if (unsigned(identity >> 32) == shared->catalogueGeneration.load())
                    manualValues[std::uint32_t(identity)] = mirror.value.load();
            }
    }
    std::atomic<bool> controlRequested{false}, controlIdle{true};
    std::atomic<unsigned> flushRequest{0}, flushReady{0}, flushRevision{0};
    std::atomic<unsigned> capturedStateReady{0}; // CONTROL publishes; actual resumed DSP acknowledges.
    std::atomic<bool> flushFailed{false};
    unsigned captureFlushSerial = 0, stateFlushTicket = 0;
    class Dsp final : public juce::Thread
    {
        WorkerApp &owner;

      public:
        explicit Dsp(WorkerApp &app) : Thread("VST3 isolated DSP"), owner(app) {}
        void run() override
        {
            juce::ScopedNoDenormals denormals;
            owner.shared->dspPhase.store(ipc::DspPhase::waiting, std::memory_order_release);
            owner.shared->dspHeartbeat.fetch_add(1, std::memory_order_relaxed);
            // Publish readiness only from the successfully entered DSP loop.
            owner.shared->status.store(1, std::memory_order_release);
            while (!threadShouldExit())
            {
                owner.shared->dspHeartbeat.fetch_add(1, std::memory_order_relaxed);
                if (owner.controlRequested.load())
                {
                    owner.controlIdle = false;
                    const auto ticket = owner.flushRequest.load(std::memory_order_acquire);
                    if (ticket != owner.flushReady.load())
                    {
                        const auto revision = owner.shared->parameterRevision.load();
                        owner.flushFailed = !owner.flushParameterChangesForState();
                        owner.flushRevision = revision;
                        owner.flushReady.store(ticket, std::memory_order_release);
                    }
                    owner.shared->dspPhase.store(ipc::DspPhase::controlPaused, std::memory_order_release);
                    owner.controlIdle = true;
                    sleep(1);
                    continue;
                }
                owner.shared->dspPhase.store(ipc::DspPhase::waiting, std::memory_order_release);
                owner.controlIdle = false;
                if (!owner.controlRequested.load())
                {
                    // State completion includes actually leaving the capture
                    // pause. Release/acquire also transfers all snapshot bytes.
                    owner.shared->stateReady.store(owner.capturedStateReady.load(std::memory_order_acquire),
                                                   std::memory_order_release);
                    owner.processPackets();
                }
                owner.controlIdle = true;
                owner.resources.wait(10); // worker waits; host only signals
            }
            owner.shared->dspPhase.store(ipc::DspPhase::stopped, std::memory_order_release);
        }
    };
    std::unique_ptr<Dsp> dsp;
    PluginParentProcess parent;
    void fail(const juce::String &error)
    {
        if (shared)
        {
            error.copyToUTF8(shared->error, sizeof(shared->error));
            shared->status.store(-1, std::memory_order_release);
        }
        setApplicationReturnValue(1);
        quit();
    }

  public:
    const juce::String getApplicationName() override
    {
        return "VibeRa VST3 Worker";
    }
    const juce::String getApplicationVersion() override
    {
        return "1";
    }
    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }
    void initialise(const juce::String &) override
    {
#if JUCE_WINDOWS
        // The worker has no visible window, so Windows may treat it as a background process and
        // apply EcoQoS power throttling (lower clocks / efficiency cores), which makes it miss the
        // host's per-block deadlines. Audio processing must never be throttled: opt out.
        PROCESS_POWER_THROTTLING_STATE throttling{};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        throttling.StateMask = 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
#endif
        formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
        auto args = getCommandLineParameterArray();
        if (args.size() == 3 && args[0] == "--scan")
        {
            juce::OwnedArray<juce::PluginDescription> found;
            formats.getFormat(0)->findAllTypesForFile(found, args[1]);
            juce::XmlElement result("VST3_SCAN");
            for (auto *type : found)
                result.addChildElement(type->createXml().release());
            const bool ok = result.writeTo(juce::File(args[2])) && !found.isEmpty();
            setApplicationReturnValue(ok ? 0 : 2);
            quit();
            return;
        }
        if (args.size() != 3 || args[0] != "--host")
        {
            fail("Invalid worker arguments");
            return;
        }
        if (!resources.open(args[1]))
        {
            fail("Cannot open IPC mapping or audio notification");
            return;
        }
        shared = resources.data();
        if (!shared || shared->signature != ipc::magic || shared->blockSize < 16 ||
            shared->blockSize > ipc::frames || !std::isfinite(shared->rate) || shared->rate < 8000 ||
            shared->rate > 192000 || !shared->requestedBuses.valid() ||
            (shared->instrument && shared->requestedBuses.auxiliaryChannels != 0) ||
            (!shared->instrument && shared->requestedBuses.extraOutputBuses != 0))
        {
            fail("Invalid IPC configuration");
            return;
        }
        parent.open(shared->parentPid);
        auto xml = juce::XmlDocument::parse(juce::File(args[2]));
        juce::PluginDescription description;
        if (!xml || !description.loadFromXml(*xml) ||
            (description.pluginFormatName != "VST3" && description.pluginFormatName != "VibeRa"))
        {
            fail("Invalid supported plugin descriptor");
            return;
        }
        juce::String error;
        if (description.pluginFormatName == "VibeRa")
        {
            if (bool(shared->instrument) != description.isInstrument ||
                (!effects::isNativeDescription(description) && !isNativeInstrument(description)))
            {
                fail("Invalid registered native effect descriptor");
                return;
            }
            if (synth::isNativeDescription(description))
                plugin = std::make_unique<synth::SynthPlugin>();
            else if (drums::isNativeDescription(description))
                plugin = std::make_unique<drums::DrumSynthPlugin>();
            else
                plugin = effects::createNativeEffect(description);
            nativeEffect = dynamic_cast<NativeProcessor *>(plugin.get());
            if (auto *instrument=dynamic_cast<synth::SynthPlugin *>(plugin.get()); instrument && instrument->contentError().isNotEmpty())
            {
                fail(instrument->contentError());
                return;
            }
            if (!nativeEffect)
            {
                fail("Cannot create registered native effect");
                return;
            }
        }
        else
            plugin = formats.createPluginInstance(description, shared->rate, shared->blockSize, error);
        if (!plugin)
        {
            fail(error);
            return;
        }
        plugin->enableAllBuses();
        auto layout = plugin->getBusesLayout();
        const auto requested = shared->requestedBuses;
        if (nativeEffect && requested != PluginBusRequest{})
        {
            fail("pluginIo.unsupportedLayout: Native effects require their owned stereo input/output buses");
            return;
        }
        preparedInputBusCount = layout.inputBuses.size();
        preparedOutputBusCount = layout.outputBuses.size();
        const bool absentDefaultInput = preparedInputBusCount == 0 && requested.mainInputBus == 0 &&
                                        requested.mainInputChannels == 2 &&
                                        requested.mainInputMonoSource == 0;
        if (preparedInputBusCount > PluginBusCatalogue::maximumPerDirection || preparedOutputBusCount < 1 ||
            preparedOutputBusCount > PluginBusCatalogue::maximumPerDirection ||
            requested.mainOutputBus >= preparedOutputBusCount ||
            1 + requested.extraOutputBuses > preparedOutputBusCount ||
            (!absentDefaultInput && requested.mainInputBus >= preparedInputBusCount) ||
            (requested.auxiliaryChannels != 0 && requested.auxiliaryInputBus >= preparedInputBusCount))
        {
            fail("pluginIo.busUnavailable: Requested native VST3 bus does not exist or exceeds bounded "
                 "catalogue capacity");
            return;
        }
        {
            // Main output plus the first extraOutputBuses other buses, all stereo; channel
            // offsets follow JUCE's ascending bus order inside the processing buffer.
            int extras = 0, offset = 0;
            extraOutputs = requested.extraOutputBuses;
            for (int i = 0; i < preparedOutputBusCount; ++i)
            {
                const bool main = i == requested.mainOutputBus;
                const bool extra = !main && extras < requested.extraOutputBuses;
                layout.outputBuses.set(i, main || extra ? juce::AudioChannelSet::stereo()
                                                        : juce::AudioChannelSet::disabled());
                if (main)
                    mainOutputOffset = offset;
                if (extra)
                    extraOutputOffsets[size_t(extras++)] = offset;
                if (main || extra)
                    offset += 2;
            }
        }
        for (int i = 0; i < preparedInputBusCount; ++i)
            layout.inputBuses.set(i,
                                  i == requested.mainInputBus
                                      ? (requested.mainInputChannels == 1 ? juce::AudioChannelSet::mono()
                                                                          : juce::AudioChannelSet::stereo())
                                  : i == requested.auxiliaryInputBus
                                      ? (requested.auxiliaryChannels == 1 ? juce::AudioChannelSet::mono()
                                                                          : juce::AudioChannelSet::stereo())
                                      : juce::AudioChannelSet::disabled());
        const int mainChannels = absentDefaultInput ? 0 : requested.mainInputChannels;
        const auto layoutMatches = [&]
        {
            return plugin->getTotalNumOutputChannels() == 2 + 2 * requested.extraOutputBuses &&
                   plugin->getTotalNumInputChannels() == mainChannels + requested.auxiliaryChannels &&
                   plugin->getBusesLayout() == layout;
        };
        if (!plugin->setBusesLayout(layout) || !layoutMatches())
        {
            fail("pluginIo.unsupportedLayout: VST3 does not support selected stereo output/mono-stereo main "
                 "and auxiliary input layout");
            return;
        }
        NativeValues restoredValues;
        if (nativeEffect && (shared->stateSize < 0 || shared->stateSize > ipc::stateBytes))
        {
            fail("Invalid saved native effect state size");
            return;
        }
        if (shared->stateSize > 0 && shared->stateSize <= ipc::stateBytes)
        {
            if (auto xml = juce::AudioProcessor::getXmlFromBinary(shared->state.data(), shared->stateSize))
                if (auto *node = xml->getChildByName("VibeRaParameterValues");
                    node && !readNativeValues(*node, restoredValues))
                {
                    fail("Invalid saved native parameter snapshot");
                    return;
                }
            if (nativeEffect)
            {
                if (!nativeEffect->restoreState(shared->state.data(), shared->stateSize))
                {
                    fail("Invalid saved native effect state");
                    return;
                }
            }
            else
                plugin->setStateInformation(shared->state.data(), shared->stateSize);
        }
        if (!plugin->setBusesLayout(layout) || !layoutMatches())
        {
            fail("pluginIo.restoredLayout: Restored VST3 state requires unsupported buses");
            return;
        }
        position.rate = shared->rate;
        plugin->setPlayHead(&position);
        plugin->setRateAndBufferSizeDetails(shared->rate, shared->blockSize);
        plugin->prepareToPlay(shared->rate, shared->blockSize);
        if (!applyNativeValues(restoredValues))
        {
            fail("Saved native parameter target is unavailable");
            return;
        }
        // Preparation and restored controls may change a native bus layout.
        // Freeze only the requested, revalidated layout before DSP starts; never
        // let a mutated plugin count become authority for fixed IPC storage.
        if (!layoutMatches() || !preparedLayoutMatches(layout))
        {
            fail("pluginIo.preparedLayout: Prepared VST3 bus layout changed from the requested layout");
            return;
        }
        shared->negotiatedBuses = requested;
        shared->negotiatedMainInputs = mainChannels;
        // All metadata is prepared before status=ready publishes the worker.
        auto &busCatalogue = shared->busCatalogue;
        busCatalogue.inputCount = preparedInputBusCount;
        busCatalogue.outputCount = preparedOutputBusCount;
        for (const bool input : {true, false})
        {
            const int count = input ? preparedInputBusCount : preparedOutputBusCount;
            for (int i = 0; i < count; ++i)
            {
                auto &row = busCatalogue.entries[size_t(input ? i : preparedInputBusCount + i)];
                row.index = i;
                row.input = input;
                auto *bus = plugin->getBus(input, i);
                if (!bus)
                {
                    fail("pluginIo.catalogue: Prepared native VST3 bus metadata disappeared");
                    return;
                }
                row.defaultChannels = bus->getDefaultLayout().size();
                row.actualChannels = (input ? layout.inputBuses : layout.outputBuses)[i].size();
                bus->getName().copyToUTF8(row.name, sizeof(row.name));
                (input ? preparedInputChannels : preparedOutputChannels)[size_t(i)] = row.actualChannels;
            }
        }
        if (!busCatalogue.valid() || !preparedBusShapeMatches())
        {
            fail("pluginIo.catalogue: Prepared native VST3 bus catalogue is inconsistent");
            return;
        }
        packSelectedBuses = requested.mainInputBus != 0 || requested.mainOutputBus != 0 ||
                            requested.mainInputChannels != 2 || requested.mainInputMonoSource != 0 ||
                            requested.extraOutputBuses != 0;
        selectedMainOffset =
            requested.auxiliaryChannels && requested.auxiliaryInputBus < requested.mainInputBus
                ? requested.auxiliaryChannels
                : 0;
        selectedAuxiliaryOffset =
            mainChannels && requested.mainInputBus < requested.auxiliaryInputBus ? mainChannels : 0;
        midi.ensureSize(ipc::events * 16);
        for (const auto &[id, value] : restoredValues)
            manualValues[id] = value;
        refreshParameterSchema();
        parameterInfoDirty = false;
        publishParameters(true);
        plugin->addListener(this);
        shared->latency.store(PluginLatencyStamp{plugin->getLatencySamples(), 1}.packed(),
                              std::memory_order_release);
        publishNativeOutputMeter(); // Initial native notProcessed / external unsupported, before readiness.
        // JUCE hasEditor() may construct a vendor view. Finish this potentially
        // slow CONTROL initialization before the DSP loop advertises readiness;
        // initial controls must not spend their runtime deadline waiting here.
        try
        {
            editorAvailability = plugin->hasEditor() ? PluginEditorAvailability::available
                                                     : PluginEditorAvailability::unsupported;
        }
        catch (...)
        {
            editorAvailability = PluginEditorAvailability::unknown;
        }
        publishEditorState();
        startTimer(5);
        dsp = std::make_unique<Dsp>(*this);
        if (!dsp->startRealtimeThread(
                juce::Thread::RealtimeOptions().withPriority(8).withApproximateAudioProcessingTime(
                    shared->blockSize, shared->rate)))
            if (!dsp->startThread(juce::Thread::Priority::high))
            {
                fail("Cannot start isolated VST3 DSP thread; worker is not ready");
                return;
            }
    }
    PluginEditorWindowState publishEditorState()
    {
        const auto previous =
            PluginEditorWindowState::unpack(shared->editorWindow.load(std::memory_order_relaxed));
        const bool exists = editor && editor->getPeer();
        const bool minimised = exists && editor->isMinimised();
        PluginEditorWindowState next{true, exists, exists && editor->isShowing() && !minimised,
                                     minimised, editorAvailability, previous.revision};
        if (next.packed() != previous.packed())
            ++next.revision;
        shared->editorWindow.store(next.packed(), std::memory_order_release);
        return next;
    }
    std::optional<unsigned> editorCompletionClock()
    {
        try
        {
            return pluginEditorMilliseconds();
        }
        catch (...)
        {
            // No reply with fabricated/stale timing. Host sees owned worker
            // failure and keeps effect uncertainty for dispatched native work.
            fail("Native editor performance clock unavailable; native outcome is unknown");
            return {};
        }
    }
    void completeEditorRequest(PluginEditorOutcome result)
    {
        auto& request = shared->editorControl;
        request.outcome = result;
        request.window = publishEditorState().packed();
        const auto completedAt = editorCompletionClock();
        if (!completedAt)
            return; // Partial fields remain unpublished while phase is nativeRunning.
        request.completedAt = *completedAt;
        request.phase.store(PluginEditorPhase::reply, std::memory_order_release);
    }
    void finishEditorAttachment()
    {
        if (!pendingEditorAttachment)
            return;
        if (editor && !editor->hasNativeEditor())
        {
            // An editorless host-control peer is not a third-party VST3 attachment.
            // Require the actual visible wrapper and its supported requested view.
            if (!editor->controlsPeerReady())
            {
                const auto now = editorCompletionClock();
                if (!now || *now - shared->editorControl.admittedAt <= pluginEditorTimeoutMs)
                    return;
            }
            auto result = *pendingEditorAttachment;
            pendingEditorAttachment.reset();
            if (!editor->controlsPeerReady() || requestedWrapperView == PluginWrapperView::plugin)
            {
                editor.reset();
                editorAvailability = PluginEditorAvailability::creationFailed;
                result = PluginEditorOutcome::creationFailed;
            }
            else
            {
                syncWrapper(true);
                if (!editor->selectView(requestedWrapperView, requestedWrapperSelection))
                    result = PluginEditorOutcome::invalidRequest;
                const auto window = publishEditorState();
                if (!window.exists || !window.visible ||
                    window.availability != PluginEditorAvailability::controlsOnly)
                    result = PluginEditorOutcome::nativeFailure;
            }
            completeEditorRequest(result);
            return;
        }
        const auto state = editor ? editor->attachmentState() : NativeVst3EditorAttachment::failed;
        if (state == NativeVst3EditorAttachment::pending)
        {
            const auto now = editorCompletionClock();
            if (!now)
                return;
            if (*now - shared->editorControl.admittedAt <= pluginEditorTimeoutMs)
                return; // No success merely because a blank host wrapper became visible.
        }
        auto result = *pendingEditorAttachment;
        pendingEditorAttachment.reset();
        if (state != NativeVst3EditorAttachment::attached)
        {
            editor.reset();
            editorAvailability = PluginEditorAvailability::creationFailed;
            result = PluginEditorOutcome::creationFailed;
        }
        else
        {
            syncWrapper(true);
            if (!editor->selectView(requestedWrapperView, requestedWrapperSelection))
                result = PluginEditorOutcome::invalidRequest;
            const auto window = publishEditorState();
            if (!window.exists || !window.visible)
                result = PluginEditorOutcome::nativeFailure;
        }
        completeEditorRequest(result);
    }
    void serviceEditorRequest()
    {
        publishEditorState(); // Includes native minimise/hide/show changes between requests.
        if (pendingEditorAttachment)
        {
            finishEditorAttachment();
            return;
        }
        auto &request = shared->editorControl;
        auto expected = PluginEditorPhase::pending;
        if (!request.phase.compare_exchange_strong(expected, PluginEditorPhase::nativeRunning,
                                                   std::memory_order_acquire))
            return;
        auto result = PluginEditorOutcome::invalidRequest;
        try
        {
            if (pluginEditorOpens(request.action))
            {
                requestedWrapperView =
                    request.action == PluginEditorAction::settings     ? PluginWrapperView::settings
                    : request.action == PluginEditorAction::parameters ? PluginWrapperView::parameters
                                                                       : PluginWrapperView::plugin;
                requestedWrapperSelection = request.wrapperSelection;
                const bool existing = bool(editor);
                if (!editor)
                {
                    // Query once: probing can create/destroy a native editor.
                    // A later false answer must not turn a claimed-but-failed
                    // native interface into an editorless fallback.
                    const bool hasNativeEditor = plugin->hasEditor();
                    if (!hasNativeEditor && requestedWrapperView == PluginWrapperView::plugin)
                    {
                        editorAvailability = PluginEditorAvailability::unsupported;
                        result = PluginEditorOutcome::unsupported;
                    }
                    else if (!hasNativeEditor)
                    {
                        editor = std::make_unique<EditorWindow>(*plugin, NativeEditorOwner{},
                                                                shared->wrapperControl,
                                                                [this] { publishEditorState(); });
                        editorAvailability = PluginEditorAvailability::controlsOnly;
                    }
                    else
                    {
                        NativeEditorOwner native(plugin->createEditorAndMakeActive());
                        if (!native)
                        {
                            editorAvailability = PluginEditorAvailability::creationFailed;
                            result = PluginEditorOutcome::creationFailed;
                        }
                        else
                        {
                            editor = std::make_unique<EditorWindow>(*plugin, std::move(native),
                                                                    shared->wrapperControl,
                                                                    [this] { publishEditorState(); });
                            editorAvailability = PluginEditorAvailability::available;
                        }
                    }
                }
                if (editor)
                {
                    if (!editor->hasNativeEditor() && requestedWrapperView == PluginWrapperView::plugin)
                    {
                        completeEditorRequest(PluginEditorOutcome::unsupported);
                        return; // Keep the existing controls-only peer and its selected view.
                    }
                    editor->setMinimised(false);
                    editor->setVisible(true);
                    bringWindowToFront(*editor); // The host granted this process the foreground.
                    pendingEditorAttachment =
                        existing ? PluginEditorOutcome::broughtToFront : PluginEditorOutcome::opened;
                    finishEditorAttachment();
                    return; // Attachment may still be pending; the physical request remains owned.
                }
            }
            else if (request.action == PluginEditorAction::close)
            {
                editor.reset();
                result = PluginEditorOutcome::closed; // Closing an already closed window is idempotent.
            }
        }
        catch (...)
        {
            pendingEditorAttachment.reset();
            editor.reset();
            editorAvailability = PluginEditorAvailability::creationFailed;
            result = PluginEditorOutcome::nativeFailure;
        }
        completeEditorRequest(result);
    }
    void syncWrapper(bool force = false)
    {
        if (!editor || !preparedParameters)
            return;
        const auto now = juce::Time::getMillisecondCounter();
        if (!force && now - wrapperUpdatedAt < 33)
            return;
        wrapperUpdatedAt = now;
        const auto generation = shared->catalogueGeneration.load(std::memory_order_acquire);
        if (!generation)
            return;
        if (wrapperGeneration != generation)
        {
            wrapperParameters.clear();
            for (const auto &entry : preparedParameters->parameters)
            {
                const auto &p = entry.info;
                const bool writable = (p.availability & 1u) &&
                                      !(p.flags & ((1u << 1) | (1u << 4) | (1u << 15)));
                wrapperParameters.push_back({p.id, generation, juce::String::fromUTF8(p.name),
                    juce::String::fromUTF8(p.label), p.value, p.defaultValue, writable,
                    bool(p.availability & 2u), bool(p.flags & 1u) && writable});
            }
            wrapperGeneration = generation;
        }
        // Metadata is owned by this message thread. Refresh only the selected
        // native controller value, never the whole catalogue from DSP or IPC.
        if (const auto selected = editor->selectedParameter())
            if (selected->second == generation)
                if (parameterBridgeAvailable())
                    for (auto &p : wrapperParameters)
                        if (p.id == selected->first)
                        {
                            float value = 0;
                            if (parameterValue(p.id, value) &&
                                std::isfinite(value) && value >= 0 && value <= 1)
                                p.value = value;
                            break;
                        }
        editor->updateWrapper(wrapperParameters, bool(shared->instrument),
                              shared->gestureNotifications.load(std::memory_order_acquire),
                              {shared->requestedBuses.mainInputBus, shared->requestedBuses.mainOutputBus,
                               shared->requestedBuses.mainInputChannels,
                               shared->requestedBuses.mainInputMonoSource},
                              shared->busCatalogue);
    }
    void publishNativeOutputMeter()
    {
        if (!nativeEffect)
            shared->nativeOutputMeter.publish(PluginNativeMeterStatus::unsupported);
        else
        {
            const auto left = nativeEffect->peak(0), right = nativeEffect->peak(1);
            const auto *instrument=dynamic_cast<synth::SynthPlugin *>(plugin.get());
            const auto synthState=instrument?instrument->telemetry():0;
            if (left == -1.f || right == -1.f)
                shared->nativeOutputMeter.publish(PluginNativeMeterStatus::notProcessed,0,0,synthState);
            else
                shared->nativeOutputMeter.publish(PluginNativeMeterStatus::ready, left, right,synthState);
        }
    }
    void timerCallback() override
    {
        if (shared->status.load(std::memory_order_acquire) == -1)
        {
            setApplicationReturnValue(1);
            quit();
            return;
        }
        if (shared->shutdown.load() || parent.exited())
        {
            quit();
            return;
        }
        publishNativeOutputMeter(); // CONTROL only, including when no editor exists or is visible.
        serviceEditorRequest();     // Independent of catalogue/state mailboxes and never called by DSP.
        if (shared->status.load(std::memory_order_acquire) == -1)
            return; // Clock failure requested shutdown; do not start other control work.
        if (parameterInfoDirty.load())
        {
            controlRequested = true;
            if (!controlIdle.load())
                return;
            collectManualGestures();
            parameterInfoDirty = false;
            refreshParameterSchema();
            publishParameters(true);
            controlRequested = false;
        }
        if ((catalogueDirty || shared->catalogueRequest.load() != catalogueSerial) &&
            shared->catalogue.status.load() == 0)
            publishParameters();
        flushSchemaTraces();
        if (shared->stateRequest.load() == stateSerial)
            applyParameterControl();
        collectManualGestures();
        syncWrapper(); // Message thread only; no change to audio packet processing.
        auto request = shared->stateRequest.load(std::memory_order_acquire);
        if (request != stateSerial)
        {
            controlRequested = true;
            if (captureFlushSerial != request)
            {
                if (!snapshotNativeValues(captureValues))
                {
                    fail("Cannot snapshot native control values");
                    return;
                }
                captureFlushSerial = request;
                stateFlushTicket = flushRequest.fetch_add(1) + 1;
                resources.signal();
                return;
            }
            if (!controlIdle.load() || flushReady.load(std::memory_order_acquire) != stateFlushTicket)
                return;
            if (flushFailed.load())
            {
                fail("Plugin rejected parameter-only state flush");
                return;
            }
            if (flushRevision.load() != shared->parameterRevision.load())
            {
                if (!snapshotNativeValues(captureValues))
                {
                    fail("Cannot refresh native control snapshot");
                    return;
                }
                stateFlushTicket = flushRequest.fetch_add(1) + 1;
                resources.signal();
                return;
            }
            juce::MemoryBlock state;
            plugin->getStateInformation(state);
            auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
            if (!xml || !stateEnvelopeMatches(*xml))
            {
                fail(nativeEffect ? "Unsupported native effect state envelope"
                                  : "Unsupported isolated VST3 state envelope");
                return;
            }
            if (auto *previous = xml->getChildByName("VibeRaParameterValues"))
                xml->removeChildElement(previous, true);
            auto *values = xml->createNewChildElement("VibeRaParameterValues");
            values->setAttribute("version", 1);
            for (const auto &[id, value] : captureValues)
            {
                auto *entry = values->createNewChildElement("PARAM");
                entry->setAttribute("id", juce::String(juce::int64(id)));
                entry->setAttribute("value", double(value));
            }
            juce::AudioProcessor::copyXmlToBinary(*xml, state);
            if (!applyNativeValues(captureValues))
            {
                fail("Captured native parameter target became unavailable");
                return;
            }
            if (state.getSize() > ipc::stateBytes)
            {
                fail("Plugin state exceeds 16 MiB preview limit");
                return;
            }
            shared->stateSize = int(state.getSize());
            std::memcpy(shared->state.data(), state.getData(), state.getSize());
            shared->stateParameterRevision = shared->parameterRevision.load();
            stateSerial = request;
            capturedStateReady.store(request, std::memory_order_release);
            controlRequested = false;
            resources.signal();
        }
    }
    void processPackets()
    {

        for (int work = 0; work < ipc::slots; ++work)
        {
            ipc::Block *next = nullptr;
            for (auto &candidate : shared->blocks)
                if (candidate.status.load(std::memory_order_acquire) == 1 &&
                    (!next || candidate.sequence < next->sequence))
                    next = &candidate;
            if (!next)
                break;
            auto &block = *next;
            if (block.sequence < shared->oldestNeeded.load(std::memory_order_acquire))
            {
                shared->parameterEventsRejected.fetch_add(
                    std::uint64_t(juce::jlimit(0, ipc::parameterEvents, block.parameterCount)),
                    std::memory_order_relaxed);
                if (shared->instrument && block.midiCount > 0)
                    shared->panic = 1;
                ++shared->processed;
                block.status.store(0, std::memory_order_release);
                continue;
            }
            int expected = 1;
            if (!block.status.compare_exchange_strong(expected, 2, std::memory_order_acquire))
                continue;
            if (block.count <= 0 || block.count > ipc::frames || block.midiCount < 0 ||
                block.midiCount > ipc::events)
            {
                constexpr char message[] = "Invalid audio packet";
                std::memcpy(shared->error, message, sizeof(message));
                shared->status.store(-1, std::memory_order_release);
                return;
            }
            if (!preparedBusShapeMatches())
            {
                constexpr char message[] =
                    "pluginIo.dynamicLayout: Dynamic VST3 bus mapping changed; prepare a new matching worker";
                std::memcpy(shared->error, message, sizeof(message));
                shared->status.store(-1, std::memory_order_release);
                return;
            }
            float *pointers[ipc::outputChannels]{block.output[0], block.output[1], block.input[2], block.input[3]};
            const int audioChannels =
                std::max(2 + 2 * extraOutputs, shared->negotiatedMainInputs + shared->negotiatedBuses.auxiliaryChannels);
            if (packSelectedBuses)
            {
                for (int c = 0; c < audioChannels; ++c)
                {
                    pointers[c] = selectedBusAudio[size_t(c)].data();
                    std::fill_n(pointers[c], block.count, 0.f);
                }
                for (int c = 0; c < shared->negotiatedMainInputs; ++c)
                    for (int n = 0; n < block.count; ++n)
                        pointers[selectedMainOffset + c][n] =
                            shared->negotiatedMainInputs == 2                  ? block.input[c][n]
                            : shared->negotiatedBuses.mainInputMonoSource == 1 ? block.input[0][n]
                            : shared->negotiatedBuses.mainInputMonoSource == 2
                                ? block.input[1][n]
                                : .5f * block.input[0][n] + .5f * block.input[1][n];
                for (int c = 0; c < shared->negotiatedBuses.auxiliaryChannels; ++c)
                    std::copy_n(block.input[c + 2], block.count, pointers[selectedAuxiliaryOffset + c]);
            }
            else
                for (int c = 0; c < 2; ++c)
                    std::copy_n(block.input[c], block.count, block.output[c]);
            juce::AudioBuffer<float> audio(pointers, audioChannels, block.count);
            midi.clear();
            if (shared->panic.exchange(0))
            {
                midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                ++shared->midiResetGeneration;
            }
            for (int i = 0; i < block.midiCount; ++i)
            {
                auto &e = block.midi[size_t(i)];
                if (e.size > 0 && e.size <= 3 && e.frame >= 0 && e.frame < block.count)
                    midi.addEvent(e.bytes, e.size, e.frame);
            }
            position.seconds = block.seconds;
            position.tempo = block.tempo;
            position.ppq = block.ppq;
            position.playing = block.playing;
            position.numerator = block.numerator;
            position.denominator = block.denominator;
            bool validParameters =
                block.parameterCount >= 0 && block.parameterCount <= ipc::parameterEvents &&
                (block.parameterCount == 0 ||
                 (block.parameterGeneration != 0 &&
                  block.parameterGeneration == shared->catalogueGeneration.load(std::memory_order_acquire)));
            if (validParameters)
                for (int i = 0; i < block.parameterCount; ++i)
                {
                    const auto &event = block.parameters[size_t(i)];
                    timedParameters[size_t(i)] = {event.id, event.frame, event.value};
                }
            if (validParameters && parameterBridgeAvailable())
                validParameters = setParameterEventsForNextBlock(
                    {timedParameters.data(), size_t(block.parameterCount)}, block.count);
            else if (block.parameterCount > 0)
                validParameters = false;
            if (!validParameters && parameterBridgeAvailable())
                setParameterEventsForNextBlock({}, block.count);
            if (!validParameters)
                shared->parameterEventsRejected.fetch_add(std::uint64_t(std::max(1, block.parameterCount)));
            else
            {
                shared->parameterEventsApplied.fetch_add(std::uint64_t(block.parameterCount));
                if (block.parameterCount > 0)
                    ++shared->parameterRevision;
            }
            shared->dspPhase.store(ipc::DspPhase::processing, std::memory_order_release);
            plugin->processBlock(audio, midi);
            if (packSelectedBuses)
            {
                for (int c = 0; c < 2; ++c)
                    std::copy_n(pointers[mainOutputOffset + c], block.count, block.output[c]);
                for (int k = 0; k < extraOutputs; ++k)
                    for (int c = 0; c < 2; ++c)
                        std::copy_n(pointers[extraOutputOffsets[size_t(k)] + c], block.count,
                                    block.output[2 + 2 * k + c]);
            }
            shared->dspPhase.store(ipc::DspPhase::waiting, std::memory_order_release);
            const auto latency = PluginLatencyStamp::unpack(shared->latency.load(std::memory_order_relaxed))
                                     .next(plugin->getLatencySamples());
            if (!latency.valid(shared->blockSize))
            {
                constexpr char message[] = "Native VST3 latency exceeds prepared host delay capacity";
                std::memcpy(shared->error, message, sizeof(message));
                shared->status.store(-1, std::memory_order_release);
                return;
            }
            block.latency = latency;
            shared->latency.store(latency.packed(), std::memory_order_release);
            ++shared->processed;
            block.status.store(3, std::memory_order_release);
        }
    }
    void shutdown() override
    {
        stopTimer();
        if (dsp)
        {
            dsp->signalThreadShouldExit();
            if (resources.data())
                resources.signal();
            dsp->stopThread(10000);
            dsp.reset();
        }
        editor.reset();
        if (shared)
            publishEditorState();
        if (plugin)
        {
            plugin->removeListener(this);
            plugin->releaseResources();
            plugin->setPlayHead(nullptr);
            nativeEffect = nullptr;
            plugin.reset();
        }
        if (shared)
        {
            if (shared->status.load() != -1)
                shared->status = 2;
            shared = nullptr;
        }
        parent.reset();
        resources.reset();
    }
};
} // namespace
START_JUCE_APPLICATION(WorkerApp)
