#pragma once

#include <map>
#include <string>
#include <string_view>

struct SearchHit {
    int doc_id;
    double score;
    std::string key = {};
};

class FieldBoosts {
    private:
        std::map<std::string, double, std::less<>> boosts;
        double fallback = 1.0;

    public:
        static FieldBoosts standard() {
            FieldBoosts result;
            result.set("title", 2.0);
            return result;
        }

        FieldBoosts& set(const std::string& field, double boost) {
            boosts[field] = boost;
            return *this;
        }

        double get(std::string_view field) const {
            auto it = boosts.find(field);
            return it == boosts.end() ? fallback : it->second;
        }
};

enum class Scorer { TfIdf, Bm25 };

struct Bm25Params {
    double k1 = 1.2;
    double b = 0.75;
};

struct Scoring {
    Scorer scorer = Scorer::TfIdf;
    Bm25Params bm25 = {};

    static Scoring tf_idf() { return {}; }
    static Scoring bm25_with(double k1 = 1.2, double b = 0.75) { return {Scorer::Bm25, {k1, b}}; }
};
