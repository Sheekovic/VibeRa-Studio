// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void testDispatchPacingTests(const juce::File &folder)
{
    const auto measure = [](auto wait)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 80; ++i)
            wait();
        return juce::Time::getMillisecondCounterHiRes() - start;
    };
    // Preserve the old pump as a diagnostic comparison, not a production fallback.
    const double legacy = measure(
        []
        {
            const auto end = juce::Time::getMillisecondCounter() + 1u;
            do
            {
#if JUCE_WINDOWS
                MSG message;
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
#endif
                juce::Thread::sleep(1);
            } while (juce::Time::getMillisecondCounter() < end);
        });
    const double precise = measure([] { dispatchMessages(1); });
    auto *data = new juce::DynamicObject;
    data->setProperty("calls", 80);
    data->setProperty("requestedTotalMs", 80);
    data->setProperty("legacyMs", legacy);
    data->setProperty("preciseMs", precise);
    data->setProperty("scope", "Test-thread pacing only; no audio callback/deadline claim");
    require(folder.getChildFile("test-dispatch-pacing.json")
                .replaceWithText(juce::JSON::toString(juce::var(data))),
            "test pacing evidence written");
    require(precise >= 79 && precise < 800,
            "test message pump honors requested pacing without coarse scheduling-tick expansion");
    std::cout << "Test dispatch80x1ms: legacy=" << legacy << "ms precise=" << precise << "ms" << std::endl;
}
