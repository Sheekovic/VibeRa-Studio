// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace juce
{
// Paired with the original pinned GUI-wrapper adapter; not a plugin ABI/API.
// Call only on the message/control thread while this editor remains owned.
int viberaVST3NativeEditorAttachment(const AudioProcessorEditor &) noexcept;
} // namespace juce
namespace vibera
{
enum class NativeVst3EditorAttachment
{
    unsupported = 0,
    pending = 1,
    attached = 2,
    failed = 3
};
inline NativeVst3EditorAttachment
nativeVst3EditorAttachment(const juce::AudioProcessorEditor &editor) noexcept
{
    return static_cast<NativeVst3EditorAttachment>(juce::viberaVST3NativeEditorAttachment(editor));
}
} // namespace vibera
