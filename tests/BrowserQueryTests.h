// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void browserQueryTests()
{
    using namespace vibera::browser_search;
    auto accepts = [](const juce::String &query, const juce::String &name)
    {
        const auto compiled = parseQuery(query);
        require(compiled.error.isEmpty(), "Valid Browser expression: " + query.toStdString());
        return compiled.matches(name.toLowerCase().toStdString());
    };
    require(accepts("kick dry", "Big Kick Dry.wav") && !accepts("kick dry", "Dry kick.wav"),
            "Plain filename phrase preserves existing order and adjacency");
    require(accepts("kick AND dry", "Dry kick.wav") && !accepts("kick AND dry", "Kick wet.wav"),
            "Explicit AND combines filename terms");
    require(accepts("kick OR snare AND dry", "Kick wet.wav") &&
                !accepts("kick OR snare AND dry", "Snare wet.wav"),
            "AND precedence above OR");
    require(!accepts("(kick OR snare) AND dry", "Kick wet.wav") &&
                accepts("(kick OR snare) AND dry", "Snare dry.wav"),
            "Parentheses override precedence");
    require(accepts("kick NOT wet", "Kick dry.wav") && !accepts("kick NOT wet", "Kick wet.wav"),
            "Adjacent expression operands imply AND");
    require(accepts("NOT NOT kick", "Kick wet.wav") && !accepts("NOT (kick OR snare)", "Snare.wav"),
            "Unary NOT and nested expressions");
    require(accepts("\"AND\" OR bass", "AND union.wav") && !accepts("\"AND\" OR bass", "Kick.wav"),
            "Quoted operators are filename text");
    require(accepts("rock and roll", "Rock and roll.wav") && accepts("\"(dry)\"", "Kick (dry).wav"),
            "Lowercase operator words and quoted parentheses stay literal");
    {
        using browser_search::Entry;
        auto entry = [](std::string name, std::string ext, juce::int64 modified)
        {
            Entry e;
            e.name = name;
            e.lowerName = juce::String(name).toLowerCase().toStdString();
            e.extension = ext;
            e.modified = modified;
            return e;
        };
        std::vector<Entry> list{entry("snare.wav", ".wav", 3), entry("Bass.flac", ".flac", 1), entry("kick.wav", ".wav", 2)};
        auto names = [&] { std::string s; for (auto &e : list) s += e.name + ","; return s; };
        browser_search::sortEntries(list, 1);
        require(names() == "Bass.flac,kick.wav,snare.wav,", "Browser sort by name is case-insensitive");
        browser_search::sortEntries(list, 2);
        require(names() == "Bass.flac,kick.wav,snare.wav,", "Browser sort by type then name");
        browser_search::sortEntries(list, 3);
        require(names() == "snare.wav,kick.wav,Bass.flac,", "Browser sort newest first");
    }
    require(accepts("sytr*", "Sytrus.wav") && accepts("s*us", "Stylus.flac") && !accepts("s*us", "Snare.wav"),
            "Documented wildcard examples against names with extensions");
    require(accepts("k?ck AND NOT wet", "Kick dry.wav") && !accepts("k?ck AND NOT wet", "Kick wet.wav"),
            "Wildcard and Boolean composition");
    require(accepts("\"foo*bar\" OR kick", "foo*bar.wav") && !accepts("\"foo*bar\" OR kick", "foozzzbar.wav"),
            "Quoted stars are literal");
    const auto music = juce::String::charToString(0x1f3b5);
    require(accepts("a?b", "a" + music + "b.wav") && !accepts("a??b", "a" + music + "b.wav"),
            "Question mark consumes one Unicode codepoint");
    const auto accented = juce::String::charToString(0xc9);
    require(accepts(accented + "*", "caf" + accented.toLowerCase() + ".wav"),
            "Unicode case normalization in wildcard pattern");
    for (const auto *invalid : {"AND kick", "kick OR", "NOT", "kick AND OR snare", "()", "(kick", "kick)",
                                "kick OR ()", "type:", "folder:", "\"unfinished"})
        require(parseQuery(invalid).error.isNotEmpty(), std::string("Invalid Browser syntax: ") + invalid);
    require(parseQuery(juce::String::repeatedString("(", 34) + "kick" + juce::String::repeatedString(")", 34))
                .error.isNotEmpty(),
            "Parser recursion is bounded");
    require(parseQuery(juce::String::repeatedString("x", 1025)).error.isNotEmpty() &&
                parseQuery(juce::String::repeatedString("x ", 129)).error.isNotEmpty(),
            "Query resource bounds");
    const auto filtered = parseQuery("(kick OR snare) NOT wet type:.WAV;flac folder:\"E:\\Drum Kits\"");
    require(filtered.error.isEmpty() && filtered.requireAnyExtension && filtered.extensions.size() == 2 &&
                filtered.scopeFolder == "E:\\Drum Kits" && filtered.matches("kick dry.wav") &&
                !filtered.matches("snare wet.flac"),
            "Expression retains global type and Windows folder filters");
    require(parseQuery("folder:a folder:b kick").error.isNotEmpty(), "Ambiguous folder scopes rejected");

    // Exhaust a small independent DP oracle rather than mirroring the greedy
    // matcher. This catches star retry/backtracking boundary regressions.
    auto oracle = [](const std::u32string &pattern, const std::u32string &name)
    {
        std::vector<std::vector<bool>> table(pattern.size() + 1, std::vector<bool>(name.size() + 1));
        table[0][0] = true;
        for (std::size_t p = 1; p <= pattern.size(); ++p)
            for (std::size_t n = 0; n <= name.size(); ++n)
                table[p][n] = pattern[p - 1] == U'*'
                                  ? table[p - 1][n] || (n && table[p][n - 1])
                                  : n && table[p - 1][n - 1] &&
                                        (pattern[p - 1] == U'?' || pattern[p - 1] == name[n - 1]);
        return bool(table.back().back());
    };
    std::vector<std::u32string> patterns{U""}, names{U""};
    for (int length = 1; length <= 4; ++length)
    {
        const auto old = patterns;
        for (const auto &prefix : old)
            if (prefix.size() == std::size_t(length - 1))
                for (auto c : U"ab*?")
                    if (c)
                        patterns.push_back(prefix + c);
        const auto oldNames = names;
        for (const auto &prefix : oldNames)
            if (prefix.size() == std::size_t(length - 1))
                for (auto c : U"ab")
                    if (c)
                        names.push_back(prefix + c);
    }
    for (const auto &pattern : patterns)
        for (const auto &name : names)
            require(Query::wildcard(pattern, name) == oracle(pattern, name), "Wildcard DP oracle agreement");
}
