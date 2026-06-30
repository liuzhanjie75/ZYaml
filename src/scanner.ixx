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
    FlowCollection,  // a complete [..] or {..} group, raw text in `text`
    EndOfInput,
};

// Style hint for a Scalar / FlowCollection token.
enum class TokenStyle {
    Plain,
    FlowSeq,   // text is "[a, b, c]" (brackets included)
    FlowMap,   // text is "{k: v, ...}" (braces included)
};

struct Token {
    TokenType type = TokenType::EndOfInput;
    std::string text;     // scalar text; key text for MapEntry; empty for SeqEntry;
                          // raw "[...]" / "{...}" for FlowCollection
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t offset = 0;
    std::size_t indent = 0;
    TokenStyle style = TokenStyle::Plain;  // trailing so aggregate init w/o it defaults to Plain
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
                    std::string scalar = readScalarValue();
                    skipSpaces();
                    if (peek() == ':') {
                        advance();
                        Token me{TokenType::MapEntry, std::move(scalar), line_, column_, pos_, *inlineIndent};
                        tokens.push_back(me);
                        skipSpaces();
                        if (!atEnd() && peek() != '\n' && peek() != '\r') {
                            std::string val = readScalarValue();
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
            // Otherwise: a scalar, a flow collection, or a standalone value.
            if (peek() == '[' || peek() == '{') {
                const char open = peek();
                TokenStyle style = (open == '[') ? TokenStyle::FlowSeq : TokenStyle::FlowMap;
                std::string raw = readFlowCollection();
                Token t{.type = TokenType::FlowCollection, .text = std::move(raw),
                        .line = line_, .column = column_, .offset = pos_, .indent = indent,
                        .style = style};
                tokens.push_back(t);
                skipToEndOfLine();
                inlineIndent.reset();
                continue;
            }
            std::string scalar = readScalarValue();
            skipSpaces();
            if (peek() == ':') {
                advance();
                Token t{TokenType::MapEntry, std::move(scalar), line_, column_, pos_, indent};
                tokens.push_back(t);
                skipSpaces();
                if (!atEnd() && peek() != '\n' && peek() != '\r') {
                    if (peek() == '[' || peek() == '{') {
                        const char open = peek();
                        TokenStyle vstyle = (open == '[') ? TokenStyle::FlowSeq : TokenStyle::FlowMap;
                        std::string raw = readFlowCollection();
                        Token v{.type = TokenType::FlowCollection, .text = std::move(raw),
                                .line = line_, .column = column_, .offset = pos_, .indent = indent,
                                .style = vstyle};
                        tokens.push_back(v);
                    } else {
                        std::string val = readScalarValue();
                        if (!val.empty()) {
                            Token v{TokenType::Scalar, std::move(val), line_, column_, pos_, indent};
                            tokens.push_back(v);
                        }
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

    // Read a quoted scalar starting at the current '"' or '\''. Returns the
    // decoded content (escapes processed). Advances past the closing quote.
    // Double-quoted honors \n \t \\ \" \' \0. Single-quoted uses '' for '.
    [[nodiscard]] std::string readQuoted() {
        const char quote = peek();
        advance();  // consume opening quote
        std::string out;
        while (!atEnd()) {
            const char c = peek();
            if (quote == '"') {
                if (c == '"') { advance(); break; }
                if (c == '\\' && !atEndAfter(1)) {
                    advance();
                    const char e = peek();
                    switch (e) {
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'r': out += '\r'; break;
                        case '\\': out += '\\'; break;
                        case '"': out += '"'; break;
                        case '\'': out += '\''; break;
                        case '0': out += '\0'; break;
                        default: out += e; break;  // unknown escape: literal
                    }
                    advance();
                    continue;
                }
                out += c;
                advance();
            } else {
                // single-quoted: '' is a literal '
                if (c == '\'') {
                    if (peekAt(1) == '\'') { out += '\''; advance(); advance(); continue; }
                    advance();  // consume closing '
                    break;
                }
                out += c;
                advance();
            }
        }
        return out;
    }

    // Read a scalar value: if it starts with a quote, readQuoted; else plain.
    [[nodiscard]] std::string readScalarValue() {
        if (peek() == '"' || peek() == '\'') {
            return readQuoted();
        }
        return readPlainScalar();
    }

    // Read a complete flow collection starting at the current '[' or '{',
    // including nested collections and quoted strings. Returns the raw text
    // with the outer brackets/braces included. The parser interprets it.
    [[nodiscard]] std::string readFlowCollection() {
        std::size_t start = pos_;
        const char open = peek();
        char close = (open == '[') ? ']' : '}';
        int depth = 0;
        char quote = 0;
        while (!atEnd()) {
            const char c = peek();
            if (quote) {
                if (c == quote) quote = 0;
                else if (c == '\\' && quote == '"' && !atEndAfter(1)) advance();
                advance();
                continue;
            }
            if (c == '"' || c == '\'') { quote = c; advance(); continue; }
            if (c == '[' || c == '{') { ++depth; advance(); continue; }
            if (c == ']' || c == '}') {
                if (depth == 0 && c == close) { advance(); break; }
                if (depth > 0) { --depth; advance(); continue; }
                // Mismatched close — stop here; parser will report.
                break;
            }
            if (c == '\n' || c == '\r') {
                // Flow can span lines, but M3 keeps it single-line for
                // simplicity. Stop at newline; the parser treats truncated
                // flow as a parse error.
                break;
            }
            advance();
        }
        return std::string(source_.substr(start, pos_ - start));
    }
};

} // namespace zyaml
