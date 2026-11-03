// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AiHttp.h"

namespace vibera::ai
{
namespace
{
juce::String headerBlock(const juce::StringPairArray &headers)
{
    juce::String block;
    for (int i = 0; i < headers.size(); ++i)
        block << headers.getAllKeys()[i] << ": " << headers.getAllValues()[i] << "\r\n";
    return block;
}
} // namespace

HttpResponse httpRequest(const juce::String &method, const juce::String &url, const juce::StringPairArray &headers,
                         const juce::String &body, int timeoutMs, const CancelCheck &cancelled)
{
    HttpResponse response;
    juce::URL target(url);
    if (body.isNotEmpty())
        target = target.withPOSTData(body);
    int status = 0;
    auto options = juce::URL::InputStreamOptions(body.isNotEmpty() ? juce::URL::ParameterHandling::inPostData
                                                                    : juce::URL::ParameterHandling::inAddress)
                       .withHttpRequestCmd(method)
                       .withExtraHeaders(headerBlock(headers))
                       .withConnectionTimeoutMs(timeoutMs)
                       .withStatusCode(&status)
                       .withProgressCallback([cancelled](int, int) { return !(cancelled && cancelled()); });
    auto stream = target.createInputStream(options);
    response.status = status;
    if (!stream)
    {
        response.error = cancelled && cancelled() ? "cancelled" : "Could not connect to " + target.getDomain();
        return response;
    }
    juce::MemoryOutputStream out;
    char buffer[16384];
    while (!stream->isExhausted())
    {
        if (cancelled && cancelled())
        {
            response.error = "cancelled";
            response.status = 0;
            return response;
        }
        const auto read = stream->read(buffer, sizeof(buffer));
        if (read <= 0)
            break;
        out.write(buffer, size_t(read));
    }
    response.body = out.toUTF8();
    return response;
}

juce::Result httpDownload(const juce::String &url, const juce::File &destination, const DownloadProgress &progress,
                          const CancelCheck &cancelled)
{
    if (!destination.getParentDirectory().createDirectory())
        return juce::Result::fail("download.folder: Cannot create " + destination.getParentDirectory().getFullPathName());
    const auto part = destination.withFileExtension(destination.getFileExtension() + ".part");
    const juce::int64 resumeFrom = part.existsAsFile() ? part.getSize() : 0;
    int status = 0;
    const juce::String range = resumeFrom > 0 ? "Range: bytes=" + juce::String(resumeFrom) + "-\r\n" : juce::String{};
    auto stream = juce::URL(url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                       .withExtraHeaders(range)
                                                       .withConnectionTimeoutMs(30000)
                                                       .withNumRedirectsToFollow(10)
                                                       .withStatusCode(&status));
    if (!stream || (status != 0 && (status < 200 || status >= 300)))
        return juce::Result::fail("download.http: " + url + " returned " + juce::String(status));
    // 206: the server continues the partial file. 200: it sent everything, so start over.
    const bool resumed = resumeFrom > 0 && status == 206;
    if (!resumed)
        part.deleteFile();
    const auto total = stream->getTotalLength() >= 0 ? stream->getTotalLength() + (resumed ? resumeFrom : 0)
                                                     : juce::int64(-1);
    {
        juce::FileOutputStream out(part); // Appends to an existing .part.
        if (!out.openedOk())
            return juce::Result::fail("download.write: Cannot write " + part.getFullPathName());
        juce::HeapBlock<char> buffer(1 << 20);
        juce::int64 written = resumed ? resumeFrom : 0;
        auto windowStart = juce::Time::getMillisecondCounterHiRes();
        juce::int64 windowBytes = 0;
        double rate = 0;
        while (!stream->isExhausted())
        {
            if (cancelled && cancelled())
            {
                out.flush();
                part.deleteFile();
                return juce::Result::fail("cancelled");
            }
            const auto read = stream->read(buffer.get(), 1 << 20);
            if (read <= 0)
                break;
            if (!out.write(buffer.get(), size_t(read)))
                return juce::Result::fail("download.write: Disk full or not writable: " + part.getFullPathName());
            written += read;
            windowBytes += read;
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (now - windowStart >= 1000)
            {
                rate = windowBytes * 1000.0 / (now - windowStart);
                windowStart = now;
                windowBytes = 0;
            }
            if (progress)
                progress(total > 0 ? double(written) / double(total) : -1, rate);
        }
        out.flush();
        if (total > 0 && written != total)
            return juce::Result::fail("download.incomplete: Received " + juce::String(written) + " of " +
                                      juce::String(total) + " bytes");
    }
    destination.deleteFile();
    return part.moveFileTo(destination) ? juce::Result::ok()
                                         : juce::Result::fail("download.rename: Cannot finish " +
                                                              destination.getFullPathName());
}

juce::String describeHttpFailure(const HttpResponse &response)
{
    if (response.status == 0)
        return response.error.isNotEmpty() ? response.error : "No response";
    const auto json = juce::JSON::parse(response.body);
    juce::String message;
    if (json["error"].isObject())
        message = json["error"]["message"].toString();
    else if (json["error"].isString())
        message = json["error"].toString();
    else if (json["message"].isString())
        message = json["message"].toString();
    if (message.isEmpty())
        message = response.body.substring(0, 300);
    return "HTTP " + juce::String(response.status) + ": " + message;
}
} // namespace vibera::ai
