// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera after ProductionPluginTransport.inc.
// Test-only private member; ordinary product code never calls this bridge.
void AudioEngine::withProductionMembershipSdkFixture(
    Controller &controller, PreparedControllerChange &model, const PluginSpec &added,
    const std::function<void(ProductionMembershipSdkFixture &)> &body)
{
    using Broker = source_membership::SourceMembershipBroker;
    const auto check = [](bool valid, const char *text)
    {
        if (!valid)
            throw std::runtime_error(text);
    };
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    check(messages && messages->isThisTheMessageThread() && bool(body),
          "Membership fixture requires CONTROL");
    check(!playing() && edit && productionPluginProjection && productionPluginPlan &&
              productionPluginProjection->plan == productionPluginPlan &&
              productionPluginProjection->context == edit->getTransport().getCurrentPlaybackContext() &&
              controller.project.state().isEquivalentTo(projectedProject.state()) &&
              controller.validatePreparedChange(model).wasOk() && model.destinationBeforeCommit(),
          "Membership fixture needs exact real stopped ordinary projection and canonical ticket");
    const auto *destination = model.destinationBeforeCommit()->plugin(added.id);
    check(destination && *destination == added && !controller.project.plugin(added.id),
          "Fresh actual added effect belongs only to this uncommitted destination");
    auto &projection = *productionPluginProjection;
    auto &broker = *projection.members;
    check(broker.bindTransitions(controller, *projection.context, *projection.session) == Broker::Error::none,
          "Actual Controller/context/session binding, no shadow broker");
    StoppedEffectWorkers::Ptr workers;
    const auto prepared = StoppedEffectWorkers::prepare({added}, projection.plan, {}, workers);
    check(prepared.wasOk() && workers && workers->transportPlan == projection.plan &&
              workers->hosts.size() == 1,
          "Actual same-plan original SDK worker prepared off DSP");
    auto fresh = workers->hosts.at(added.id);
    std::vector<source_membership::PreparedSourceHosts::Binding> bindings;
    bindings.reserve(currentHosts.size() + 1);
    for (const auto &[id, host] : currentHosts)
    {
        const auto *spec = controller.project.plugin(id);
        check(spec && host && host->alive(), "Exact incumbent Host membership");
        bindings.push_back({spec, host, {}, {}});
    }
    bindings.push_back({destination, fresh, {}, {}});
    Broker::Ticket::Ptr ticket;
    ProductionMembershipSdkFixture fixture;
    fixture.fresh = fresh;
    fixture.prepare = [&]
    { return broker.prepareExpansion(controller, model, bindings, bindings.size(), ticket); };
    fixture.request = [&] { return broker.requestExpansion(ticket, controller, model); };
    fixture.cancel = [&] { return broker.cancel(ticket); };
    fixture.scopes = [&](source_membership::Receipt &members, te::SourceBurstSession::Outcome &native)
    { return projection.session->read(native) && broker.read(members); };
    fixture.outcome = [&]
    {
        te::SourceBurstSession::Outcome native;
        if (!ticket || !projection.session->read(native))
        {
            Broker::Outcome value;
            value.error = Broker::Error::pendingNativeClose;
            return value;
        }
        return broker.observe(*ticket, native);
    };
    fixture.acknowledge = [&]
    {
        te::SourceBurstSession::Outcome native;
        return projection.session->read(native) ? broker.acknowledge(ticket, native)
                                                : Broker::Error::pendingNativeClose;
    };
    fixture.size = [&] { return broker.size(); };
    body(fixture); // Callbacks are borrowed for this synchronous body ONLY.
    // A thrown body never performs CONTROL abort/reset. An accepted request
    // remains broker-owned until the caller's real context teardown excludes DSP.
}
