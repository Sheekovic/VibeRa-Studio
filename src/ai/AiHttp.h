// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <functional>

// Blocking HTTP helpers for AI worker threads only (never the message thread or DSP).
namespace vibera::ai
{
struct HttpResponse
{
    int status = 0; // 0: no HTTP response (connection failure, timeout or cancellation).
    juce::String body, error;
    bool ok() const noexcept
    {
        return status >= 200 && status < 300;
    }
};

using CancelCheck = std::function<bool()>;

HttpResponse httpRequest(const juce::String &method, const juce::String &url, const juce::StringPairArray &headers,
                         const juce::String &body, int timeoutMs, const CancelCheck &cancelled = {});

// Streams a (possibly multi-GB) download to `destination` through a .part file. An existing
// .part resumes with an HTTP Range request (restarting cleanly if the server ignores it).
// Progress reports the 0..1 fraction (when the size is known) and the current bytes/second.
// The final file appears only after a complete transfer.
using DownloadProgress = std::function<void(double fraction, double bytesPerSecond)>;
juce::Result httpDownload(const juce::String &url, const juce::File &destination, const DownloadProgress &progress,
                          const CancelCheck &cancelled);

// "{status}: {provider error message}" for failed responses; JSON error bodies are summarized.
juce::String describeHttpFailure(const HttpResponse &);
} // namespace vibera::ai
