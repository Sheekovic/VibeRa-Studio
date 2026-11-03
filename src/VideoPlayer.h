// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace vibera
{
// Where the video should be for a Song position: `offset` seconds of video align with
// Song time zero (negative starts the video later). Clamped to the video.
inline double videoTargetSeconds(double songSeconds, double offset, double duration) noexcept
{
    return juce::jlimit(0., std::max(0., duration), songSeconds + offset);
}
// Decoder jumps (instead of decoding forward) when the target moves back or far ahead.
inline bool videoNeedsSeek(double decoded, double target) noexcept
{
    return target < decoded - .05 || target > decoded + 1.;
}

// Windows Media Foundation frame decoder (MP4/H.264 and whatever codecs Windows has):
// a worker thread decodes toward the requested time and publishes the latest frame.
class VideoDecoder
{
  public:
    VideoDecoder();
    ~VideoDecoder();
    juce::Result open(const juce::File &);
    void close();
    bool isOpen() const noexcept { return open_.load(); }
    double duration() const noexcept { return duration_; }
    double position() const noexcept { return decoded_.load(); }
    void setTarget(double seconds) noexcept { target_.store(seconds); }
    // Latest decoded frame (thread-safe copy of the reference).
    juce::Image frame() const;

  private:
    void run();
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::thread worker;
    std::atomic<bool> open_{false}, stop_{false};
    std::atomic<double> target_{0}, decoded_{-1};
    double duration_ = 0;
    mutable std::mutex frameLock;
    juce::Image latest;
};

// Floating window that shows the decoded frame for the current Song position.
class VideoPlayerWindow final : public juce::DocumentWindow, private juce::Timer
{
  public:
    explicit VideoPlayerWindow(std::function<void()> onClose)
        : juce::DocumentWindow("Video", panel, juce::DocumentWindow::closeButton, true), closed(std::move(onClose))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(30);
        setResizable(true, true);
        setContentNonOwned(&view, false);
        centreWithSize(640, 390);
        startTimerHz(30);
    }
    juce::Result open(const juce::File &file)
    {
        auto result = decoder.open(file);
        if (result.wasOk())
        {
            path = file;
            setName("Video - " + file.getFileName());
            setVisible(true);
        }
        return result;
    }
    void follow(double songSeconds, bool)
    {
        decoder.setTarget(videoTargetSeconds(songSeconds, offset, decoder.duration()));
    }
    void closeButtonPressed() override
    {
        if (closed)
            closed();
    }
    VideoDecoder decoder;
    juce::File path;
    double offset = 0;

  private:
    struct View final : juce::Component
    {
        juce::Image image;
        void paint(juce::Graphics &g) override
        {
            g.fillAll(juce::Colours::black);
            if (image.isValid())
                g.drawImageWithin(image, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred);
        }
    } view;
    void timerCallback() override
    {
        auto next = decoder.frame();
        if (next.isValid() && !next.isNull() && next != view.image)
        {
            view.image = next;
            view.repaint();
        }
    }
    std::function<void()> closed;
};
} // namespace vibera
