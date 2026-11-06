// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EffectChain.h"
#include "PluginStateEncoding.h"
#include "effects/NativeEffectsRegistry.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>

namespace vibera::effect_chain
{
namespace
{
constexpr std::uint32_t nameBytes = 1024, descriptorBytes = 65536, stateBytes = 24 * 1024 * 1024;
constexpr std::array<std::byte, 8> magic{std::byte{'V'}, std::byte{'i'}, std::byte{'b'}, std::byte{'e'},
                                         std::byte{'R'}, std::byte{'a'}, std::byte{'F'}, std::byte{'X'}};
juce::Result fail(const char *code)
{
    return juce::Result::fail(code);
}
const MixerInsert *findInsert(const Project &p, int id)
{
    const auto it =
        std::find_if(p.mixer.begin(), p.mixer.end(), [id](const auto &m) { return m.index == id; });
    return it == p.mixer.end() ? nullptr : &*it;
}
MixerInsert *findInsert(Project &p, int id)
{
    const auto it =
        std::find_if(p.mixer.begin(), p.mixer.end(), [id](const auto &m) { return m.index == id; });
    return it == p.mixer.end() ? nullptr : &*it;
}
bool bounded(const juce::String &s, std::uint32_t maximum, bool nonempty)
{
    const auto bytes = s.getNumBytesAsUTF8();
    if ((nonempty && s.isEmpty()) || bytes > maximum)
        return false;
    // A JUCE String can contain an isolated surrogate created through its
    // scalar APIs. Enforce the same exact UTF-8 contract as the wire reader,
    // after bounds admission and before any bytes enter an encoded result.
    return juce::CharPointer_UTF8::isValidString(s.toRawUTF8(), static_cast<int>(bytes));
}
bool same(const Project &a, const Project &b)
{
    return a.state().isEquivalentTo(b.state());
}
struct Writer
{
    std::vector<std::byte> bytes;
    void u32(std::uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(std::byte((value >> (8 * i)) & 255));
    }
    void text(const juce::String &s)
    {
        const auto n = s.getNumBytesAsUTF8();
        u32(static_cast<std::uint32_t>(n));
        const auto *p = reinterpret_cast<const std::byte *>(s.toRawUTF8());
        bytes.insert(bytes.end(), p, p + n);
    }
};
struct Reader
{
    std::span<const std::byte> bytes;
    std::size_t offset = 0;
    std::uint32_t u32()
    {
        if (bytes.size() - offset < 4)
            throw std::runtime_error("effectChain.truncated");
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= std::uint32_t(std::to_integer<unsigned>(bytes[offset++])) << (8 * i);
        return value;
    }
    juce::String text(std::uint32_t maximum)
    {
        const auto n = u32();
        if (n > maximum || n > bytes.size() - offset)
            throw std::runtime_error("effectChain.stringLength");
        const auto *p = reinterpret_cast<const char *>(bytes.data() + offset);
        if (!juce::CharPointer_UTF8::isValidString(p, static_cast<int>(n)))
            throw std::runtime_error("effectChain.utf8");
        const auto s = juce::String::fromUTF8(p, static_cast<int>(n));
        if (s.getNumBytesAsUTF8() != n || (n && std::memcmp(s.toRawUTF8(), p, n) != 0))
            throw std::runtime_error("effectChain.utf8");
        offset += n;
        return s;
    }
};
template <class Fn> juce::Result guarded(Fn &&fn)
{
    try
    {
        return fn();
    }
    catch (const std::bad_alloc &)
    {
        return fail("effectChain.preparationFailed");
    }
    catch (const std::exception &e)
    {
        return juce::Result::fail(e.what());
    }
}
bool referenced(const Project &p, Id plugin)
{
    const auto match = [plugin](const auto &key)
    { return key.kind == AutomationParameter::pluginNormalized && key.owner == plugin; };
    for (const auto &s : p.automation.sources)
        for (const auto &l : s.links)
            if (match(l.key))
                return true;
    return std::any_of(p.automation.initialValues.begin(), p.automation.initialValues.end(),
                       [&](const auto &v) { return match(v.key); });
}
} // namespace

juce::Result validate(const Chain &chain)
{
    return guarded(
        [&]() -> juce::Result
        {
            if (!bounded(chain.name, nameBytes, true) || chain.effects.size() > maximumSlots)
                return fail("effectChain.shape");
            const bool extended = std::any_of(
                chain.effects.begin(), chain.effects.end(),
                [](const auto &slot)
                {
                    return PluginMainIo{slot.mainInputBus, slot.mainOutputBus, slot.mainInputChannels,
                                        slot.mainInputMonoSource} != PluginMainIo{};
                });
            std::size_t total = 24 + chain.name.getNumBytesAsUTF8();
            for (const auto &slot : chain.effects)
            {
                if (!bounded(slot.name, nameBytes, true) ||
                    !bounded(slot.descriptor, descriptorBytes, true) ||
                    !bounded(slot.state, stateBytes, false) || !std::isfinite(slot.wet) || slot.wet < 0 ||
                    slot.wet > 1 || !validPluginStateEncoding(slot.state) ||
                    !PluginMainIo{slot.mainInputBus, slot.mainOutputBus, slot.mainInputChannels,
                                  slot.mainInputMonoSource}
                         .valid())
                    return fail("effectChain.slot");
                // Portable descriptors are self-contained PluginDescription XML.
                // Reject declarations lexically BEFORE parser entry: a bounded
                // source string does not bound recursive entity expansion.
                if (slot.descriptor.containsIgnoreCase("<!DOCTYPE") ||
                    slot.descriptor.containsIgnoreCase("<!ENTITY"))
                    return fail("effectChain.descriptorDeclarations");
                const auto xml = juce::parseXML(slot.descriptor);
                juce::PluginDescription type;
                if (!xml || !type.loadFromXml(*xml) || type.isInstrument ||
                    (type.pluginFormatName != "VST3" && !effects::isNativeDescription(type)))
                    return fail("effectChain.unsupportedDescriptor");
                total += (extended ? 36 : 20) + slot.name.getNumBytesAsUTF8() +
                         slot.descriptor.getNumBytesAsUTF8() + slot.state.getNumBytesAsUTF8();
                if (total > maximumBytes)
                    return fail("effectChain.capacity");
            }
            return juce::Result::ok();
        });
}
juce::Result encode(const Chain &chain, std::unique_ptr<juce::MemoryBlock> &output)
{
    return guarded(
        [&]() -> juce::Result
        {
            if (const auto result = validate(chain); result.failed())
                return result;
            Writer w;
            w.bytes.insert(w.bytes.end(), magic.begin(), magic.end());
            const bool extended = std::any_of(
                chain.effects.begin(), chain.effects.end(),
                [](const auto &slot)
                {
                    return PluginMainIo{slot.mainInputBus, slot.mainOutputBus, slot.mainInputChannels,
                                        slot.mainInputMonoSource} != PluginMainIo{};
                });
            w.u32(extended ? 2 : 1);
            w.u32((chain.effectsBypassed ? 1u : 0u) | (chain.track ? 2u : 0u));
            w.text(chain.name);
            w.u32(static_cast<std::uint32_t>(chain.effects.size()));
            for (const auto &slot : chain.effects)
            {
                w.u32(slot.bypass ? 1 : 0);
                w.u32(std::bit_cast<std::uint32_t>(slot.wet));
                if (extended)
                {
                    w.u32(static_cast<std::uint32_t>(slot.mainInputBus));
                    w.u32(static_cast<std::uint32_t>(slot.mainOutputBus));
                    w.u32(static_cast<std::uint32_t>(slot.mainInputChannels));
                    w.u32(static_cast<std::uint32_t>(slot.mainInputMonoSource));
                }
                w.text(slot.name);
                w.text(slot.descriptor);
                w.text(slot.state);
            }
            if (const auto &track = chain.track)
            {
                w.text(track->name);
                w.u32(track->colour);
                for (float value : {track->gain, track->pan, track->stereoSeparation})
                    w.u32(std::bit_cast<std::uint32_t>(value));
                w.u32((track->phaseInverted ? 1u : 0u) | (track->stereoSwapped ? 2u : 0u));
                for (float value : track->eq)
                    w.u32(std::bit_cast<std::uint32_t>(value));
                w.u32(std::bit_cast<std::uint32_t>(track->delayMs));
                w.u32(static_cast<std::uint32_t>(track->dock + 1));
            }
            auto next = std::make_unique<juce::MemoryBlock>(w.bytes.data(), w.bytes.size());
            output.swap(next);
            return juce::Result::ok();
        });
}
juce::Result decode(std::span<const std::byte> bytes, std::unique_ptr<Chain> &output)
{
    return guarded(
        [&]() -> juce::Result
        {
            if (bytes.size() < 24 || bytes.size() > maximumBytes ||
                !std::equal(magic.begin(), magic.end(), bytes.begin()))
                return fail("effectChain.format");
            Reader r{bytes, 8};
            const auto version = r.u32();
            if (version != 1 && version != 2)
                return fail("effectChain.version");
            const auto flags = r.u32();
            if (flags > 3)
                return fail("effectChain.flags");
            auto next = std::make_unique<Chain>();
            next->effectsBypassed = (flags & 1u) != 0;
            next->name = r.text(nameBytes);
            const auto count = r.u32();
            if (count > maximumSlots)
                return fail("effectChain.capacity");
            next->effects.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                Slot slot;
                const auto f = r.u32();
                if (f > 1)
                    return fail("effectChain.flags");
                slot.bypass = f == 1;
                slot.wet = std::bit_cast<float>(r.u32());
                if (version == 2)
                {
                    std::array<std::uint32_t, 4> io{};
                    for (auto &value : io)
                    {
                        value = r.u32();
                        if (value > 31)
                            return fail("effectChain.mainIo");
                    }
                    slot.mainInputBus = int(io[0]);
                    slot.mainOutputBus = int(io[1]);
                    slot.mainInputChannels = int(io[2]);
                    slot.mainInputMonoSource = int(io[3]);
                }
                slot.name = r.text(nameBytes);
                slot.descriptor = r.text(descriptorBytes);
                slot.state = r.text(stateBytes);
                next->effects.push_back(std::move(slot));
            }
            if (flags & 2u)
            {
                TrackControls track;
                track.name = r.text(nameBytes);
                track.colour = r.u32();
                track.gain = std::bit_cast<float>(r.u32());
                track.pan = std::bit_cast<float>(r.u32());
                track.stereoSeparation = std::bit_cast<float>(r.u32());
                const auto modes = r.u32();
                if (modes > 3)
                    return fail("effectChain.track");
                track.phaseInverted = (modes & 1u) != 0;
                track.stereoSwapped = (modes & 2u) != 0;
                for (auto &value : track.eq)
                    value = std::bit_cast<float>(r.u32());
                track.delayMs = std::bit_cast<float>(r.u32());
                const auto dock = r.u32();
                if (dock > 2)
                    return fail("effectChain.track");
                track.dock = int(dock) - 1;
                const auto finite = [](float v) { return std::isfinite(v); };
                if (!finite(track.gain) || track.gain < 0 || track.gain > 2 || !finite(track.pan) ||
                    std::abs(track.pan) > 1 || !finite(track.stereoSeparation) || std::abs(track.stereoSeparation) > 1 ||
                    !finite(track.delayMs) || track.delayMs < 0 || track.delayMs > 500 ||
                    !std::all_of(track.eq.begin(), track.eq.end(), finite) ||
                    (track.colour != 0 && (track.colour >> 24) != 255))
                    return fail("effectChain.track");
                next->track = std::move(track);
            }
            if (r.offset != bytes.size())
                return fail("effectChain.trailingBytes");
            if (const auto result = validate(*next); result.failed())
                return result;
            output.swap(next);
            return juce::Result::ok();
        });
}
juce::Result beginCapture(const Project &source, const juce::String &identity, std::uint64_t revision,
                          int insert, std::span<const PluginRuntimeState> owners,
                          std::unique_ptr<CaptureIntent> &output)
{
    return guarded(
        [&]() -> juce::Result
        {
            if (source.validate().failed() || identity.isEmpty())
                return fail("effectChain.source");
            const auto *m = findInsert(source, insert);
            if (!m)
                return fail("effectChain.missingInsert");
            if (owners.size() != m->effects.size())
                return fail("effectChain.ownerCount");
            std::set<Id> seen;
            for (std::size_t i = 0; i < owners.size(); ++i)
            {
                const auto &o = owners[i];
                if (o.plugin != m->effects[i].id || !seen.insert(o.plugin).second)
                    return fail("effectChain.ownerIdentity");
                if (o.available != o.instance.isNotEmpty())
                    return fail("effectChain.workerUnavailable");
            }
            auto next = std::make_unique<CaptureIntent>();
            next->source = source;
            next->projectIdentity = identity;
            next->revision = revision;
            next->insert = insert;
            next->owners.assign(owners.begin(), owners.end());
            output.swap(next);
            return juce::Result::ok();
        });
}
juce::Result finishCapture(const CaptureIntent &intent, const Project &current, const juce::String &identity,
                           std::uint64_t revision, const Project &captured,
                           std::span<const CaptureWitness> witnesses, std::unique_ptr<Clipboard> &output)
{
    return guarded(
        [&]() -> juce::Result
        {
            if (identity != intent.projectIdentity || revision != intent.revision ||
                !same(current, intent.source))
                return fail("effectChain.staleCapture");
            const auto *source = findInsert(intent.source, intent.insert),
                       *copy = findInsert(captured, intent.insert);
            if (!source || !copy || captured.validate().failed() ||
                copy->effects.size() != source->effects.size() ||
                intent.owners.size() != source->effects.size() || witnesses.size() != source->effects.size())
                return fail("effectChain.captureShape");
            Project normalized = captured;
            auto *normal = findInsert(normalized, intent.insert);
            for (std::size_t i = 0; i < source->effects.size(); ++i)
                normal->effects[i].state = source->effects[i].state;
            if (!same(normalized, intent.source))
                return fail("effectChain.captureMutation");
            auto next = std::make_unique<Clipboard>();
            next->sourceProjectIdentity = identity;
            next->sourceRevision = revision;
            next->sourceInsert = intent.insert;
            next->chain.name = source->name;
            next->chain.effectsBypassed = source->effectsBypassed;
            std::set<Id> seen;
            for (std::size_t i = 0; i < source->effects.size(); ++i)
            {
                const auto &original = source->effects[i], &saved = copy->effects[i];
                const auto &owner = intent.owners[i];
                const auto &w = witnesses[i];
                if (owner.plugin != original.id || w.current.plugin != original.id ||
                    !seen.insert(original.id).second || owner.instance != w.current.instance ||
                    owner.available != w.current.available)
                    return fail("effectChain.staleWorker");
                if (w.pendingParameters || w.editorBusy)
                    return fail("effectChain.controlBusy");
                StateReceipt receipt{original.id, owner.instance, std::nullopt,
                                     StateProvenance::retainedProjectBytes};
                if (owner.available)
                {
                    if (owner.instance.isEmpty() || !w.capturedRevision ||
                        *w.capturedRevision != w.current.parameterRevision)
                        return fail("effectChain.staleNativeState");
                    receipt.revision = w.capturedRevision;
                    receipt.provenance = StateProvenance::nativeAcknowledgement;
                }
                else if (owner.instance.isNotEmpty() || w.capturedRevision)
                    return fail("effectChain.placeholderState");
                else if (saved.state != original.state)
                    receipt.provenance = StateProvenance::retainedRecoveryBytes;
                next->chain.effects.push_back({saved.name, saved.descriptor, saved.state, saved.bypass,
                                               saved.wet, saved.mainInputBus, saved.mainOutputBus,
                                               saved.mainInputChannels, saved.mainInputMonoSource});
                next->receipts.push_back(std::move(receipt));
            }
            if (const auto result = validate(next->chain); result.failed())
                return result;
            output.swap(next);
            return juce::Result::ok();
        });
}
juce::Result prepareReplacement(const Project &target, int insert, const Chain &chain, Id firstReservedId,
                                std::unique_ptr<ReplacementPlan> &output)
{
    return guarded(
        [&]() -> juce::Result
        {
            if (target.validate().failed())
                return fail("effectChain.target");
            if (const auto result = validate(chain); result.failed())
                return result;
            const auto *old = findInsert(target, insert);
            if (!old)
                return fail("effectChain.missingInsert");
            constexpr Id maximumPublicId = 1000000000;
            const auto count = static_cast<Id>(chain.effects.size());
            if (firstReservedId < target.nextId || firstReservedId < 1 ||
                (count &&
                 (firstReservedId > maximumPublicId || count - 1 > maximumPublicId - firstReservedId)))
                return fail("effectChain.idReservation");
            for (const auto &fx : old->effects)
                if (referenced(target, fx.id))
                    return fail("effectChain.targetAutomationReferenced");
            auto next = std::make_unique<ReplacementPlan>();
            next->destination = target;
            auto *destination = findInsert(next->destination, insert);
            for (const auto &fx : old->effects)
                next->removedIds.push_back(fx.id);
            destination->effects.clear();
            for (const auto &slot : chain.effects)
            {
                PluginSpec fx;
                fx.id = firstReservedId + static_cast<Id>(next->newIds.size());
                fx.name = slot.name;
                fx.descriptor = slot.descriptor;
                fx.state = slot.state;
                fx.bypass = slot.bypass;
                fx.wet = slot.wet;
                fx.mainInputBus = slot.mainInputBus;
                fx.mainOutputBus = slot.mainOutputBus;
                fx.mainInputChannels = slot.mainInputChannels;
                fx.mainInputMonoSource = slot.mainInputMonoSource;
                next->newIds.push_back(fx.id);
                destination->effects.push_back(std::move(fx));
            }
            destination->effectsBypassed = chain.effectsBypassed;
            if (count)
                next->destination.nextId = firstReservedId + count;
            if (const auto result = next->destination.validate(); result.failed())
                return result;
            output.swap(next);
            return juce::Result::ok();
        });
}
} // namespace vibera::effect_chain
