// SPDX-License-Identifier: AGPL-3.0-or-later
#include "effects/NativeEffects.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>

namespace native_effects_test
{
inline thread_local bool callback = false;
inline thread_local unsigned allocations = 0, destructions = 0;
struct CallbackScope
{
    CallbackScope()
    {
        callback = true;
    }
    ~CallbackScope()
    {
        callback = false;
    }
};
} // namespace native_effects_test
void *operator new(std::size_t bytes)
{
    if (native_effects_test::callback)
        ++native_effects_test::allocations;
    if (auto *memory = std::malloc(std::max<std::size_t>(bytes, 1)))
        return memory;
    throw std::bad_alloc();
}
void *operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void *memory) noexcept
{
    if (memory && native_effects_test::callback)
        ++native_effects_test::destructions;
    std::free(memory);
}
void operator delete[](void *memory) noexcept
{
    ::operator delete(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
void operator delete[](void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

using namespace vibera::effects;
namespace
{
unsigned checks = 0;
double maximumUiReadbackMs = 0;
void require(bool condition, const juce::String &message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message.toStdString());
}
juce::uint32 parameterId(EffectId effect, std::size_t index = 0)
{
    return (static_cast<juce::uint32>(effect) + 1) * 256 + static_cast<juce::uint32>(index) + 1;
}
juce::NormalisableRange<float> range(const ParameterSpec &p)
{
    return {p.minimum, p.maximum, p.interval, p.skew};
}
float normalized(const ParameterSpec &p, float native)
{
    const auto r = range(p);
    return r.convertTo0to1(r.snapToLegalValue(native));
}
Values snapshot(NativeEffectInstance &instance)
{
    Values values{};
    for (int i = 0; i < instance.getParameters().size(); ++i)
        values[std::size_t(i)] = instance.getParameters()[i]->getValue();
    return values;
}
void sameValues(NativeEffectInstance &instance, const Values &expected, const juce::String &reason)
{
    require(snapshot(instance) == expected, reason);
}
juce::MemoryBlock encode(const juce::XmlElement &xml)
{
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(xml, bytes);
    return bytes;
}
struct Listener final : juce::AudioProcessorParameter::Listener
{
    int changes = 0, begins = 0, ends = 0;
    void parameterValueChanged(int, float) override
    {
        ++changes;
    }
    void parameterGestureChanged(int, bool starting) override
    {
        if (starting)
            ++begins;
        else
            ++ends;
    }
};
void registryAndParameters()
{
    const auto descriptions = nativeDescriptions();
    require(descriptions.size() == catalogueEntries().size(), "Native catalogue missing effects");
    std::set<int> classes;
    std::set<juce::uint32> parameterIds;
    for (const auto &spec : catalogueEntries())
    {
        const auto description = nativeDescription(spec.id);
        require(isNativeDescription(description) && findNativeType(spec.slug) == spec.id,
                "Native descriptor/type refused");
        require(classes.insert(description.uniqueId).second, "Duplicate native class ID");
        juce::PluginDescription decoded;
        require(decoded.loadFromXml(*description.createXml()) && isNativeDescription(decoded),
                "Native descriptor XML lost strict identity");
        auto spoof = description;
        spoof.isInstrument = true;
        require(!createNativeEffect(spoof), "Native instrument spoof instantiated");
        spoof = description;
        ++spoof.uniqueId;
        require(!findNativeDescription(spoof), "Spoofed native class ID accepted");
        spoof = description;
        spoof.pluginFormatName = "VST3";
        require(!isNativeDescription(spoof), "Native descriptor accepted a foreign format");
        auto instance = createNativeEffect(description);
        auto *native = dynamic_cast<NativeEffectInstance *>(instance.get());
        require(native && native->effectId() == spec.id && !native->getVST3Client(),
                "Native factory effect identity or format contract wrong");
        require(native->getParameters().size() == int(spec.parameterCount),
                "Native instance exposes selector/bypass/foreign parameters");
        require(native->getTotalNumInputChannels() == 2 && native->getTotalNumOutputChannels() == 2,
                "Native instance is not stereo");
        require(!native->isBusesLayoutSupported(
                    {{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::mono()}}),
                "Native instance admits unsupported mono layout");
        for (std::size_t i = 0; i < spec.parameterCount; ++i)
        {
            const auto id = parameterId(spec.id, i);
            auto *parameter = native->getHostedParameter(int(i));
            NativeEffectInstance::ParameterInfo info;
            float value = -1;
            require(parameterIds.insert(id).second &&
                        parameter->getParameterID() == juce::String(juce::int64(id)) &&
                        native->parameterForId(id) == parameter && native->parameterInfo(id, info) &&
                        native->parameterValue(id, value),
                    "Stable hosted scalar identity mismatch");
            require(info.id == id && info.flags == 1 && info.defaultValue == value &&
                        info.stepCount ==
                            (spec.parameters[i].interval > 0 ? parameter->getNumSteps() - 1 : 0),
                    "Native semantic metadata mismatch");
            const auto initial = parameter->getValue();
            parameter->setValue(std::numeric_limits<float>::quiet_NaN());
            require(parameter->getValue() == initial, "Non-finite scalar changed native state");
            parameter->setValue(-1);
            require(parameter->getValue() == 0, "Native scalar lower bound failed");
            parameter->setValue(2);
            require(parameter->getValue() == 1, "Native scalar upper bound failed");
            parameter->setValue(initial);
            if (juce::String(spec.parameters[i].unit) == "Hz" && spec.parameters[i].minimum < .1f)
            {
                const auto lowValue = normalized(spec.parameters[i], spec.parameters[i].minimum);
                const auto text = parameter->getText(lowValue, 64);
                require(text.getDoubleValue() > 0 &&
                            std::abs(text.getDoubleValue() - spec.parameters[i].minimum) < .0001,
                        "Low-frequency scalar text lost precision: " + juce::String(spec.slug) + "." +
                            spec.parameters[i].id + " text=" + text);
            }
        }
        float unknown = 0;
        require(!native->parameterValue(0, unknown) && !native->parameterForId(0),
                "Unknown native scalar resolved");
    }
}
void stateRoundTrips()
{
    for (const auto &spec : catalogueEntries())
    {
        NativeEffectInstance source(spec.id), restored(spec.id);
        for (std::size_t i = 0; i < spec.parameterCount; ++i)
            source.getHostedParameter(int(i))->setValue(.31f + .01f * float(i));
        const auto expected = snapshot(source);
        juce::MemoryBlock saved;
        source.getStateInformation(saved);
        require(restored.restoreState(saved.getData(), int(saved.getSize())), "Own native state refused");
        sameValues(restored, expected, "Native state roundtrip changed parameters");
        const auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), int(saved.getSize()));
        require(xml && xml->hasTagName("VIBERA_NATIVE_EFFECT") &&
                    xml->getStringAttribute("effect") == spec.slug,
                "Native state envelope mislabeled");
        const auto refuse = [&](std::unique_ptr<juce::XmlElement> invalid, const juce::String &reason)
        {
            const auto bytes = encode(*invalid);
            require(!restored.restoreState(bytes.getData(), int(bytes.getSize())), "Accepted " + reason);
            sameValues(restored, expected, "Partial mutation on " + reason);
        };
        auto invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->setAttribute("unknown", "ignored?");
        refuse(std::move(invalid), "unknown root attribute");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->getFirstChildElement()->setAttribute("unknown", "ignored?");
        refuse(std::move(invalid), "unknown parameter attribute");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        auto *unknownSidecar = invalid->createNewChildElement("VibeRaParameterValues");
        unknownSidecar->setAttribute("version", 1);
        unknownSidecar->setAttribute("unknown", "ignored?");
        refuse(std::move(invalid), "unknown sidecar attribute");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->setAttribute("schemaVersion", "1.5");
        refuse(std::move(invalid), "fractional schema");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->setAttribute("effect", "unknown-effect");
        refuse(std::move(invalid), "foreign effect identity");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->getFirstChildElement()->setAttribute("value", "NaN");
        refuse(std::move(invalid), "non-finite state");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->getFirstChildElement()->setAttribute("value", "0.5junk");
        refuse(std::move(invalid), "trailing numeric junk");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->getFirstChildElement()->setAttribute("value", 2);
        refuse(std::move(invalid), "out-of-range state");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->removeChildElement(invalid->getFirstChildElement(), true);
        refuse(std::move(invalid), "missing scalar");
        invalid = std::make_unique<juce::XmlElement>(*xml);
        invalid->addChildElement(new juce::XmlElement(*invalid->getFirstChildElement()));
        refuse(std::move(invalid), "duplicate scalar");
        auto appended = std::make_unique<juce::XmlElement>(*xml);
        auto *sidecar = appended->createNewChildElement("VibeRaParameterValues");
        sidecar->setAttribute("version", 1);
        auto *entry = sidecar->createNewChildElement("PARAM");
        entry->setAttribute("id", source.getHostedParameter(0)->getParameterID());
        entry->setAttribute("value", 1.0);
        const auto appendedBytes = encode(*appended);
        require(restored.restoreState(appendedBytes.getData(), int(appendedBytes.getSize())) &&
                    restored.getHostedParameter(0)->getValue() == 1,
                "Valid worker scalar sidecar not restored");
        entry->setAttribute("id", "0");
        const auto badSidecar = encode(*appended);
        const auto withSidecar = snapshot(restored);
        require(!restored.restoreState(badSidecar.getData(), int(badSidecar.getSize())),
                "Unknown sidecar scalar accepted");
        sameValues(restored, withSidecar, "Sidecar refusal partially replaced state");
    }
}
void eventsAndRealtime()
{
    for (const auto &spec : catalogueEntries())
    {
        NativeEffectInstance actual(spec.id), reference(spec.id);
        actual.prepareToPlay(48000, 128);
        reference.prepareToPlay(48000, 128);
        Listener listener;
        actual.getHostedParameter(0)->addListener(&listener);
        const std::array<NativeEffectInstance::ParameterEvent, 3> events{
            {{parameterId(spec.id), 17, 0}, {parameterId(spec.id), 63, 1}, {parameterId(spec.id), 127, .5f}}};
        juce::AudioBuffer<float> result(2, 128), expected(2, 128);
        juce::MidiBuffer midi;
        for (int i = 0; i < 128; ++i)
        {
            const auto sample = .2f * std::sin(float(i) * .17f);
            result.setSample(0, i, sample);
            result.setSample(1, i, sample * -.7f);
        }
        expected.makeCopyOf(result);
        bool accepted = false;
        {
            native_effects_test::CallbackScope callback;
            accepted = actual.setParameterEventsForNextBlock(events, 128);
            actual.processBlock(result, midi);
        }
        require(accepted, "Valid timed native controls refused");
        int begin = 0;
        for (const auto &event : events)
        {
            if (event.sampleOffset > begin)
            {
                juce::AudioBuffer<float> section(expected.getArrayOfWritePointers(), 2, begin,
                                                 event.sampleOffset - begin);
                reference.processBlock(section, midi);
            }
            reference.getHostedParameter(0)->setValue(event.value);
            begin = event.sampleOffset;
        }
        juce::AudioBuffer<float> last(expected.getArrayOfWritePointers(), 2, begin, 128 - begin);
        reference.processBlock(last, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 128; ++i)
                require(std::isfinite(result.getSample(channel, i)) &&
                            std::abs(result.getSample(channel, i) - expected.getSample(channel, i)) < 1.0e-6f,
                        "Native sample-offset control differs from timed scalar reference");
        require(actual.getHostedParameter(0)->getValue() == reference.getHostedParameter(0)->getValue(),
                "Timed scalar readback does not show its final target");
        require(listener.changes == 0 && listener.begins == 0 && listener.ends == 0,
                "DSP event emitted host/UI callbacks");
        auto invalid = events;
        invalid[1].sampleOffset = 16;
        require(!actual.setParameterEventsForNextBlock(invalid, 128), "Unsorted event list accepted");
        invalid = events;
        invalid[0].id = 0;
        require(!actual.setParameterEventsForNextBlock(invalid, 128), "Unknown event parameter accepted");
        invalid = events;
        invalid[0].value = std::numeric_limits<float>::infinity();
        require(!actual.setParameterEventsForNextBlock(invalid, 128), "Non-finite event accepted");
        invalid = events;
        invalid[2].sampleOffset = 128;
        require(!actual.setParameterEventsForNextBlock(invalid, 128), "Out-of-block event accepted");
        require(!actual.setParameterEventsForNextBlock(events, 0), "Zero-frame automation admitted");
        const auto before = snapshot(actual);
        {
            native_effects_test::CallbackScope callback;
            actual.flushParameterChangesForState();
            actual.reset();
        }
        sameValues(actual, before, "Flush/reset fabricated scalar targets");
        actual.getHostedParameter(0)->removeListener(&listener);
        actual.releaseResources();
        actual.prepareToPlay(96000, 128);
        reference.releaseResources();
        reference.prepareToPlay(96000, 128);
        result.clear();
        expected.clear();
        result.setSample(0, 0, .25f);
        expected.setSample(0, 0, .25f);
        actual.processBlock(result, midi);
        reference.processBlock(expected, midi);
        for (int i = 0; i < 128; ++i)
            require(result.getSample(0, i) == expected.getSample(0, i),
                    "Reprepare retained stale effect state or failed current parameter snap");
    }
    require(native_effects_test::allocations == 0 && native_effects_test::destructions == 0,
            "Native parameter/process/reset performed C++ heap allocation/destruction");
}
void flushDoesNotAdvanceAudio()
{
    NativeEffectInstance actual(EffectId::delay), reference(EffectId::delay);
    const auto &spec = effectSpec(EffectId::delay);
    for (auto *instance : {&actual, &reference})
    {
        instance->getHostedParameter(0)->setValue(normalized(spec.parameters[0], 10));
        instance->getHostedParameter(1)->setValue(0);
        instance->getHostedParameter(2)->setValue(1);
        instance->prepareToPlay(48000, 128);
    }
    juce::AudioBuffer<float> a(2, 128), b(2, 128);
    juce::MidiBuffer midi;
    a.clear();
    b.clear();
    a.setSample(0, 0, 1);
    b.setSample(0, 0, 1);
    actual.processBlock(a, midi);
    reference.processBlock(b, midi);
    for (int i = 0; i < 100; ++i)
        require(actual.flushParameterChangesForState(), "Scalar-only native state flush failed");
    juce::AudioBuffer<float> afterA(2, 512), afterB(2, 512);
    afterA.clear();
    afterB.clear();
    actual.processBlock(afterA, midi);
    reference.processBlock(afterB, midi);
    for (int i = 0; i < 512; ++i)
        require(afterA.getSample(0, i) == afterB.getSample(0, i), "State flush advanced delay history");
    require(afterA.getMagnitude(0, 0, 512) > .99f, "Delay continuity oracle contained no impulse");
}
void editors(const juce::File &output)
{
    for (const auto &spec : catalogueEntries())
    {
        NativeEffectInstance instance(spec.id);
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.createEditor());
        editor->setVisible(true);
        std::vector<juce::Slider *> sliders;
        for (int i = 0; i < editor->getNumChildComponents(); ++i)
        {
            auto *component = editor->getChildComponent(i);
            require(dynamic_cast<juce::ComboBox *>(component) == nullptr,
                    "Native effect has a mode selector");
            if (auto *slider = dynamic_cast<juce::Slider *>(component))
                sliders.push_back(slider);
        }
        require(sliders.size() == spec.parameterCount,
                "Native editor control count differs from host schema");
        require(editor->isResizable() && editor->getConstrainer(),
                "Native editor declares genuine host resize constraints");
        auto *meter = editor->findChildWithID("nativeEffects.outputMeter");
        require(meter && dynamic_cast<juce::TooltipClient *>(meter),
                "Native editor exposes its actual stereo output meter");
        require(instance.peak(0) == -1.f && instance.peak(1) == -1.f &&
                    dynamic_cast<juce::TooltipClient *>(meter)->getTooltip() ==
                        "Native output not processed yet; sample peaks unavailable",
                "Fresh native meter distinguishes unprocessed output from actual zero silence");
        const auto originalSize = juce::Point<int>(editor->getWidth(), editor->getHeight());
        const auto *limits = editor->getConstrainer();
        for (const auto size :
             {originalSize, juce::Point<int>(limits->getMinimumWidth(), limits->getMinimumHeight()),
              juce::Point<int>(1180, std::max(660, limits->getMinimumHeight()))})
        {
            editor->setSize(size.x, size.y);
            require(editor->getLocalBounds().contains(meter->getBounds()),
                    "Output meter stays visible at declared compact and wide sizes");
            for (std::size_t i = 0; i < sliders.size(); ++i)
            {
                const auto bounds = sliders[i]->getBounds();
                require(bounds.getWidth() >= 60 && bounds.getHeight() >= 70 &&
                            editor->getLocalBounds().contains(bounds) &&
                            !bounds.intersects(meter->getBounds()),
                        "Responsive native control remains readable and separate from output metering");
                for (std::size_t j = i + 1; j < sliders.size(); ++j)
                    require(!bounds.intersects(sliders[j]->getBounds()),
                            "Responsive parameter cards never overlap");
            }
        }
        editor->setSize(originalSize.x, originalSize.y);
        for (std::size_t i = 0; i < sliders.size(); ++i)
        {
            auto *slider = sliders[i];
            auto *parameter = instance.getHostedParameter(int(i));
            Listener listener;
            parameter->addListener(&listener);
            require(!slider->getBounds().isEmpty() && editor->getLocalBounds().contains(slider->getBounds()),
                    "Native control is outside the original editor layout");
            const auto &p = spec.parameters[i];
            require(slider->getComponentID() == "nativeEffects.parameter." + juce::String(p.id) &&
                        slider->getMinimum() == p.minimum && slider->getMaximum() == p.maximum &&
                        slider->getDoubleClickReturnValue() == p.initial &&
                        slider->getTextValueSuffix() ==
                            (juce::String(p.unit).isEmpty() ? "" : " " + juce::String(p.unit)),
                    "Native redesign preserves exact IDs, ranges, reset values and visible units");
            require(slider->getSliderStyle() == (spec.id == EffectId::graphicEq
                                                     ? juce::Slider::LinearVertical
                                                     : juce::Slider::RotaryVerticalDrag),
                    "EQ gains use band faders and other parameters use original rotary controls");
            const bool lowerEndpoint = parameter->getValue() == 1;
            slider->setValue(lowerEndpoint ? p.minimum : p.maximum, juce::sendNotificationSync);
            require(parameter->getValue() == (lowerEndpoint ? 0.0f : 1.0f) && listener.changes > 0 &&
                        listener.begins == listener.ends && listener.begins > 0,
                    "Native UI did not share hosted scalar/gesture behavior");
            const auto begins = listener.begins, ends = listener.ends;
            slider->onDragStart();
            slider->setValue(lowerEndpoint ? p.maximum : p.minimum, juce::sendNotificationSync);
            require(listener.begins == begins + 1 && listener.ends == ends,
                    "Actual native drag owns one open host gesture across value updates");
            slider->onDragEnd();
            require(listener.begins == listener.ends && listener.ends == ends + 1,
                    "Native drag closes the exact corresponding host gesture");
            slider->setValue(slider->getDoubleClickReturnValue(), juce::sendNotificationSync);
            require(std::abs(parameter->getValue() - parameter->getDefaultValue()) < .000001f &&
                        listener.begins == listener.ends,
                    "Declared native reset value uses the same hosted write and paired gesture");
            parameter->setValueNotifyingHost(parameter->getDefaultValue());
            parameter->removeListener(&listener);
        }
        const auto readbackStart = juce::Time::getMillisecondCounterHiRes();
        const auto readbackReady = [&]
        {
            for (std::size_t i = 0; i < sliders.size(); ++i)
                if (std::abs(sliders[i]->getValue() - spec.parameters[i].initial) >= .001)
                    return false;
            return true;
        };
        do
        {
            juce::Timer::callPendingTimersSynchronously();
            if (readbackReady())
                break;
            juce::Thread::sleep(10);
        } while (juce::Time::getMillisecondCounterHiRes() - readbackStart < 500);
        maximumUiReadbackMs =
            std::max(maximumUiReadbackMs, juce::Time::getMillisecondCounterHiRes() - readbackStart);
        for (std::size_t i = 0; i < sliders.size(); ++i)
            require(
                std::abs(sliders[i]->getValue() - spec.parameters[i].initial) < .001,
                "Host scalar readback did not reach fixed native UI: " + juce::String(spec.slug) + "." +
                    spec.parameters[i].id + " slider=" + juce::String(sliders[i]->getValue(), 9) +
                    " nativeTarget=" + juce::String(spec.parameters[i].initial, 9) +
                    " normalizedTarget=" + juce::String(instance.getHostedParameter(int(i))->getValue(), 9));
        if (spec.id == EffectId::balance)
        {
            instance.prepareToPlay(48000, 256);
            juce::AudioBuffer<float> pcm(2, 256);
            juce::MidiBuffer midi;
            for (int frame = 0; frame < 256; ++frame)
            {
                pcm.setSample(0, frame, .125f);
                pcm.setSample(1, frame, .5f);
            }
            instance.processBlock(pcm, midi);
            require(std::abs(pcm.getMagnitude(0, 0, 256) - .125f) < .000001f &&
                        std::abs(pcm.getMagnitude(1, 0, 256) - .5f) < .000001f,
                    "Meter fixture has independently known asymmetric unity-output PCM");
            auto *tooltip = dynamic_cast<juce::TooltipClient *>(meter);
            const auto waitMeter = [&](const juce::String &expected)
            {
                const auto began = juce::Time::getMillisecondCounterHiRes();
                do
                {
                    juce::Timer::callPendingTimersSynchronously();
                    if (tooltip->getTooltip() == expected)
                        return true;
                    juce::Thread::sleep(10);
                } while (juce::Time::getMillisecondCounterHiRes() - began < 500);
                return false;
            };
            require(waitMeter("Latest processed block output sample peak: L -18.1 dBFS, R -6.0 dBFS"),
                    "Stereo meter reports actual independent left/right output, not invented gain reduction");
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
                auto stream = output.getChildFile("balance-meter-active.png").createOutputStream();
                require(stream && stream->setPosition(0), "Cannot create owned active-meter image");
                stream->truncate();
                require(juce::PNGImageFormat().writeImageToStream(image, *stream),
                        "Cannot encode actual asymmetric active output-meter image");
            }
            pcm.clear();
            instance.processBlock(pcm, midi);
            require(waitMeter("Latest processed block output sample peak: L -inf dBFS, R -inf dBFS"),
                    "Actual silent output clears both meter readings without fabricated animation");
            instance.releaseResources();
        }
        if (spec.id == EffectId::graphicEq || spec.id == EffectId::delay || spec.id == EffectId::limiter ||
            spec.id == EffectId::compressor || spec.id == EffectId::filter || spec.id == EffectId::parametricEq ||
            spec.id == EffectId::maximizer || spec.id == EffectId::studioReverb || spec.id == EffectId::pitchCorrect)
        {
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
            auto stream = output.getChildFile(juce::String(spec.slug) + ".png").createOutputStream();
            require(stream && stream->setPosition(0), "Cannot create owned native image");
            stream->truncate();
            require(juce::PNGImageFormat().writeImageToStream(image, *stream), "Cannot encode native image");
        }
        Listener closing;
        auto *firstParameter = instance.getHostedParameter(0);
        firstParameter->addListener(&closing);
        sliders.front()->onDragStart();
        require(closing.begins == 1 && closing.ends == 0, "Prepare actual open editor gesture before close");
        editor.reset();
        require(closing.begins == 1 && closing.ends == 1,
                "Closing native editor ends its owned gesture exactly once before controls die");
        firstParameter->removeListener(&closing);
    }
}
} // namespace
int main(int argc, char **argv)
{
    const juce::ScopedJuceInitialiser_GUI initialise;
    const auto start = std::chrono::steady_clock::now();
    try
    {
        require(argc == 2, "Owned acceptance output directory required");
        const juce::File output(juce::String::fromUTF8(argv[1]));
        require(output.createDirectory().wasOk(), "Cannot create owned acceptance output");
        registryAndParameters();
        stateRoundTrips();
        eventsAndRealtime();
        flushDoesNotAdvanceAudio();
        editors(output);
        auto *receipt = new juce::DynamicObject();
        receipt->setProperty("passed", true);
        receipt->setProperty("fixedEffectCount", int(catalogueEntries().size()));
        receipt->setProperty("checks", int(checks));
        receipt->setProperty("callbackCppAllocations", int(native_effects_test::allocations));
        receipt->setProperty("callbackCppDestructions", int(native_effects_test::destructions));
        receipt->setProperty("maximumUiReadbackMs", maximumUiReadbackMs);
        receipt->setProperty("seconds",
                             std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        receipt->setProperty("evidence",
                             "Built-in fixed-effect registry/scalars/state/sample-offset DSP/native "
                             "component handlers and renders; no OS-input/listening/hardware claim");
        require(output.getChildFile("verification.json")
                    .replaceWithText(juce::JSON::toString(juce::var(receipt))),
                "Cannot save native acceptance receipt");
        std::cout << "PASS built-in native effects " << catalogueEntries().size() << "; " << checks
                  << " checks; callback C++ heap=" << native_effects_test::allocations << "/"
                  << native_effects_test::destructions << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
