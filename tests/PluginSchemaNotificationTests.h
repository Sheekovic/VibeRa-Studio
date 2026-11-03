// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

static void pluginSchemaNotificationTests(const juce::File &folder, const juce::File &binary)
{
    PluginHost host;
    require(host.start(parameterSpec(binary), 48000, 256, false).wasOk(),
            "schema notification original worker starts");
    const auto pid = host.processId();
    const auto before = parameterCatalogue(host);
    host.requestParameterCatalogue();
    juce::Thread::sleep(60); // Keep a ready catalogue unread while the provider notifies.
    require(parameterResult(host, host.setParameter(42, .25f, before.generation)).result == 0,
            "original fixture sends a titles notification without changing its schema");
    juce::Thread::sleep(60);
    const auto unchanged = parameterCatalogue(host, true);
    const auto trace = "before=" + juce::String(before.generation) +
                       " unchanged=" + juce::String(unchanged.generation) + "\n";
    require(folder.getChildFile("schema-notification.txt").replaceWithText(trace),
            "retain notification red/green evidence before assertions");
    require(unchanged.generation == before.generation &&
                std::abs(nativeParameter(unchanged, 42).value - .25f) < .00001f && host.processId() == pid &&
                host.alive(),
            "redundant titles notification preserves generation and native control value");
    juce::AudioBuffer<float> block(2, 256);
    const ipc::ParameterEvent event{0, 0, .8f};
    const auto deliver = [&](unsigned generation)
    {
        block.clear();
        host.process(block, 0, 256, nullptr, 0, 0, 120, true, false, 4, 4, &event, 1, generation);
        const auto end = juce::Time::getMillisecondCounter() + 2000;
        while (host.statistics().processed < host.statistics().submitted &&
               juce::Time::getMillisecondCounter() < end)
            juce::Thread::sleep(2); // Test-control wait after the live callback returned.
        require(host.statistics().processed == host.statistics().submitted,
                "schema test worker completes the submitted packet");
    };
    deliver(before.generation);
    require(host.statistics().parametersApplied == 1 && host.statistics().parametersRejected == 0,
            "pre-notification automation route remains valid after redundant notification");
    require(parameterResult(host, host.setParameter(42, 1, unchanged.generation)).result == 0,
            "original fixture really changes sparse target writability");
    juce::Thread::sleep(60);
    const auto changed = parameterCatalogue(host, true);
    require(changed.generation > unchanged.generation && !nativeParameter(changed, 0xfedcba98u).writable(),
            "real schema change still advances generation and publishes changed capabilities");
    require(parameterResult(host, host.setParameter(0, .4f, before.generation)).result == 1,
            "real change still rejects stale native control authority");
    deliver(before.generation);
    require(host.statistics().parametersApplied == 1 && host.statistics().parametersRejected > 0,
            "real change still refuses automation admitted against the stale schema");
    deliver(changed.generation);
    require(host.statistics().parametersApplied == 2 && host.processId() == pid && host.alive(),
            "fresh schema readmission restores native delivery without replacing the worker");
    require(folder.getChildFile("schema-notification.txt")
                .replaceWithText(trace + "real change=" + juce::String(changed.generation) +
                                 "; unchanged route, actual native delivery, stale rejection and fresh "
                                 "readmission PASS; no hardware/listening.\n"),
            "schema notification integration evidence");
}
