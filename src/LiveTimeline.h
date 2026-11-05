// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "MeterMap.h"
#include "Sampler.h"
#include "Automation.h"
#include "SamplerCuts.h"
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <algorithm>

namespace vibera
{
struct LiveNote
{
    Id note = 0, placement = 0;
    double start = 0, end = 0;
    int pitch = 60;
    float velocity = .8f;
    bool step = false;
    PreparedNoteExpression expression;
    float cents = 0; // MPE plugin Channels: tuning + fine cents sent as a per-note pitch bend.
    std::uint32_t repeatOrdinal = 0; // Prepared sub-trigger, never a persisted source ID.
    std::uint8_t noteOffVelocity = 0; // Prepared MIDI byte; not envelope release duration.
    bool slide = false, portamento = false;
    int colourGroup = 0;
};
struct LiveChannel
{
    Id id = 0;
    float gain = 1, pan = 0;
    PreparedSamplerSettings sampler;
    bool sampleInstrument = true;
    bool mpe = false; // Plugin instrument with MPE tuning (LiveMidi member channels).
    // Live keyboard tuning for local samplers (prepared off-thread): the key each played key
    // sounds and its cents, from the Channel tuning table or keyboard-mapped scale.
    bool liveTuned = false;
    std::array<std::uint8_t, 128> liveKey{};
    std::array<PreparedNoteExpression, 128> liveExpression{};
    // Lane pointers belong to this same immutable LiveDocument; no callback lookup/ownership.
    std::vector<SamplerModulationAutomationLane> modulationLanes;
    std::vector<SamplerCutInput> inputCuts;
    std::vector<LiveNote> notes; // sorted by stable identity
    struct Event
    {
        double time;
        std::uint32_t index;
        bool on;
    };
    std::vector<Event> events; // compiled/sorted outside the callback
    // Recorded controllers as ready MIDI bytes, sorted by time (plugin instruments only).
    struct Control
    {
        double time;
        std::array<std::uint8_t, 3> bytes;
    };
    std::vector<Control> controls;
    const LiveNote *find(Id note, Id placement, std::uint32_t repeatOrdinal = 0) const noexcept;
};
struct LiveDocument
{
    LiveDocument() = default;
    // Moving vector storage preserves lane addresses; copying would retain foreign lane pointers.
    LiveDocument(const LiveDocument &) = delete;
    LiveDocument &operator=(const LiveDocument &) = delete;
    LiveDocument(LiveDocument &&) = default;
    LiveDocument &operator=(LiveDocument &&) = default;
    std::uint64_t generation = 0;
    double tempo = 120;  // Base tempo; Song timing uses tempoMap.
    TempoMap tempoMap;   // Immutable after publication: callback lookups are allocation-free.
    int numerator = 4, denominator = 4;
    MeterMap meters; // Song time-signature changes (empty in Pattern mode).
    std::vector<LiveChannel> channels;
    struct CutEvent
    {
        double time = 0;
        Id sender = 0, note = 0;
        bool step = false;
        int previousOther = -1, previousStep = -1;
    };
    std::array<std::vector<CutEvent>, 100> cuts; // 0 is disabled; shared once across receivers.
    struct MixerControl
    {
        int index = 0;
        float gain = 1;
        bool muted = false;
        float pan = 0, stereoSeparation = 0;
        bool phaseInverted = false, stereoSwapped = false;
        MixerEqSettings eq;
        float delayMs = 0;
    };
    std::vector<MixerControl> mixer; // Prepared stable identity lookup; no ValueTree access in DSP.
    struct MixerSendControl { int source=0,target=0;float gain=1; };
    std::vector<MixerSendControl> mixerSends; // Sorted immutable (source,target); control-thread preparation.
    struct EffectControl
    {
        Id plugin = 0;
        float wet = 1;
        bool bypass = false;
    };
    std::vector<EffectControl> effects; // Sorted immutable targets, compiled off RT; not native state.
    const EffectControl *effect(Id) const noexcept;
    AutomationProgram automation;    // Immutable Mixer/Master program for live and offline graphs.
    // Prepared runtime authority only; never serialized. Filled after host admission.
    struct NativeAutomationRoute
    {
        Id plugin = 0;
        unsigned generation = 0;
        juce::String instance;
    };
    std::vector<NativeAutomationRoute> nativeAutomation; // Sorted by stable plugin identity.
    const NativeAutomationRoute *nativeRoute(Id) const noexcept;
    const LiveChannel *find(Id) const noexcept;
};
std::unique_ptr<LiveDocument> compileLiveDocument(const Project &, bool, Id, std::uint64_t);
void bindLiveCutInputs(LiveDocument &, std::span<const SamplerCutInput>);

// Three immutable slots, one message-thread producer and multiple audio consumers.
// Sequentially consistent publication/pinning prevents reuse while a consumer reads.
// Acquisition has two attempts, no unbounded retry, allocation, mutex or deletion.
class LiveTimeline
{
    struct Slot
    {
        std::atomic<unsigned> readers{0};
        std::unique_ptr<LiveDocument> document;
    };
    std::array<Slot, 3> slots;
    std::atomic<int> published{-1};
    // One control producer only; this is not read by audio consumers.
    std::uint64_t publicationSerial = 0;

  public:
    class Lease
    {
        LiveTimeline *owner = nullptr;
        int slot = -1;

      public:
        const LiveDocument *document = nullptr;
        Lease() = default;
        Lease(LiveTimeline &o, int i) : owner(&o), slot(i), document(o.slots[size_t(i)].document.get()) {}
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&other) noexcept : owner(other.owner), slot(other.slot), document(other.document)
        {
            other.owner = nullptr;
        }
        Lease &operator=(Lease &&other) noexcept
        {
            if (this != &other)
            {
                if (owner)
                    owner->slots[size_t(slot)].readers.fetch_sub(1);
                owner = other.owner;
                slot = other.slot;
                document = other.document;
                other.owner = nullptr;
                other.document = nullptr;
            }
            return *this;
        }
        ~Lease()
        {
            if (owner)
                owner->slots[size_t(slot)].readers.fetch_sub(1);
        }
    };
    bool publish(std::unique_ptr<LiveDocument> &);
    std::uint64_t controlPublicationSerial() const noexcept { return publicationSerial; }
    Lease acquire() noexcept;
};

// Each instrument owns its fixed gate state. Unchanged voices are not retriggered.
class LiveSequencer
{
    struct Gate
    {
        bool active = false;
        Id note = 0, placement = 0;
        double start = 0, end = 0;
        int pitch = 0;
        std::uint32_t repeatOrdinal = 0;
    };
    std::array<Gate, 128> gates{};
    struct SlideControl
    {
        bool present = false;
        LiveNote source;
        double reference = 0;
    };
    std::array<SlideControl, 16> slides{};
    std::uint64_t generation = 0;
    double previousEnd = -1;
    double lastInputCutTime = -std::numeric_limits<double>::infinity();
    bool wasPlaying = false;
    bool active(Id, Id, std::uint32_t) const noexcept;

  public:
    void reset() noexcept
    {
        gates = {};
        slides = {};
        generation = 0;
        previousEnd = -1;
        lastInputCutTime = -std::numeric_limits<double>::infinity();
        wasPlaying = false;
    }
    void process(const LiveDocument &, Id, double, int, double, bool, VoiceBank &, juce::AudioBuffer<float> &,
                 int, std::span<VoiceBank *const> extra = {}, bool liveCut = false) noexcept;
};
} // namespace vibera
