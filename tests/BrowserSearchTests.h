// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BrowserSearchService.h"
#include "BrowserQueryTests.h"
#include "BrowserCollectionsTests.h"
#include "BrowserTagTests.h"
static void browserSearchTests(const juce::File &output)
{
    using namespace vibera::browser_search;
    browserQueryTests();
    const juce::KeyPress f2(juce::KeyPress::F2Key), f3(juce::KeyPress::F3Key);
    const juce::KeyPress find('F', juce::ModifierKeys::ctrlModifier, 0);
    auto defaults = Customization::familiarDefaults();
    Customization restored;
    const auto defaultsResult = restored.importState(defaults.state());
    require(
        defaultsResult.wasOk() && restored.resolve(f2, Scope::browser) == "browser.previousMatch" &&
            restored.resolve(f2, Scope::rack) == "channel.properties" &&
            restored.resolve(f2, Scope::patterns) == "pattern.renameDialog" &&
            restored.resolve(f2, Scope::mixer) == "mixer.rename" &&
            restored.resolve(juce::KeyPress('R', juce::ModifierKeys::ctrlModifier, 0), Scope::browser) ==
                "browser.reindex" &&
            restored.resolve(juce::KeyPress('R', juce::ModifierKeys::ctrlModifier, 0), Scope::rack) ==
                "project.export",
        "Browser defaults round trip while Rack/Pattern/Mixer F2 and global export retain their contexts: " +
            defaultsResult.getErrorMessage().toStdString());
    auto old = defaults.state();
    auto *oldBindings = old["bindings"].getArray();
    for (int i = oldBindings->size(); --i >= 0;)
        if ((*oldBindings)[i]["action"].toString().startsWith("browser.") ||
            (*oldBindings)[i]["action"].toString() == "channel.properties")
            oldBindings->remove(i);
    old.getDynamicObject()->removeProperty("knownActions");
    require(restored.importState(old).wasOk() && restored.resolve(find, Scope::browser) == "browser.find" &&
                restored.resolve(f2, Scope::browser) == "browser.previousMatch" &&
                restored.resolve(f2, Scope::rack) == "channel.properties" &&
                restored.resolve(f2, Scope::patterns) == "pattern.renameDialog",
            "Legacy profile gains contextual Browser and Channel defaults without replacing Pattern F2");
    restored.unbind("browser.nextMatch", Scope::browser);
    Customization deliberate;
    require(deliberate.importState(restored.state()).wasOk() &&
                deliberate.resolve(f3, Scope::browser).isEmpty(),
            "Deliberately unbound Browser command stays unbound");
    require(defaults.bind("project.save", Scope::global, find).wasOk(), "Custom global Find fixture");
    old = defaults.state();
    oldBindings = old["bindings"].getArray();
    for (int i = oldBindings->size(); --i >= 0;)
        if ((*oldBindings)[i]["action"].toString().startsWith("browser."))
            oldBindings->remove(i);
    old.getDynamicObject()->removeProperty("knownActions");
    require(restored.importState(old).wasOk() && restored.resolve(find, Scope::browser) == "project.save",
            "New Browser defaults do not shadow a customized global shortcut");
    auto customRefresh = Customization::familiarDefaults();
    const juce::KeyPress refresh('R', juce::ModifierKeys::ctrlModifier, 0);
    customRefresh.unbind("project.export", Scope::global);
    require(customRefresh.bind("project.save", Scope::global, refresh).wasOk(),
            "Custom global refresh fixture");
    customRefresh.unbind("browser.reindex", Scope::browser);
    auto legacyRefresh = customRefresh.state();
    legacyRefresh.getDynamicObject()->removeProperty("knownActions");
    require(restored.importState(legacyRefresh).wasOk() &&
                restored.resolve(refresh, Scope::browser) == "project.save",
            "New contextual refresh default respects a customized global Ctrl+R");
    auto unboundRefresh = Customization::familiarDefaults();
    unboundRefresh.unbind("browser.reindex", Scope::browser);
    require(restored.importState(unboundRefresh.state()).wasOk() &&
                restored.resolve(refresh, Scope::browser) == "project.export",
            "Known unbound Browser refresh stays unbound on profile update");
    require(output.isDirectory(), "Browser output already created by runner");
    // Every file belongs to a new random child. Never delete a fixed-name folder
    // or touch the user's Browser preferences/cache.
    const auto owned = output.getNonexistentChildFile("browser-" + juce::Uuid().toString(), "", false);
    require(!owned.exists() && owned.isAChildOf(output) && owned.createDirectory().wasOk(),
            "Browser fixture ownership");
    browserCollectionsTests(owned);
    browserTagTests(owned);
    const auto a = owned.getChildFile("Library A"), b = owned.getChildFile("Library B");
    require(a.createDirectory().wasOk() && b.createDirectory().wasOk(), "Browser libraries");
    const auto nested = a.getChildFile("Nested Kits");
    require(nested.createDirectory().wasOk(), "Browser nested library");
    for (int i = 0; i < 1537; ++i)
        require(a.getChildFile("Kick " + juce::String(i).paddedLeft('0', 4) + ".wav")
                    .replaceWithText("Index metadata fixture, not playable audio"),
                "Browser wide fixture");
    require(nested.getChildFile("Glass bell.FLAC").replaceWithText("metadata"), "Browser FLAC fixture");
    require(nested.getChildFile("Phrase.mid").replaceWithText("metadata"), "Browser MIDI fixture");
    require(a.getChildFile("ignored.txt").replaceWithText("not music"), "Browser ignored fixture");
    require(b.getChildFile("Snare.wav").replaceWithText("metadata"), "Browser second root");
    const std::vector<std::string> roots{a.getFullPathName().toStdString(),
                                         nested.getFullPathName().toStdString()};
    Index index;
    IndexStats stats;
    require(Scanner(index, roots).run([] { return false; }, stats), "Browser full scan");
    require(index.size() == 1539 && stats.scannedFolders == 2, "Browser no per-folder truncation/dedup");
    require(index.match(parseQuery("KiCk type:.WAV")).size() == 1537, "Browser case/type matching");
    require(index.match(parseQuery("type:mid;flac")).size() == 2, "Browser multiple types");
    require(index.match(parseQuery("(k?ck OR bell) AND NOT 0000 type:wav;flac")).size() == 1537,
            "Browser indexed Boolean/wildcard/NOT/type composition");
    require(index.match(parseQuery("NOT (kick OR bell) type:mid")).size() == 1,
            "Browser negative expression over actual scanned MIDI metadata");
    require(index.match(parseQuery("bell folder:\"" + nested.getFullPathName() + "\" type:flac")).size() == 1,
            "Browser quoted Windows scope");
    require(!parseQuery("folder:\"unterminated").error.isEmpty(), "Browser invalid quote reported");
    require(withinFolder("E:\\Kits\\Drums\\kick.wav", "e:/kits") &&
                !withinFolder("E:/KitsElse/kick.wav", "E:/Kits"),
            "Browser scope boundary");
    std::vector<Entry> matches;
    int polls = 0;
    require(!index.match(parseQuery("kick"), matches, [&] { return ++polls == 17; }) && matches.empty(),
            "Browser canceled matching discards partial result");
    Index canceled;
    IndexStats canceledStats;
    polls = 0;
    require(!Scanner(canceled, roots).run([&] { return ++polls == 20; }, canceledStats) &&
                canceled.size() < index.size(),
            "Browser scanner observes cancellation");
    const auto cache = owned.getChildFile("cache/index.json");
    require(saveCache(cache, roots, index.snapshot()), "Browser explicit atomic cache");
    Index loaded;
    require(loadCache(loaded, cache, roots) && loaded.size() == 1539, "Browser cache round trip");
    require(!loadCache(loaded, cache, {b.getFullPathName().toStdString()}) && loaded.size() == 1539,
            "Browser wrong-root cache preserves index");
    const auto valid = cache.loadFileAsString();
    auto reject = [&](const juce::String &data, const char *message)
    {
        require(cache.replaceWithText(data), "Browser corrupt cache fixture write");
        require(!loadCache(loaded, cache, roots) && loaded.size() == 1539, message);
    };
    reject("{broken", "Browser corrupt JSON refused");
    auto data = juce::JSON::parse(valid);
    data.getDynamicObject()->setProperty("version", 99);
    reject(juce::JSON::toString(data), "Browser future cache version refused");
    data = juce::JSON::parse(valid);
    data.getDynamicObject()->setProperty("built", juce::Time::currentTimeMillis() - juce::int64(90000000));
    reject(juce::JSON::toString(data), "Browser stale cache refused");
    data = juce::JSON::parse(valid);
    auto *rows = data.getDynamicObject()->getProperty("entries").getArray();
    rows->add((*rows)[0]);
    reject(juce::JSON::toString(data), "Browser duplicate cache rows refused");
    require(cache.replaceWithText(valid), "Browser restore owned cache");

    auto wait = [](const std::function<bool()> &ready, int limit = 6000)
    {
        const auto start = juce::Time::getMillisecondCounter();
        while (!ready())
        {
            if (juce::Time::getMillisecondCounter() - start > unsigned(limit))
                return false;
            juce::Thread::sleep(5);
        }
        return true;
    };
    std::shared_ptr<const Service::Result> result;
    auto service = std::make_unique<Service>(cache);
    const auto terminal = service->terminalFlag();
    service->request(roots, parseQuery("no match"));
    const auto latest = service->request(roots, parseQuery("bell"));
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }),
            "Browser service completes");
    require(result->serial == latest && result->matches.size() == 1 && result->fromCache,
            "Browser latest-only cache result");
    result.reset();
    service->request(roots, parseQuery("kick"));
    service->cancel();
    juce::Thread::sleep(200);
    require(!service->takeResult() && service->phase() == Service::Phase::canceled,
            "Browser canceled request cannot publish");
    const auto preparations = service->indexPreparations();
    service->request(roots, parseQuery("bell"));
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->matches.size() == 1 && service->indexPreparations() == preparations,
            "Browser Escape preserves a completed index without filesystem/cache reread");
    result.reset();
    const auto beforeStarred = service->indexPreparations();
    Collections tagMetadata;
    const auto tagged0 = a.getChildFile("Kick 0000.wav"), tagged1 = a.getChildFile("Kick 0001.wav");
    require(tagMetadata.setTags(tagged0, {"Drum", "Tight"}).wasOk() &&
                tagMetadata.setTags(tagged1, {"Drum"}).wasOk(),
            "Worker tag metadata fixture");
    auto tagQuery = parseQuery("kick type:wav");
    tagQuery.tagCatalog = tagMetadata.tags;
    tagQuery.tagFilter = {{"drum", "tight"}, false};
    service->request(roots, tagQuery);
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->matches.size() == 2 && result->error.isEmpty() &&
                service->indexPreparations() == beforeStarred,
            "Tag ANY filtering reuses the completed file index");
    result.reset();
    tagQuery.tagFilter.all = true;
    const auto oldCatalog = tagQuery.tagCatalog;
    service->request(roots, tagQuery);
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->matches.size() == 1 && result->matches[0].tags.size() == 2,
            "Tag ALL matches and returns snapshot metadata labels");
    result.reset();
    require(tagMetadata.setTags(tagged1, {"Drum", "Tight"}).wasOk(), "Tag edit while index retained");
    tagQuery.tagCatalog = tagMetadata.tags;
    service->request(roots, tagQuery);
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->matches.size() == 2 && service->indexPreparations() == beforeStarred &&
                oldCatalog->tagsFor(tagged1.getFullPathName().toStdString()).size() == 1,
            "Metadata publication changes results without scanning, mutating prior snapshots or changing "
            "index cache");
    result.reset();
    auto starred = parseQuery("k?ck OR absent");
    starred.favoritesOnly = true;
    starred.favoritePaths = {a.getChildFile("Kick 0000.wav").getFullPathName().toStdString(),
                             a.getChildFile("Absent.wav").getFullPathName().toStdString()};
    service->request({}, starred);
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->error.isEmpty() && result->matches.size() == 2 &&
                service->indexPreparations() == beforeStarred,
            "Starred references work without configured roots or directory enumeration");
    require(std::count_if(result->matches.begin(), result->matches.end(),
                          [](const auto &entry) { return entry.missing; }) == 1,
            "Unavailable Starred file is retained and marked missing");
    result.reset();
    starred.favoritePaths.clear();
    service->request({}, starred);
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->error.isEmpty() && result->matches.empty(),
            "Empty Starred view reports zero rows rather than scanning libraries");
    result.reset();
    service->request(roots, parseQuery("kick"), true);
    const auto replacement = service->request({b.getFullPathName().toStdString()}, parseQuery("snare"));
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->serial == replacement && result->matches.size() == 1 &&
                result->matches[0].name == "Snare.wav",
            "Browser root generation supersedes old scan");
    result.reset();
    require(b.getChildFile("Snare two.wav").replaceWithText("metadata"), "Browser refresh new file");
    service->request({b.getFullPathName().toStdString()}, parseQuery("snare"), true);
    require(wait(
                [&]
                {
                    result = service->takeResult();
                    return bool(result);
                }) &&
                result->matches.size() == 2 && !result->fromCache,
            "Browser Refresh discovers new files");
    result.reset();
    service->request(roots, parseQuery("kick"), true);
    require(wait(
                [&]
                {
                    return service->phase() == Service::Phase::indexing ||
                           service->phase() == Service::Phase::ready;
                }),
            "Browser replacement scan started before destruction");
    const auto began = juce::Time::getMillisecondCounterHiRes();
    service.reset();
    require(juce::Time::getMillisecondCounterHiRes() - began < 100,
            "Browser destruction does not wait for filesystem worker");
    require(wait([&] { return terminal->load(); }), "Browser owned worker terminates cooperatively");
    // Retain all unique fixtures as evidence. No cleanup touches user data.
    std::cout << "PASS Browser wide scan/query/cache/cancellation/lifetime: 1539 indexed files\n";
}
