// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NativeEffects.h"
#include "NativeEffectsEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace vibera::effects
{
namespace
{
bool readNormalized(const juce::String &text, float &value)
{
    if (text.isEmpty())
        return false;
    char *end = nullptr;
    const double parsed = std::strtod(text.toRawUTF8(), &end);
    if (!end || *end != '\0' || !std::isfinite(parsed) || parsed < 0 || parsed > 1)
        return false;
    value = static_cast<float>(parsed);
    return true;
}
} // namespace

class NativeEffectInstance::NativeParameter final : public juce::HostedAudioProcessorParameter
{
    const ParameterSpec &spec_;
    const juce::uint32 id_;
    const juce::String textId_;
    juce::NormalisableRange<float> range_;
    std::atomic<float> value_;

  public:
    NativeParameter(const ParameterSpec &spec, juce::uint32 id)
        : HostedAudioProcessorParameter(1), spec_(spec), id_(id), textId_(juce::int64(id)),
          range_(spec.minimum, spec.maximum, spec.interval, spec.skew),
          value_(range_.convertTo0to1(spec.initial))
    {
    }
    juce::uint32 id() const noexcept
    {
        return id_;
    }
    juce::String getParameterID() const override
    {
        return textId_;
    }
    float getValue() const override
    {
        return value_.load(std::memory_order_relaxed);
    }
    float legalValue(float value) const noexcept
    {
        if (spec_.interval > 0)
            return range_.convertTo0to1(range_.snapToLegalValue(range_.convertFrom0to1(value)));
        return value;
    }
    void setValue(float value) override
    {
        if (std::isfinite(value))
            value_.store(legalValue(std::clamp(value, 0.0f, 1.0f)), std::memory_order_relaxed);
    }
    float nativeValue() const noexcept
    {
        return range_.convertFrom0to1(getValue());
    }
    float getDefaultValue() const override
    {
        return range_.convertTo0to1(spec_.initial);
    }
    juce::String getName(int length) const override
    {
        const juce::String text(spec_.name);
        return length > 0 ? text.substring(0, length) : text;
    }
    juce::String getLabel() const override
    {
        return spec_.unit;
    }
    int getNumSteps() const override
    {
        return spec_.interval > 0 ? 1 + int(std::lround((spec_.maximum - spec_.minimum) / spec_.interval))
                                  : getDefaultNumParameterSteps();
    }
    bool isDiscrete() const override
    {
        return spec_.interval > 0;
    }
    bool isBoolean() const override
    {
        return isDiscrete() && getNumSteps() == 2;
    }
    juce::String getText(float value, int length) const override
    {
        if (!std::isfinite(value))
            value = getDefaultValue();
        const auto native = range_.snapToLegalValue(range_.convertFrom0to1(std::clamp(value, 0.0f, 1.0f)));
        const juce::String unit(spec_.unit);
        const auto decimals = isDiscrete()                    ? 0
                              : unit == "Hz"                  ? (std::abs(native) < 1    ? 3
                                                                 : std::abs(native) < 10 ? 2
                                                                                         : 1)
                              : unit == "ms" || unit == "dB"  ? 1
                              : juce::String(spec_.id) == "q" ? 3
                                                              : 2;
        const juce::String text(native, decimals);
        return length > 0 ? text.substring(0, length) : text;
    }
    float getValueForText(const juce::String &text) const override
    {
        const auto trimmed = text.trim();
        char *end = nullptr;
        const auto native = std::strtod(trimmed.toRawUTF8(), &end);
        if (trimmed.isEmpty() || !end || *end != '\0' || !std::isfinite(native))
            return getValue();
        return range_.convertTo0to1(
            range_.snapToLegalValue(std::clamp(static_cast<float>(native), spec_.minimum, spec_.maximum)));
    }
};

NativeEffectInstance::NativeEffectInstance(EffectId effect)
    : AudioPluginInstance(BusesProperties()
                              .withInput("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      effect_(effect), spec_(effectSpec(effect))
{
    for (std::size_t i = 0; i < spec_.parameterCount; ++i)
    {
        const auto id = (static_cast<juce::uint32>(effect_) + 1) * 256 + static_cast<juce::uint32>(i) + 1;
        auto parameter = std::make_unique<NativeParameter>(spec_.parameters[i], id);
        parameters_[i] = parameter.get();
        addHostedParameter(std::move(parameter));
    }
}
NativeEffectInstance::~NativeEffectInstance() = default;

NativeEffectInstance::NativeParameter *NativeEffectInstance::findParameter(juce::uint32 id) const noexcept
{
    const auto first = (static_cast<juce::uint32>(effect_) + 1) * 256 + 1;
    return id >= first && id - first < spec_.parameterCount ? parameters_[id - first] : nullptr;
}
bool NativeEffectInstance::parameterInfo(juce::uint32 id, ParameterInfo &info) const
{
    const auto *parameter = findParameter(id);
    if (!parameter)
        return false;
    info = {};
    info.id = id;
    info.flags = 1; // Shared host DTO: automatable, visible, writable scalar.
    info.stepCount = parameter->isDiscrete() ? parameter->getNumSteps() - 1 : 0;
    info.defaultValue = parameter->getDefaultValue();
    return true;
}
bool NativeEffectInstance::parameterValue(juce::uint32 id, float &value) const
{
    const auto *parameter = findParameter(id);
    if (!parameter)
        return false;
    value = parameter->getValue();
    return true;
}
juce::AudioProcessorParameter *NativeEffectInstance::parameterForId(juce::uint32 id) const noexcept
{
    return findParameter(id);
}
Values NativeEffectInstance::nativeValues() const noexcept
{
    Values values{};
    for (std::size_t i = 0; i < spec_.parameterCount; ++i)
        values[i] = parameters_[i]->nativeValue();
    return values;
}
bool NativeEffectInstance::setParameterEventsForNextBlock(juce::Span<const ParameterEvent> events,
                                                          int frames) noexcept
{
    eventCount_ = 0;
    eventFrames_ = 0;
    if (frames <= 0 || events.size() > events_.size())
        return false;
    int previousOffset = -1;
    for (const auto &event : events)
    {
        if (!findParameter(event.id) || event.sampleOffset < previousOffset || event.sampleOffset < 0 ||
            event.sampleOffset >= frames || !std::isfinite(event.value) || event.value < 0 || event.value > 1)
            return false;
        previousOffset = event.sampleOffset;
    }
    std::copy(events.begin(), events.end(), events_.begin());
    eventCount_ = events.size();
    eventFrames_ = frames;
    return true;
}
bool NativeEffectInstance::flushParameterChangesForState() noexcept
{
    // Hosted parameters already contain current scalar targets. State capture
    // reads those atomics directly; it must not simulate audio or consume events.
    return true;
}
void NativeEffectInstance::fillInPluginDescription(juce::PluginDescription &description) const
{
    description = nativeDescription(effect_);
}
const juce::String NativeEffectInstance::getName() const
{
    return "VibeRa " + juce::String(spec_.name);
}
void NativeEffectInstance::prepareToPlay(double rate, int)
{
    prepared_ = false;
    engine_.prepare(rate);
    setLatencySamples(engine_.latencySamples(effect_)); // The host compensates Tune's delay.
    engine_.setParameters(effect_, nativeValues());
    eventCount_ = 0;
    eventFrames_ = 0;
    peakLeft_.store(-1, std::memory_order_relaxed);
    peakRight_.store(-1, std::memory_order_relaxed);
    prepared_ = true;
}
void NativeEffectInstance::releaseResources()
{
    reset();
    prepared_ = false;
}
void NativeEffectInstance::reset()
{
    engine_.reset();
    eventCount_ = 0;
    eventFrames_ = 0;
}
bool NativeEffectInstance::isBusesLayoutSupported(const BusesLayout &layout) const
{
    return layout.inputBuses.size() == 1 && layout.outputBuses.size() == 1 &&
           layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void NativeEffectInstance::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
    const juce::ScopedNoDenormals noDenormals;
    const auto frames = buffer.getNumSamples();
    if (!prepared_ || buffer.getNumChannels() != 2 || (eventCount_ != 0 && eventFrames_ != frames))
    {
        buffer.clear();
        eventCount_ = 0;
        eventFrames_ = 0;
        return;
    }
    auto values = nativeValues();
    if (!engine_.setParameters(effect_, values))
    {
        buffer.clear();
        eventCount_ = 0;
        eventFrames_ = 0;
        return;
    }
    auto *left = buffer.getWritePointer(0), *right = buffer.getWritePointer(1);
    int cursor = 0;
    std::size_t event = 0;
    while (cursor < frames)
    {
        if (event < eventCount_ && events_[event].sampleOffset == cursor)
        {
            do
            {
                const auto &change = events_[event++];
                auto *parameter = findParameter(change.id);
                // DSP automation updates scalar readback only. It never emits
                // UI/host callbacks or gestures from the audio thread.
                parameter->setValue(change.value);
                const auto index = change.id - ((static_cast<juce::uint32>(effect_) + 1) * 256 + 1);
                values[index] = parameter->nativeValue();
            } while (event < eventCount_ && events_[event].sampleOffset == cursor);
            engine_.setParameters(effect_, values);
        }
        const auto end = event < eventCount_ ? events_[event].sampleOffset : frames;
        engine_.process(left + cursor, right + cursor, end - cursor);
        cursor = end;
    }
    eventCount_ = 0;
    eventFrames_ = 0;
    float maximumLeft = 0, maximumRight = 0;
    for (int i = 0; i < frames; ++i)
    {
        maximumLeft = std::max(maximumLeft, std::abs(left[i]));
        maximumRight = std::max(maximumRight, std::abs(right[i]));
    }
    peakLeft_.store(maximumLeft, std::memory_order_relaxed);
    reduction_.store(engine_.gainReductionDb(), std::memory_order_relaxed);
    peakRight_.store(maximumRight, std::memory_order_relaxed);
}
float NativeEffectInstance::peak(int channel) const noexcept
{
    return (channel == 0 ? peakLeft_ : peakRight_).load(std::memory_order_relaxed);
}
double NativeEffectInstance::getTailLengthSeconds() const
{
    const auto values = nativeValues();
    if (effect_ == EffectId::delay)
    {
        const auto feedback = std::clamp(double(values[1]), .001, .95);
        return values[0] * .001 * (1 + std::log(.001) / std::log(feedback));
    }
    if (effect_ == EffectId::reverb)
        return 1.5 * values[1] + .2;
    if (effect_ == EffectId::flanger)
    {
        const auto feedback = std::clamp(std::abs(double(values[2])), .001, .9);
        return (0.2 + 2 * values[1]) * .001 * (1 + std::log(.001) / std::log(feedback));
    }
    if (effect_ == EffectId::phaser)
        return 2;
    if (effect_ == EffectId::chorus)
        return .03;
    if (effect_ == EffectId::filter)
        return std::max(.1, double(values[2]) * std::log(1000.0) /
                                (std::numbers::pi * std::max(20.0f, values[1])));
    if (effect_ == EffectId::graphicEq || effect_ == EffectId::bassBoost || effect_ == EffectId::dcBlock)
        return .5;
    return 0;
}
void NativeEffectInstance::getStateInformation(juce::MemoryBlock &destination)
{
    juce::XmlElement xml("VIBERA_NATIVE_EFFECT");
    xml.setAttribute("schemaVersion", 1);
    xml.setAttribute("effect", spec_.slug);
    for (std::size_t i = 0; i < spec_.parameterCount; ++i)
    {
        auto *node = xml.createNewChildElement("PARAM");
        node->setAttribute("id", parameters_[i]->getParameterID());
        node->setAttribute("value", double(parameters_[i]->getValue()));
    }
    copyXmlToBinary(xml, destination);
}
bool NativeEffectInstance::restoreState(const void *data, int bytes)
{
    const auto refuse = [&]
    {
        restoreAccepted_.store(false, std::memory_order_relaxed);
        return false;
    };
    if (!data || bytes < 8 || bytes > 1024 * 1024)
        return refuse();
    const auto xml = getXmlFromBinary(data, bytes);
    if (!xml || !xml->hasTagName("VIBERA_NATIVE_EFFECT") || xml->getNumAttributes() != 2 ||
        xml->getStringAttribute("schemaVersion") != "1" || xml->getStringAttribute("effect") != spec_.slug)
        return refuse();
    Values candidate{};
    std::array<bool, maximumParameters> seen{};
    const juce::XmlElement *sidecar = nullptr;
    std::size_t count = 0;
    for (auto *node = xml->getFirstChildElement(); node; node = node->getNextElement())
    {
        if (node->hasTagName("VibeRaParameterValues"))
        {
            if (sidecar || node->getNumAttributes() != 1 || node->getStringAttribute("version") != "1")
                return refuse();
            sidecar = node;
            continue;
        }
        if (!node->hasTagName("PARAM") || node->getNumAttributes() != 2 || node->getNumChildElements() != 0)
            return refuse();
        std::size_t index = 0;
        while (index < spec_.parameterCount &&
               node->getStringAttribute("id") != parameters_[index]->getParameterID())
            ++index;
        if (index == spec_.parameterCount || seen[index] ||
            !readNormalized(node->getStringAttribute("value"), candidate[index]) ||
            std::abs(candidate[index] - parameters_[index]->legalValue(candidate[index])) > 1.0e-6f)
            return refuse();
        seen[index] = true;
        ++count;
    }
    if (count != spec_.parameterCount)
        return refuse();
    if (sidecar)
    {
        seen.fill(false);
        for (auto *node = sidecar->getFirstChildElement(); node; node = node->getNextElement())
        {
            if (!node->hasTagName("PARAM") || node->getNumAttributes() != 2 ||
                node->getNumChildElements() != 0)
                return refuse();
            std::size_t index = 0;
            while (index < spec_.parameterCount &&
                   node->getStringAttribute("id") != parameters_[index]->getParameterID())
                ++index;
            if (index == spec_.parameterCount || seen[index] ||
                !readNormalized(node->getStringAttribute("value"), candidate[index]) ||
                std::abs(candidate[index] - parameters_[index]->legalValue(candidate[index])) > 1.0e-6f)
                return refuse();
            seen[index] = true;
        }
    }
    // Validation is complete before the first scalar is replaced. State loading
    // is a host control operation; sample-offset events remain DSP-thread owned.
    for (std::size_t i = 0; i < spec_.parameterCount; ++i)
        parameters_[i]->setValue(candidate[i]);
    restoreAccepted_.store(true, std::memory_order_relaxed);
    return true;
}
void NativeEffectInstance::setStateInformation(const void *data, int bytes)
{
    restoreState(data, bytes);
}
juce::AudioProcessorEditor *NativeEffectInstance::createEditor()
{
    return new NativeEffectsEditor(*this);
}
std::unique_ptr<juce::AudioPluginInstance> createNativeEffect(const juce::PluginDescription &description)
{
    const auto effect = findNativeDescription(description);
    return effect ? std::make_unique<NativeEffectInstance>(*effect) : nullptr;
}
} // namespace vibera::effects
