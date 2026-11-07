#pragma once

#include "InvertedIndex.h"
#include "Scoring.h"
#include "TermScorer.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct ScoredDoc {
    int doc_id;
    double score;
};

using ScoredDocs = std::vector<ScoredDoc>;

struct QueryContext {
    const InvertedIndex& index;
    const FieldBoosts& boosts;
    const Scoring& scoring;

    Scope scope(const std::optional<std::string>& field) const {
        return Scope{index, boosts, field ? std::optional<std::string_view>(*field) : std::nullopt};
    }
};

struct PhraseTerm {
    std::string term;
    std::size_t position;
};

struct LeafTerm {
    std::string term;
    std::optional<std::string> field;
};

struct LeafPhrase {
    std::vector<PhraseTerm> terms;
    std::optional<std::string> field;
};

struct QueryLeaves {
    std::vector<LeafTerm> terms;
    std::vector<LeafPhrase> phrases;
};

class Query {
public:
    virtual ~Query() = default;

    virtual ScoredDocs execute(const QueryContext& context) const = 0;
    virtual void add_leaves(QueryLeaves& leaves) const = 0;
};

class TermQuery : public Query {
private:
    std::string term;
    std::optional<std::string> field;

public:
    explicit TermQuery(std::string term, std::optional<std::string> field = std::nullopt)
        : term(std::move(term)), field(std::move(field)) {}

    const std::string& get_term() const { return term; }
    const std::optional<std::string>& get_field() const { return field; }

    ScoredDocs execute(const QueryContext& context) const override;
    void add_leaves(QueryLeaves& leaves) const override;
};

enum class Occur { Must, Should, MustNot };

struct BooleanClause {
    Occur occur;
    std::unique_ptr<Query> query;
};

class BooleanQuery : public Query {
private:
    std::vector<BooleanClause> clauses;

public:
    BooleanQuery& add(Occur occur, std::unique_ptr<Query> query);

    const std::vector<BooleanClause>& get_clauses() const { return clauses; }

    ScoredDocs execute(const QueryContext& context) const override;
    void add_leaves(QueryLeaves& leaves) const override;
};

class PhraseQuery : public Query {
private:
    std::vector<PhraseTerm> terms;
    std::optional<std::string> field;

public:
    explicit PhraseQuery(std::vector<PhraseTerm> terms, std::optional<std::string> field = std::nullopt);
    explicit PhraseQuery(const std::vector<std::string>& terms, std::optional<std::string> field = std::nullopt);

    const std::vector<PhraseTerm>& get_terms() const { return terms; }
    const std::optional<std::string>& get_field() const { return field; }

    ScoredDocs execute(const QueryContext& context) const override;
    void add_leaves(QueryLeaves& leaves) const override;
};
