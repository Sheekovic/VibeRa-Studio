// SPDX-License-Identifier: AGPL-3.0-or-later
// Original source-only actual-native request-token acceptance.
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <type_traits>

namespace vibera::test::publication_serial
{
using Player = ready_graph_acceptance::Player;
using Status = Player::ReadyGraphStatus;
inline void tests(const juce::File &folder)
{
    static_assert(noexcept(std::declval<const Player::ReadyGraph &>().publicationSerial()));
    static_assert(std::is_nothrow_move_constructible_v<Player::ReadyGraph>);
    require(folder.createDirectory().wasOk(), "Owned publication identity evidence folder");
    const auto file = folder.getChildFile("publication-serial.txt");
    require(!file.exists(), "Fresh exact publication identity evidence");
    juce::String report = "Original actual NodePlayer request identity and selected DSP. "
                          "Not application routing completion, recording lineage or hardware.\n";
    bool passed = true;
    const auto check = [&](bool ok, const char *name)
    {
        passed = passed && ok;
        report += juce::String(name) + "=" + juce::String(int(ok)) + "\n";
        require(file.replaceWithText(report), "Retain truthful original request-token results");
    };
    ready_graph_acceptance::Fixture fixture;
    auto &player = fixture.player;
    check(fixture.process(.125f), "actual_incumbent_PCM_before_private_requests");
    const auto original = player.graphSelection();
    Player::ReadyGraph empty;
    check(empty.publicationSerial() == 0 && empty.empty(), "default_no_reserved_request_identity");
    auto first = player.prepareFixedGraph(
        std::make_unique<ready_graph_acceptance::Constant>(.75f, 37, fixture.counts));
    auto competing = player.prepareFixedGraph(
        std::make_unique<ready_graph_acceptance::Constant>(.5f, 19, fixture.counts));
    const auto firstID = first.publicationSerial(), competingID = competing.publicationSerial();
    auto slot = player.makeGraphRetirementSlot();
    check(firstID != 0 && competingID != 0 && firstID != competingID &&
              firstID != original.selectedSerial && competingID != original.selectedSerial &&
              player.graphSelection().postedSerial == original.postedSerial &&
              player.graphSelection().selectedSerial == original.selectedSerial,
          "distinct_private_reserved_tokens_are_neither_posted_nor_adopted");
    auto moved = std::move(first);
    check(first.empty() && first.publicationSerial() == 0 && moved.publicationSerial() == firstID,
          "move_preserves_actual_ticket_token_and_empties_moved_from_identity");
    bool clean = true;
    check(ready_graph_acceptance::publishWithoutNewDelete(player, moved, slot, clean) == Status::ready &&
              clean && moved.empty() && moved.publicationSerial() == firstID,
          "actual_post_consumes_holder_but_retains_exact_request_identity_no_cpp_new_delete");
    const auto posted = player.graphSelection();
    check(posted.postedSerial == firstID && posted.selectedSerial == original.selectedSerial,
          "exact_prepared_token_matches_post_before_actual_DSP_adoption");
    check(ready_graph_acceptance::publishWithoutNewDelete(player, competing, slot, clean) ==
              Status::staleGeneration && clean && !competing.empty() &&
              competing.publicationSerial() == competingID &&
              player.graphSelection().postedSerial == firstID &&
              player.graphSelection().selectedSerial != competingID,
          "reserved_competing_identity_is_not_success_after_typed_stale_refusal");
    check(fixture.process(.75f), "actual_selected_target_PCM_no_observed_callback_new_delete");
    const auto selected = player.graphSelection();
    check(selected.state == Player::GraphSelectionState::selected &&
              selected.selectedSerial == firstID && selected.postedSerial == firstID &&
              selected.selectedSerial != competingID && moved.publicationSerial() == firstID,
          "actual_DSP_receipt_matches_prepared_identity_and_never_refused_request");
    player.clearNode();
    check(player.graphSelection().state == Player::GraphSelectionState::none &&
              moved.publicationSerial() == firstID && moved.empty(),
          "clear_invalidates_actual_selection_without_rewriting_historical_ticket_identity");
    require(passed, "Actual native publication token acceptance");
}

// Integrate into a genuine retained-Edit fixture. The caller owns its graph
// overlay, device/synthetic driver and independent PCM oracle. driveAndVerify()
// MUST execute and verify actual app callback(s), not merely poll state.
template <class DriveAndVerify, class Check>
void contextTest(tracktion::EditPlaybackContext &context,
                 std::shared_ptr<const tracktion::EditPlaybackContext::GraphOverlay> overlay,
                 DriveAndVerify driveAndVerify, Check check)
{
    const auto before = context.getGraphSelection();
    check(before.state == Player::GraphSelectionState::selected &&
              before.selectedSerial == before.postedSerial,
          "Context request oracle requires genuine selected incumbent");
    auto candidate = context.prepareGraphOnly(std::move(overlay));
    const auto requestID = candidate->publicationSerial();
    check(requestID != 0 && requestID != before.selectedSerial && !candidate->wasPublished(),
          "Actual EPC candidate owns exact request identity before publication");
    check(context.publishPreparedGraph(*candidate) ==
              tracktion::EditPlaybackContext::ReadyGraphChangeStatus::ready &&
              candidate->wasPublished() && candidate->publicationSerial() == requestID,
          "Actual EPC post retains prepared serial independently of consumed holder");
    const auto posted = context.getGraphSelection();
    check(posted.postedSerial == requestID && posted.selectedSerial == before.selectedSerial,
          "Sole control fixture distinguishes accepted request from actual selected graph");
    driveAndVerify();
    const auto selected = context.getGraphSelection();
    check(selected.state == Player::GraphSelectionState::selected &&
              selected.selectedSerial == requestID && candidate->publicationSerial() == requestID,
          "Actual app DSP adoption acknowledges the exact retained EPC request token");
    check(context.validatePreparedGraph(*candidate) ==
              tracktion::EditPlaybackContext::ReadyGraphChangeStatus::published,
          "Consumed EPC request cannot publish twice despite retained read-only identity");
}
}
