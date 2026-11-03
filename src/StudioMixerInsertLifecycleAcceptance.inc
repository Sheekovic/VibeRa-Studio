// SPDX-License-Identifier: AGPL-3.0-or-later
// Actual native variable-universe publication through shared UI/MCP history.
stage = "actual-variable-insert-create-route-delete-history";
const auto originalMixerCount = controller.project.mixer.size();
std::set<int> originalMixerIds;
for (const auto &row : controller.project.mixer)
    originalMixerIds.insert(row.index);
const auto lifecycle = [&](const char *command, const juce::var &values)
{
    auto request = object();
    put(request, "command", command);
    put(request, "args", values);
    put(request, "revision", juce::int64(controller.revision));
    return invoke("vibera_command", request);
};
const auto lifecycleHistory = [&](bool undo)
{
    if (undo)
        undoButton.onClick();
    else
        history("edit.redo");
    return finishChain(mixerChainReadback(lastLifecycleOperation), "completed");
};
audio.playStop();
tick();
auto create = object();
put(create, "text", "Original live auxiliary insert");
finishChain(lifecycle("mixer.insert.create", create), "completed");
const auto created = std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                                  [&](const auto &row) { return !originalMixerIds.contains(row.index); });
check(created != controller.project.mixer.end() &&
          controller.project.mixer.size() == originalMixerCount + 1 &&
          created->name == "Original live auxiliary insert" && created->index != 0 &&
          mixerInsert(controller.project, 0),
      "Actual create publishes a new stable native insert without replacing Master or original18");
const auto createdId = created->index;
verifyRecoveredSound();
lifecycleHistory(true);
check(controller.project.mixer.size() == originalMixerCount && !mixerInsert(controller.project, createdId),
      "Actual UI Undo removes only the created insert");
verifyRecoveredSound();
lifecycleHistory(false);
check(controller.project.mixer.size() == originalMixerCount + 1 && mixerInsert(controller.project, createdId),
      "Actual MCP Redo restores original stable insert identity");
verifyRecoveredSound();
const auto routedNew = route(createdId);
check(routedNew["completionState"].toString() == "pending",
      "New native insert accepts the ordinary channel assignment command");
tick();
check(!routingPending() && controller.project.channel(selectedChannel)->mixer == createdId,
      "Actual callback selects created insert as channel destination");
verifyRecoveredSound();
auto erase = object();
put(erase, "mixer", createdId);
const auto beforeRefusedDelete = controller.project.state().toXmlString();
const auto beforeRefusedDeleteRevision = controller.revision;
bool refusedReferencedInsert = false;
try
{
    (void)lifecycle("mixer.insert.delete", erase);
}
catch (const std::exception &error)
{
    // The real stdio transport converts McpFailure to a structured error. This
    // component fixture calls its underlying dispatcher directly.
    refusedReferencedInsert = juce::String(error.what()).contains("mixer.replacementRequired");
}
check(refusedReferencedInsert &&controller.revision == beforeRefusedDeleteRevision &&
          controller.project.state().toXmlString() == beforeRefusedDelete,
      "Referenced insert deletion refuses without implicit reroute or history mutation");
auto policy = object();
put(policy, "replacement", destinationInsert);
put(erase, "mixerDelete", policy);
finishChain(lifecycle("mixer.insert.delete", erase), "completed");
check(!mixerInsert(controller.project, createdId) && controller.project.mixer.size() == originalMixerCount &&
          controller.project.channel(selectedChannel)->mixer == destinationInsert,
      "Explicit delete reroutes channel to intended surviving insert through actual new native universe");
verifyRecoveredSound();
lifecycleHistory(true);
check(mixerInsert(controller.project, createdId) &&
          controller.project.channel(selectedChannel)->mixer == createdId,
      "UI Undo restores deleted insert and original assigned channel exactly");
verifyRecoveredSound();
lifecycleHistory(false);
check(!mixerInsert(controller.project, createdId) &&
          controller.project.channel(selectedChannel)->mixer == destinationInsert,
      "MCP Redo restores exact explicit deletion/reroute");
verifyRecoveredSound();
audio.stop();
tick();
passed.add(stage);
