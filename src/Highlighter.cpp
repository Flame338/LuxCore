#include "Highlighter.h"

#include "Utf8.h"

#include <algorithm>
#include <numeric>
#include <optional>
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

using TokensAtPosition = std::unordered_map<std::size_t, std::vector<std::size_t>>;

void add_phrase_matches(const std::vector<Token>& tokens, const TokensAtPosition& at_position,
                        const PlanPhrase& phrase, std::vector<Match>& matches) {
    std::vector<char> marked(tokens.size(), 0);
    std::vector<std::size_t> occurrence;
    for (std::size_t first = 0; first < tokens.size(); first++) {
        if (tokens[first].term != phrase.terms.front().term) {
            continue;
        }
        occurrence.assign(1, first);
        for (std::size_t i = 1; i < phrase.terms.size(); i++) {
            const auto found = at_position.find(tokens[first].position + phrase.terms[i].position);
            const std::size_t before = occurrence.size();
            if (found != at_position.end()) {
                for (const std::size_t candidate : found->second) {
                    if (tokens[candidate].term == phrase.terms[i].term) {
                        occurrence.push_back(candidate);
                    }
                }
            }
            if (occurrence.size() == before) {
                occurrence.clear();
                break;
            }
        }
        for (const std::size_t index : occurrence) {
            if (!marked[index]) {
                marked[index] = 1;
                matches.push_back({tokens[index].start_offset, tokens[index].end_offset, phrase.unit});
            }
        }
    }
}

std::vector<Match> find_matches(const std::vector<Token>& tokens, const MatchPlan& plan) {
    std::vector<Match> matches;
    for (const Token& token : tokens) {
        if (const std::optional<std::size_t> unit = plan.unit_of(token.term)) {
            matches.push_back({token.start_offset, token.end_offset, *unit});
        }
    }
    if (!plan.phrases().empty()) {
        TokensAtPosition at_position;
        for (std::size_t i = 0; i < tokens.size(); i++) {
            at_position[tokens[i].position].push_back(i);
        }
        for (const PlanPhrase& phrase : plan.phrases()) {
            add_phrase_matches(tokens, at_position, phrase, matches);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        return std::pair(a.start, a.end) < std::pair(b.start, b.end);
    });
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

std::string highlight(const HighlightOptions& options, std::string_view text, const MatchPlan& plan) {
    std::vector<Match> matches = find_matches(plan.analyzer().analyze_tokens(text), plan);
    assign_code_points(text, matches);

    const std::size_t total = utf8::count_code_points(text);
    const Window window = choose_window(matches, total, options.length);
    const std::size_t window_begin = advance_code_points(text, 0, window.begin);
    const std::size_t window_end = advance_code_points(text, window_begin, window.end - window.begin);

    return render(text, merge_spans(matches, window_begin, window_end), window_begin, window_end, options);
}

}

Highlighter::Highlighter(HighlightOptions options) : options(std::move(options)) {}

std::string Highlighter::snippet(std::string_view stored_text, const MatchPlan& plan) const {
    return highlight(options, stored_text, plan);
}

std::string Highlighter::snippet(const Document& document, std::string_view field, const MatchPlan& plan) const {
    const Field* found = document.find(field);
    return found ? snippet(found->value, plan) : std::string();
}
