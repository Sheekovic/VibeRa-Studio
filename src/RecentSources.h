// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace vibera
{
// Learns which samples and instruments the producer reaches for. Every load into a Channel
// is recorded; the score is the use count with a 30-day half-life, so favourites rise and
// abandoned sounds fade. favourites() lists the top sound of each drum/instrument role
// first (the producer's kick, snare, hat...), then the next most-used sounds.
class RecentSources
{
  public:
    struct Entry
    {
        juce::String source; // Absolute sample path or builtin:name.
        int uses = 0;
        juce::int64 lastUsed = 0; // Milliseconds since epoch.
        double score(juce::int64 now) const
        {
            const double days = double(now - lastUsed) / 86400000.;
            return uses * std::pow(.5, std::max(0., days) / 30.);
        }
    };

    // Role from the file or builtin name ("Kick", "Snare", ...), or "Sample".
    static juce::String role(const juce::String &source)
    {
        if (source.startsWith("plugin:"))
            return "Instrument";
        const auto name = (source.startsWith("builtin:") ? source.fromFirstOccurrenceOf(":", false, false)
                                                        : juce::File(source).getFileNameWithoutExtension())
                              .toLowerCase();
        const auto has = [&](std::initializer_list<const char *> words)
        {
            for (auto *w : words)
                if (name.contains(w))
                    return true;
            return false;
        };
        if (has({"kick", "bd_", "bassdrum", "bass drum"}))
            return "Kick";
        if (has({"snare", "sd_", "rim"}))
            return "Snare";
        if (has({"clap", "snap"}))
            return "Clap";
        if (has({"hat", "hh", "hihat", "hi-hat"}))
            return "Hat";
        if (has({"808"}))
            return "808";
        if (has({"crash", "ride", "cymbal", "cym"}))
            return "Cymbal";
        if (has({"perc", "tom", "conga", "bongo", "shaker", "tamb", "darbuka", "tabla", "riq"}))
            return "Perc";
        if (has({"bass", "sub"}))
            return "Bass";
        if (has({"vox", "vocal", "chant"}))
            return "Vocal";
        if (has({"fx", "riser", "impact", "sweep", "noise"}))
            return "FX";
        return "Sample";
    }

    void load(const juce::File &store)
    {
        file = store;
        entries.clear();
        if (!file.existsAsFile())
            return;
        const auto json = juce::JSON::parse(file.loadFileAsString());
        if (const auto *list = json["sources"].getArray())
            for (const auto &item : *list)
            {
                Entry e{item["source"].toString(), int(item["uses"]), juce::int64(item["lastUsed"])};
                if (e.source.isNotEmpty() && e.uses > 0)
                    entries.push_back(e);
            }
    }
    void record(const juce::String &source)
    {
        if (source.isEmpty() ||
            (!source.startsWith("builtin:") && !source.startsWith("plugin:") && !juce::File::isAbsolutePath(source)))
            return;
        const auto now = juce::Time::currentTimeMillis();
        auto found = std::find_if(entries.begin(), entries.end(), [&](const Entry &e) { return e.source == source; });
        if (found == entries.end())
            entries.push_back({source, 1, now});
        else
        {
            ++found->uses;
            found->lastUsed = now;
        }
        // Keep the store small: drop the weakest beyond 200 sounds.
        if (entries.size() > 200)
        {
            std::sort(entries.begin(), entries.end(),
                      [&](const Entry &a, const Entry &b) { return a.score(now) > b.score(now); });
            entries.resize(200);
        }
        save();
    }
    void forget(const juce::String &source)
    {
        std::erase_if(entries, [&](const Entry &e) { return e.source == source; });
        save();
    }
    // Up to `count` sounds: the best of each role first, then the remaining by score.
    // Missing files are skipped (they stay remembered in case the drive comes back).
    std::vector<Entry> favourites(int count) const
    {
        const auto now = juce::Time::currentTimeMillis();
        auto ranked = entries;
        std::erase_if(ranked, [](const Entry &e)
                      {
                          return !e.source.startsWith("builtin:") && !e.source.startsWith("plugin:") &&
                                 !juce::File(e.source).existsAsFile();
                      });
        std::sort(ranked.begin(), ranked.end(),
                  [&](const Entry &a, const Entry &b) { return a.score(now) > b.score(now); });
        std::vector<Entry> result;
        juce::StringArray roles;
        for (const auto &e : ranked)
            if (int(result.size()) < count && !roles.contains(role(e.source)))
            {
                roles.add(role(e.source));
                result.push_back(e);
            }
        for (const auto &e : ranked)
            if (int(result.size()) < count &&
                std::none_of(result.begin(), result.end(), [&](const Entry &r) { return r.source == e.source; }))
                result.push_back(e);
        return result;
    }
    bool empty() const { return entries.empty(); }

  private:
    juce::File file;
    std::vector<Entry> entries;
    void save() const
    {
        if (file == juce::File{})
            return;
        juce::Array<juce::var> list;
        for (const auto &e : entries)
        {
            auto *o = new juce::DynamicObject;
            o->setProperty("source", e.source);
            o->setProperty("uses", e.uses);
            o->setProperty("lastUsed", e.lastUsed);
            list.add(juce::var(o));
        }
        auto *root = new juce::DynamicObject;
        root->setProperty("version", 1);
        root->setProperty("sources", list);
        file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::JSON::toString(juce::var(root)), false, false, "\n");
    }
};
} // namespace vibera
