// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MissingMedia.h"
#include "Commands.h"

// Missing samples: a project with a missing file still saves (keeping the last known path),
// lists the missing Channels, finds the files again by name or collected-media content, and
// relinks them in one Undo step without touching other Channel settings.
namespace missing_media_acceptance
{
using namespace vibera;

inline void writeTone(const juce::File &file, float frequency)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.release(), 48000, 1, 24, {}, 0));
    juce::AudioBuffer<float> buffer(1, 4800);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, .5f * std::sin(float(i) * frequency / 48000.f * juce::MathConstants<float>::twoPi));
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

inline void tests(const juce::File &folder)
{
    const auto library = folder.getChildFile("library"), projectFolder = folder.getChildFile("project");
    const auto kick = library.getChildFile("Kick 01.wav"), snare = library.getChildFile("Snare.wav");
    writeTone(kick, 60);
    writeTone(snare, 200);
    auto project = Project::empty();
    project.channels[0].source = kick.getFullPathName();
    project.channels[0].name = "MY KICK";
    project.channels[0].gain = .5f;
    project.channels[1].source = snare.getFullPathName();
    require(missingMedia(project).empty(), "Nothing is missing at first");
    const auto file = projectFolder.getChildFile("song.vibra");
    require(saveProject(project, file).wasOk(), "Save collects the samples into media/");

    // The collected copy of the kick and the original snare go missing.
    Project loaded;
    require(loadProject(file, loaded).wasOk(), "Load the saved project");
    const juce::File collectedKick(loaded.channels[0].source);
    require(collectedKick.getParentDirectory().getFileName() == "media" && collectedKick.existsAsFile(),
            "The kick was collected under media/");
    const auto backup = folder.getChildFile("elsewhere").getChildFile("renamed kick.wav");
    backup.getParentDirectory().createDirectory();
    require(collectedKick.copyFileTo(backup) && collectedKick.deleteFile(), "Move the collected kick away");
    loaded.channels[1].source = folder.getChildFile("old-machine").getChildFile("Snare.wav").getFullPathName();
    require(saveProject(loaded, file).wasOk(), "A project with missing samples still saves");
    Project reopened;
    require(loadProject(file, reopened).wasOk(), "Reopen it");
    const auto missing = missingMedia(reopened);
    require(missing.size() == 2 && missing[0] == reopened.channels[0].id && missing[1] == reopened.channels[1].id,
            "Both missing Channels are listed after reopening");
    require(reopened.channels[1].source.endsWith("Snare.wav"), "The last known path is kept for finding it again");

    // Search a folder: the snare by name, the collected kick by content (renamed file).
    const auto search = folder.getChildFile("search");
    require(search.getChildFile("drums").createDirectory().wasOk() && search.getChildFile("misc").createDirectory().wasOk() &&
                snare.copyFileTo(search.getChildFile("drums").getChildFile("snare.WAV")) &&
                backup.copyFileTo(search.getChildFile("misc").getChildFile("renamed kick.wav")),
            "Prepare the search folder");
    const auto found = findMissingMedia(reopened, search);
    require(found.size() == 2, "Both files are found");
    for (const auto &[id, path] : found)
        require(juce::File(path).existsAsFile() &&
                    (id == reopened.channels[0].id ? path.endsWith("renamed kick.wav") : path.endsWithIgnoreCase("snare.wav")),
                "The kick is found by content, the snare by name");
    require(findMissingMedia(reopened, folder.getChildFile("nowhere")).empty(), "A missing folder finds nothing");

    Controller controller;
    controller.replace(reopened);
    const auto before = controller.project.state();
    CommandArgs relink;
    relink.relinks = found;
    require(controller.execute("media.relink", relink).wasOk() && missingMedia(controller.project).empty(),
            "Relinking leaves nothing missing");
    require(controller.project.channels[0].name == "MY KICK" && controller.project.channels[0].gain == .5f,
            "Relinking keeps the Channel's name and settings");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "Relinking is one exact Undo step");
    CommandArgs bad;
    bad.relinks = {{controller.project.channels[0].id, "relative.wav"}};
    require(controller.execute("media.relink", bad).failed(), "Relative paths are refused");
}
} // namespace missing_media_acceptance
