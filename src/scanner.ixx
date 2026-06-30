// :scanner partition — character stream → token stream.
//
// M2: recognizes block maps, block sequences, nested structures, and plain
// scalars. Each token carries its line indent so the parser can decide
// nesting. Flow, quotes, comments, anchors arrive in later milestones.
//
// Token kinds:
//   MapEntry  "key:"        — a map key, value (if any) follows on the same
//                             line as a Scalar, or on subsequent lines as a
//                             nested block.
//   Scalar    "value"       — a plain scalar value (also used for standalone
//                             scalar docs and seq element values).
//   SeqEntry   "-"           — a block sequence dash; the element value (if
//                             any) follows as a Scalar on the same line, or
//                             as a nested block on subsequent lines.

module;

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module ZYaml:scanner;

import :error;

export namespace zyaml {

enum class TokenType {
    Scalar,
    MapEntry,
    SeqEntry,
    EndOfInput,
};

struct Token {
    TokenType type = TokenType::EndOfInput;
    std::string text;     // scalar text; key text for MapEntry; empty for SeqEntry
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t offset = 0;
    std::size_t indent = 0;  // leading-space count of the line this token starts on
};

class Scanner {
public:
    explicit Scanner(std::string_view source) : source_(source) {}

    [[nodiscard]] Result<std::vector<Token>> scanAll() {
        std::vector<Token> tokens;
        // When non-null, we're emitting tokens that continue a "- " entry on
        // the same line; their logical indent is the column right after "- ".
        // This makes `  - name: floor` parse as: SeqEntry@2, MapEntry@4
        // (not both @2), so the parser sees name:floor as the seq element's
        // nested child rather than a sibling of the dash.
        std::optional<std::size_t> inlineIndent;

        while (!atEnd()) {
            skipBlankLines();
            if (atEnd()) break;
            std::size_t indent = currentLineIndent();
            if (inlineIndent) indent = *inlineIndent;
            skipSpaces();
            // Block sequence entry: "- " (or "-" at EOL).
            if (peek() == '-' && (peekAt(1) == ' ' || peekAt(1) == '\t' ||
                                  peekAt(1) == '\n' || peekAt(1) == '\r' || atEndAfter(1))) {
                advance();  // consume '-'
                // inlineIndent is the column right after '-' (before skipping
                // the following space). For "  - name", dash is at col 3, so
                // inlineIndent=4 — matching the physical indent of the next
                // line's "    name" continuation.
                inlineIndent = column_;
                skipSpaces();
                Token t{TokenType::SeqEntry, {}, line_, column_, pos_, indent};
                tokens.push_back(t);
                // Optional inline value on the same line.
                if (!atEnd() && peek() != '\n' && peek() != '\r') {
                    std::string scalar = readPlainScalar();
                    skipSpaces();
                    if (peek() == ':') {
                        advance();
                        Token me{TokenType::MapEntry, std::move(scalar), line_, column_, pos_, *inlineIndent};
                        tokens.push_back(me);
                        skipSpaces();
                        if (!atEnd() && peek() != '\n' && peek() != '\r') {
                            std::string val = readPlainScalar();
                            if (!val.empty()) {
                                Token v{TokenType::Scalar, std::move(val), line_, column_, pos_, *inlineIndent};
                                tokens.push_back(v);
                            }
                        }
                    } else if (!scalar.empty()) {
                        Token v{TokenType::Scalar, std::move(scalar), line_, column_, pos_, *inlineIndent};
                        tokens.push_back(v);
                    }
                }
                skipToEndOfLine();
                inlineIndent.reset();
                continue;
            }
            // Otherwise: a scalar (key or standalone value).
            std::string scalar = readPlainScalar();
            skipSpaces();
            if (peek() == ':') {
                advance();
                Token t{TokenType::MapEntry, std::move(scalar), line_, column_, pos_, indent};
                tokens.push_back(t);
                skipSpaces();
                if (!atEnd() && peek() != '\n' && peek() != '\r') {
                    std::string val = readPlainScalar();
                    if (!val.empty()) {
                        Token v{TokenType::Scalar, std::move(val), line_, column_, pos_, indent};
                        tokens.push_back(v);
                    }
                }
                skipToEndOfLine();
            } else {
                Token t{TokenType::Scalar, std::move(scalar), line_, column_, pos_, indent};
                tokens.push_back(t);
                skipToEndOfLine();
            }
            inlineIndent.reset();
        }
        tokens.push_back(Token{TokenType::EndOfInput, {}, line_, column_, pos_, 0});
        return tokens;
    }

private:
    std::string_view source_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    [[nodiscard]] bool atEnd() const noexcept { return pos_ >= source_.size(); }
    [[nodiscard]] char peek() const noexcept {
        return atEnd() ? '\0' : source_[pos_];
    }
    [[nodiscard]] char peekAt(std::size_t off) const noexcept {
        return (pos_ + off >= source_.size()) ? '\0' : source_[pos_ + off];
    }
    [[nodiscard]] bool atEndAfter(std::size_t off) const noexcept {
        return pos_ + off >= source_.size();
    }
    void advance() noexcept {
        if (atEnd()) return;
        if (source_[pos_] == '\n') { ++line_; column_ = 1; }
        else { ++column_; }
        ++pos_;
    }

    void skipSpaces() {
        while (!atEnd() && (peek() == ' ' || peek() == '\t')) advance();
    }

    void skipBlankLines() {
        while (!atEnd()) {
            std::size_t save = pos_;
            std::size_t saveline = line_, savecol = column_;
            while (!atEnd() && (peek() == ' ' || peek() == '\t')) advance();
            if (atEnd()) return;
            if (peek() == '\n') { advance(); continue; }
            if (peek() == '\r' && peekAt(1) == '\n') { advance(); advance(); continue; }
            if (peek() == '#') { skipToEndOfLine(); continue; }
            pos_ = save; line_ = saveline; column_ = savecol;
            return;
        }
    }

    void skipToEndOfLine() {
        while (!atEnd() && peek() != '\n') advance();
        if (!atEnd() && peek() == '\n') advance();
        else if (!atEnd() && peek() == '\r') {
            advance();
            if (!atEnd() && peek() == '\n') advance();
        }
    }

    [[nodiscard]] std::size_t currentLineIndent() const {
        std::size_t i = pos_;
        std::size_t indent = 0;
        while (i < source_.size() && source_[i] == ' ') { ++indent; ++i; }
        return indent;
    }

    [[nodiscard]] std::string readPlainScalar() {
        std::size_t start = pos_;
        while (!atEnd()) {
            const char c = peek();
            if (c == ':' || c == '#' || c == '\n' || c == '\r') break;
            advance();
        }
        std::string s(source_.substr(start, pos_ - start));
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        return s;
    }
};

} // namespace zyaml
