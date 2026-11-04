// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// VibeRa Drum Synth as a built-in instrument: hosted parameters (automation and history),
// a validated XML state envelope, MIDI triggering, editor pads that audition through a
// lock-free request mask, and a symmetric editor with a live visual.
#include "DrumSynth.h"
#include "DrumRegistry.h"
#include "../NativeProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdlib>

namespace vibera::drums
{
class DrumSynthPlugin final : public juce::AudioPluginInstance, public NativeProcessor
{
  public:
    static constexpr int captureSize = 4096;
    class Parameter final : public juce::HostedAudioProcessorParameter
    {
        const effects::ParameterSpec &spec_;
        const juce::uint32 id_;
        const juce::String textId_;
        juce::NormalisableRange<float> range_;
        std::atomic<float> value_;

      public:
        Parameter(const effects::ParameterSpec &spec, juce::uint32 id)
            : HostedAudioProcessorParameter(1), spec_(spec), id_(id), textId_(juce::int64(id)),
              range_(spec.minimum, spec.maximum, spec.interval, spec.skew), value_(range_.convertTo0to1(spec.initial))
        {
        }
        juce::uint32 id() const noexcept { return id_; }
        const effects::ParameterSpec &spec() const noexcept { return spec_; }
        const juce::NormalisableRange<float> &range() const noexcept { return range_; }
        juce::String getParameterID() const override { return textId_; }
        float getValue() const override { return value_.load(std::memory_order_relaxed); }
        void setValue(float v) override
        {
            if (std::isfinite(v))
                value_.store(std::clamp(v, 0.f, 1.f), std::memory_order_relaxed);
        }
        float nativeValue() const noexcept { return range_.convertFrom0to1(getValue()); }
        float getDefaultValue() const override { return range_.convertTo0to1(spec_.initial); }
        juce::String getName(int length) const override
        {
            const juce::String text(spec_.name);
            return length > 0 ? text.substring(0, length) : text;
        }
        juce::String getLabel() const override { return spec_.unit; }
        juce::String getText(float v, int length) const override
        {
            const juce::String text(range_.convertFrom0to1(std::clamp(std::isfinite(v) ? v : getDefaultValue(), 0.f, 1.f)), 2);
            return length > 0 ? text.substring(0, length) : text;
        }
        float getValueForText(const juce::String &text) const override
        {
            char *end = nullptr;
            const auto trimmed = text.trim();
            const auto native = std::strtod(trimmed.toRawUTF8(), &end);
            if (trimmed.isEmpty() || !end || *end != '\0' || !std::isfinite(native))
                return getValue();
            return range_.convertTo0to1(std::clamp(float(native), spec_.minimum, spec_.maximum));
        }
    };

    DrumSynthPlugin() : AudioPluginInstance(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        const auto &specs = parameterSpecs();
        for (int i = 0; i < parameterCount; ++i)
        {
            auto parameter = std::make_unique<Parameter>(specs[std::size_t(i)], parameterBaseId + juce::uint32(i));
            parameters_[std::size_t(i)] = parameter.get();
            addHostedParameter(std::move(parameter));
        }
    }
    Parameter *parameter(int index) const noexcept
    {
        return index >= 0 && index < parameterCount ? parameters_[std::size_t(index)] : nullptr;
    }
    Parameter *findParameter(juce::uint32 id) const noexcept
    {
        return id >= parameterBaseId && id - parameterBaseId < juce::uint32(parameterCount) ? parameters_[id - parameterBaseId]
                                                                                           : nullptr;
    }
    // NativeProcessor.
    bool parameterInfo(juce::uint32 id, ParameterInfo &info) const override
    {
        const auto *p = findParameter(id);
        if (!p)
            return false;
        info = {};
        info.id = id;
        info.flags = 1;
        info.defaultValue = p->getDefaultValue();
        return true;
    }
    bool parameterValue(juce::uint32 id, float &value) const override
    {
        const auto *p = findParameter(id);
        if (!p)
            return false;
        value = p->getValue();
        return true;
    }
    juce::AudioProcessorParameter *parameterForId(juce::uint32 id) const noexcept override { return findParameter(id); }
    bool setParameterEventsForNextBlock(juce::Span<const ParameterEvent> events, int frames) noexcept override
    {
        eventCount_ = 0;
        eventFrames_ = 0;
        if (frames <= 0 || events.size() > events_.size())
            return false;
        int previous = -1;
        for (const auto &e : events)
        {
            if (!findParameter(e.id) || e.sampleOffset < previous || e.sampleOffset < 0 || e.sampleOffset >= frames ||
                !std::isfinite(e.value) || e.value < 0 || e.value > 1)
                return false;
            previous = e.sampleOffset;
        }
        std::copy(events.begin(), events.end(), events_.begin());
        eventCount_ = events.size();
        eventFrames_ = frames;
        return true;
    }
    bool flushParameterChangesForState() noexcept override { return true; }
    bool acceptsStateEnvelope(const juce::XmlElement &xml) const override
    {
        return xml.hasTagName("VIBERA_DRUM_SYNTH") && xml.getStringAttribute("schemaVersion") == "1";
    }
    float peak(int channel) const noexcept override { return peaks_[std::size_t(channel == 0 ? 0 : 1)].load(std::memory_order_relaxed); }
    bool restoreState(const void *data, int bytes) override
    {
        if (!data || bytes < 8 || bytes > 1024 * 1024)
            return false;
        const auto xml = getXmlFromBinary(data, bytes);
        if (!xml || !acceptsStateEnvelope(*xml) || xml->getNumAttributes() != 1)
            return false;
        std::array<float, parameterCount> candidate{};
        std::array<bool, parameterCount> seen{};
        int count = 0;
        // The host may append its parameter overlay (VibeRaParameterValues); it wins.
        const juce::XmlElement *overlay = nullptr;
        const auto read = [&](const juce::XmlElement &node, bool overlayEntry)
        {
            if (!node.hasTagName("PARAM") || node.getNumAttributes() != 2 || node.getNumChildElements() != 0)
                return false;
            const auto *p = findParameter(juce::uint32(node.getStringAttribute("id").getLargeIntValue()));
            if (!p || node.getStringAttribute("id") != p->getParameterID())
                return false;
            char *end = nullptr;
            const auto text = node.getStringAttribute("value");
            const double v = std::strtod(text.toRawUTF8(), &end);
            if (text.isEmpty() || !end || *end != '\0' || !std::isfinite(v) || v < 0 || v > 1)
                return false;
            const auto index = std::size_t(p->id() - parameterBaseId);
            if (!overlayEntry)
            {
                if (seen[index])
                    return false;
                seen[index] = true;
                ++count;
            }
            candidate[index] = float(v);
            return true;
        };
        for (auto *node = xml->getFirstChildElement(); node; node = node->getNextElement())
        {
            if (node->hasTagName("VibeRaParameterValues"))
            {
                if (overlay || node->getNumAttributes() != 1 || node->getStringAttribute("version") != "1")
                    return false;
                overlay = node;
                continue;
            }
            if (!read(*node, false))
                return false;
        }
        if (count != parameterCount)
            return false;
        if (overlay)
            for (auto *node = overlay->getFirstChildElement(); node; node = node->getNextElement())
                if (!read(*node, true))
                    return false;
        for (int i = 0; i < parameterCount; ++i)
            parameters_[std::size_t(i)]->setValue(candidate[std::size_t(i)]);
        return true;
    }
    // Editor pads: request a hit on the next block (lock-free).
    void auditionPad(int drum, float velocity = .9f) noexcept
    {
        if (drum >= 0 && drum < drumCount)
        {
            padVelocity_.store(velocity, std::memory_order_relaxed);
            padRequests_.fetch_or(1u << drum, std::memory_order_release);
        }
    }
    float drumLevel(int drum) const noexcept { return levels_[std::size_t(std::clamp(drum, 0, drumCount - 1))].load(std::memory_order_relaxed); }
    void latestSamples(float *out, int count) const noexcept
    {
        count = std::min(count, captureSize);
        const auto end = captureWrite_.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
            out[i] = capture_[std::size_t((end - std::uint32_t(count) + std::uint32_t(i)) % captureSize)].load(std::memory_order_relaxed);
    }
    std::array<float, parameterCount> nativeValues() const noexcept
    {
        std::array<float, parameterCount> values{};
        for (int i = 0; i < parameterCount; ++i)
            values[std::size_t(i)] = parameters_[std::size_t(i)]->nativeValue();
        return values;
    }

    // AudioProcessor.
    void fillInPluginDescription(juce::PluginDescription &d) const override { d = nativeDescription(); }
    const juce::String getName() const override { return "VibeRa Drum Synth"; }
    void prepareToPlay(double rate, int) override
    {
        engine_.prepare(rate);
        eventCount_ = 0;
        eventFrames_ = 0;
        peaks_[0].store(-1, std::memory_order_relaxed);
        peaks_[1].store(-1, std::memory_order_relaxed);
        prepared_ = true;
    }
    void releaseResources() override
    {
        engine_.reset();
        prepared_ = false;
    }
    void reset() override { engine_.reset(); }
    bool isBusesLayoutSupported(const BusesLayout &layout) const override
    {
        return layout.inputBuses.isEmpty() && layout.outputBuses.size() == 1 &&
               layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi) override
    {
        const juce::ScopedNoDenormals noDenormals;
        const int frames = buffer.getNumSamples();
        buffer.clear();
        if (!prepared_ || buffer.getNumChannels() != 2 || (eventCount_ != 0 && eventFrames_ != frames))
        {
            eventCount_ = eventFrames_ = 0;
            return;
        }
        auto values = nativeValues();
        if (const auto pads = padRequests_.exchange(0, std::memory_order_acquire))
            for (int d = 0; d < drumCount; ++d)
                if (pads & (1u << d))
                    engine_.noteOn(padNote(d), padVelocity_.load(std::memory_order_relaxed), values);
        auto *left = buffer.getWritePointer(0), *right = buffer.getWritePointer(1);
        int cursor = 0;
        std::size_t event = 0;
        auto message = midi.cbegin();
        while (cursor < frames)
        {
            while (event < eventCount_ && events_[event].sampleOffset <= cursor)
            {
                const auto &change = events_[event++];
                auto *p = findParameter(change.id);
                p->setValue(change.value);
                values[std::size_t(change.id - parameterBaseId)] = p->nativeValue();
            }
            while (message != midi.cend() && (*message).samplePosition <= cursor)
            {
                const auto m = (*message).getMessage();
                if (m.isNoteOn())
                    engine_.noteOn(m.getNoteNumber(), m.getFloatVelocity(), values);
                ++message;
            }
            int end = frames;
            if (event < eventCount_)
                end = std::min(end, events_[event].sampleOffset);
            if (message != midi.cend())
                end = std::min(end, std::max(cursor + 1, (*message).samplePosition));
            end = std::max(end, cursor + 1);
            engine_.render(left + cursor, right + cursor, end - cursor, values[std::size_t(parameterCount - 1)]);
            cursor = end;
        }
        eventCount_ = eventFrames_ = 0;
        float l = 0, r = 0;
        auto write = captureWrite_.load(std::memory_order_relaxed);
        for (int i = 0; i < frames; ++i)
        {
            l = std::max(l, std::abs(left[i]));
            r = std::max(r, std::abs(right[i]));
            capture_[std::size_t(write++ % captureSize)].store(.5f * (left[i] + right[i]), std::memory_order_relaxed);
        }
        captureWrite_.store(write, std::memory_order_release);
        peaks_[0].store(l, std::memory_order_relaxed);
        peaks_[1].store(r, std::memory_order_relaxed);
        for (int d = 0; d < drumCount; ++d)
            levels_[std::size_t(d)].store(engine_.drumLevel(d), std::memory_order_relaxed);
    }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default kit"; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &destination) override
    {
        juce::XmlElement xml("VIBERA_DRUM_SYNTH");
        xml.setAttribute("schemaVersion", 1);
        for (const auto *p : parameters_)
        {
            auto *node = xml.createNewChildElement("PARAM");
            node->setAttribute("id", p->getParameterID());
            node->setAttribute("value", double(p->getValue()));
        }
        copyXmlToBinary(xml, destination);
    }
    void setStateInformation(const void *data, int bytes) override { (void)restoreState(data, bytes); }

  private:
    std::array<Parameter *, parameterCount> parameters_{};
    Engine engine_;
    std::array<ParameterEvent, juce::AudioPluginExtensions::VST3Client::maximumParameterEvents> events_{};
    std::size_t eventCount_ = 0;
    int eventFrames_ = 0;
    bool prepared_ = false;
    std::atomic<std::uint32_t> padRequests_{0};
    std::atomic<float> padVelocity_{.9f};
    std::array<std::atomic<float>, 2> peaks_{{-1.f, -1.f}};
    std::array<std::atomic<float>, drumCount> levels_{};
    std::array<std::atomic<float>, captureSize> capture_{};
    std::atomic<std::uint32_t> captureWrite_{0};
};

// Symmetric editor: header with the master level, a display of the selected drum (its
// rendered shape) beside the live output, a 2x4 pad grid that flashes on hits, and the
// four knobs of the selected drum.
class DrumSynthEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
    static inline const juce::Colour background{0xff24323c}, backgroundTop{0xff31424f}, card{0xff364753},
        border{0xff4f6370}, accent{0xff5fe6cf}, warm{0xffffb35c}, text{0xfff2f7f9}, muted{0xffb6c7d0},
        plate{0xff283842};
    class Look final : public juce::LookAndFeel_V4
    {
      public:
        void drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float position, float start, float end,
                              juce::Slider &) override
        {
            auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(6.f);
            const float d = std::min(bounds.getWidth(), bounds.getHeight());
            bounds = bounds.withSizeKeepingCentre(d, d);
            const auto c = bounds.getCentre();
            const float r = d * .5f, angle = start + position * (end - start);
            juce::Path track, active;
            track.addCentredArc(c.x, c.y, r, r, 0, start, end, true);
            active.addCentredArc(c.x, c.y, r, r, 0, start, angle, true);
            g.setColour(plate.brighter(.05f));
            g.strokePath(track, juce::PathStrokeType(5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(accent);
            g.strokePath(active, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            auto face = bounds.reduced(9.f);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5d717d), face.getTopLeft(), juce::Colour(0xff34444f),
                                                   face.getBottomRight(), false));
            g.fillEllipse(face);
            const float pr = std::max(1.f, r - 15.f);
            g.setColour(text);
            g.drawLine(c.x + std::sin(angle) * pr * .45f, c.y - std::cos(angle) * pr * .45f, c.x + std::sin(angle) * pr,
                       c.y - std::cos(angle) * pr, 2.5f);
        }
    } look_;
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        DrumSynthPlugin::Parameter *parameter = nullptr;
        bool gesture = false;
    };
    DrumSynthPlugin &plugin_;
    int selected_ = 0;
    std::array<Knob, parametersPerDrum> knobs_;
    Knob master_;
    std::array<juce::Rectangle<int>, drumCount> pads_;
    juce::Rectangle<int> display_, knobArea_;
    std::array<float, drumCount> flash_{};
    std::vector<float> scope_ = std::vector<float>(2048), preview_ = std::vector<float>(512);
    std::array<float, parameterCount> previewFor_{};
    int previewDrum_ = -1;

    void bind(Knob &k, DrumSynthPlugin::Parameter *p, const juce::String &caption)
    {
        if (k.gesture && k.parameter)
            k.parameter->endChangeGesture();
        k.gesture = false;
        k.parameter = p;
        const auto &spec = p->spec();
        k.slider.setNormalisableRange(juce::NormalisableRange<double>(spec.minimum, spec.maximum, spec.interval, spec.skew));
        k.slider.setDoubleClickReturnValue(true, spec.initial);
        const juce::String unit(spec.unit);
        k.slider.setTextValueSuffix(unit.isEmpty() ? "" : " " + unit);
        k.slider.setNumDecimalPlacesToDisplay(unit == "st" ? 1 : 2);
        k.slider.setValue(p->nativeValue(), juce::dontSendNotification);
        k.slider.setTooltip(juce::String(spec.name) + ". Double-click to reset.");
        k.label.setText(caption, juce::dontSendNotification);
    }
    void setup(Knob &k)
    {
        k.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 110, 24);
        k.slider.setLookAndFeel(&look_);
        k.slider.setColour(juce::Slider::textBoxTextColourId, accent);
        k.slider.setColour(juce::Slider::textBoxBackgroundColourId, plate);
        k.slider.setColour(juce::Slider::textBoxOutlineColourId, border);
        k.label.setJustificationType(juce::Justification::centred);
        k.label.setFont(juce::FontOptions(14.f, juce::Font::bold));
        k.label.setColour(juce::Label::textColourId, text);
        auto *knob = &k;
        k.slider.onDragStart = [knob]
        {
            if (knob->parameter && !knob->gesture)
            {
                knob->gesture = true;
                knob->parameter->beginChangeGesture();
            }
        };
        k.slider.onDragEnd = [knob]
        {
            if (knob->parameter && knob->gesture)
            {
                knob->parameter->endChangeGesture();
                knob->gesture = false;
            }
        };
        k.slider.onValueChange = [knob]
        {
            if (!knob->parameter)
                return;
            const bool single = !knob->gesture;
            if (single)
                knob->parameter->beginChangeGesture();
            knob->parameter->setValueNotifyingHost(knob->parameter->range().convertTo0to1(float(knob->slider.getValue())));
            if (single)
                knob->parameter->endChangeGesture();
        };
        addAndMakeVisible(k.slider);
        addAndMakeVisible(k.label);
    }
    void select(int drum)
    {
        selected_ = std::clamp(drum, 0, drumCount - 1);
        const char *captions[parametersPerDrum]{"Tune", "Decay", toneName(selected_), "Level"};
        for (int p = 0; p < parametersPerDrum; ++p)
            bind(knobs_[std::size_t(p)], plugin_.parameter(selected_ * parametersPerDrum + p), captions[p]);
        repaint();
    }
    void renderPreview()
    {
        // Offline render of the selected drum (0.6 s) with the current settings, for the display.
        const auto values = plugin_.nativeValues();
        if (values == previewFor_ && previewDrum_ == selected_)
            return;
        previewFor_ = values;
        previewDrum_ = selected_;
        Engine engine;
        constexpr double rate = 24000;
        engine.prepare(rate);
        engine.noteOn(padNote(selected_), 1.f, values);
        std::vector<float> l(size_t(rate * .6)), r(l.size());
        engine.render(l.data(), r.data(), int(l.size()), 0.f);
        const auto bin = l.size() / preview_.size();
        for (std::size_t i = 0; i < preview_.size(); ++i)
        {
            float m = 0;
            for (std::size_t k = i * bin; k < (i + 1) * bin; ++k)
                m = std::max(m, std::abs(l[k]));
            preview_[i] = m;
        }
    }
    void timerCallback() override
    {
        for (auto *k : {&knobs_[0], &knobs_[1], &knobs_[2], &knobs_[3], &master_})
            if (k->parameter && !k->gesture)
                k->slider.setValue(k->parameter->nativeValue(), juce::dontSendNotification);
        for (int d = 0; d < drumCount; ++d)
            flash_[std::size_t(d)] = std::max(plugin_.drumLevel(d), flash_[std::size_t(d)] * .8f);
        repaint(display_);
        for (const auto &pad : pads_)
            repaint(pad);
    }

  public:
    explicit DrumSynthEditor(DrumSynthPlugin &plugin) : AudioProcessorEditor(plugin), plugin_(plugin)
    {
        for (auto &k : knobs_)
            setup(k);
        setup(master_);
        bind(master_, plugin_.parameter(parameterCount - 1), "Master");
        select(0);
        setResizable(true, false);
        setResizeLimits(680, 640, 1400, 1100);
        setSize(820, 680);
        startTimerHz(30);
    }
    ~DrumSynthEditor() override
    {
        stopTimer();
        for (auto *k : {&knobs_[0], &knobs_[1], &knobs_[2], &knobs_[3], &master_})
        {
            if (k->gesture && k->parameter)
                k->parameter->endChangeGesture();
            k->slider.onDragStart = k->slider.onDragEnd = k->slider.onValueChange = nullptr;
            k->slider.setLookAndFeel(nullptr);
        }
    }
    int selectedDrum() const noexcept { return selected_; }
    void selectDrum(int drum) { select(drum); }
    void mouseDown(const juce::MouseEvent &e) override
    {
        for (int d = 0; d < drumCount; ++d)
            if (pads_[std::size_t(d)].contains(e.getPosition()))
            {
                select(d);
                plugin_.auditionPad(d, juce::jlimit(.3f, 1.f, 1.f - float(e.y - pads_[std::size_t(d)].getY()) / float(pads_[std::size_t(d)].getHeight()) * .5f));
            }
    }
    void paint(juce::Graphics &g) override
    {
        g.setGradientFill(juce::ColourGradient(backgroundTop, 0, 0, background, float(getWidth()), float(getHeight()), false));
        g.fillAll();
        g.setColour(accent);
        g.setFont(juce::FontOptions(11.f, juce::Font::bold));
        g.drawText("V I B E R A   /   I N S T R U M E N T S", 24, 12, 400, 20, juce::Justification::centredLeft);
        g.setColour(text);
        g.setFont(juce::FontOptions(30.f, juce::Font::bold));
        g.drawText("VibeRa Drum Synth", 22, 34, 500, 40, juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(juce::FontOptions(12.f));
        g.drawText("SYNTHESIZED KIT   /   8 VOICES   /   CLICK A PAD TO PLAY", 24, 76, 600, 20, juce::Justification::centredLeft);
        // Display: the selected drum's shape (left) and the live output (right).
        g.setColour(plate);
        g.fillRoundedRectangle(display_.toFloat(), 12.f);
        g.setColour(border);
        g.drawRoundedRectangle(display_.toFloat().reduced(.5f), 12.f, 1.f);
        auto inner = display_.toFloat().reduced(16.f, 14.f);
        auto left = inner.removeFromLeft(inner.getWidth() * .5f - 8.f);
        inner.removeFromLeft(16.f);
        renderPreview();
        g.setFont(juce::FontOptions(11.f, juce::Font::bold));
        g.setColour(muted);
        g.drawText(juce::String(drumName(selected_)).toUpperCase() + "  /  SHAPE", left.removeFromTop(18.f), juce::Justification::centredLeft);
        g.drawText("LIVE OUTPUT", inner.removeFromTop(18.f), juce::Justification::centredLeft);
        g.setColour(plate.brighter(.06f));
        g.fillRoundedRectangle(left, 6.f);
        g.fillRoundedRectangle(inner, 6.f);
        const float peak = std::max(1e-4f, *std::max_element(preview_.begin(), preview_.end()));
        juce::Path shape;
        shape.startNewSubPath(left.getX(), left.getCentreY());
        for (std::size_t i = 0; i < preview_.size(); ++i)
            shape.lineTo(left.getX() + float(i) / float(preview_.size()) * left.getWidth(),
                         left.getCentreY() - preview_[i] / peak * left.getHeight() * .45f);
        for (std::size_t i = preview_.size(); i-- > 0;)
            shape.lineTo(left.getX() + float(i) / float(preview_.size()) * left.getWidth(),
                         left.getCentreY() + preview_[i] / peak * left.getHeight() * .45f);
        shape.closeSubPath();
        g.setColour(accent.withAlpha(.35f));
        g.fillPath(shape);
        g.setColour(accent);
        g.strokePath(shape, juce::PathStrokeType(1.4f));
        plugin_.latestSamples(scope_.data(), int(scope_.size()));
        juce::Path wave;
        for (int x = 0; x <= int(inner.getWidth()); ++x)
        {
            const auto i = std::size_t(double(x) / inner.getWidth() * (scope_.size() - 1));
            const float y = inner.getCentreY() - juce::jlimit(-1.f, 1.f, scope_[i]) * inner.getHeight() * .46f;
            if (x == 0)
                wave.startNewSubPath(inner.getX(), y);
            else
                wave.lineTo(inner.getX() + float(x), y);
        }
        g.setColour(warm);
        g.strokePath(wave, juce::PathStrokeType(1.4f));
        // Pads.
        for (int d = 0; d < drumCount; ++d)
        {
            const auto r = pads_[std::size_t(d)].toFloat();
            const bool on = d == selected_;
            g.setColour(card);
            g.fillRoundedRectangle(r, 12.f);
            g.setColour(accent.withAlpha(.15f + .7f * juce::jlimit(0.f, 1.f, flash_[std::size_t(d)])));
            g.fillRoundedRectangle(r.reduced(4.f), 9.f);
            g.setColour(on ? accent : border);
            g.drawRoundedRectangle(r.reduced(.5f), 12.f, on ? 2.f : 1.f);
            g.setColour(text);
            g.setFont(juce::FontOptions(15.f, juce::Font::bold));
            g.drawText(drumName(d), r.withTrimmedBottom(r.getHeight() * .35f), juce::Justification::centred);
            g.setColour(muted);
            g.setFont(juce::FontOptions(11.f));
            static const char *notes[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const int n = padNote(d);
            g.drawText(juce::String(notes[n % 12]) + juce::String(n / 12 - 1), r.withTrimmedTop(r.getHeight() * .55f),
                       juce::Justification::centred);
        }
        // Knob cards.
        for (const auto &k : knobs_)
        {
            const auto c = k.slider.getBounds().getUnion(k.label.getBounds()).expanded(6, 6).toFloat();
            g.setColour(card);
            g.fillRoundedRectangle(c, 12.f);
            g.setColour(border);
            g.drawRoundedRectangle(c.reduced(.5f), 12.f, 1.f);
        }
    }
    void resized() override
    {
        const int w = getWidth(), h = getHeight();
        master_.label.setBounds(w - 130, 10, 110, 18);
        master_.slider.setBounds(w - 130, 28, 110, 80);
        auto area = juce::Rectangle<int>(20, 116, w - 40, h - 136);
        display_ = area.removeFromTop(juce::jlimit(150, 230, h / 4));
        area.removeFromTop(12);
        knobArea_ = area.removeFromBottom(juce::jlimit(150, 200, area.getHeight() / 2 - 6));
        area.removeFromBottom(12);
        const int gap = 10, pw = (area.getWidth() - 3 * gap) / 4, ph = (area.getHeight() - gap) / 2;
        for (int d = 0; d < drumCount; ++d)
            pads_[std::size_t(d)] = {area.getX() + (d % 4) * (pw + gap), area.getY() + (d / 4) * (ph + gap), pw, ph};
        const int kw = (knobArea_.getWidth() - 3 * gap) / 4;
        for (int p = 0; p < parametersPerDrum; ++p)
        {
            auto cell = juce::Rectangle<int>(knobArea_.getX() + p * (kw + gap), knobArea_.getY(), kw, knobArea_.getHeight()).reduced(6);
            knobs_[std::size_t(p)].label.setBounds(cell.removeFromTop(24));
            knobs_[std::size_t(p)].slider.setBounds(cell);
        }
    }
};
inline juce::AudioProcessorEditor *DrumSynthPlugin::createEditor() { return new DrumSynthEditor(*this); }
} // namespace vibera::drums
