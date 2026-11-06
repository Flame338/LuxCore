#include "QueryParser.h"

#include <cctype>
#include <utility>
#include <vector>

namespace {

constexpr int max_depth = 64;

enum class Lexeme { End, LeftParen, RightParen, Plus, Minus, And, Or, Not, Field, Phrase, Word };

struct Lexed {
    Lexeme kind;
    std::string text;
    std::size_t offset;
    std::size_t end;
};

struct ParseFailure {
    ParseError error;
};

bool is_space(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool is_field_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

bool ends_word(char c) {
    return is_space(c) || c == '(' || c == ')' || c == '"';
}

std::vector<Lexed> lex(const std::string& text) {
    std::vector<Lexed> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (is_space(c)) {
            i++;
        } else if (c == '(' || c == ')' || c == '+' || c == '-') {
            const Lexeme kind = c == '(' ? Lexeme::LeftParen
                                : c == ')' ? Lexeme::RightParen
                                : c == '+' ? Lexeme::Plus
                                           : Lexeme::Minus;
            tokens.push_back({kind, std::string(1, c), i, i + 1});
            i++;
        } else if (c == '"') {
            const std::size_t close = text.find('"', i + 1);
            if (close == std::string::npos) {
                throw ParseFailure{{"unterminated quote", i}};
            }
            tokens.push_back({Lexeme::Phrase, text.substr(i + 1, close - i - 1), i, close + 1});
            i = close + 1;
        } else {
            std::size_t j = i;
            bool field_like = true;
            while (j < text.size() && !ends_word(text[j])) {
                if (text[j] == ':' && field_like && j > i) {
                    break;
                }
                field_like = field_like && is_field_char(text[j]);
                j++;
            }
            if (j < text.size() && text[j] == ':' && field_like && j > i) {
                tokens.push_back({Lexeme::Field, text.substr(i, j - i), i, j + 1});
                i = j + 1;
                continue;
            }
            std::string word = text.substr(i, j - i);
            Lexeme kind = Lexeme::Word;
            if (word == "AND") {
                kind = Lexeme::And;
            } else if (word == "OR") {
                kind = Lexeme::Or;
            } else if (word == "NOT") {
                kind = Lexeme::Not;
            }
            tokens.push_back({kind, std::move(word), i, j});
            i = j;
        }
    }
    tokens.push_back({Lexeme::End, "", text.size(), text.size()});
    return tokens;
}

class Parser {
private:
    const Analyzer& analyzer;
    std::vector<Lexed> tokens;
    std::size_t cursor = 0;
    int depth = 0;

    const Lexed& peek() const { return tokens[cursor]; }
    const Lexed& next() { return tokens[cursor++]; }

    [[noreturn]] void fail(const std::string& message, std::size_t offset) const {
        throw ParseFailure{{message, offset}};
    }

    bool starts_operand() const {
        switch (peek().kind) {
            case Lexeme::Plus:
            case Lexeme::Minus:
            case Lexeme::Not:
            case Lexeme::Field:
            case Lexeme::Phrase:
            case Lexeme::Word:
            case Lexeme::LeftParen:
                return true;
            default:
                return false;
        }
    }

    std::unique_ptr<Query> analyzed(const std::string& text, const std::optional<std::string>& field) const {
        const std::vector<Token> found = analyzer.analyze_tokens(text);
        if (found.empty()) {
            return nullptr;
        }
        if (found.size() == 1) {
            return std::make_unique<TermQuery>(found.front().term, field);
        }
        std::vector<PhraseTerm> terms;
        for (const Token& token : found) {
            terms.push_back({token.term, token.position});
        }
        return std::make_unique<PhraseQuery>(std::move(terms), field);
    }

    static std::unique_ptr<Query> group(std::vector<BooleanClause> clauses) {
        std::erase_if(clauses, [](const BooleanClause& clause) { return clause.query == nullptr; });
        if (clauses.empty()) {
            return nullptr;
        }
        if (clauses.size() == 1 && clauses.front().occur != Occur::MustNot) {
            return std::move(clauses.front().query);
        }
        auto boolean = std::make_unique<BooleanQuery>();
        for (BooleanClause& clause : clauses) {
            boolean->add(clause.occur, std::move(clause.query));
        }
        return boolean;
    }

    std::unique_ptr<Query> parse_primary(const std::optional<std::string>& field) {
        const Lexed token = next();
        switch (token.kind) {
            case Lexeme::Field: {
                if (peek().offset != token.end || peek().kind == Lexeme::End) {
                    fail("expected a term after '" + token.text + ":'", token.end);
                }
                const Lexeme following = peek().kind;
                if (following != Lexeme::Word && following != Lexeme::Phrase && following != Lexeme::LeftParen &&
                    following != Lexeme::Field) {
                    fail("expected a term after '" + token.text + ":'", token.end);
                }
                return parse_primary(token.text);
            }
            case Lexeme::LeftParen: {
                if (++depth > max_depth) {
                    fail("query is nested too deeply", token.offset);
                }
                if (peek().kind == Lexeme::RightParen) {
                    fail("empty parentheses", token.offset);
                }
                std::unique_ptr<Query> inner = parse_or(field);
                if (peek().kind != Lexeme::RightParen) {
                    fail("missing closing parenthesis for the one opened here", token.offset);
                }
                next();
                depth--;
                return inner;
            }
            case Lexeme::Phrase:
            case Lexeme::Word:
                return analyzed(token.text, field);
            default:
                fail("expected a term", token.offset);
        }
    }

    BooleanClause parse_unary(const std::optional<std::string>& field) {
        Occur occur = Occur::Should;
        if (peek().kind == Lexeme::Plus) {
            occur = Occur::Must;
            next();
        } else if (peek().kind == Lexeme::Minus || peek().kind == Lexeme::Not) {
            occur = Occur::MustNot;
            next();
        }
        return {occur, parse_primary(field)};
    }

    BooleanClause parse_and(const std::optional<std::string>& field) {
        std::vector<BooleanClause> operands;
        operands.push_back(parse_unary(field));
        while (peek().kind == Lexeme::And) {
            next();
            operands.push_back(parse_unary(field));
        }
        if (operands.size() == 1) {
            return std::move(operands.front());
        }
        for (BooleanClause& operand : operands) {
            if (operand.occur == Occur::Should) {
                operand.occur = Occur::Must;
            }
        }
        std::erase_if(operands, [](const BooleanClause& operand) { return operand.query == nullptr; });
        if (operands.size() == 1) {
            return std::move(operands.front());
        }
        return {Occur::Should, group(std::move(operands))};
    }

    std::unique_ptr<Query> parse_or(const std::optional<std::string>& field) {
        std::vector<BooleanClause> clauses;
        clauses.push_back(parse_and(field));
        while (true) {
            if (peek().kind == Lexeme::Or) {
                next();
            } else if (!starts_operand()) {
                break;
            }
            clauses.push_back(parse_and(field));
        }
        return group(std::move(clauses));
    }

public:
    Parser(const Analyzer& analyzer, std::vector<Lexed> tokens) : analyzer(analyzer), tokens(std::move(tokens)) {}

    std::unique_ptr<Query> parse() {
        if (peek().kind == Lexeme::End) {
            return nullptr;
        }
        std::unique_ptr<Query> query = parse_or(std::nullopt);
        if (peek().kind == Lexeme::RightParen) {
            fail("unmatched closing parenthesis", peek().offset);
        }
        return query;
    }
};

}

ParseResult QueryParser::parse(const std::string& text) const {
    try {
        Parser parser(analyzer, lex(text));
        std::unique_ptr<Query> query = parser.parse();
        if (!query) {
            query = std::make_unique<BooleanQuery>();
        }
        return {std::move(query), std::nullopt};
    } catch (const ParseFailure& failure) {
        return {nullptr, failure.error};
    }
}
