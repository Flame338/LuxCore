#include "Highlighter.h"

#include "Utf8.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Match {
    std::size_t start;
    std::size_t end;
    std::size_t term_index;
    std::size_t cp_start = 0;
    std::size_t cp_end = 0;
};

struct Span {
    std::size_t start;
    std::size_t end;
};

struct Window {
    std::size_t begin;
    std::size_t end;
};

struct QueryTerms {
    std::unordered_map<std::string, std::size_t> index_of;
    std::string prefix;
    std::size_t prefix_index = 0;
    bool has_prefix = false;
};

QueryTerms collect_query_terms(const AnalyzedTokenQuery& query) {
    QueryTerms terms;
    for (const Token& token : query.tokens) {
        terms.index_of.try_emplace(token.term, terms.index_of.size());
    }
    if (query.prefix) {
        terms.has_prefix = true;
        terms.prefix = query.prefix->term;
        terms.prefix_index = terms.index_of.try_emplace(terms.prefix, terms.index_of.size()).first->second;
    }
    return terms;
}

void assign_code_points(std::string_view text, std::vector<Match>& matches) {
    std::size_t byte = 0;
    std::size_t code_point = 0;
    for (Match& match : matches) {
        while (byte < match.start) {
            byte += utf8::decode(text, byte).length;
            code_point++;
        }
        match.cp_start = code_point;
        match.cp_end = code_point + utf8::count_code_points(text.substr(match.start, match.end - match.start));
    }
}

std::vector<Match> find_matches(const Analyzer& analyzer, const std::string& text,
                                const std::string& query_string) {
    const QueryTerms terms = collect_query_terms(analyzer.analyze_query_tokens(query_string));

    std::vector<Match> matches;
    for (const Token& token : analyzer.analyze_tokens(text)) {
        const auto exact = terms.index_of.find(token.term);
        if (exact != terms.index_of.end()) {
            matches.push_back({token.start_offset, token.end_offset, exact->second});
        } else if (terms.has_prefix && token.term.starts_with(terms.prefix)) {
            matches.push_back({token.start_offset, token.end_offset, terms.prefix_index});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        return std::pair(a.start, a.end) < std::pair(b.start, b.end);
    });
    assign_code_points(text, matches);
    return matches;
}

Window choose_window(const std::vector<Match>& matches, std::size_t total_code_points,
                     std::size_t length) {
    if (total_code_points <= length) {
        return {0, total_code_points};
    }

    std::size_t term_count = 0;
    for (const Match& match : matches) {
        term_count = std::max(term_count, match.term_index + 1);
    }

    const std::size_t last_begin = total_code_points - length;
    const std::size_t lead = length / 4;

    std::vector<std::size_t> by_end(matches.size());
    std::iota(by_end.begin(), by_end.end(), std::size_t{0});
    std::sort(by_end.begin(), by_end.end(), [&matches](std::size_t a, std::size_t b) {
        return matches[a].cp_end < matches[b].cp_end;
    });

    std::vector<std::size_t> counts(term_count, 0);
    std::vector<char> inside(matches.size(), 0);
    std::size_t distinct = 0;
    std::size_t inside_total = 0;
    std::size_t next_to_add = 0;
    std::size_t next_to_drop = 0;

    Window best{0, length};
    std::pair<std::size_t, std::size_t> best_score{0, 0};

    for (std::size_t i = 0; i < matches.size(); i++) {
        const std::size_t cp = matches[i].cp_start;
        const std::size_t begin = std::min(cp >= lead ? cp - lead : 0, last_begin);
        const std::size_t end = begin + length;

        while (next_to_add < by_end.size() && matches[by_end[next_to_add]].cp_end <= end) {
            const std::size_t j = by_end[next_to_add++];
            if (matches[j].cp_start >= begin) {
                inside[j] = 1;
                inside_total++;
                if (counts[matches[j].term_index]++ == 0) {
                    distinct++;
                }
            }
        }
        while (next_to_drop < matches.size() && matches[next_to_drop].cp_start < begin) {
            if (inside[next_to_drop]) {
                inside[next_to_drop] = 0;
                inside_total--;
                if (--counts[matches[next_to_drop].term_index] == 0) {
                    distinct--;
                }
            }
            next_to_drop++;
        }

        const std::pair<std::size_t, std::size_t> score{distinct, inside_total};
        if (i == 0 || score > best_score) {
            best = {begin, end};
            best_score = score;
        }
    }
    return best;
}

std::size_t advance_code_points(std::string_view text, std::size_t byte, std::size_t count) {
    while (count > 0 && byte < text.size()) {
        byte += utf8::decode(text, byte).length;
        count--;
    }
    return byte;
}

std::vector<Span> merge_spans(const std::vector<Match>& matches, std::size_t window_begin,
                              std::size_t window_end) {
    std::vector<Span> spans;
    for (const Match& match : matches) {
        if (match.start >= window_end) {
            break;
        }
        const std::size_t start = std::max(match.start, window_begin);
        const std::size_t end = std::min(match.end, window_end);
        if (start >= end) {
            continue;
        }
        if (!spans.empty() && start <= spans.back().end) {
            spans.back().end = std::max(spans.back().end, end);
        } else {
            spans.push_back({start, end});
        }
    }
    return spans;
}

std::string render(std::string_view text, const std::vector<Span>& spans, std::size_t window_begin,
                   std::size_t window_end, const HighlightOptions& options) {
    std::string out;
    if (window_begin > 0) {
        out += options.ellipsis;
    }
    std::size_t cursor = window_begin;
    for (const Span& span : spans) {
        out += text.substr(cursor, span.start - cursor);
        out += options.start_marker;
        out += text.substr(span.start, span.end - span.start);
        out += options.end_marker;
        cursor = span.end;
    }
    out += text.substr(cursor, window_end - cursor);
    if (window_end < text.size()) {
        out += options.ellipsis;
    }
    return out;
}

}

Highlighter::Highlighter(const Analyzer& analyzer, HighlightOptions options)
    : analyzer(analyzer), options(std::move(options)) {}

std::string Highlighter::snippet(const std::string& stored_text, const std::string& query_string) const {
    const std::vector<Match> matches = find_matches(analyzer, stored_text, query_string);

    const std::size_t total = utf8::count_code_points(stored_text);
    const Window window = choose_window(matches, total, options.length);
    const std::size_t window_begin = advance_code_points(stored_text, 0, window.begin);
    const std::size_t window_end = advance_code_points(stored_text, window_begin, window.end - window.begin);

    return render(stored_text, merge_spans(matches, window_begin, window_end), window_begin, window_end,
                  options);
}

std::string Highlighter::snippet(const Document& document, std::string_view field,
                                 const std::string& query_string) const {
    const Field* found = document.find(field);
    return found ? snippet(found->value, query_string) : std::string();
}
