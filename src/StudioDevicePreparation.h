// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
namespace vibera
{
struct Studio::DeferredDevicePreparation
{
    enum class Phase { closing, capturing, preparing, draining, completed, failed, cancelled };
    struct Binding
    {
        Id id = 0;
        juce::String descriptor, instance, closeOperation;
        std::shared_ptr<PluginHost> host;
        unsigned capturedRevision = 0;
        bool requiresCapture = false;
    };
    std::uint64_t operation = 0, revision = 0;
    juce::String identity, code, message;
    Project source, captured;
    std::vector<Binding> bindings;
    std::optional<PendingInstrumentOpening> instrumentIntent;
    bool patternMode = true;
    Id pattern = 0;
    double rate = 0;
    int block = 0;
    Phase phase = Phase::closing;
    bool cancelRequested = false, captureAdmitted = false, captureFinished = false, captureValidated = false;
    bool documentCommitted = false, captureApplied = false, playRequested = false;
    bool nativePrepared = false, workersReusedOnly = false;
    juce::Result captureResult = juce::Result::ok();
    juce::String capturePollExceptionCode{"device.capturePollException"};
    juce::String capturePollExceptionMessage{"Native snapshot delivery failed; retained source is draining and recoverable"};
    juce::String exceptionCode{"device.preparationException"};
    juce::String exceptionMessage{"Preparation exception; source workers and document are retained"};
};
}
