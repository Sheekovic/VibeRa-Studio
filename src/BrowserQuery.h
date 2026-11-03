// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BrowserTags.h"
#include <juce_core/juce_core.h>
#include <array>
#include <string>
#include <vector>
#include <algorithm>

namespace vibera::browser_search
{
// Original bounded query compiler. This defines VibeRa semantics; the FL
// manual establishes feature expectations, not its undocumented parser rules.
struct Query
{
    struct Clause
    {
        enum Kind
        {
            term,
            conjunction,
            disjunction,
            negation
        } kind = term;
        std::string text;
        std::u32string glob;
    };
    std::string text, scopeFolder;
    std::vector<std::string> extensions;
    std::vector<Clause> program;
    std::vector<std::string> favoritePaths;
    TagFilter tagFilter;
    std::shared_ptr<const TagCatalog> tagCatalog;
    juce::String error;
    bool requireAnyExtension = false;
    bool favoritesOnly = false;
    bool empty() const
    {
        return text.empty() && scopeFolder.empty() && extensions.empty() && tagFilter.empty();
    }
    static std::u32string codepoints(const juce::String &value)
    {
        std::u32string out;
        for (auto c : value)
            out.push_back(char32_t(c));
        return out;
    }
    static bool wildcard(const std::u32string &pattern, const std::u32string &name)
    {
        // Iterative glob with a single backtracking star: bounded O(n*m), no
        // regex recursion/exponential expression expansion. '?' is one Unicode
        // codepoint, not one UTF-8 byte. Stars surrounding a term mean contains.
        std::size_t p = 0, n = 0, star = std::u32string::npos, retry = 0;
        while (n < name.size())
        {
            if (p < pattern.size() && (pattern[p] == U'?' || pattern[p] == name[n]))
            {
                ++p;
                ++n;
            }
            else if (p < pattern.size() && pattern[p] == U'*')
            {
                star = p++;
                retry = n;
            }
            else if (star != std::u32string::npos)
            {
                p = star + 1;
                n = ++retry;
            }
            else
                return false;
        }
        while (p < pattern.size() && pattern[p] == U'*')
            ++p;
        return p == pattern.size();
    }
    bool matches(const std::string &lowerName) const
    {
        if (error.isNotEmpty())
            return false;
        if (program.empty())
            return text.empty() || lowerName.find(text) != std::string::npos;
        std::array<bool, 128> stack{};
        std::size_t count = 0;
        std::u32string unicodeName;
        bool converted = false;
        for (const auto &clause : program)
        {
            if (clause.kind == Clause::term)
            {
                if (count == stack.size())
                    return false;
                if (!clause.glob.empty() && !converted)
                {
                    unicodeName = codepoints(juce::String::fromUTF8(lowerName.c_str()));
                    converted = true;
                }
                stack[count++] = clause.glob.empty() ? lowerName.find(clause.text) != std::string::npos
                                                     : wildcard(clause.glob, unicodeName);
            }
            else if (clause.kind == Clause::negation)
            {
                if (!count)
                    return false;
                stack[count - 1] = !stack[count - 1];
            }
            else
            {
                if (count < 2)
                    return false;
                const bool right = stack[--count];
                stack[count - 1] = clause.kind == Clause::conjunction ? stack[count - 1] && right
                                                                      : stack[count - 1] || right;
            }
        }
        return count == 1 && stack[0];
    }
};

inline Query parseQuery(const juce::String &raw)
{
    Query result;
    if (raw.length() > 1024)
    {
        result.error = "Search is limited to 1024 characters";
        return result;
    }
    struct Token
    {
        juce::String value;
        bool quoted = false, literal = false;
        bool is(const char *word) const
        {
            return !quoted && value == word;
        }
    };
    std::vector<Token> tokens;
    Token token;
    bool quoted = false;
    auto consume = [&]
    {
        if (token.value.isNotEmpty())
            tokens.push_back(std::move(token));
        token = {};
    };
    for (auto c : raw)
    {
        if (c == '"')
        {
            if (token.value.isEmpty() && !quoted)
                token.literal = true;
            token.quoted = true;
            quoted = !quoted;
        }
        else if (!quoted && juce::CharacterFunctions::isWhitespace(c))
            consume();
        else if (!quoted && (c == '(' || c == ')'))
        {
            consume();
            tokens.push_back({juce::String::charToString(c), false, false});
        }
        else
            token.value += juce::String::charToString(c);
    }
    consume();
    if (quoted)
        result.error = "Close the quoted folder or search text";
    if (tokens.size() > 128)
        result.error = "Search is limited to 128 terms and operators";
    if (result.error.isNotEmpty())
        return result;
    std::vector<Token> expression;
    bool advanced = false;
    for (auto &item : tokens)
    {
        const auto &value = item.value;
        if (!item.literal && value.startsWithIgnoreCase("type:"))
        {
            const auto before = result.extensions.size();
            const auto types = juce::StringArray::fromTokens(value.substring(5), ";,", {});
            for (auto extension : types)
            {
                extension = extension.trim().trimCharactersAtStart(".").toLowerCase();
                if (extension.isNotEmpty())
                    result.extensions.push_back(extension.toStdString());
            }
            if (result.extensions.size() == before)
                result.error = "Add a file extension after type:";
            result.requireAnyExtension = !result.extensions.empty();
        }
        else if (!item.literal && value.startsWithIgnoreCase("folder:"))
        {
            const auto folder = value.substring(7).trim().toStdString();
            if (folder.empty() || (!result.scopeFolder.empty() && folder != result.scopeFolder))
                result.error = "Use one nonempty folder: scope";
            result.scopeFolder = folder;
        }
        else
        {
            advanced = advanced || item.is("AND") || item.is("OR") || item.is("NOT") || item.is("(") ||
                       item.is(")") || (!item.quoted && value.containsAnyOf("*?"));
            expression.push_back(std::move(item));
        }
    }
    if (result.error.isNotEmpty())
        return result;
    // Preserve the existing plain-text phrase default. Special syntax opts in
    // to expressions: NOT > AND (including adjacent terms) > OR.
    for (const auto &item : expression)
        result.text += (result.text.empty() ? "" : " ") + item.value.toLowerCase().toStdString();
    if (!advanced || expression.empty())
        return result;
    struct Parser
    {
        Query &query;
        const std::vector<Token> &tokens;
        std::size_t at = 0;
        void emit(Query::Clause::Kind kind)
        {
            query.program.push_back({kind, {}, {}});
        }
        bool is(const char *word) const
        {
            return at < tokens.size() && tokens[at].is(word);
        }
        bool unary(int depth)
        {
            if (depth > 32 || at == tokens.size())
                return false;
            if (is("NOT"))
            {
                ++at;
                if (!unary(depth + 1))
                    return false;
                emit(Query::Clause::negation);
                return true;
            }
            if (is("("))
            {
                ++at;
                if (!either(depth + 1) || !is(")"))
                    return false;
                ++at;
                return true;
            }
            if (is(")") || is("AND") || is("OR"))
                return false;
            const auto &token = tokens[at++];
            Query::Clause clause;
            clause.text = token.value.toLowerCase().toStdString();
            if (!token.quoted && token.value.containsAnyOf("*?"))
                clause.glob = U"*" + Query::codepoints(token.value.toLowerCase()) + U"*";
            query.program.push_back(std::move(clause));
            return true;
        }
        bool both(int depth)
        {
            if (!unary(depth))
                return false;
            while (at < tokens.size() && !is("OR") && !is(")"))
            {
                if (is("AND"))
                    ++at;
                if (!unary(depth))
                    return false;
                emit(Query::Clause::conjunction);
            }
            return true;
        }
        bool either(int depth)
        {
            if (!both(depth))
                return false;
            while (is("OR"))
            {
                ++at;
                if (!both(depth))
                    return false;
                emit(Query::Clause::disjunction);
            }
            return true;
        }
    } parser{result, expression};
    if (!parser.either(0) || parser.at != expression.size())
    {
        result.error = "Check AND / OR / NOT operands and parentheses; quote literal words";
        result.program.clear();
    }
    return result;
}
} // namespace vibera::browser_search
