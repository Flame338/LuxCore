#pragma once

#include <string>
#include <unordered_set>
#include <vector>

class TokenFilter {
public:
    virtual ~TokenFilter() = default;

    virtual void apply(std::vector<std::string>& tokens) const = 0;
    virtual bool applies_to_partial_words() const { return true; }
};

class LowercaseFilter : public TokenFilter {
public:
    void apply(std::vector<std::string>& tokens) const override;
};

class UnicodeFoldingFilter : public TokenFilter {
public:
    void apply(std::vector<std::string>& tokens) const override;
};

class NormalizeApostropheFilter : public TokenFilter {
public:
    void apply(std::vector<std::string>& tokens) const override;
};

class EnglishPossessiveFilter : public TokenFilter {
public:
    void apply(std::vector<std::string>& tokens) const override;
};

class StopWordFilter : public TokenFilter {
private:
    std::unordered_set<std::string> stop_words;

public:
    explicit StopWordFilter(const std::string& stop_words_path = "");

    void apply(std::vector<std::string>& tokens) const override;
    bool applies_to_partial_words() const override { return false; }
};
