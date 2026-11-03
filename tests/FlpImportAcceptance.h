// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "FlpImport.h"
#include "AudioFileCheck.h"

// FLP import from a small original fixture built here byte by byte (no FL files).
namespace flp_import_acceptance
{
using namespace vibera;

struct Writer
{
    juce::MemoryOutputStream out;
    void byteEvent(int id, int value) { out.writeByte(char(id)); out.writeByte(char(value)); }
    void wordEvent(int id, int value) { out.writeByte(char(id)); out.writeShort(short(value)); }
    void dwordEvent(int id, juce::uint32 value) { out.writeByte(char(id)); out.writeInt(int(value)); }
    void dataEvent(int id, const juce::MemoryBlock &data)
    {
        out.writeByte(char(id));
        auto size = data.getSize();
        do
        {
            auto b = juce::uint8(size & 0x7f);
            size >>= 7;
            out.writeByte(char(size ? b | 0x80 : b));
        } while (size);
        out.write(data.getData(), data.getSize());
    }
    void textEvent(int id, const juce::String &text)
    {
        juce::MemoryOutputStream utf16;
        for (auto c : text)
            utf16.writeShort(short(c));
        utf16.writeShort(0);
        dataEvent(id, utf16.getMemoryBlock());
    }
};

inline void tests(const juce::File &folder)
{
    const auto sample = folder.getChildFile("flp-kick.wav");
    {
        juce::AudioBuffer<float> pcm(1, 4800);
        pcm.clear();
        pcm.setSample(0, 0, .5f);
        sample.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(sample.createOutputStream().release(), 48000., 1, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, 4800), "FLP fixture sample written");
    }
    Writer w;
    {
        juce::MemoryBlock version("20.5.0.1\0", 9);
        w.dataEvent(199, version);
    }
    w.dwordEvent(156, 128000); // 128 BPM
    w.byteEvent(17, 3);
    w.byteEvent(18, 4);
    // Pattern 1 with two notes on FL Channel 0 (one step, one held).
    w.wordEvent(65, 1);
    w.textEvent(193, "Beat");
    {
        juce::MemoryOutputStream notes;
        auto note = [&](juce::uint32 position, juce::uint32 length, int key, int velocity)
        {
            notes.writeInt(int(position));
            notes.writeShort(0x4000);
            notes.writeShort(0);   // FL Channel index
            notes.writeInt(int(length));
            notes.writeShort(short(key));
            notes.writeShort(0);
            for (int b : {120, 0, 64, 0, 64, velocity, 128, 128})
                notes.writeByte(char(b));
        };
        note(0, 0, 60, 100);
        note(96, 48, 64, 64);
        w.dataEvent(224, notes.getMemoryBlock());
    }
    // Channel 0: sampler with a sample, routed to insert 3; Channel 1: an FL plugin.
    w.wordEvent(64, 0);
    w.byteEvent(21, 0);
    w.byteEvent(22, 3);
    w.textEvent(203, "Kick");
    w.textEvent(196, sample.getFullPathName());
    {
        juce::MemoryOutputStream levels;
        levels.writeInt(6400);
        levels.writeInt(6400);
        levels.writeInt(0);
        levels.writeInt(256);
        w.dataEvent(219, levels.getMemoryBlock());
    }
    w.wordEvent(64, 1);
    w.byteEvent(21, 2);
    w.textEvent(201, "Sytrus");
    w.textEvent(203, "Lead");
    w.wordEvent(99, 0);
    {
        juce::MemoryOutputStream items;
        items.writeInt(384); // beat 4
        items.writeShort(20480);
        items.writeShort(20481); // Pattern 1
        items.writeInt(288);     // 3 beats
        items.writeShort(499 - 2);
        items.writeShort(0);
        items.writeShort(0);
        items.writeShort(0);
        items.writeInt(0);
        items.writeFloat(-1.f);
        items.writeFloat(-1.f);
        w.dataEvent(233, items.getMemoryBlock());
    }
    juce::MemoryOutputStream file;
    file.write("FLhd", 4);
    file.writeInt(6);
    file.writeShort(0);
    file.writeShort(2);
    file.writeShort(96);
    file.write("FLdt", 4);
    file.writeInt(int(w.out.getDataSize()));
    file.write(w.out.getData(), w.out.getDataSize());
    const auto flp = folder.getChildFile("fixture.flp");
    require(flp.replaceWithData(file.getData(), file.getDataSize()), "FLP fixture written");

    Project project;
    FlpImportReport report;
    const auto result = importFlp(flp, project, report);
    require(result.wasOk(), "The fixture imports: " + result.getErrorMessage().toStdString());
    require(project.tempo == 128 && project.numerator == 3 && project.denominator == 4 && report.flVersion == "20.5.0.1",
            "Tempo, time signature and FL version are read");
    require(project.channels.size() == 2 && project.channels[0].name == "Kick" &&
                project.channels[0].source == sample.getFullPathName() && project.channels[0].mixer == 3 &&
                std::abs(project.channels[0].gain - .5f) < 1.e-6f && project.mixer.size() >= 4,
            "The sampler Channel keeps its sample, insert and volume");
    require(project.channels[1].muted && project.channels[1].name.contains("FL plugin") &&
                report.unconvertedPlugins.contains("Sytrus"),
            "An FL plugin Channel becomes a named silent placeholder and is reported");
    require(project.patterns.size() == 1 && project.patterns[0].name == "Beat" && project.patterns[0].notes.size() == 2,
            "The Pattern keeps its name and notes");
    const auto &notes = project.patterns[0].notes;
    require(notes[0].step && notes[0].beat == 0 && notes[1].beat == 1 && notes[1].length == .5 && notes[1].pitch == 64 &&
                notes[1].channel == project.channels[0].id && std::abs(notes[1].velocity - .5f) < 1.e-6f,
            "Note timing (96 PPQ), steps, keys, velocity and Channel mapping are converted");
    require(project.placements.size() == 1 && project.placements[0].beat == 4 && project.placements[0].length == 3 &&
                project.placements[0].lane == 2 && project.placements[0].pattern == project.patterns[0].id,
            "The Playlist Pattern clip lands on its beat, length and track");
    require(project.validate().wasOk(), "The converted project is valid");
    // A WAV holding compressed data (FL's Ogg-in-WAV tag 0x674F) is refused before decoding.
    {
        juce::MemoryOutputStream wav;
        wav.write("RIFF", 4);
        wav.writeInt(36);
        wav.write("WAVEfmt ", 8);
        wav.writeInt(16);
        wav.writeShort(short(0x674f));
        wav.writeShort(1);
        wav.writeInt(44100);
        wav.writeInt(88200);
        wav.writeShort(2);
        wav.writeShort(16);
        wav.write("data", 4);
        wav.writeInt(0);
        const auto ogg = folder.getChildFile("ogg-in-wav.wav");
        require(ogg.replaceWithData(wav.getData(), wav.getDataSize()), "Ogg-in-WAV fixture written");
        require(!decodableAudioFile(ogg) && decodableAudioFile(sample),
                "Ogg-in-WAV is refused; a PCM WAV is accepted");
    }
    const auto bad = folder.getChildFile("not-flp.flp");
    bad.replaceWithText("hello");
    require(importFlp(bad, project, report).failed(), "Non-FLP files are refused");
}
} // namespace flp_import_acceptance
