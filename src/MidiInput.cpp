// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiInput.h"
namespace vibera
{
static juce::File midiPrefs()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VibeRa Studio/midi-input.json");
}
MidiInputRouter::MidiInputRouter(bool hardware) : persistent(hardware)
{
    if (hardware)
    {
        auto prefs = juce::JSON::parse(midiPrefs());
        filter = juce::jlimit(0, 16, int(prefs["channel"]));
        quantizeStart = bool(prefs["quantizeNoteStarts"]);
        quantizeEnd = bool(prefs["quantizeNoteEnds"]);
        setDevice(prefs["identifier"].toString());
    }
}
MidiInputRouter::~MidiInputRouter()
{
    close();
}
void MidiInputRouter::save()
{
    if (!persistent)
        return;
    auto data = std::make_unique<juce::DynamicObject>();
    data->setProperty("version", 1);
    data->setProperty("identifier", wanted);
    data->setProperty("channel", filter.load());
    data->setProperty("quantizeNoteStarts", quantizeStart);
    data->setProperty("quantizeNoteEnds", quantizeEnd);
    auto file = midiPrefs();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(data.release())));
}
std::shared_ptr<MidiLane> MidiInputRouter::createLane()
{
    auto lane = std::make_shared<MidiLane>();
    lanes.push_back(lane);
    return lane;
}
void MidiInputRouter::select(MidiLane *next)
{
    auto *previous = target.load();
    if (previous == next)
        return;
    historyEnabled = false;
    target.store(nullptr);
    quiesceHistory();
    const Id nextHistory = next ? next->channel : 0;
    if (historyChannel.load() != nextHistory && historyChannel.load() && historyInterrupted)
        historyInterrupted(historyChannel.load(), juce::Time::getMillisecondCounterHiRes() / 1000.);
    const bool wasCapturing = capture.active.load();
    if (wasCapturing)
        stopCapture();
    if (previous)
    {
        MidiClock::Position position;
        if (wasCapturing && captureInterrupted && previous->clock.read(position))
            captureInterrupted(previous->channel, position.seconds);
        previous->enabled = false;
        ++previous->epoch;
    }
    if (next)
    {
        ++next->epoch;
        next->enabled = true;
    }
    target.store(next);
    if (historyChannel.load() != nextHistory)
    {
        historyChannel = nextHistory;
        ++historyEpoch;
    }
    historyEnabled = true;
    if (wasCapturing && !capture.failed.load())
        capture.active = true;
}
void MidiInputRouter::setHistoryTarget(Id channel)
{
    if (historyChannel.load() == channel)
        return;
    historyEnabled = false;
    quiesceHistory();
    auto previous = historyChannel.load();
    if (previous && historyInterrupted)
        historyInterrupted(previous, juce::Time::getMillisecondCounterHiRes() / 1000.);
    historyChannel = channel;
    ++historyEpoch;
    historyEnabled = true;
}
void MidiInputRouter::close()
{
    if (device)
    {
        device->stop();
        device.reset();
    }
    active.clear();
    panic();
}
void MidiInputRouter::panic()
{
    controls.resetPerformance();
    historyEnabled = false;
    auto *previous = target.exchange(nullptr);
    quiesceHistory();
    if (historyChannel.load() && historyInterrupted)
        historyInterrupted(historyChannel.load(), juce::Time::getMillisecondCounterHiRes() / 1000.);
    const bool wasCapturing = capture.active.load();
    if (wasCapturing)
        stopCapture();
    if (auto *lane = previous)
    {
        MidiClock::Position position;
        if (wasCapturing && captureInterrupted && lane->clock.read(position))
            captureInterrupted(lane->channel, position.seconds);
    }
    for (auto &lane : lanes)
    {
        ++lane->panicEpoch;
        ++lane->epoch;
    }
    target.store(previous);
    ++historyEpoch;
    historyEnabled = true;
    if (wasCapturing && !capture.failed.load())
        capture.active = true;
}
juce::Result MidiInputRouter::setDevice(const juce::String &identifier)
{
    close();
    wanted = identifier;
    error.clear();
    if (wanted.isNotEmpty())
    {
        device = juce::MidiInput::openDevice(wanted, this);
        if (device)
        {
            active = wanted;
            device->start();
        }
        else
            error = "MIDI device unavailable or could not be opened";
    }
    save();
    return error.isEmpty() ? juce::Result::ok() : juce::Result::fail(error);
}
void MidiInputRouter::setChannel(int channel)
{
    filter = juce::jlimit(0, 16, channel);
    panic();
    save();
}
juce::String MidiInputRouter::status() const
{
    return (wanted.isEmpty()   ? "MIDI input off"
            : active.isEmpty() ? error
                               : "MIDI connected") +
           "  |  " + juce::String(received.load()) + " messages";
}
void MidiInputRouter::poll()
{
    auto now = juce::Time::getMillisecondCounter();
    if (now - lastPoll < 1000)
        return;
    lastPoll = now;
    if (persistent && wanted.isNotEmpty())
    {
        bool available = false;
        for (auto &info : juce::MidiInput::getAvailableDevices())
            available = available || info.identifier == wanted;
        if (!available && device)
        {
            close();
            error = "MIDI device disconnected; waiting for reconnect";
        }
        if (available && !device)
            setDevice(wanted);
    }
    // Reader pins protect a callback that loaded the previous raw target during a switch.
    if (readers.load() == 0)
        std::erase_if(lanes,
                      [&](auto &lane) { return lane.get() != target.load() && lane.use_count() == 1; });
}
void MidiInputRouter::handleIncomingMidiMessage(juce::MidiInput *, const juce::MidiMessage &message)
{
    if (!controls.observe(message, 0))
        route(message, true, true);
}
void MidiInputRouter::route(const juce::MidiMessage &message, bool monitor, bool record)
{
    auto size = message.getRawDataSize();
    if (size < 2 || size > 3 || !message.isForChannel(message.getChannel()) || message.getChannel() == 0)
        return;
    auto channel = filter.load();
    if (channel && message.getChannel() != channel)
        return;
    unsigned char bytes[3]{message.getRawData()[0], message.getRawData()[1], 0};
    if (size == 3)
        bytes[2] = message.getRawData()[2];
    if (const auto mask = scaleSnap.load(std::memory_order_relaxed);
        mask && size == 3 && ((bytes[0] & 0xf0) == 0x80 || (bytes[0] & 0xf0) == 0x90))
        bytes[1] = static_cast<unsigned char>(snapToScale(bytes[1], mask));
    ++readers;
    if (record && historyEnabled.load())
        history.push(historyChannel.load(), historyEpoch.load(), bytes);
    if (auto *lane = target.load())
    {
        auto overflow = lane->overflows.load();
        if (monitor)
            lane->push(bytes, static_cast<unsigned char>(size));
        if (capture.active.load() && lane->overflows.load() != overflow)
            capture.failed = true;

        if (record)
            capture.push(lane->channel, lane->clock, lane->epoch.load(), bytes);
        ++received;
    }
    --readers;
}
bool MidiInputRouter::recordingPosition(MidiClock::Position &position) const
{
    auto *lane = target.load();
    return lane && lane->clock.read(position);
}
void MidiInputRouter::quiesceHistory()
{
    const auto start = juce::Time::getMillisecondCounter();
    while (readers.load() && juce::Time::getMillisecondCounter() - start < 100)
        juce::Thread::yield();
    if (readers.load())
        history.failed = true;
}
void MidiInputRouter::stopCapture()
{
    capture.active = false;
    const auto started = juce::Time::getMillisecondCounter();
    // Only the control thread may wait; an in-flight producer must finish publishing before drain.
    while (readers.load() && juce::Time::getMillisecondCounter() - started < 100)
        juce::Thread::yield();
    if (readers.load())
        capture.failed = true;
}
} // namespace vibera
