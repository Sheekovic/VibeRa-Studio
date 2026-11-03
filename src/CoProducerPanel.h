// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CoProducer.h"
#include "CoProducerWorker.h"
#include "MidiScore.h"
#include "Studio.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace vibera
{
class CoProducerPanel final : public juce::Component, private juce::Timer
{
    juce::Component::SafePointer<Studio> studio;
    juce::ComboBox destination;
    std::vector<Id> destinationIds;
    juce::Slider start, bars;
    juce::TextButton contextChannels{"Context Channels: Auto"};
    enum class ContextMode
    {
        automatic,
        all,
        custom
    };
    ContextMode contextMode = ContextMode::automatic;
    std::vector<Id> chosenContext;
    juce::ToggleButton lockSelection{"Protect selected notes"};
    juce::ToggleButton replaceRegion{"Replace Pattern region (all linked placements)"};
    juce::TextEditor chord, pitchClasses, summary;
    juce::ComboBox mappingChannel;
    juce::TextEditor mappingProgram, mappingPitches;
    juce::Label mappingLabel;
    juce::TextButton saveMapping{"Set mapping"}, clearMapping{"Clear mapping"}, runtimeButton{"Runtime..."},
        clearChordCorrection{"Clear chord correction"};
    std::vector<coproducer::ModelInstrumentMapping> mappings;
    juce::String mappingProject;
    juce::Label destinationLabel, startLabel, barsLabel, chordLabel, pitchesLabel, providerLabel;
    juce::TextButton inspect{"Inspect context"};
    juce::ComboBox model, family, variation, candidateChoice;
    juce::Slider seed, variationAmount;
    juce::Label seedLabel, variationAmountLabel;
    juce::TextButton generate{"Generate"}, cancel{"Cancel"}, preview{"Audition"},
        stopPreviewButton{"Stop preview"}, acceptButton{"Accept new part"}, discard{"Discard"},
        modelsButton{"Models..."};
    coproducer::ProviderConfig provider;
    coproducer::ProviderConfig magentaProvider, anticipationProvider;
    int providerSelection = 1;
    coproducer::Options lastOptions;
    coproducer::Request lastRequest;
    std::vector<coproducer::Candidate> candidates;
    std::unique_ptr<coproducer::GenerationJob> generation;
    std::unique_ptr<juce::FileChooser> modelChooser;
    bool generateAfterContext = false, previewActive = false, previewStarted = false;
    double previousCursor = 0;
    std::uint64_t previewRevision = 0;
    juce::String previewProject;
    juce::ThreadPool workers{1};
    struct Work
    {
        std::mutex mutex;
        juce::String result;
        coproducer::Options options;
        coproducer::Snapshot snapshot;
        bool valid = false;
        bool ready = false;
    };
    std::shared_ptr<Work> work;

    void stopPreview(bool preserveTransport = false)
    {
        if (!previewActive || !studio)
            return;
        previewActive = false;
        stopPreviewButton.setEnabled(false);
        if (studio->controller.projectIdentity != previewProject)
        {
            previewStarted = false;
            return;
        }
        auto result =
            studio->audio.rebuild(studio->controller.project, studio->patternMode, studio->selectedPattern);
        if (previewStarted && !preserveTransport)
        {
            studio->audio.stop();
            studio->audio.seekBeat(previousCursor);
        }
        studio->needsRebuild = result.failed();
        if (result.failed())
            studio->status = result.getErrorMessage();
        else
            studio->engineRevision = studio->controller.revision;
        previewStarted = false;
    }
    void dropCandidates()
    {
        stopPreview();
        candidates.clear();
        candidateChoice.clear();
        preview.setEnabled(false);
        acceptButton.setEnabled(false);
        discard.setEnabled(false);
    }
    void setBusy(bool busy)
    {
        for (auto *control : std::initializer_list<juce::Component *>{
                 &destination, &start, &bars, &contextChannels, &lockSelection, &chord, &pitchClasses, &model,
                 &variation, &seed, &modelsButton, &generate, &inspect, &replaceRegion})
            control->setEnabled(!busy);
        family.setEnabled(!busy && model.getSelectedId() == 2);
        variation.setEnabled(!busy && model.getSelectedId() == 2);
        variationAmount.setEnabled(!busy && model.getSelectedId() == 2 && variation.getSelectedId() == 2);
        for (auto *control : std::initializer_list<juce::Component *>{
                 &mappingChannel, &mappingProgram, &mappingPitches, &saveMapping, &clearMapping,
                 &runtimeButton, &clearChordCorrection})
            control->setEnabled(!busy);
        cancel.setEnabled(busy && bool(generation));
    }
    juce::Result currentOptions(coproducer::Options &options)
    {
        if (!studio)
            return juce::Result::fail("Project window closed.");
        options.projectIdentity = studio->controller.projectIdentity;
        options.revision = studio->controller.revision;
        options.pluginRuntime = studio->audio.pluginRuntimeStates();
        options.selection.patternMode = studio->patternMode;
        options.selection.pattern = studio->selectedPattern;
        options.selection.destination = 0;
        const auto &project = studio->controller.project;
        const auto index = destination.getSelectedId() - 1;
        if (index >= 0 && index < int(destinationIds.size()))
            options.selection.destination = destinationIds[size_t(index)];
        const double bar = project.numerator * 4.0 / project.denominator;
        options.selection.region = {start.getValue(), bars.getValue() * bar};
        options.selection.notes = studio->selectedNotes;
        options.selection.placements = studio->selectedClips;
        if (lockSelection.getToggleState())
            options.lockedNotes = studio->selectedNotes;
        options.destinationProfile.family =
            model.getSelectedId() == 2 ? family.getText().toLowerCase() : "melody";
        options.destinationProfile.provenance = coproducer::Provenance::userSpecified;
        if (model.getSelectedId() == 3)
        {
            options.destinationProfile.family = "explicit GM mapping";
            options.modelMappings = mappings;
            options.preceding = options.following = project.tempo / 12.0; // Five physical seconds.
        }
        if (contextMode != ContextMode::automatic)
        {
            options.automaticTracks = false;
            options.tracks = effectiveContext();
        }
        if (chord.getText().isNotEmpty())
        {
            coproducer::Chord correction{options.selection.region.start,
                                         options.selection.region.length,
                                         chord.getText(),
                                         {},
                                         coproducer::Provenance::userSpecified};
            auto tokens = juce::StringArray::fromTokens(pitchClasses.getText(), ", ", "");
            tokens.removeEmptyStrings();
            for (const auto &token : tokens)
            {
                if (!token.containsOnly("0123456789") || token.getIntValue() > 11)
                {
                    return juce::Result::fail("Chord pitch classes must be integers from 0 to 11.");
                }
                correction.pitchClasses.push_back(token.getIntValue());
            }
            if (correction.pitchClasses.empty())
            {
                return juce::Result::fail(
                    "Supply chord pitch classes (0=C, 1=C#, ... 11=B). No harmony is inferred from a label.");
            }
            options.chords.push_back(std::move(correction));
        }
        return juce::Result::ok();
    }
    Id mappingDestination() const
    {
        const int index = mappingChannel.getSelectedId() - 1;
        return index >= 0 && index < int(destinationIds.size()) ? destinationIds[size_t(index)] : 0;
    }
    void loadMapping()
    {
        mappingProgram.clear();
        mappingPitches.clear();
        for (const auto &mapping : mappings)
            if (mapping.channel == mappingDestination())
            {
                mappingProgram.setText(juce::String(mapping.program), false);
                juce::StringArray pitches;
                auto values = mapping.pitches;
                std::sort(values.begin(), values.end());
                for (size_t index = 0; index < values.size(); ++index)
                {
                    const int first = values[index];
                    while (index + 1 < values.size() && values[index + 1] == values[index] + 1)
                        ++index;
                    pitches.add(juce::String(first) + (values[index] == first
                                                           ? juce::String{}
                                                           : "-" + juce::String(values[index])));
                }
                mappingPitches.setText(pitches.joinIntoString(","), false);
            }
    }
    void editMapping(bool clear)
    {
        if (!studio || work || generation || !studio->controller.project.channel(mappingDestination()))
            return;
        coproducer::ModelInstrumentMapping value;
        value.channel = mappingDestination();
        value.provenance = coproducer::Provenance::userSpecified;
        if (!clear)
        {
            const auto text = mappingProgram.getText().trim();
            if (text.isEmpty() || text.length() > 3 || !text.containsOnly("0123456789") ||
                text.getIntValue() > 128)
            {
                summary.setText("Enter a zero-based GM program 0..127, or 128 for percussion.");
                return;
            }
            value.program = text.getIntValue();
            auto tokens = juce::StringArray::fromTokens(mappingPitches.getText(), ", ", "");
            tokens.removeEmptyStrings();
            if (tokens.isEmpty() || tokens.size() > 128 || mappingPitches.getText().length() > 1024)
            {
                summary.setText("Supply permitted MIDI pitches, for example 48-83 or 36,38,42,46.");
                return;
            }
            for (const auto &token : tokens)
            {
                auto ends = juce::StringArray::fromTokens(token, "-", "");
                if (ends.size() < 1 || ends.size() > 2 ||
                    std::any_of(ends.begin(), ends.end(),
                                [](const auto &part)
                                {
                                    return part.isEmpty() || part.length() > 3 ||
                                           !part.containsOnly("0123456789") || part.getIntValue() > 127;
                                }))
                {
                    summary.setText("Permitted pitches must be integers/ranges from 0 to 127.");
                    return;
                }
                const int first = ends[0].getIntValue(), last = ends[ends.size() - 1].getIntValue();
                if (first > last)
                {
                    summary.setText("Pitch ranges must run from low to high.");
                    return;
                }
                for (int pitch = first; pitch <= last; ++pitch)
                    if (std::find(value.pitches.begin(), value.pitches.end(), pitch) == value.pitches.end())
                        value.pitches.push_back(pitch);
            }
        }
        dropCandidates();
        std::erase_if(mappings, [&](const auto &mapping) { return mapping.channel == value.channel; });
        if (!clear)
            mappings.push_back(std::move(value));
        loadMapping();
        summary.setText(clear ? "Mapping cleared; this Channel's capabilities are unknown."
                              : "Explicit mapping set for this panel. Inspect/generate to use it. Source "
                                "notes are unchanged.");
    }
    void showProviderControls()
    {
        const bool anticipation = model.getSelectedId() == 3;
        clearChordCorrection.setVisible(anticipation && chord.getText().isNotEmpty());
        for (auto *control :
             std::initializer_list<juce::Component *>{&chordLabel, &chord, &pitchesLabel, &pitchClasses})
            control->setVisible(!anticipation);
        for (auto *control :
             std::initializer_list<juce::Component *>{&mappingLabel, &mappingChannel, &mappingProgram,
                                                      &mappingPitches, &saveMapping, &clearMapping})
            control->setVisible(anticipation);
        // Supplied corrections remain authoritative. They reject unsupported providers until explicitly
        // cleared.
        setBusy(bool(work || generation));
        resized();
    }
    std::vector<Id> effectiveContext() const
    {
        if (!studio)
            return {};
        std::vector<Id> result;
        for (const auto &channel : studio->controller.project.channels)
            if (contextMode == ContextMode::all ||
                (contextMode == ContextMode::automatic &&
                 coproducer::channelAudible(studio->controller.project, channel.id)) ||
                (contextMode == ContextMode::custom &&
                 std::find(chosenContext.begin(), chosenContext.end(), channel.id) != chosenContext.end()))
                result.push_back(channel.id);
        return result;
    }
    void chooseContext(int choice, const std::vector<Id> &menuChannels)
    {
        if (!studio || work || generation || choice == 0)
            return;
        if (choice == 1)
            contextMode = ContextMode::automatic;
        else if (choice == 2)
            contextMode = ContextMode::all;
        else if (choice >= 100 && choice - 100 < int(menuChannels.size()))
        {
            const auto id = menuChannels[size_t(choice - 100)];
            if (!studio->controller.project.channel(id))
                return;
            auto selected = effectiveContext();
            auto found = std::find(selected.begin(), selected.end(), id);
            if (found == selected.end())
                selected.push_back(id);
            else
                selected.erase(found);
            chosenContext = std::move(selected);
            contextMode = ContextMode::custom;
        }
        else
            return;
        dropCandidates();
        contextChannels.setButtonText(contextMode == ContextMode::automatic ? "Context Channels: Auto"
                                      : contextMode == ContextMode::all
                                          ? "Context Channels: All (incl. muted)"
                                          : "Context Channels: Custom (" +
                                                juce::String(int(effectiveContext().size())) + ")");
        summary.setText("Context Channels changed. Inspect or generate to read the new selection.");
    }
    void showContextMenu(bool embeddedForTest = false)
    {
        if (!studio || work || generation)
            return;
        juce::PopupMenu menu;
        menu.addItem(1, "Automatic: audible Channels", true, contextMode == ContextMode::automatic);
        menu.addItem(2, "All Channels (include muted material)", true, contextMode == ContextMode::all);
        menu.addSeparator();
        menu.addSectionHeader("Toggle a Channel to choose custom context");
        const auto selected = effectiveContext();
        std::vector<Id> ids;
        for (const auto &channel : studio->controller.project.channels)
        {
            const bool checked = std::find(selected.begin(), selected.end(), channel.id) != selected.end();
            menu.addItem(100 + int(ids.size()),
                         channel.name + (coproducer::channelAudible(studio->controller.project, channel.id)
                                             ? ""
                                             : " [muted/zero gain]"),
                         true, checked);
            ids.push_back(channel.id);
        }
        auto safe = juce::Component::SafePointer<CoProducerPanel>(this);
        auto menuOptions = juce::PopupMenu::Options().withTargetComponent(contextChannels);
        if (embeddedForTest)
            menuOptions = menuOptions.withParentComponent(this);
        menu.showMenuAsync(menuOptions,
                           [safe, ids = std::move(ids)](int choice)
                           {
                               if (safe)
                                   safe->chooseContext(choice, ids);
                           });
    }
    void collectContext()
    {
        if (!studio || work || generation)
            return;
        coproducer::Options options;
        auto result = currentOptions(options);
        if (result.failed())
        {
            summary.setText(result.getErrorMessage());
            generateAfterContext = false;
            return;
        }
        auto immutable = studio->controller.project;
        work = std::make_shared<Work>();
        auto pending = work;
        setBusy(true);
        summary.setText("Reading immutable musical context...");
        workers.addJob(
            [immutable = std::move(immutable), options = std::move(options), pending]()
            {
                coproducer::Snapshot snapshot;
                const auto result = coproducer::collect(immutable, options, snapshot);
                std::lock_guard guard(pending->mutex);
                pending->result = result.wasOk() ? snapshot.summary() : result.getErrorMessage();
                pending->valid = result.wasOk();
                pending->options = options;
                pending->snapshot = std::move(snapshot);
                pending->ready = true;
            });
    }
    void launchGeneration()
    {
        if (!studio)
            return;
        const auto *channel = studio->controller.project.channel(lastOptions.selection.destination);
        if (channel && (channel->source == "builtin:kick" || channel->source == "builtin:snare" ||
                        channel->source == "builtin:hat"))
        {
            summary.setText("This built-in one-shot destination's pitch/drum behavior is unqualified. "
                            "Choose a MIDI instrument with a supported mapping.");
            return;
        }
        lastRequest.role = model.getSelectedId() == 1 ? "lead" : "native part";
        lastRequest.variation =
            model.getSelectedId() == 1 ? "continuation" : variation.getText().toLowerCase();
        lastRequest.variationAmount = variationAmount.getValue() / 100.0;
        lastRequest.seed = std::uint64_t(seed.getValue());
        lastRequest.candidateCount = 2;
        provider.model = model.getSelectedId() == 1   ? "basic_rnn"
                         : model.getSelectedId() == 2 ? "multitrack_chords"
                                                      : "anticipation-small";
        provider.cache = provider.installation.getChildFile("jobs");
        generation = std::make_unique<coproducer::GenerationJob>();
        auto result = generation->start(lastRequest, provider);
        if (result.failed())
        {
            generation.reset();
            setBusy(false);
            summary.setText(result.getErrorMessage());
            return;
        }
        setBusy(true);
        providerLabel.setText("Generating locally. The project remains unchanged.",
                              juce::dontSendNotification);
    }
    void auditionCandidate()
    {
        if (!studio || candidates.empty())
            return;
        stopPreview();
        coproducer::Options current;
        auto result = currentOptions(current);
        if (result.wasOk())
            result = coproducer::checkFresh(studio->controller.project, current, lastRequest.snapshot);
        if (result.failed() || studio->controller.projectIdentity != lastRequest.snapshot.projectIdentity)
        {
            summary.setText("Context changed. Generate again before auditioning.");
            return;
        }
        const int index = candidateChoice.getSelectedId() - 1;
        if (index < 0 || index >= int(candidates.size()))
            return;
        const auto &selection = lastRequest.snapshot.selection;
        auto temporary = studio->controller.project;
        if (replaceRegion.getToggleState())
        {
            Controller previewController;
            previewController.project = temporary;
            previewController.projectIdentity = studio->controller.projectIdentity;
            auto replacement =
                coproducer::accept(previewController, current, lastRequest, candidates[size_t(index)], 0,
                                   coproducer::ApplyOperation::replaceRegion);
            if (replacement.failed())
            {
                summary.setText(replacement.getErrorMessage());
                return;
            }
            temporary = std::move(previewController.project);
        }
        else
        {
            Pattern *target = nullptr;
            if (selection.patternMode)
                target = temporary.pattern(selection.pattern);
            else
            {
                temporary.patterns.push_back(
                    {temporary.allocate(), "Temporary CoProducer preview", selection.region.length, {}});
                target = &temporary.patterns.back();
                target->autoLength = false; // Audition the same explicit region accepted by the command.
                temporary.placements.push_back({temporary.allocate(), target->id, 0, selection.region.start,
                                                selection.region.length, 0});
            }
            if (!target)
            {
                summary.setText("Preview destination disappeared.");
                return;
            }
            for (const auto &event : candidates[size_t(index)].events)
                target->notes.push_back({temporary.allocate(), selection.destination,
                                         event.start + (selection.patternMode ? selection.region.start : 0),
                                         event.length, event.pitch, event.velocity, false});
            refreshAutomaticPatternLength(temporary, *target);
        }
        previousCursor = studio->audio.positionBeat();
        previewStarted = !studio->audio.playing();
        result = studio->audio.rebuild(temporary, studio->patternMode, studio->selectedPattern);
        if (result.failed())
        {
            summary.setText(result.getErrorMessage());
            return;
        }
        previewActive = true;
        stopPreviewButton.setEnabled(true);
        // This projection includes the latest canonical source plus the temporary part.
        // Clear only the already-satisfied source publication; later edits schedule their own rebuild.
        studio->needsRebuild = false;
        studio->engineRevision = studio->controller.revision;
        previewProject = studio->controller.projectIdentity;
        previewRevision = studio->controller.revision;
        if (previewStarted)
        {
            studio->audio.seekBeat(selection.region.start);
            studio->audio.playStop();
        }
        providerLabel.setText("Preview in project context; no notes are saved until acceptance.",
                              juce::dontSendNotification);
    }
    void acceptCandidate()
    {
        if (!studio || candidates.empty())
            return;
        const int index = candidateChoice.getSelectedId() - 1;
        if (index < 0 || index >= int(candidates.size()))
            return;
        stopPreview();
        coproducer::Options current;
        auto result = currentOptions(current);
        if (result.wasOk())
            result =
                coproducer::accept(studio->controller, current, lastRequest, candidates[size_t(index)], 0,
                                   replaceRegion.getToggleState() ? coproducer::ApplyOperation::replaceRegion
                                                                  : coproducer::ApplyOperation::newPart);
        if (result.failed())
        {
            summary.setText(result.getErrorMessage());
            return;
        }
        studio->selectedPattern = replaceRegion.getToggleState() && studio->patternMode
                                      ? lastRequest.snapshot.selection.pattern
                                      : studio->controller.project.patterns.back().id;
        studio->selectedChannel = lastRequest.snapshot.selection.destination;
        studio->refresh();
        studio->audio.rebuild(studio->controller.project, studio->patternMode, studio->selectedPattern);
        studio->showEditor(1);
        dropCandidates();
        providerLabel.setText(replaceRegion.getToggleState()
                                  ? "Destination region replaced. Undo restores the original project."
                                  : "Accepted as a new part. Undo restores the original project.",
                              juce::dontSendNotification);
        if (studio->patternMode && !replaceRegion.getToggleState())
            start.setValue(0);
    }
    bool runtimeChanged() const
    {
        if (!studio)
            return true;
        const auto current = studio->audio.pluginRuntimeStates();
        for (const auto &old : lastRequest.snapshot.pluginRuntime)
        {
            PluginRuntimeState now{old.plugin, {}, 0, false};
            for (const auto &state : current)
                if (state.plugin == old.plugin)
                {
                    now = state;
                    break;
                }
            if (now != old)
                return true;
        }
        return false;
    }
    void timerCallback() override
    {
        if (studio && mappingProject != studio->controller.projectIdentity)
        {
            mappingProject = studio->controller.projectIdentity;
            mappings.clear();
            loadMapping();
            dropCandidates();
        }
        if (studio)
            replaceRegion.setButtonText(studio->patternMode ? "Replace Pattern region (all linked placements)"
                                                            : "Replace selected clip region (make unique)");
        if ((!candidates.empty() || previewActive) && runtimeChanged())
        {
            stopPreview(true);
            dropCandidates();
            summary.setText("A relevant instrument/effect changed or became unavailable. Generate again.");
            providerLabel.setText("Candidates discarded after a live plugin change.",
                                  juce::dontSendNotification);
        }
        if (previewActive && studio && studio->controller.revision != previewRevision)
            stopPreview(true);
        if (work)
        {
            bool ready = false;
            juce::String result;
            {
                std::lock_guard guard(work->mutex);
                ready = work->ready;
                if (ready)
                    result = work->result;
            }
            if (ready)
            {
                summary.setText(result);
                const bool valid = work->valid;
                lastOptions = work->options;
                lastRequest = {};
                lastRequest.snapshot = std::move(work->snapshot);
                work.reset();
                setBusy(false);
                if (generateAfterContext && valid)
                    launchGeneration();
                generateAfterContext = false;
            }
        }
        if (generation)
        {
            auto result = generation->poll();
            if (result.status == coproducer::JobStatus::running)
            {
                providerLabel.setText("Generating locally: " + juce::String(int(result.progress * 100)) + "%",
                                      juce::dontSendNotification);
                return;
            }
            generation.reset();
            setBusy(false);
            if (result.status == coproducer::JobStatus::succeeded)
            {
                coproducer::Options current;
                auto fresh = currentOptions(current);
                if (fresh.wasOk() && studio)
                    fresh = coproducer::checkFresh(studio->controller.project, current, lastRequest.snapshot);
                if (fresh.failed() || !studio ||
                    studio->controller.projectIdentity != lastRequest.snapshot.projectIdentity)
                {
                    summary.setText(
                        "Relevant context changed during generation. Results discarded; generate again.");
                    return;
                }
                candidates = std::move(result.candidates);
                candidateChoice.clear();
                for (int i = 0; i < int(candidates.size()); ++i)
                    candidateChoice.addItem("Candidate " + juce::String(i + 1) + " - " +
                                                juce::String(int(candidates[size_t(i)].events.size())) +
                                                " notes",
                                            i + 1);
                candidateChoice.setSelectedId(1, juce::dontSendNotification);
                preview.setEnabled(true);
                acceptButton.setEnabled(true);
                discard.setEnabled(true);
                summary.setText(lastRequest.snapshot.summary() + "\n\nProvider limitations:\n" +
                                result.warnings.joinIntoString("\n"));
                if (const auto *ledger = result.contextReport["ledger"].getArray())
                {
                    juce::String report = "\n\nProvider context (Channel / note ID):\n";
                    for (const auto &row : *ledger)
                        report += row["channel"].toString() + " / " + row["note"].toString() + ": " +
                                  (row["omitted"].isString()
                                       ? "omitted - " + row["omitted"].toString()
                                       : row["kind"].toString() + "; onset rounding " +
                                             juce::String(double(row["onsetRoundingSeconds"]) * 1000, 3) +
                                             " ms; duration rounding " +
                                             juce::String(double(row["durationRoundingSeconds"]) * 1000, 3) +
                                             " ms") +
                                  "\n";
                    summary.setText(summary.getText() + report);
                }
                providerLabel.setText("Candidates ready. Audition, accept a new part, or discard.",
                                      juce::dontSendNotification);
            }
            else
            {
                providerLabel.setText(result.status == coproducer::JobStatus::cancelled
                                          ? "Generation cancelled; project preserved."
                                          : "Generation stopped; project preserved.",
                                      juce::dontSendNotification);
                if (result.error.isNotEmpty())
                    summary.setText(result.error);
            }
        }
    }

  public:
    juce::Result verifyGenerationForTest(const juce::File &folder, const std::function<float()> &tick,
                                         bool replaceForTest = false, bool anticipationForTest = false)
    {
        auto wait = [this, &tick]
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120000;
            while ((work || generation) && juce::Time::getMillisecondCounterHiRes() < deadline)
            {
                timerCallback();
                tick();
                juce::Thread::sleep(2);
            }
            return !work && !generation;
        };
        if (!wait())
            return juce::Result::fail("Initial context did not settle.");
        if (variationAmount.isEnabled())
            return juce::Result::fail("MelodyRNN must disable latent mix.");
        model.setSelectedId(2, juce::sendNotificationSync);
        if (variationAmount.isEnabled())
            return juce::Result::fail("New ideas must disable latent mix.");
        variation.setSelectedId(2, juce::sendNotificationSync);
        if (!variationAmount.isEnabled())
            return juce::Result::fail("Idea variation must enable latent mix.");
        variationAmount.setValue(25, juce::sendNotificationSync);
        model.setSelectedId(1, juce::sendNotificationSync);
        int destinationId = 0;
        for (size_t i = 0; i < destinationIds.size(); ++i)
            if (const auto *channel = studio->controller.project.channel(destinationIds[i]);
                channel && channel->source == "builtin:bass")
                destinationId = int(i) + 1;
        if (!destinationId)
            return juce::Result::fail("Integration test needs the original melodic sampler.");
        destination.setSelectedId(destinationId, juce::dontSendNotification);
        model.setSelectedId(1, juce::dontSendNotification);
        start.setValue(studio->patternMode && !replaceForTest ? 0 : 4, juce::dontSendNotification);
        bars.setValue(1, juce::dontSendNotification);
        chord.setText({}, false);
        pitchClasses.setText({}, false);
        chooseContext(2, {});
        for (auto id : destinationIds)
            if (id != destinationIds[size_t(destinationId - 1)])
                chooseContext(100, {id});
        if (effectiveContext() != std::vector<Id>{destinationIds[size_t(destinationId - 1)]})
            return juce::Result::fail("Real generation test requires destination-only custom context.");
        if (anticipationForTest)
        {
            model.setSelectedId(3, juce::sendNotificationSync);
            start.setValue(4, juce::dontSendNotification);
            chooseContext(2, {});
            for (int index = 0; index < int(destinationIds.size()); ++index)
            {
                mappingChannel.setSelectedId(index + 1, juce::sendNotificationSync);
                mappingProgram.setText(index == 0 ? "0" : index == 2 ? "25" : "32", false);
                mappingPitches.setText("0-127", false);
                saveMapping.onClick();
            }
            if (mappings.size() != destinationIds.size())
                return juce::Result::fail(
                    "Real Anticipation panel fixture failed explicit mapping controls.");
        }
        replaceRegion.setToggleState(replaceForTest, juce::dontSendNotification);
        replaceRegion.onClick();
        const auto source = studio->controller.project.state().toXmlString();
        const auto graph = studio->audio.projectionDiagnostics();
        studio->audio.seekBeat(start.getValue());
        if (!studio->audio.playing())
            studio->audio.playStop();
        for (int i = 0; i < 12; ++i)
            tick();
        generate.onClick();
        if (!wait() || candidates.size() != 2 || candidates.front().testFixture)
            return juce::Result::fail("Actual panel generation failed: " + summary.getText());
        if (studio->controller.project.state().toXmlString() != source || !studio->audio.playing())
            return juce::Result::fail("Generation mutated source or interrupted playback.");
        if (anticipationForTest && (!summary.getText().contains("Provider context (Channel / note ID)") ||
                                    !summary.getText().contains("history; onset rounding") ||
                                    !summary.getText().contains("control; onset rounding")))
            return juce::Result::fail("Real provider consumption ledger did not reach the panel.");
        if (anticipationForTest)
        {
            if (!folder.getChildFile("provider-context-summary.txt").replaceWithText(summary.getText()))
                return juce::Result::fail("Provider context report evidence could not be retained.");
            folder.getChildFile("pre-audition-transport.txt")
                .replaceWithText("Generation completed at quarter-note cursor " +
                                 juce::String(studio->audio.positionBeat(), 4) + "; target starts at " +
                                 juce::String(start.getValue(), 4) +
                                 ". Test explicitly seeks the target before bounded capture; production "
                                 "audition preserves running transport.\n");
            studio->audio.seekBeat(start.getValue());
            for (int settle = 0; settle < 20; ++settle)
                tick();
        }
        studio->needsRebuild = true; // Labelled pending-publication regression fixture.
        preview.onClick();
        if (!previewActive || studio->needsRebuild || studio->engineRevision != studio->controller.revision)
            return juce::Result::fail("Actual candidate did not enter in-context audition.");
        float peak = 0;
        for (int i = 0; i < 420; ++i)
            peak = std::max(peak, tick());
        const auto playback = studio->audio.projectionDiagnostics();
        if (peak < .001f || studio->controller.project.state().toXmlString() != source ||
            !studio->audio.playing())
            return juce::Result::fail(
                "Preview did not produce virtual PCM or changed source/transport: peak=" +
                juce::String(peak, 6) + ", previewActive=" + juce::String(int(previewActive)) +
                ", playing=" + juce::String(int(studio->audio.playing())) +
                ", position=" + juce::String(studio->audio.positionBeat(), 3) + ", sourceChanged=" +
                juce::String(int(studio->controller.project.state().toXmlString() != source)) +
                ", firstNote=" + juce::String(candidates.front().events.front().start, 3) +
                ", headPlaying=" + juce::String(int(playback.headPlaying)) +
                ", playPending=" + juce::String(int(playback.playPending)) +
                ", headSeconds=" + juce::String(playback.headSeconds, 3) +
                ", patternMode=" + juce::String(int(studio->patternMode)) + ", looping=" +
                juce::String(int(bool(reinterpret_cast<te::Edit *>(playback.edit)->getTransport().looping))));
        stopPreviewButton.onClick();
        if (previewActive || !studio->audio.playing())
            return juce::Result::fail("Stopping preview interrupted the pre-existing transport.");
        const auto after = studio->audio.projectionDiagnostics();
        if (graph.edit != after.edit || graph.samplerCreations != after.samplerCreations)
            return juce::Result::fail("Candidate preview replaced the working graph/instruments.");
        if (graph.playbackContext != after.playbackContext || graph.channels.size() != after.channels.size())
            return juce::Result::fail("Candidate preview replaced the playback context/channel projection.");
        for (size_t i = 0; i < graph.channels.size(); ++i)
            if (graph.channels[i].track != after.channels[i].track ||
                graph.channels[i].sample != after.channels[i].sample ||
                graph.channels[i].graphInitialisations != after.channels[i].graphInitialisations)
                return juce::Result::fail(
                    "Candidate preview rebuilt the graph or replaced prepared samples.");
        candidateChoice.setSelectedId(2, juce::dontSendNotification);
        const auto acceptedSeed = candidates[1].seed;
        acceptButton.onClick();
        if (studio->controller.project.state().toXmlString() == source || !candidates.empty())
            return juce::Result::fail("Accept control did not commit the real model part.");
        if (!studio->patternMode)
        {
            const auto &placement = replaceForTest ? studio->controller.project.placements.front()
                                                   : studio->controller.project.placements.back();
            if (placement.pattern != studio->controller.project.patterns.back().id ||
                placement.beat != (replaceForTest ? 0 : 4) || placement.length != (replaceForTest ? 16 : 4) ||
                placement.offset != 0)
                return juce::Result::fail(
                    "Song acceptance did not place its own source at the requested timeline region.");
        }
        const auto accepted = studio->controller.project.state().toXmlString();
        const auto settings = juce::JSON::parse(studio->controller.project.patterns.back().generation);
        if (juce::int64(settings["seed"]) != juce::int64(acceptedSeed) || bool(settings["fixture"]))
            return juce::Result::fail("Accepted candidate did not retain its individual real-provider seed.");
        auto *contextMetadata = settings["contextChannels"].getArray();
        if (settings["contextPolicy"].toString() != "explicit Channels" || !contextMetadata ||
            contextMetadata->size() != (anticipationForTest ? int(destinationIds.size()) : 1) ||
            (!anticipationForTest &&
             (*contextMetadata)[0].toString() != juce::String(lastRequest.snapshot.selection.destination)))
            return juce::Result::fail("Accepted part lost its explicit context selection provenance.");
        auto *runtimeMetadata = settings["livePluginValidation"].getArray();
        if (!runtimeMetadata || runtimeMetadata->size() != int(lastRequest.snapshot.pluginRuntime.size()))
            return juce::Result::fail("Accepted part lost live-plugin validation provenance.");
        for (size_t i = 0; i < lastRequest.snapshot.pluginRuntime.size(); ++i)
            if ((*runtimeMetadata)[int(i)]["plugin"].toString() !=
                    juce::String(lastRequest.snapshot.pluginRuntime[i].plugin) ||
                juce::int64((*runtimeMetadata)[int(i)]["reportedParameterRevision"]) !=
                    juce::int64(lastRequest.snapshot.pluginRuntime[i].parameterRevision))
                return juce::Result::fail(
                    "Accepted live-plugin revision metadata differs from the validated snapshot.");
        auto saved = saveProject(studio->controller.project, folder.getChildFile("generated-part.vibra"));
        Project reopened;
        if (saved.failed() || loadProject(folder.getChildFile("generated-part.vibra"), reopened).failed() ||
            reopened.state().toXmlString() != accepted)
            return juce::Result::fail("Accepted generation did not round-trip exactly.");
        const auto &part = studio->controller.project.patterns.back();
        juce::MemoryBlock midiBytes;
        MidiScore midiScore;
        if (writeMidiScore(part, lastRequest.snapshot.selection.destination, studio->controller.project.tempo,
                           studio->controller.project.numerator, studio->controller.project.denominator,
                           midiBytes)
                .failed() ||
            !folder.getChildFile("generated-part.mid")
                 .replaceWithData(midiBytes.getData(), midiBytes.getSize()) ||
            readMidiScore(midiBytes, {}, midiScore).failed() ||
            midiScore.notes.size() !=
                size_t(std::count_if(part.notes.begin(), part.notes.end(), [&](const auto &note)
                                     { return note.channel == lastRequest.snapshot.selection.destination; })))
            return juce::Result::fail("Accepted generation did not export/reopen as generic MIDI.");
        auto expected = part.notes;
        std::erase_if(expected, [&](const auto &note)
                      { return note.channel != lastRequest.snapshot.selection.destination; });
        for (auto &note : expected)
        {
            // SMF is the documented 960-PPQ interchange grid; project timing stays exact.
            const auto startTick = std::round(note.beat * 960.);
            const auto endTick = std::max(startTick + 1., std::round((note.beat + note.length) * 960.));
            note.beat = startTick / 960.;
            note.length = (endTick - startTick) / 960.;
        }
        auto order = [](const auto &a, const auto &b)
        {
            return std::tie(a.beat, a.pitch, a.length, a.velocity) <
                   std::tie(b.beat, b.pitch, b.length, b.velocity);
        };
        std::sort(expected.begin(), expected.end(), order);
        std::sort(midiScore.notes.begin(), midiScore.notes.end(), order);
        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (std::abs(expected[i].beat - midiScore.notes[i].beat) > 1e-9 ||
                std::abs(expected[i].length - midiScore.notes[i].length) > 1e-9 ||
                expected[i].pitch != midiScore.notes[i].pitch ||
                std::abs(std::max(1, int(std::lround(expected[i].velocity * 127))) / 127.0f -
                         midiScore.notes[i].velocity) > 1e-6f)
                return juce::Result::fail(
                    "Generic MIDI changed grid timing, pitch or the expected 7-bit velocity.");
        }
        studio->controller.undoEdit();
        if (studio->controller.project.state().toXmlString() != source)
            return juce::Result::fail("Panel acceptance needs more than one undo.");
        studio->controller.redoEdit();
        if (studio->controller.project.state().toXmlString() != accepted)
            return juce::Result::fail("Panel acceptance redo differs.");
        studio->audio.stop();
        auto stream = folder.getChildFile("coproducer-integrated.png").createOutputStream();
        if (!stream)
            return juce::Result::fail("CoProducer integration capture failed.");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f),
                                                       *stream))
            return juce::Result::fail("CoProducer integration capture encoding failed.");
        folder.getChildFile("coproducer-integrated.txt")
            .replaceWithText(
                juce::String(anticipationForTest
                                 ? "PASS: actual Anticipation with explicit multi-Channel controls/ledger. "
                                 : "PASS: actual local MelodyRNN. ") +
                "Native Generate handler, two validated candidates "
                "while virtual playback remains active, "
                "in-context sampler PCM audition without source mutation, retained graph/instruments and "
                "transport on preview removal, "
                "Accept handler, exact save/reopen, generic MIDI timing/pitch/velocity export and one "
                "undo/redo. Component/JUCE handlers and virtual "
                "output; no physical input/listening or broad musical qualification.\n");
        return juce::Result::ok();
    }
    juce::Result verifyForTest(const juce::File &folder)
    {
        auto wait = [this]
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000;
            while (work && juce::Time::getMillisecondCounterHiRes() < deadline)
            {
                timerCallback();
                if (work)
                    juce::Thread::sleep(10);
            }
            return !work;
        };
        if (!wait() || !summary.getText().contains("CoProducer context"))
            return juce::Result::fail("CoProducer inspector did not collect an immutable context.");
        const auto before = studio->controller.project.state().toXmlString();
        const auto firstChannel = studio->controller.project.channels.front().id;
        coproducer::Options controls;
        if (currentOptions(controls).failed() || !controls.automaticTracks)
            return juce::Result::fail("Context chooser must default to automatic audibility.");
        chooseContext(2, {});
        if (currentOptions(controls).failed() || controls.automaticTracks ||
            controls.tracks.size() != studio->controller.project.channels.size())
            return juce::Result::fail("All-context handler lost Channels.");
        chooseContext(100, {firstChannel});
        inspect.onClick();
        if (!wait() ||
            std::find(lastOptions.tracks.begin(), lastOptions.tracks.end(), firstChannel) !=
                lastOptions.tracks.end() ||
            std::any_of(lastRequest.snapshot.events.begin(), lastRequest.snapshot.events.end(),
                        [&](const auto &event) { return event.channel == firstChannel; }))
            return juce::Result::fail("Custom context exclusion still read the Channel's notes.");
        if (lastRequest.snapshot.selection.destination == firstChannel &&
            std::any_of(lastRequest.snapshot.tracks.begin(), lastRequest.snapshot.tracks.end(),
                        [&](const auto &track) { return track.channel == firstChannel && track.included; }))
            return juce::Result::fail("Excluded destination metadata was mislabelled as note context.");
        auto selected = effectiveContext();
        for (auto id : selected)
            chooseContext(100, {id});
        inspect.onClick();
        if (!wait() || !lastRequest.snapshot.events.empty() || !lastOptions.tracks.empty())
            return juce::Result::fail("Empty explicit context was silently repopulated.");
        chooseContext(100, {firstChannel});
        inspect.onClick();
        if (!wait() || lastOptions.tracks != std::vector<Id>{firstChannel})
            return juce::Result::fail("Custom context inclusion did not retain the stable Channel ID.");
        chooseContext(1, {});
        inspect.onClick();
        if (!wait() || !lastOptions.automaticTracks ||
            studio->controller.project.state().toXmlString() != before)
            return juce::Result::fail("Context choices modified source or failed to restore automatic mode.");
        showContextMenu(true);
        auto *menuComponent = juce::Component::getCurrentlyModalComponent();
        if (!menuComponent || menuComponent->getName() != "menu")
            return juce::Result::fail("Context popup did not create its own JUCE menu component.");
        auto menuStream = folder.getChildFile("coproducer-context-menu.png").createOutputStream();
        if (!menuStream)
            return juce::Result::fail("Context popup capture failed.");
        menuStream->setPosition(0);
        menuStream->truncate();
        const auto encoded = juce::PNGImageFormat().writeImageToStream(
            menuComponent->createComponentSnapshot(menuComponent->getLocalBounds(), true, 1.5f), *menuStream);
        juce::PopupMenu::dismissAllActiveMenus(); // Only the disposable test process owns menus here.
        if (!encoded)
            return juce::Result::fail("Context popup PNG encoding failed.");
        chord.setText("C minor");
        pitchClasses.setText("0,3,7");
        inspect.onClick(); // Actual control handler; not native/physical input.
        if (!wait() || !summary.getText().contains("1 supplied annotations") ||
            studio->controller.project.state().toXmlString() != before)
            return juce::Result::fail("Chord correction failed or mutated the source document.");
        pitchClasses.setText("12");
        inspect.onClick();
        if (!summary.getText().contains("integers from 0 to 11"))
            return juce::Result::fail("Invalid chord correction was not rejected by the inspector.");
        pitchClasses.setText("0,3,7");
        inspect.onClick();
        if (!wait())
            return juce::Result::fail("CoProducer context refresh timed out.");
        model.setSelectedId(3, juce::sendNotificationSync);
        if (!mappingProgram.isVisible() || family.isEnabled() || variationAmount.isEnabled() ||
            provider.model != "anticipation-small" || provider.memoryMegabytes != 4096)
            return juce::Result::fail("Experimental provider controls/policy were not activated.");
        if (!clearChordCorrection.isVisible())
            return juce::Result::fail(
                "Unsupported existing chord correction must remain explicitly clearable.");
        clearChordCorrection.onClick();
        if (chord.getText().isNotEmpty() || clearChordCorrection.isVisible())
            return juce::Result::fail("Explicit chord clearing did not update provider controls.");
        mappingChannel.setSelectedId(1, juce::sendNotificationSync);
        mappingProgram.setText("128", false);
        mappingPitches.setText("36,38,42-46", false);
        saveMapping.onClick();
        coproducer::Options mapped;
        if (currentOptions(mapped).failed() || mapped.modelMappings.size() != 1 ||
            mapped.modelMappings.front().channel != firstChannel ||
            mapped.modelMappings.front().program != 128 || mapped.modelMappings.front().pitches.size() != 7)
            return juce::Result::fail("Mapping control did not bind the explicit Channel/program/pitches.");
        mappingProgram.setText("129", false);
        saveMapping.onClick();
        if (!summary.getText().contains("0..127") || mappings.front().program != 128)
            return juce::Result::fail("Invalid mapping program changed the applied profile.");
        mappingProgram.setText("128", false);
        mappingPitches.setText("60-48", false);
        saveMapping.onClick();
        if (!summary.getText().contains("low to high") || mappings.front().pitches.size() != 7)
            return juce::Result::fail("Invalid mapping range changed the applied profile.");
        loadMapping();
        summary.setText(
            "Explicit mapping retained after invalid edits. Other Channels remain unknown until mapped.\n"
            "Experimental provider: mapped MIDI context only; no text style, exact locks, velocity or "
            "articulation controls.");
        auto mappingStream = folder.getChildFile("coproducer-anticipation-controls.png").createOutputStream();
        if (!mappingStream)
            return juce::Result::fail("Mapping control capture failed.");
        mappingStream->setPosition(0);
        mappingStream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f),
                                                       *mappingStream))
            return juce::Result::fail("Mapping control capture encoding failed.");
        clearMapping.onClick();
        if (!mappings.empty() || !mappingProgram.getText().isEmpty())
            return juce::Result::fail("Clearing a mapping silently retained capabilities.");
        model.setSelectedId(1, juce::sendNotificationSync);
        if (mappingProgram.isVisible() || provider.executable != magentaProvider.executable ||
            studio->controller.project.state().toXmlString() != before)
            return juce::Result::fail("Provider switch failed to restore its runtime or mutated source.");
        auto stream = folder.getChildFile("coproducer-context.png").createOutputStream();
        if (!stream)
            return juce::Result::fail("Could not create CoProducer render evidence.");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f),
                                                       *stream))
            return juce::Result::fail("Could not encode CoProducer render evidence.");
        folder.getChildFile("coproducer-context-gui.txt")
            .replaceWithText(
                "PASS: asynchronous context inspector, actual correction handlers, invalid "
                "correction rejection, "
                "automatic/all/custom context handlers, empty context, destination exclusion, "
                "explicit program/pitch-map controls, invalid mapping rejection/clear/provider switch, "
                "source preservation and component render at 150%. No native input, model "
                "integration or listening qualification.\n");
        return juce::Result::ok();
    }
    explicit CoProducerPanel(Studio &owner) : studio(&owner)
    {
        int height = 640;
        if (const auto *display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            height = juce::jlimit(600, 820, display->userArea.getHeight() - 80);
        setSize(760, height);
        auto label = [this](juce::Label &control, const char *text)
        {
            control.setText(text, juce::dontSendNotification);
            control.setFont(juce::Font(vibera::uiFont, 12.5f, juce::Font::plain));
            control.setColour(juce::Label::textColourId, muted);
            addAndMakeVisible(control);
        };
        label(destinationLabel, "Destination instrument");
        label(startLabel, "Start (quarter notes)");
        label(barsLabel, "Length (project bars)");
        label(chordLabel, "Chord correction (optional; applies to this region)");
        label(pitchesLabel, "Pitch classes (0=C, 1=C#, ... 11=B)");
        label(providerLabel,
              "Local Magenta: melodic continuation or instrument parts. Advanced controls are unqualified.");
        provider.executable = juce::File("C:/Program Files/nodejs/node.exe");
        auto discover = [this](juce::File root)
        {
            for (int i = 0; i < 7; ++i)
            {
                const auto script = root.getChildFile("scripts/coproducer/model-worker.cjs");
                const auto installation = root.getChildFile("build/coproducer-research/magenta");
                if (script.existsAsFile() &&
                    installation.getChildFile("node_modules/@magenta/music/package.json").existsAsFile())
                {
                    provider.script = script;
                    provider.installation = installation;
                    return;
                }
                const auto parent = root.getParentDirectory();
                if (parent == root)
                    return;
                root = parent;
            }
        };
        discover(juce::File::getCurrentWorkingDirectory());
        if (provider.script == juce::File())
            discover(juce::File(__FILE__).getParentDirectory());
        const auto installedScript = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                         .getParentDirectory()
                                         .getChildFile("CoProducer/model-worker.cjs");
        if (installedScript.existsAsFile())
            provider.script = installedScript;
        magentaProvider = provider;
        anticipationProvider.model = "anticipation-small";
        anticipationProvider.memoryMegabytes = 4096;
        anticipationProvider.timeoutMilliseconds = 300000;
        auto findAnticipation = [this](juce::File root)
        {
            for (int i = 0; i < 8; ++i)
            {
                auto installation = root.getChildFile("build/coproducer-research/anticipation");
                if (installation.getChildFile("provenance.json").existsAsFile())
                {
                    anticipationProvider.installation = installation;
                    anticipationProvider.executable = installation.getChildFile("env/Scripts/python.exe");
                    anticipationProvider.script =
                        root.getChildFile("scripts/coproducer/anticipation-worker.py");
                    return;
                }
                const auto parent = root.getParentDirectory();
                if (parent == root)
                    return;
                root = parent;
            }
        };
        findAnticipation(juce::File::getCurrentWorkingDirectory());
        if (anticipationProvider.installation == juce::File())
            findAnticipation(juce::File(__FILE__).getParentDirectory());
        const auto installedAnticipation = installedScript.getSiblingFile("anticipation-worker.py");
        if (installedAnticipation.existsAsFile())
            anticipationProvider.script = installedAnticipation;
        model.addItem("MelodyRNN - lead continuation", 1);
        model.addItem("MusicVAE - instrument parts", 2);
        model.addItem("Anticipation - experimental context", 3);
        model.setSelectedId(1, juce::dontSendNotification);
        family.addItem("Melodic lead", 1);
        family.setSelectedId(1, juce::dontSendNotification);
        family.setTooltip("Choose the intended part family. This is not inferred from the plugin name.");
        variation.addItem("Continuation", 1);
        variation.setSelectedId(1, juce::dontSendNotification);
        variation.setTooltip("MusicVAE varies destination notes in each bar. Performance/rhythm/pitch locks "
                             "are not supported by these models.");
        label(variationAmountLabel, "Idea amount");
        variationAmount.setRange(0, 100, 1);
        variationAmount.setValue(50);
        variationAmount.setTextValueSuffix("%");
        variationAmount.setSliderStyle(juce::Slider::LinearHorizontal);
        variationAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 26);
        variationAmount.setTooltip("Latent noise mix: 0% decodes the source encoding, 100% uses noise. "
                                   "Neither endpoint guarantees exact notes or a musical distance.");
        addAndMakeVisible(variationAmountLabel);
        addAndMakeVisible(variationAmount);
        label(seedLabel, "Seed");
        seed.setRange(0, 4294967295.0, 1);
        seed.setValue(17);
        seed.setSliderStyle(juce::Slider::IncDecButtons);
        seed.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 115, 26);
        seed.setTooltip("Seed: reproducibility is qualified only within the pinned local CPU backend.");
        int index = 1;
        mappingProject = owner.controller.projectIdentity;
        for (const auto &channel : owner.controller.project.channels)
        {
            destinationIds.push_back(channel.id);
            destination.addItem(channel.name, index);
            mappingChannel.addItem(channel.name, index);
            if (channel.id == owner.selectedChannel)
                destination.setSelectedId(index, juce::dontSendNotification);
            ++index;
        }
        if (destination.getSelectedId() == 0)
            destination.setSelectedId(1);
        mappingChannel.setSelectedId(destination.getSelectedId(), juce::dontSendNotification);
        mappingLabel.setText("Explicit Channel mapping: GM 0..127 / 128 percussion; permitted MIDI pitches",
                             juce::dontSendNotification);
        mappingProgram.setTextToShowWhenEmpty("GM program", juce::Colours::grey);
        mappingPitches.setTextToShowWhenEmpty("Pitches: 48-83 or 36,38,42,46", juce::Colours::grey);
        clearChordCorrection.onClick = [this]
        {
            dropCandidates();
            chord.setText({}, false);
            pitchClasses.setText({}, false);
            showProviderControls();
            summary.setText("Chord correction cleared explicitly. Anticipation reads mapped MIDI harmony "
                            "notes, not chord labels.");
        };
        mappingProgram.setTooltip(
            "Zero-based General MIDI program; 128 denotes percussion. Choose explicitly for this Channel.");
        mappingPitches.setTooltip("Permitted MIDI notes, including an explicit drum map. Numbers/ranges "
                                  "0..127. Unknown capabilities remain unknown.");
        mappingChannel.onChange = [this] { loadMapping(); };
        saveMapping.onClick = [this] { editMapping(false); };
        clearMapping.onClick = [this] { editMapping(true); };
        start.setRange(0, 1000000, .25);
        start.setValue(owner.audio.positionBeat());
        bars.setRange(1, 64, 1);
        bars.setValue(4);
        for (auto *slider : {&start, &bars})
        {
            slider->setSliderStyle(juce::Slider::IncDecButtons);
            slider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 112, 26);
            addAndMakeVisible(slider);
        }
        // Selected clips/notes supply initial bounds; explicit fields remain editable for empty projects.
        double first = std::numeric_limits<double>::infinity(), last = 0;
        if (!owner.patternMode)
            for (const auto &clip : owner.controller.project.placements)
                if (std::find(owner.selectedClips.begin(), owner.selectedClips.end(), clip.id) !=
                    owner.selectedClips.end())
                {
                    first = std::min(first, clip.beat);
                    last = std::max(last, clip.beat + clip.length);
                }
        if (owner.patternMode)
            if (const auto *pattern = owner.controller.project.pattern(owner.selectedPattern))
                for (const auto &note : pattern->notes)
                    if (std::find(owner.selectedNotes.begin(), owner.selectedNotes.end(), note.id) !=
                        owner.selectedNotes.end())
                    {
                        first = std::min(first, note.beat);
                        last = std::max(last, note.beat + note.length);
                    }
        if (std::isfinite(first))
        {
            start.setValue(first);
            bars.setValue(std::ceil((last - first) / (owner.controller.project.numerator * 4.0 /
                                                      owner.controller.project.denominator)));
        }
        for (auto *control : std::initializer_list<juce::Component *>{
                 &destination, &contextChannels, &lockSelection, &chord, &pitchClasses, &summary, &inspect,
                 &model, &family, &variation, &candidateChoice, &seed, &generate, &cancel, &preview,
                 &stopPreviewButton, &acceptButton, &discard, &modelsButton})
            addAndMakeVisible(control);
        for (auto *control : std::initializer_list<juce::Component *>{
                 &mappingLabel, &mappingChannel, &mappingProgram, &mappingPitches, &saveMapping,
                 &clearMapping, &runtimeButton, &clearChordCorrection})
            addAndMakeVisible(control);
        contextChannels.setTooltip(
            "Choose the Channels CoProducer reads. Checked muted Channels supply context without changing "
            "playback mute. Model conditioning limits remain in the summary.");
        addAndMakeVisible(replaceRegion);
        replaceRegion.setTooltip(
            "Off: accept a new part. Pattern: replace this destination's notes in the source region, "
            "affecting every linked placement. Song: select one clip; only its unique copy changes. Notes "
            "crossing region edges are split; protected overlaps reject replacement.");
        replaceRegion.onClick = [this]
        {
            stopPreview();
            acceptButton.setButtonText(replaceRegion.getToggleState() ? "Replace region" : "Accept new part");
        };
        summary.setMultiLine(true);
        summary.setReadOnly(true);
        summary.setScrollbarsShown(true);
        for (auto *editor : {&summary, &chord, &pitchClasses, &mappingProgram, &mappingPitches})
        {
            editor->setFont(juce::Font(vibera::uiFont, 13.f, juce::Font::plain));
            editor->setIndents(6, 5);
        }
        chord.setTextToShowWhenEmpty("e.g. C minor", muted);
        for (auto *primary : {&generate, &acceptButton})
        {
            primary->setColour(juce::TextButton::buttonOnColourId, accent.darker(.3f));
            primary->setToggleState(true, juce::dontSendNotification); // Primary actions stand out.
        }
        pitchClasses.setTextToShowWhenEmpty("e.g. 0,3,7", muted);
        inspect.onClick = [this]
        {
            dropCandidates();
            collectContext();
        };
        auto changed = [this] { dropCandidates(); };
        destination.onChange = changed;
        family.onChange = changed;
        variation.onChange = [this]
        {
            dropCandidates();
            setBusy(false);
        };
        variationAmount.onValueChange = changed;
        start.onValueChange = changed;
        bars.onValueChange = changed;
        seed.onValueChange = changed;
        chord.onTextChange = changed;
        pitchClasses.onTextChange = changed;
        contextChannels.onClick = [this] { showContextMenu(); };
        lockSelection.onClick = changed;
        model.onChange = [this]
        {
            dropCandidates();
            if (providerSelection == 3)
                anticipationProvider = provider;
            else
                magentaProvider = provider;
            providerSelection = model.getSelectedId();
            provider = providerSelection == 3 ? anticipationProvider : magentaProvider;
            family.clear(juce::dontSendNotification);
            variation.clear(juce::dontSendNotification);
            if (model.getSelectedId() == 1)
            {
                family.addItem("Melodic lead", 1);
                variation.addItem("Continuation", 1);
            }
            else if (model.getSelectedId() == 2)
            {
                int id = 1;
                for (const char *name : {"Keys", "Bass", "Guitar", "Strings", "Synth"})
                    family.addItem(name, id++);
                variation.addItem("New idea", 1);
                variation.addItem("Idea variation", 2);
            }
            else
            {
                family.addItem("Explicit GM", 1);
                variation.addItem("New idea", 1);
            }
            family.setSelectedId(1, juce::dontSendNotification);
            variation.setSelectedId(1, juce::dontSendNotification);
            setBusy(false);
            showProviderControls();
            providerLabel.setText(model.getSelectedId() == 1
                                      ? "MelodyRNN uses destination melody only; other parts, chords and "
                                        "styles are not model inputs."
                                  : model.getSelectedId() == 2
                                      ? "MusicVAE uses supplied triads and destination-note variations; "
                                        "other parts/styles are not model inputs."
                                      : "Experimental: map each included Channel; styles, exact locks and "
                                        "song summaries are unsupported.",
                                  juce::dontSendNotification);
        };
        generate.onClick = [this]
        {
            dropCandidates();
            generateAfterContext = true;
            collectContext();
        };
        cancel.onClick = [this]
        {
            if (generation)
                generation->cancel();
            dropCandidates();
        };
        preview.onClick = [this] { auditionCandidate(); };
        stopPreviewButton.onClick = [this] { stopPreview(); };
        acceptButton.onClick = [this] { acceptCandidate(); };
        discard.onClick = [this]
        {
            dropCandidates();
            providerLabel.setText("Candidates discarded; project preserved.", juce::dontSendNotification);
        };
        candidateChoice.onChange = [this] { stopPreview(); };
        modelsButton.onClick = [this]
        {
            modelChooser = std::make_unique<juce::FileChooser>(
                model.getSelectedId() == 3
                    ? "Choose local Anticipation installation (provenance.json, model, source)"
                    : "Choose local Magenta installation (node_modules and checkpoints)",
                provider.installation);
            auto safe = juce::Component::SafePointer<CoProducerPanel>(this);
            const int selectedProvider = model.getSelectedId();
            modelChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [safe, selectedProvider](const juce::FileChooser &chooser)
                {
                    if (!safe || safe->model.getSelectedId() != selectedProvider || safe->work ||
                        safe->generation || !chooser.getResult().isDirectory())
                        return;
                    safe->dropCandidates();
                    safe->provider.installation = chooser.getResult();
                    if (safe->model.getSelectedId() == 3)
                    {
                        const auto python = chooser.getResult().getChildFile("env/Scripts/python.exe");
                        if (python.existsAsFile())
                            safe->provider.executable = python;
                    }
                    safe->providerLabel.setText("Local installation selected. No network inference is used.",
                                                juce::dontSendNotification);
                });
        };
        runtimeButton.onClick = [this]
        {
            modelChooser = std::make_unique<juce::FileChooser>("Choose local Python/Node runtime executable",
                                                               provider.executable, "*.exe");
            auto safe = juce::Component::SafePointer<CoProducerPanel>(this);
            const int selectedProvider = model.getSelectedId();
            modelChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe, selectedProvider](const juce::FileChooser &chooser)
                {
                    if (!safe || safe->model.getSelectedId() != selectedProvider || safe->work ||
                        safe->generation || !chooser.getResult().existsAsFile())
                        return;
                    safe->dropCandidates();
                    safe->provider.executable = chooser.getResult();
                    safe->providerLabel.setText(
                        "Local runtime selected. Inspect/generate to verify availability.",
                        juce::dontSendNotification);
                });
        };
        preview.setEnabled(false);
        stopPreviewButton.setEnabled(false);
        acceptButton.setEnabled(false);
        discard.setEnabled(false);
        cancel.setEnabled(false);
        startTimerHz(10);
        showProviderControls();
        collectContext();
    }
    ~CoProducerPanel() override
    {
        stopTimer();
        stopPreview();
        if (generation)
        {
            generation->cancel();
            generation.reset();
        }
        workers.removeAllJobs(true, -1); // Control/UI lifecycle only; never an audio callback.
    }
    // Idle: nothing generating, previewing or awaiting Accept, so the dock may rebuild it from fresh context.
    bool idle() const
    {
        return !work && !generation && candidates.empty() && !previewActive;
    }
    // Height the controls need at this width; the summary takes any extra space.
    int preferredHeight(int width)
    {
        return layout(width, false) + 220;
    }
    void resized() override
    {
        layout(getWidth(), true);
    }

  private:
    // Single-column flow that pairs controls side by side when there is room, so the panel works
    // both docked beside the workspace (360-700 px) and in a wide window.
    int layout(int width, bool apply)
    {
        const int margin = 14, gap = 8, w = std::max(240, width - 2 * margin);
        const bool wide = w >= 360;
        int y = 10;
        auto put = [&](juce::Component &c, int x, int top, int cw, int h)
        {
            if (!apply)
                return;
            c.setBounds(margin + x, top, cw, h);
            // Compact -/+ steppers: the value box takes the width, the buttons stay square.
            if (auto *stepper = dynamic_cast<juce::Slider *>(&c);
                stepper && stepper->getSliderStyle() == juce::Slider::IncDecButtons)
                stepper->setTextBoxStyle(juce::Slider::TextBoxLeft, false, std::max(40, cw - 2 * h), h);
        };
        auto full = [&](juce::Component &c, int h, int after = 8)
        {
            put(c, 0, y, w, h);
            y += h + after;
        };
        auto pair = [&](juce::Component &a, juce::Component &b, int h, float split = .5f)
        {
            const int left = int((w - gap) * split);
            put(a, 0, y, left, h);
            put(b, left + gap, y, w - left - gap, h);
            y += h + gap;
        };
        auto section = [&] { y += 6; };
        full(providerLabel, 24, 4);
        full(destinationLabel, 18, 2);
        full(destination, 28);
        if (wide)
        {
            pair(startLabel, barsLabel, 18);
            y -= gap - 2;
            pair(start, bars, 28);
        }
        else
        {
            full(startLabel, 18, 2);
            full(start, 28);
            full(barsLabel, 18, 2);
            full(bars, 28);
        }
        if (wide)
            pair(contextChannels, lockSelection, 26, .55f);
        else
        {
            full(contextChannels, 26);
            full(lockSelection, 24);
        }
        if (mappingLabel.isVisible())
        {
            full(mappingLabel, 18, 2);
            pair(mappingChannel, mappingProgram, 26, .62f);
            full(mappingPitches, 28);
            pair(saveMapping, clearMapping, 28);
            if (clearChordCorrection.isVisible())
                full(clearChordCorrection, 26);
        }
        else
        {
            full(chordLabel, 18, 2);
            full(chord, 26);
            full(pitchesLabel, 18, 2);
            full(pitchClasses, 26);
        }
        full(inspect, 30);
        section();
        full(model, 28);
        pair(family, variation, 28, .4f);
        {
            put(seedLabel, 0, y, 40, 28);
            const int buttons = wide ? 216 : 0;
            put(seed, 44, y, w - 44 - buttons - (wide ? gap : 0), 28);
            if (wide)
            {
                put(modelsButton, w - buttons, y, 112, 28);
                put(runtimeButton, w - buttons + 112 + gap, y, buttons - 112 - gap, 28);
            }
            y += 28 + gap;
            if (!wide)
                pair(modelsButton, runtimeButton, 28);
        }
        pair(generate, cancel, 32, .62f);
        section();
        full(candidateChoice, 28);
        pair(preview, stopPreviewButton, 28);
        pair(acceptButton, discard, 30, .7f);
        put(variationAmountLabel, 0, y, 96, 26);
        put(variationAmount, 100, y, w - 100, 26);
        y += 26 + gap;
        full(replaceRegion, 26);
        if (apply)
            summary.setBounds(margin, y, w, std::max(160, getHeight() - y - margin));
        return y;
    }
};
} // namespace vibera
