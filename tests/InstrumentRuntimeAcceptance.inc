// SPDX-License-Identifier: AGPL-3.0-or-later
// SOURCE-ONLY original proposal. Include INSIDE namespace vibera immediately
// after FixedSampler definition, alongside the existing internal fixture seam.
namespace test
{
__declspec(noinline) void instrumentRuntimeTests(const juce::File &folder, const PluginSpec &original)
{
    const auto require = [](bool condition, const juce::String &message)
    {
        if (!condition)
            throw std::runtime_error(message.toStdString());
    };
    require(folder.createDirectory().wasOk() && original.instrument &&
                original.name == "VibeRa Instrument Runtime Fixture",
            "Genuine original SDK instrument classification and owned output required");
    const auto catalogue = [&](PluginHost &host, bool refresh = false)
    {
        const auto request = refresh ? host.requestParameterCatalogue() : 0;
        PluginParameterCatalogue result;
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (host.readParameterCatalogue(result) && result.serial >= request)
            {
                require(result.error == 0, "Actual instrument parameter catalogue valid");
                return result;
            }
            juce::Thread::sleep(2);
        }
        throw std::runtime_error("Original instrument catalogue timeout");
    };
    juce::String report;
    for (const double rate : {48000., 44100.})
        for (const bool adopt : {false, true})
        {
            constexpr int quantum = 64, graphBlock = 71, total = 384;
            AudioEngine audio(false);
            auto project = Project::empty();
            auto spec = original;
            spec.id = project.allocate();
            juce::String error;
            auto edit =
                audio.buildEdit(project, false, project.patterns.front().id, te::Edit::forRendering, error);
            require(bool(edit), "Actual native Edit for original instrument lifecycle");
            auto host = std::make_shared<PluginHost>();
            require(host->start(spec, rate, quantum, true).wasOk(),
                    "Actual original native instrument worker prepared");
            const auto metadata = catalogue(*host);
            const auto pid = host->processId();
            const auto instance = host->runtimeIdentity();
            const auto originalReset = host->midiResetGeneration();
            auto timeline = std::make_shared<LiveTimeline>(), nextTimeline = std::make_shared<LiveTimeline>();
            const auto channel = project.channels.front().id;
            const auto note = project.allocate(), placement = project.allocate();
            const auto document = [&]
            {
                auto result = std::make_unique<LiveDocument>();
                result->generation = 1;
                LiveChannel lane;
                lane.id = channel;
                lane.sampleInstrument = false;
                lane.notes = {{note, placement, 0, 160 / rate, 60, 1.f, false}};
                lane.events = {{0, 0, true}, {160 / rate, 0, false}};
                result->channels.push_back(std::move(lane));
                AutomationDocument constant;
                constant.initialValues = {{{AutomationParameter::pluginNormalized, spec.id, 77}, .25f}};
                std::string why;
                require(AutomationProgram::compile(constant, true, result->automation, why),
                        "Original immutable instrument gain target");
                result->nativeAutomation = {{spec.id, metadata.generation, instance}};
                return result;
            };
            auto doc = document();
            require(timeline->publish(doc), "Original live timeline published");
            doc = document();
            require(nextTimeline->publish(doc), "Independent candidate timeline has same musical source IDs");
            auto source = edit->getPluginCache().createNewPlugin(FixedSampler::xmlTypeName, {});
            auto *incumbent = dynamic_cast<FixedSampler *>(source.get());
            require(incumbent != nullptr, "Actual native instrument Channel adapter");
            incumbent->setTimeline(timeline, channel);
            incumbent->setRemote(host, false, spec.id);
            auto input = std::make_shared<MidiLane>(), typing = std::make_shared<MidiLane>();
            input->enabled = true;
            typing->enabled = true;
            incumbent->setInput(input);
            incumbent->setTypingInput(typing);
            const te::PluginInitialisationInfo info{{}, rate, graphBlock};
            source->baseClassInitialise(info);
            struct Release
            {
                te::Plugin::Ptr source, candidate;
                ~Release()
                {
                    if (candidate && !candidate->baseClassNeedsInitialising())
                        candidate->baseClassDeinitialise();
                    if (source && !source->baseClassNeedsInitialising())
                        source->baseClassDeinitialise();
                }
            } release{source, {}};
            const auto runtime = incumbent->runtimeIdentity();
            juce::AudioBuffer<float> buffer(2, 64);
            std::array<std::array<float, total>, 2> observed{};
            const auto process = [&](FixedSampler &shell, int start, int frames)
            {
                buffer.clear();
                te::PluginRenderContext fc(&buffer, 0, frames, nullptr, 0,
                                           {te::TimePosition::fromSeconds(start / rate),
                                            te::TimePosition::fromSeconds((start + frames) / rate)},
                                           true, false, false, false);
                shell.applyToBuffer(fc); // Sole processing owner, direct component experiment.
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < frames; ++n)
                        observed[c][start + n] = buffer.getSample(c, n);
            };
            const auto push = [](MidiLane &lane, bool on, int pitch)
            {
                const unsigned char bytes[3] = {static_cast<unsigned char>(on ? 0x90 : 0x80),
                                                static_cast<unsigned char>(pitch), 127};
                lane.push(bytes, 3);
            };
            process(*incumbent, 0, 7);
            push(*input, true, 60);
            require(host->packetFrame() == 7,
                    "Actual instrument quantum is partial before candidate preparation");
            if (rate == 48000 && !adopt)
            {
                auto other = FixedSampler::createDetachedGraph(*edit, source->state.createCopy());
                auto *wrong = dynamic_cast<FixedSampler *>(other.get());
                wrong->setTimeline(nextTimeline, channel);
                auto replacement = std::make_shared<PluginHost>();
                require(replacement->start(spec, rate, quantum, true).wasOk() &&
                            replacement->runtimeIdentity() != instance,
                        "Actual second instrument worker has distinct immutable runtime identity");
                wrong->setRemote(replacement, false, spec.id);
                require(wrong->shareCompatibleRuntimeFrom(*incumbent).failed(),
                        "A real replacing instrument Host is not continuous");
                wrong->setRemote(host, false, spec.id + 1);
                require(wrong->shareCompatibleRuntimeFrom(*incumbent).failed(),
                        "Same worker cannot impersonate a different instrument ID");
                wrong->setRemote(host, false, spec.id);
                auto foreignInput = std::make_shared<MidiLane>();
                wrong->setInput(foreignInput);
                require(wrong->shareCompatibleRuntimeFrom(*incumbent).failed() && host->packetFrame() == 7 &&
                            incumbent->runtimeAttachmentsControl() == 1,
                        "Different immutable MIDI lane cannot replace held ownership");
                auto incompatible = FixedSampler::createDetachedGraph(*edit, source->state.createCopy());
                auto *unprepared = dynamic_cast<FixedSampler *>(incompatible.get());
                unprepared->setTimeline(nextTimeline, channel);
                unprepared->setRemote(host, false, spec.id);
                require(unprepared->shareCompatibleRuntimeFrom(*incumbent).wasOk(),
                        "Compatible original identity before graph-capacity refusal");
                bool refused = false;
                try
                {
                    incompatible->attachFixedGraphInitialisation({{}, rate, graphBlock + 1});
                }
                catch (const std::logic_error &)
                {
                    refused = true;
                }
                require(refused && incompatible->baseClassNeedsInitialising() &&
                            incumbent->runtimeAttachmentsControl() == 1 && host->packetFrame() == 7,
                        "Incompatible native graph capacity fails balanced initialization without "
                        "partial-packet panic/reset");
            }
            auto candidate = FixedSampler::createDetachedGraph(*edit, source->state.createCopy());
            auto *successor = dynamic_cast<FixedSampler *>(candidate.get());
            successor->setTimeline(nextTimeline, channel);
            successor->setRemote(host, false, spec.id);
            require(successor->shareCompatibleRuntimeFrom(*incumbent).wasOk(),
                    "Exact worker and immutable lane ownership admitted");
            candidate->attachFixedGraphInitialisation(info);
            release.candidate = candidate;
            require(successor->runtimeIdentity() == runtime && successor->runtimeAttachmentsControl() == 2 &&
                        host->packetFrame() == 7,
                    "Private shell initialization preserves pending native MIDI/automation stream");
            process(*incumbent, 7, 16);
            push(*typing, true, 60);
            require(incumbent->heldInputKeys() == 1 && host->packetFrame() == 23,
                    "Original input owner remains held before adoption");
            FixedSampler *active = incumbent;
            if (adopt)
            {
                process(*successor, 23, 13);
                source->baseClassDeinitialise();
                release.source = nullptr;
                active = successor;
                require(successor->heldInputKeys() == 2 && successor->runtimeAttachmentsControl() == 1,
                        "Retiring source cannot clear physical/typing owners of same held score key");
            }
            else
            {
                candidate->baseClassDeinitialise();
                release.candidate = nullptr;
                candidate = nullptr;
                process(*incumbent, 23, 13);
                require(incumbent->runtimeAttachmentsControl() == 1 && incumbent->heldInputKeys() == 2,
                        "Discarding private candidate cannot clear original held owners");
            }
            process(*active, 36, 28);
            process(*active, 64, 64);
            process(*active, 128, 32);
            process(*active, 160, 32);
            push(*input, false, 60);
            process(*active, 192, 8); // Typing still holds after score and physical release.
            push(*typing, false, 60);
            process(*active, 200, 20);
            push(*input, true, 67);
            process(*active, 220, 36);
            process(*active, 256, 24);
            push(*input, false, 67);
            process(*active, 280, 40);
            process(*active, 320, 64);
            const auto deadline = juce::Time::getMillisecondCounter() + 5000;
            while (host->statistics().processed < host->statistics().submitted &&
                   juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep(2);
            const auto stats = host->statistics();
            double maximumError = 0;
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < total; ++n)
                {
                    const int sourceFrame = n - quantum;
                    const int pitch = sourceFrame >= 0 && sourceFrame < 200     ? 60
                                      : sourceFrame >= 220 && sourceFrame < 280 ? 67
                                                                                : -1;
                    const int onCount = sourceFrame < 220 ? 1 : 2;
                    const double expected = pitch < 0 ? 0
                                            : c == 0  ? .25 * ((pitch + 1) / 512. + onCount / 4096.)
                                                      : -.25 * ((128 - pitch) / 512. + onCount / 8192.);
                    maximumError = !std::isfinite(observed[c][n]) || !std::isfinite(expected)
                                       ? std::numeric_limits<double>::infinity()
                                       : std::max(maximumError, std::abs(double(observed[c][n]) - expected));
                }
            report += "rate=" + juce::String(rate) + ";adopt=" + juce::String(int(adopt)) +
                      ";maxError=" + juce::String(maximumError, 12) +
                      ";nativeGainEvents=" + juce::String(juce::int64(stats.parametersApplied)) +
                      ";resetGeneration=" + juce::String(host->midiResetGeneration()) + "\n";
            require(folder.getChildFile("instrument-runtime-measurements.txt").replaceWithText(report),
                    "Retain original PCM evidence before oracle assertions");
            require(maximumError < 1.e-6 && stats.processed == stats.submitted &&
                        stats.parametersApplied == total / quantum && stats.parametersRejected == 0 &&
                        !host->deliveryFailed() && host->midiResetGeneration() == originalReset,
                    "Independent signed native held-key/retrigger/automation PCM oracle, exact event count, "
                    "no old-shell panic");
            const auto after = catalogue(*host, true);
            const auto value = [&](std::uint32_t id)
            {
                for (const auto &p : after.parameters)
                    if (p.id == id)
                        return p.value;
                throw std::runtime_error("Missing original diagnostic counter");
            };
            require(std::abs(value(90) - 2.f / 1024) < 1.e-7 && std::abs(value(91) - 2.f / 1024) < 1.e-7 &&
                        active->heldInputKeys() == 0 && host->processId() == pid &&
                        host->runtimeIdentity() == instance && host->alive(),
                    "Real native counters prove exactly two on/off deliveries, no retriggers/stuck owners or "
                    "worker replacement");
            active->baseClassDeinitialise();
            release.source = nullptr;
            release.candidate = nullptr;
            require(active->runtimeAttachmentsControl() == 0,
                    "Final initialized shell alone releases runtime ownership");
            buffer.clear();
            host->process(buffer, 0, 64, nullptr, 0, total / rate, 120, false, false);
            const auto panicDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (host->midiResetGeneration() == originalReset &&
                   juce::Time::getMillisecondCounter() < panicDeadline)
                juce::Thread::sleep(2);
            require(host->midiResetGeneration() == originalReset + 1,
                    "Only last initialized shell requests actual processed native panic");
        }
    require(folder.getChildFile("instrument-runtime-result.txt")
                .replaceWithText("PASS4 original SDK instrument/component/worker cases, held "
                                 "score+physical+typing owners, independent signed stereo/retrigger PCM, "
                                 "partial native automation event counts, candidate discard/sequential "
                                 "adoption/retirement, final-only panic. "
                                 "No actual retained graph publication, live timing, installed Analog Lab, "
                                 "native desktop, hardware or listening qualification.\n"),
            "Retain actual original instrument runtime result");
}
} // namespace test
