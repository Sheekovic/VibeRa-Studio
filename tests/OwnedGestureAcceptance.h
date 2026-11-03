// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void ownedGestureTests()
{
    Controller c;
    const auto before = c.project.state();
    const auto channel = c.project.channels.front().id;
    CommandArgs a;
    a.channel = channel;
    a.value = .35;
    require(c.beginGesture("remote-A"), "remote gesture admitted without replacing local edit");
    require(c.execute("channel.gain", a, "remote-A").wasOk(), "owned preview follows canonical command");
    require(!c.beginGesture("remote-B"), "different remote owner cannot take over");
    c.endGesture("invalid owner", "remote-B");
    require(c.hasGesture() && c.gestureOwner() == "remote-A", "foreign commit cannot finish gesture");
    c.cancelGesture("remote-B");
    require(c.hasGesture(), "foreign cancellation cannot finish gesture");
    // A local slider drag rolls back the remote preview before starting its own
    // baseline. The local edit's Undo must not resurrect a remote preview.
    require(c.beginGesture(), "local UI drag preempts remote gesture");
    require(before.isEquivalentTo(c.project.state()), "local drag starts at pre-remote project");
    a.value = .6;
    require(c.execute("channel.gain", a).wasOk(), "local drag remains functional after preemption");
    c.endGesture("Local slider");
    c.undoEdit();
    require(before.isEquivalentTo(c.project.state()), "local Undo excludes cancelled remote preview");
    require(c.execute("channel.gain", a, "remote-A").failed(), "stale owner cannot edit later project");
    c.redoEdit();
    const auto local = c.project.state();
    c.dirty = false;
    require(c.beginGesture("remote-C"), "cancel fixture admitted");
    a.value = .1;
    require(c.execute("channel.gain", a, "remote-C").wasOk(), "cancel fixture live preview");
    c.cancelGesture("remote-C");
    require(!c.dirty && local.isEquivalentTo(c.project.state()), "cancel restores exact clean baseline");
    c.undoEdit();
    require(before.isEquivalentTo(c.project.state()), "cancel adds no Undo step");
    require(c.beginGesture("remote-D"), "replacement fixture admitted");
    c.replace(Project::demo());
    require(!c.hasGesture() && c.gestureOwner().isEmpty(), "project replacement releases owner");
    require(c.execute("channel.gain", a, "remote-D").failed(), "replaced session rejects old owner");
    Controller external;
    const auto externalBefore = external.project.state();
    a.channel = external.project.channels.front().id;
    require(external.beginGesture("remote-native-interleave") &&
                external.execute("channel.gain", a, "remote-native-interleave").wasOk(),
            "remote preview before unmodelled native edit");
    external.markExternalChange();
    require(!external.hasGesture() && external.dirty &&
                externalBefore.isEquivalentTo(external.project.state()) && !external.undo.canUndo(),
            "native edit preempts remote preview and remains unsaved without fabricated knob Undo");
    external.dirty = false;
    external.beginGesture();
    external.markExternalChange();
    external.cancelGesture();
    require(external.dirty && externalBefore.isEquivalentTo(external.project.state()),
            "local gesture cancellation cannot clear interleaved native dirtiness");
    external.dirty = false;
    external.beginGesture();
    external.markExternalChange();
    external.endGesture("No model change");
    require(external.dirty && !external.undo.canUndo(),
            "no-op local gesture completion retains external state without an empty Undo step");
}
