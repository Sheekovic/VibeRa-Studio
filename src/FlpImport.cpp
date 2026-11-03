// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FlpImport.h"
#include "AudioFileCheck.h"
#include <map>
#include <set>

namespace vibera
{
namespace
{
// FLP event ids (byte < 64, word < 128, dword < 192, else length-prefixed data).
enum : int
{
    channelType = 21,
    channelMixer = 22,
    timeSigNumerator = 17,
    timeSigBeat = 18,
    channelNew = 64,
    patternNew = 65,
    tempoCoarse = 66,
    tempoFine = 93,
    arrangementNew = 99,
    tempo = 156,
    patternName = 193,
    samplePath = 196,
    flVersion = 199,
    pluginName = 201,
    displayName = 203,
    channelLevels = 219,
    patternNotes = 224,
    playlistItems = 233,
};
struct Event
{
    int id = 0;
    juce::uint32 value = 0;
    juce::MemoryBlock data;
};
struct FlChannel
{
    int index = 0, type = 0, mixer = 0;
    juce::String name, plugin, path;
    double gain = .78, pan = 0;
};
juce::uint16 u16(const juce::uint8 *p) { return juce::uint16(p[0] | (p[1] << 8)); }
juce::uint32 u32(const juce::uint8 *p) { return juce::uint32(p[0] | (p[1] << 8) | (p[2] << 16)) | (juce::uint32(p[3]) << 24); }
float f32(const juce::uint8 *p)
{
    const auto bits = u32(p);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
juce::String utf16(const juce::MemoryBlock &data)
{
    juce::String text;
    const auto *p = static_cast<const juce::uint8 *>(data.getData());
    for (size_t i = 0; i + 1 < data.getSize(); i += 2)
        if (const auto c = u16(p + i); c != 0)
            text += juce::String::charToString(juce::juce_wchar(c));
    return text;
}
// %FLStudioFactoryData% and %FLStudioUserData% are FL install / user folders; relative paths
// are relative to the project. Missing files keep their best-guess path (Find missing samples).
juce::String resolveSample(juce::String path, const juce::File &project)
{
    if (path.isEmpty())
        return {};
    path = path.replaceCharacter('\\', '/');
    if (path.startsWithIgnoreCase("%FLStudioFactoryData%"))
    {
        const auto tail = path.fromFirstOccurrenceOf("%", false, false).fromFirstOccurrenceOf("%", false, false);
        juce::Array<juce::File> roots;
        for (char drive = 'C'; drive <= 'Z'; ++drive)
        {
            const juce::String letter = juce::String::charToString(drive) + ":/";
            roots.add(juce::File(letter + "FL Studio"));
            for (const auto &found : juce::File(letter + "Program Files/Image-Line")
                                         .findChildFiles(juce::File::findDirectories, false, "FL Studio*"))
                roots.add(found);
        }
        for (const auto &root : roots)
            if (root.getChildFile(tail.trimCharactersAtStart("/")).existsAsFile())
                return root.getChildFile(tail.trimCharactersAtStart("/")).getFullPathName();
        return (roots.isEmpty() ? juce::File("C:/FL Studio") : roots.getFirst())
            .getChildFile(tail.trimCharactersAtStart("/"))
            .getFullPathName();
    }
    if (path.startsWithIgnoreCase("%FLStudioUserData%"))
    {
        const auto tail = path.fromFirstOccurrenceOf("%", false, false).fromFirstOccurrenceOf("%", false, false);
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("Image-Line/FL Studio")
            .getChildFile(tail.trimCharactersAtStart("/"))
            .getFullPathName();
    }
    if (!juce::File::isAbsolutePath(path))
        return project.getParentDirectory().getChildFile(path).getFullPathName();
    return juce::File(path).getFullPathName();
}
double audioSeconds(const juce::String &path)
{
    if (!decodableAudioFile(juce::File(path)))
        return 0;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(path)));
    return reader && reader->sampleRate > 0 ? double(reader->lengthInSamples) / reader->sampleRate : 0.;
}
} // namespace

juce::String FlpImportReport::summary() const
{
    juce::String text;
    text << "Imported FL Studio " << flVersion << " project: " << channels << " Channels, " << patterns
         << " Patterns (" << notes << " notes), " << patternClips << " Pattern clips, " << audioClips << " audio clips.";
    if (!unconvertedPlugins.isEmpty())
        text << " Not converted (FL plugins): " << unconvertedPlugins.joinIntoString(", ") << ".";
    if (!missingSamples.isEmpty())
        text << " " << missingSamples.size() << " sample(s) missing.";
    if (!unreadableSamples.isEmpty())
        text << " " << unreadableSamples.size() << " sample(s) in an unsupported format (e.g. .wv) left silent.";
    if (automationClipsSkipped > 0)
        text << " " << automationClipsSkipped << " FL automation clip(s) not converted.";
    if (unusedChannelsSkipped > 0)
        text << " " << unusedChannelsSkipped << " unused Channel(s) left out (Channel limit).";
    return text;
}

juce::Result importFlp(const juce::File &file, Project &result, FlpImportReport &report)
{
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes) || bytes.getSize() < 22)
        return juce::Result::fail("Cannot read " + file.getFileName());
    const auto *data = static_cast<const juce::uint8 *>(bytes.getData());
    const size_t size = bytes.getSize();
    if (std::memcmp(data, "FLhd", 4) != 0)
        return juce::Result::fail(file.getFileName() + " is not an FL Studio project");
    const size_t headerLength = u32(data + 4);
    if (headerLength < 6 || 8 + headerLength + 8 > size)
        return juce::Result::fail("Damaged FLP header");
    const int ppq = std::max(1, int(u16(data + 12)));
    size_t pos = 8 + headerLength;
    if (std::memcmp(data + pos, "FLdt", 4) != 0)
        return juce::Result::fail("Damaged FLP data chunk");
    const size_t end = std::min(size, pos + 8 + size_t(u32(data + pos + 4)));
    pos += 8;
    std::vector<Event> events;
    while (pos < end)
    {
        Event e;
        e.id = data[pos++];
        if (e.id < 64)
        {
            if (pos + 1 > end)
                break;
            e.value = data[pos];
            pos += 1;
        }
        else if (e.id < 128)
        {
            if (pos + 2 > end)
                break;
            e.value = u16(data + pos);
            pos += 2;
        }
        else if (e.id < 192)
        {
            if (pos + 4 > end)
                break;
            e.value = u32(data + pos);
            pos += 4;
        }
        else
        {
            size_t length = 0;
            for (int shift = 0; pos < end && shift < 35; shift += 7)
            {
                const auto b = data[pos++];
                length |= size_t(b & 0x7f) << shift;
                if (!(b & 0x80))
                    break;
            }
            if (length > end - pos)
                return juce::Result::fail("Damaged FLP event");
            e.data.append(data + pos, length);
            pos += length;
        }
        events.push_back(std::move(e));
    }

    Project p = Project::empty();
    p.channels.clear();
    p.patterns.clear();
    p.name = file.getFileNameWithoutExtension();
    int coarse = 0, fine = 0;
    double bpm = 0;
    std::vector<FlChannel> flChannels;
    std::map<int, Pattern> patterns;
    std::map<int, juce::String> patternNames;
    int currentPattern = -1, arrangements = 0;
    bool inChannel = false;
    const Event *playlist = nullptr;
    for (const auto &e : events)
    {
        switch (e.id)
        {
        case flVersion:
            report.flVersion = juce::String::fromUTF8(static_cast<const char *>(e.data.getData()),
                                                      int(e.data.getSize()))
                                   .trimCharactersAtEnd(juce::String::charToString(0));
            break;
        case tempo:
            bpm = e.value / 1000.;
            break;
        case tempoCoarse:
            coarse = int(e.value);
            break;
        case tempoFine:
            fine = int(e.value);
            break;
        case timeSigNumerator:
            p.numerator = juce::jlimit(1, 32, int(e.value));
            break;
        case timeSigBeat:
            if (e.value == 2 || e.value == 4 || e.value == 8 || e.value == 16)
                p.denominator = int(e.value);
            break;
        case channelNew:
            flChannels.push_back({});
            flChannels.back().index = int(e.value);
            inChannel = true;
            break;
        case patternNew:
            currentPattern = int(e.value);
            inChannel = false;
            break;
        case arrangementNew:
            ++arrangements;
            inChannel = false;
            break;        case 98: case 147: case 154: case 235: case 236: case 238: // Mixer/track sections end a Channel.
            inChannel = false;
            break;
        case channelType:
            if (inChannel)
                flChannels.back().type = int(e.value);
            break;
        case channelMixer:
            if (inChannel)
                flChannels.back().mixer = int(e.value);
            break;
        case channelLevels:
            if (inChannel && e.data.getSize() >= 8)
            {
                const auto *d = static_cast<const juce::uint8 *>(e.data.getData());
                flChannels.back().pan = juce::jlimit(-1., 1., (int(u32(d)) - 6400) / 6400.);
                flChannels.back().gain = juce::jlimit(0., 1., int(u32(d + 4)) / 12800.);
            }
            break;
        case samplePath:
            if (inChannel)
                flChannels.back().path = utf16(e.data);
            break;
        case pluginName:
            if (inChannel)
                flChannels.back().plugin = utf16(e.data);
            else if (const auto name = utf16(e.data); name.isNotEmpty())
                report.unconvertedPlugins.addIfNotAlreadyThere(name);
            break;
        case displayName:
            if (inChannel)
                flChannels.back().name = utf16(e.data);
            break;
        case patternName:
            if (currentPattern >= 0)
                patternNames[currentPattern] = utf16(e.data);
            break;
        case patternNotes:
            if (currentPattern >= 0)
            {
                auto &pattern = patterns[currentPattern];
                const auto *d = static_cast<const juce::uint8 *>(e.data.getData());
                for (size_t i = 0; i + 24 <= e.data.getSize(); i += 24)
                {
                    const auto *n = d + i;
                    Note note;
                    note.channel = u16(n + 6); // FL Channel index; mapped to a VibeRa id below.
                    note.beat = u32(n) / double(ppq);
                    const auto length = u32(n + 8);
                    note.step = length == 0;
                    note.length = note.step ? .25 : length / double(ppq);
                    note.pitch = juce::jlimit(0, 127, int(u16(n + 12)));
                    note.fineCents = juce::jlimit(-100., 100., double(int(n[16]) - 120));
                    note.pan = juce::jlimit(-1., 1., (int(n[20]) - 64) / 64.);
                    note.velocity = float(juce::jlimit(0., 1., n[21] / 128.));
                    note.modX = juce::jlimit(0., 1., n[22] / 255.);
                    note.modY = juce::jlimit(0., 1., n[23] / 255.);
                    pattern.notes.push_back(note);
                }
            }
            break;
        case playlistItems:
            if (arrangements <= 1 && playlist == nullptr)
                playlist = &e;
            break;
        default:
            break;
        }
    }
    p.tempo = juce::jlimit(20., 400., bpm > 0 ? bpm : coarse > 0 ? coarse + fine / 1000. : 140.);

    // Channels: samplers keep their sample, audio-clip Channels their file; FL plugins become
    // named silent placeholders (their sound lives inside FL).
    // FL automation clips (type 5) have no VibeRa Channel equivalent here; beyond the Rack
    // capacity only Channels that notes or Playlist items use are kept.
    std::set<int> used;
    for (const auto &[index, pattern] : patterns)
        for (const auto &note : pattern.notes)
            used.insert(int(note.channel));
    const size_t itemSize = report.flVersion.upToFirstOccurrenceOf(".", false, false).getIntValue() >= 21 ? 60 : 32;
    if (playlist)
        for (size_t i = 0; i + itemSize <= playlist->data.getSize(); i += itemSize)
            used.insert(u16(static_cast<const juce::uint8 *>(playlist->data.getData()) + i + 6));
    report.automationClipsSkipped = int(std::count_if(flChannels.begin(), flChannels.end(),
                                                      [](const auto &c) { return c.type == 5; }));
    std::erase_if(flChannels, [](const auto &c) { return c.type == 5; });
    if (flChannels.size() > size_t(Project::maxChannels))
    {
        const auto before = flChannels.size();
        std::erase_if(flChannels, [&](const auto &c) { return !used.count(c.index); });
        if (flChannels.size() > size_t(Project::maxChannels))
            flChannels.resize(size_t(Project::maxChannels));
        report.unusedChannelsSkipped = int(before - flChannels.size());
    }
    int highestInsert = 0;
    std::map<int, Id> channelIds;
    for (const auto &fl : flChannels)
    {
        Channel channel;
        channel.id = p.allocate();
        const auto path = resolveSample(fl.path, file);
        channel.name = fl.name.isNotEmpty() ? fl.name
                       : fl.plugin.isNotEmpty() ? fl.plugin
                       : path.isNotEmpty()      ? juce::File(path).getFileNameWithoutExtension()
                                                : "Channel " + juce::String(fl.index + 1);
        channel.mixer = juce::jlimit(0, 128, fl.mixer);
        channel.gain = float(fl.gain);
        channel.pan = float(fl.pan);
        highestInsert = std::max(highestInsert, channel.mixer);
        // A file that exists but cannot be decoded stays a named silent placeholder, so the
        // project still plays and exports.
        const bool unreadable = path.isNotEmpty() && juce::File(path).existsAsFile() && !(audioSeconds(path) > 0);
        if (unreadable)
            report.unreadableSamples.add(path);
        if (unreadable)
        {
            channel.name << " (unsupported sample)";
            channel.muted = true;
            channel.source = "builtin:kick";
        }
        else if (fl.type == 4 && path.isNotEmpty())
        {
            channel.audioClip = true;
            channel.source = path;
            channel.audioDuration = audioSeconds(path);
        }
        else if (fl.type == 0 && path.isNotEmpty())
            channel.source = path;
        else
        {
            if (fl.plugin.isNotEmpty())
                report.unconvertedPlugins.addIfNotAlreadyThere(fl.plugin);
            channel.name << " (FL plugin)";
            channel.muted = true;
            channel.source = "builtin:kick"; // Placeholder sound; muted so it stays silent.
        }
        if (channel.source.isNotEmpty() && !channel.source.startsWith("builtin:") &&
            !juce::File(channel.source).existsAsFile())
            report.missingSamples.add(channel.source);
        channelIds[fl.index] = channel.id;
        p.channels.push_back(std::move(channel));
    }
    while (int(p.mixer.size()) <= highestInsert)
        p.mixer.push_back({int(p.mixer.size()), "Insert " + juce::String(int(p.mixer.size())), 1.f, 0.f, false});

    // Patterns (FL numbers them from 1), notes mapped to the imported Channels.
    std::map<int, Id> patternIds;
    for (auto &[index, pattern] : patterns)
    {
        pattern.id = p.allocate();
        pattern.name = patternNames.count(index) && patternNames[index].isNotEmpty() ? patternNames[index]
                                                                                    : "Pattern " + juce::String(index);
        double last = 0;
        std::vector<Note> kept;
        for (auto note : pattern.notes)
        {
            const auto found = channelIds.find(int(note.channel));
            const auto *target = found == channelIds.end() ? nullptr : p.channel(found->second);
            if (!target || target->audioClip)
                continue;
            note.id = p.allocate();
            note.channel = found->second;
            last = std::max(last, note.beat + note.length);
            kept.push_back(note);
        }
        pattern.notes = std::move(kept);
        const double bar = p.numerator * 4. / p.denominator;
        pattern.length = std::max(bar, std::ceil(last / bar - 1.e-9) * bar);
        report.notes += int(pattern.notes.size());
        patternIds[index] = pattern.id;
        p.patterns.push_back(std::move(pattern));
    }
    for (const auto &[index, name] : patternNames)
        if (!patternIds.count(index))
        {
            patternIds[index] = p.allocate();
            p.patterns.push_back({patternIds[index], name.isNotEmpty() ? name : "Pattern " + juce::String(index), 4, {}});
        }
    if (p.patterns.empty())
        p.patterns.push_back({p.allocate(), "Pattern 1", 4, {}});

    // Playlist: 32-byte items (60 from FL 21). Pattern items are base 20480 + pattern index;
    // others name a Channel (audio clips). Tracks count down from 499.
    if (playlist)
    {
        const auto *d = static_cast<const juce::uint8 *>(playlist->data.getData());
        const double secondsPerBeat = 60. / p.tempo;
        for (size_t i = 0; i + itemSize <= playlist->data.getSize(); i += itemSize)
        {
            const auto *item = d + i;
            const double beat = u32(item) / double(ppq), length = u32(item + 8) / double(ppq);
            const int base = u16(item + 4), index = u16(item + 6);
            const int lane = juce::jlimit(0, 127, 499 - int(u16(item + 12)));
            const bool muted = false; // Item mute flags are not decoded yet.
            const float start = f32(item + 24);
            if (!(length > 0))
                continue;
            if (index > base)
            {
                const auto found = patternIds.find(index - base);
                if (found == patternIds.end())
                    continue;
                p.placements.push_back({p.allocate(), found->second, lane, beat, length,
                                        start > 0 ? start / double(ppq) : 0., muted});
                ++report.patternClips;
            }
            else if (const auto found = channelIds.find(index); found != channelIds.end())
            {
                auto *source = p.channel(found->second);
                if (!source || !source->audioClip)
                    continue;
                if (!(source->audioDuration > 0))
                    source->audioDuration = length * secondsPerBeat; // Missing file: keep the clip's span.
                const double duration = std::min(length * secondsPerBeat, source->audioDuration);
                AudioPlacement clip{p.allocate(), source->id, lane, beat, 0., duration, muted};
                p.audioPlacements.push_back(clip);
                ++report.audioClips;
            }
        }
    }
    // Audio-clip Channels whose file is missing and that no clip uses still need a duration.
    for (auto &channel : p.channels)
        if (channel.audioClip && !(channel.audioDuration > 0))
            channel.audioDuration = 1;
    report.channels = int(p.channels.size());
    report.patterns = int(p.patterns.size());
    if (auto valid = p.validate(); valid.failed())
        return juce::Result::fail("The converted project is invalid: " + valid.getErrorMessage());
    result = std::move(p);
    return juce::Result::ok();
}
} // namespace vibera
