// SPDX-License-Identifier: AGPL-3.0-or-later
#include "VideoPlayer.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace vibera
{
struct VideoDecoder::Impl
{
#if JUCE_WINDOWS
    IMFSourceReader *reader = nullptr;
    int width = 0, height = 0, stride = 0;
    bool started = false;
    ~Impl()
    {
        if (reader)
            reader->Release();
        if (started)
            MFShutdown();
    }
#endif
};

VideoDecoder::VideoDecoder() : impl(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() { close(); }

juce::Result VideoDecoder::open(const juce::File &file)
{
    close();
#if JUCE_WINDOWS
    impl = std::make_unique<Impl>();
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
        return juce::Result::fail("Media Foundation is unavailable");
    impl->started = true;
    IMFAttributes *attributes = nullptr;
    MFCreateAttributes(&attributes, 1);
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); // Converts to RGB32 for us.
    const auto hr = MFCreateSourceReaderFromURL(file.getFullPathName().toWideCharPointer(), attributes, &impl->reader);
    attributes->Release();
    if (FAILED(hr) || !impl->reader)
        return juce::Result::fail("Windows cannot open this video (missing codec?)");
    impl->reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    impl->reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    IMFMediaType *type = nullptr;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    const auto set = impl->reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
    type->Release();
    if (FAILED(set))
        return juce::Result::fail("No decoder for this video format on this PC");
    IMFMediaType *actual = nullptr;
    if (SUCCEEDED(impl->reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actual)))
    {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &w, &h);
        impl->width = int(w);
        impl->height = int(h);
        UINT32 stride = 0;
        impl->stride = SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? int(INT32(stride)) : int(w) * 4;
        actual->Release();
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(impl->reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)))
        duration_ = double(value.uhVal.QuadPart) / 1.e7;
    PropVariantClear(&value);
    if (impl->width <= 0 || impl->height <= 0)
        return juce::Result::fail("The video has no usable picture");
    stop_ = false;
    decoded_ = -1;
    open_ = true;
    worker = std::thread([this] { run(); });
    return juce::Result::ok();
#else
    juce::ignoreUnused(file);
    return juce::Result::fail("Video playback needs Windows");
#endif
}

void VideoDecoder::close()
{
    stop_ = true;
    if (worker.joinable())
        worker.join();
    open_ = false;
    impl = std::make_unique<Impl>();
    std::lock_guard<std::mutex> lock(frameLock);
    latest = {};
}

juce::Image VideoDecoder::frame() const
{
    std::lock_guard<std::mutex> lock(frameLock);
    return latest;
}

void VideoDecoder::run()
{
#if JUCE_WINDOWS
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    double decoded = -1;
    while (!stop_.load())
    {
        const double target = target_.load();
        if (decoded >= 0 && !videoNeedsSeek(decoded, target) && decoded >= target - .02)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (decoded < 0 || videoNeedsSeek(decoded, target))
        {
            PROPVARIANT position;
            PropVariantInit(&position);
            position.vt = VT_I8;
            position.hVal.QuadPart = LONGLONG(std::max(0., target) * 1.e7);
            impl->reader->SetCurrentPosition(GUID_NULL, position);
            PropVariantClear(&position);
        }
        DWORD flags = 0;
        LONGLONG stamp = 0;
        IMFSample *sample = nullptr;
        if (FAILED(impl->reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &stamp,
                                            &sample)) ||
            (flags & MF_SOURCE_READERF_ENDOFSTREAM))
        {
            if (sample)
                sample->Release();
            decoded = std::max(decoded, duration_);
            decoded_.store(decoded);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (!sample)
            continue;
        decoded = double(stamp) / 1.e7;
        decoded_.store(decoded);
        if (decoded >= target - .05)
        {
            IMFMediaBuffer *buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)))
            {
                BYTE *data = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)))
                {
                    // RGB32 (B,G,R,X) has the same byte order as JUCE's ARGB pixels: copy rows,
                    // then make every pixel opaque.
                    juce::Image image(juce::Image::ARGB, impl->width, impl->height, false);
                    {
                        juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
                        const int stride = std::abs(impl->stride);
                        for (int y = 0; y < impl->height && DWORD((y + 1) * stride) <= length; ++y)
                        {
                            const auto *row = data + (impl->stride < 0 ? (impl->height - 1 - y) : y) * stride;
                            auto *line = pixels.getLinePointer(y);
                            std::memcpy(line, row, size_t(impl->width) * 4);
                            for (int x = 0; x < impl->width; ++x)
                                line[x * 4 + 3] = 255;
                        }
                    }
                    buffer->Unlock();
                    std::lock_guard<std::mutex> lock(frameLock);
                    latest = image;
                }
                buffer->Release();
            }
        }
        sample->Release();
    }
    CoUninitialize();
#endif
}
} // namespace vibera
