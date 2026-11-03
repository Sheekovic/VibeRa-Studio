// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/AudioEngine.h"
#include "../src/PreparedSourceMembership.h"

namespace vibera
{
// Synchronous CONTROL-only test bridge. AudioEngine owns the implementation;
// this friend struct cannot access broker private methods transitively.
struct ProductionMembershipSdkFixture
{
    using Broker = source_membership::SourceMembershipBroker;
    std::shared_ptr<PluginHost> fresh;
    std::function<Broker::Error()> prepare, request, cancel, acknowledge;
    std::function<Broker::Outcome()> outcome;
    std::function<bool(source_membership::Receipt &, te::SourceBurstSession::Outcome &)> scopes;
    std::function<std::size_t()> size;
    static void with(AudioEngine &audio, Controller &controller, PreparedControllerChange &model,
                     const PluginSpec &added,
                     const std::function<void(ProductionMembershipSdkFixture &)> &body)
    {
        audio.withProductionMembershipSdkFixture(controller, model, added, body);
    }
};
} // namespace vibera
