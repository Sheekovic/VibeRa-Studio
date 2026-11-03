// SPDX-License-Identifier: AGPL-3.0-or-later
// Fault containment fixture, never a plugin compatibility substitute.
#include "WorkerProcess.h"
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
int main(int argc, char **argv)
{
    if (argc == 3 && juce::String(argv[1]) == "--parent-crash")
    {
        vibera::WorkerProcess owned;
        auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        if (!owned.start({exe.getFullPathName(), "--scan", "hang.vst3", "unused.xml"}))
            return 2;
        juce::File(argv[2]).replaceWithText(juce::String(owned.processId()));
        Sleep(300);
        ExitProcess(4); // simulate an abrupt parent exit; job owns only this worker
    }
    if (argc != 4 || juce::String(argv[1]) != "--scan")
        return 2;
    juce::File candidate(
        juce::String(argv[2]).containsChar(':')
            ? juce::String(argv[2])
            : juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]).getFullPathName());
    if (candidate.getFileName() == "crash.vst3")
        ExitProcess(0xc0000005); // controlled crash exit, no WER popup
    if (candidate.getFileName() == "hang.vst3")
    {
        for (;;)
            Sleep(1000);
    }
    juce::PluginDescription type;
    type.name = "Containment fixture";
    type.pluginFormatName = "VST3";
    type.fileOrIdentifier = candidate.getFullPathName();
    type.uniqueId = 42;
    juce::XmlElement result("VST3_SCAN");
    result.addChildElement(type.createXml().release());
    return result.writeTo(juce::File(argv[3])) ? 0 : 2;
}
