// :scanner partition — character stream → token stream.
//
// M1: minimal scanner. Recognizes:
//   - Block map entries:  "<key>: <value>\n"
//   - Plain scalars (no quotes, no flow, no multi-line block scalars)
//   - Document end (EOF)
// Comments, flow, quotes, block scalars arrive in later milestones.
//
// Indentation is tracked via a simple "current line indent" model: block
// map entries must be at the same column. The scanner emits tokens with
// line/column/offset; the parser decides structure.

module;

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

export module ZYaml:scanner;

import :error;

export namespace zyaml {

enum class TokenType {
    Scalar,        // a plain scalar value (key or value position)
    MapEntry,      // "key:" — the parser pairs this with the following Scalar
    Newline,
    EndOfInput,
};

struct Token {
    TokenType type = TokenType::EndOfInput;
    std::string text;     // scalar text (for Scalar); key text (for MapEntry)
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t offset = 0;
    std::size_t indent = 0;  // leading-space count of the line this token starts on
};

class Scanner {
public:
    explicit Scanner(std::string_view source) : source_(source) {}

    // Scan the entire source into a token vector. Returns an error on
    // malformed input (M1: nothing is malformed yet — plain scalars only).
    [[nodiscard]] Result<std::vector<Token>> scanAll() {
        std::vector<Token> tokens;
        while (!atEnd()) {
            skipBlankLines();
            if (atEnd()) break;
            const std::size_t indent = currentLineIndent();
            skipSpaces();
            // Read a plain scalar (the key or a standalone scalar value).
            std::string scalar = readPlainScalar();
            skipSpaces();
            if (peek() == ':') {
                advance();  // consume ':'
                Token t{TokenType::MapEntry, std::move(scalar), line_, column_, pos_, indent};
                tokens.push_back(t);
                skipSpaces();
                // Optional value on the same line.
                if (!atEnd() && peek() != '\n' && peek() != '\r') {
                    std::string val = readPlainScalar();
                    Token v{TokenType::Scalar, std::move(val), line_, column_, pos_, indent};
                    tokens.push_back(v);
                }
                skipToEndOfLine();
            } else {
                Token t{TokenType::Scalar, std::move(scalar), line_, column_, pos_, indent};
                tokens.push_back(t);
                skipToEndOfLine();
            }
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
            // Save pos to peek the line's first non-space char.
            std::size_t save = pos_;
            std::size_t saveline = line_, savecol = column_;
            while (!atEnd() && (peek() == ' ' || peek() == '\t')) advance();
            if (atEnd()) return;
            if (peek() == '\n') { advance(); continue; }
            if (peek() == '\r' && peekAt(1) == '\n') { advance(); advance(); continue; }
            if (peek() == '#') { skipToEndOfLine(); continue; }  // comment line
            // Non-blank, non-comment content — restore and stop.
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
        // Count leading spaces on the current line (ignoring tabs — YAML
        // indent is spaces-only; tabs would be a parse error in strict mode,
        // but we tolerate by not counting them as indent).
        std::size_t i = pos_;
        std::size_t indent = 0;
        while (i < source_.size() && source_[i] == ' ') { ++indent; ++i; }
        return indent;
    }

    // Read a plain scalar: characters until ':', '#', newline, or EOF.
    // Trailing spaces are trimmed.
    [[nodiscard]] std::string readPlainScalar() {
        std::size_t start = pos_;
        while (!atEnd()) {
            const char c = peek();
            if (c == ':' || c == '#' || c == '\n' || c == '\r') break;
            advance();
        }
        std::string s(source_.substr(start, pos_ - start));
        // Trim trailing spaces/tabs.
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        return s;
    }
};

} // namespace zyaml
