// SPDX-License-Identifier: AGPL-3.0-or-later
// Included in Commands.cpp after StateAction and fresh-capture helpers.
namespace
{
bool nativeScalarValue(float value) noexcept
{
    return std::isfinite(value) && value >= 0 && value <= 1;
}
juce::Result nativeScalarShape(const Project &project, std::span<const NativeScalarEdit> edits)
{
    if (edits.empty() || edits.size() > nativeScalarHistoryMaxEdits)
        return juce::Result::fail("parameterHistory.capacity");
    std::set<std::pair<Id, std::uint32_t>> seen;
    for (const auto &edit : edits)
    {
        if (edit.plugin <= 0 || !project.plugin(edit.plugin))
            return juce::Result::fail("parameterHistory.targetMissing");
        if (!nativeScalarValue(edit.before) || !nativeScalarValue(edit.after) ||
            (edit.manualBefore && !nativeScalarValue(*edit.manualBefore)))
            return juce::Result::fail("parameterHistory.value");
        if (edit.before == edit.after)
            return juce::Result::fail("parameterHistory.noChange");
        if (!seen.emplace(edit.plugin, edit.parameter).second)
            return juce::Result::fail("parameterHistory.duplicate");
    }
    return juce::Result::ok();
}
PluginBinding *nativeScalarBinding(PluginSpec &plugin, std::uint32_t parameter)
{
    const auto found = std::find_if(plugin.automationBindings.begin(), plugin.automationBindings.end(),
                                    [&](const auto &binding) { return binding.id == parameter; });
    return found == plugin.automationBindings.end() ? nullptr : &*found;
}
juce::Result nativeScalarRewrite(Project &destination, std::span<const NativeScalarEdit> edits,
                                 bool expectedAfter, bool replaceAfter, std::size_t &bytes)
{
    std::map<Id, std::vector<PluginParameterStateEdit>> patches;
    for (const auto &edit : edits)
        patches[edit.plugin].push_back({edit.parameter, expectedAfter ? edit.after : edit.before,
                                        replaceAfter ? edit.after : edit.before});
    for (const auto &[id, patch] : patches)
    {
        auto *plugin = destination.plugin(id);
        if (!plugin)
            return juce::Result::fail("parameterHistory.targetMissing");
        const auto size = plugin->state.getNumBytesAsUTF8();
        if (size > nativeScalarHistoryRewriteBytes - bytes)
            return juce::Result::fail("parameterHistory.capacity");
        bytes += size;
        if (auto result = rewritePluginParameterStates(plugin->state, patch, plugin->state); result.failed())
            return result;
    }
    return juce::Result::ok();
}
// Positive immutable-action proof. Normalize ONLY witnessed numeric values and
// their existing manual bindings; every opaque chunk/other parameter/model field
// must match. A public generic snapshot call cannot forge a scalar history tag.
juce::Result validateNativeScalarSnapshots(const Project &before, const Project &after,
                                           std::span<const NativeScalarEdit> edits)
{
    if (auto valid = nativeScalarShape(after, edits); valid.failed())
        return valid;
    auto normalizedBefore = before, normalizedAfter = after;
    std::size_t bytes = 0;
    for (const auto &edit : edits)
    {
        auto *old = normalizedBefore.plugin(edit.plugin), *next = normalizedAfter.plugin(edit.plugin);
        if (!old || !next || old->descriptor != next->descriptor)
            return juce::Result::fail("parameterHistory.targetChanged");
        auto *oldBinding = nativeScalarBinding(*old, edit.parameter);
        auto *nextBinding = nativeScalarBinding(*next, edit.parameter);
        if (bool(oldBinding) != bool(nextBinding) || bool(oldBinding) != edit.manualBefore.has_value() ||
            (oldBinding && (oldBinding->manual != *edit.manualBefore || nextBinding->manual != edit.after)))
            return juce::Result::fail("parameterHistory.bindingMismatch");
        if (oldBinding)
            oldBinding->manual = edit.after;
    }
    if (auto valid = nativeScalarRewrite(normalizedBefore, edits, false, true, bytes); valid.failed())
        return valid;
    if (auto valid = nativeScalarRewrite(normalizedAfter, edits, true, true, bytes); valid.failed())
        return valid;
    if (!normalizedBefore.state().isEquivalentTo(normalizedAfter.state()))
        return juce::Result::fail("parameterHistory.unsupportedDelta");
    return juce::Result::ok();
}
} // namespace

juce::Result Controller::prepareNativeScalarEdit(const Project &capturedAfter,
                                                 std::span<const NativeScalarEdit> witnesses,
                                                 std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (hasGesture() || undo.isPerformingUndoRedo() ||
            revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("parameterHistory.busy");
        if (auto valid = validateMixerChainCapture(project, capturedAfter); valid.failed())
            return valid;
        if (auto valid = nativeScalarShape(capturedAfter, witnesses); valid.failed())
            return valid;
        auto before = capturedAfter, after = capturedAfter;
        std::vector<NativeScalarEdit> edits(witnesses.begin(), witnesses.end());
        std::size_t bytes = 0;
        for (auto &edit : edits)
        {
            auto *binding = nativeScalarBinding(*before.plugin(edit.plugin), edit.parameter);
            if (binding)
            {
                if (edit.manualBefore && *edit.manualBefore != binding->manual)
                    return juce::Result::fail("parameterHistory.bindingMismatch");
                edit.manualBefore = binding->manual;
                nativeScalarBinding(*after.plugin(edit.plugin), edit.parameter)->manual = edit.after;
            }
            else if (edit.manualBefore)
                return juce::Result::fail("parameterHistory.bindingMismatch");
        }
        if (auto valid = nativeScalarRewrite(before, edits, true, false, bytes); valid.failed())
            return valid;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto valid = prepareSnapshotChange(std::move(after), before, "Native parameter edit", true, false,
                                               true, prepared, edits);
            valid.failed())
            return valid;
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("parameterHistory.preparationFailed");
    }
}

bool Controller::isNativeScalarHistory(HistoryDirection direction) const
{
    if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
        return false;
    juce::Array<const juce::UndoableAction *> actions;
    if (direction == HistoryDirection::undo)
        undo.getActionsInUndoTransaction(actions);
    else
        undo.getActionsInRedoTransaction(actions);
    const auto *action = actions.size() == 1 ? dynamic_cast<const StateAction *>(actions[0]) : nullptr;
    return action && action->belongsTo(*this) && action->isNativeScalarAction();
}

juce::Result Controller::prepareNativeScalarHistory(HistoryDirection direction, const Project &capturedSource,
                                                    std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
            return juce::Result::fail("parameterHistory.invalidDirection");
        if (hasGesture() || undo.isPerformingUndoRedo() ||
            revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("parameterHistory.busy");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        juce::Array<const juce::UndoableAction *> actions;
        if (direction == HistoryDirection::undo)
            undo.getActionsInUndoTransaction(actions);
        else
            undo.getActionsInRedoTransaction(actions);
        const auto *action = actions.size() == 1 ? dynamic_cast<const StateAction *>(actions[0]) : nullptr;
        if (!action || !action->belongsTo(*this) || !action->isNativeScalarAction())
            return juce::Result::fail("parameterHistory.unsupportedTransaction");
        const bool undoing = direction == HistoryDirection::undo;
        auto edge = undoing ? action->afterSnapshot() : action->beforeSnapshot();
        copyMatchingMixerHistoryStates(edge, project);
        if (!edge.state().isEquivalentTo(project.state()))
            return juce::Result::fail("parameterHistory.staleSource");
        // Both snapshots start from FRESH current opaque state. Only the owned
        // target scalar/manual value differs; unrelated current native changes
        // and hidden chunks are retained, never restored from historical bytes.
        auto before = capturedSource, after = capturedSource;
        std::size_t bytes = 0;
        for (const auto &edit : action->nativeScalars())
        {
            auto *old = before.plugin(edit.plugin), *next = after.plugin(edit.plugin);
            if (!old || !next)
                return juce::Result::fail("parameterHistory.targetMissing");
            auto *oldBinding = nativeScalarBinding(*old, edit.parameter);
            auto *nextBinding = nativeScalarBinding(*next, edit.parameter);
            if (bool(oldBinding) != edit.manualBefore.has_value() ||
                (oldBinding && oldBinding->manual != (undoing ? edit.after : *edit.manualBefore)))
                return juce::Result::fail("parameterHistory.bindingMismatch");
            if (oldBinding)
            {
                oldBinding->manual = *edit.manualBefore;
                nextBinding->manual = edit.after;
            }
        }
        if (auto valid = nativeScalarRewrite(undoing ? before : after, action->nativeScalars(), undoing,
                                             !undoing, bytes);
            valid.failed())
            return valid;
        if (auto valid = validateNativeScalarSnapshots(before, after, action->nativeScalars());
            valid.failed())
            return valid;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto valid =
                prepareSnapshotChange(undoing ? before : after, project, {}, false, false, true, prepared);
            valid.failed())
            return valid;
        auto replacement = std::make_unique<StateAction>(*this, std::move(before), std::move(after),
                                                         action->nativeScalars(), true);
        const auto units = replacement->cachedSize();
        auto history = undo.prepareHistoryReplacement(undoing, std::move(replacement), units,
                                                      admittedSnapshotUnits, this);
        if (!history)
            return juce::Result::fail("parameterHistory.historyAdmissionRefused");
        prepared->history = std::move(history);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("parameterHistory.preparationFailed");
    }
}
