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
#include <utility>
#include <vector>

export module ZYaml:scanner;

import :error;

export namespace zyaml {

enum class TokenType {
    Scalar,
    MapEntry,
    SeqEntry,
    FlowCollection,
    Comment,
    Anchor,
    Alias,
    Tag,
    DocStart,  // --- document separator
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
                          // raw "[...]" / "{...}" for FlowCollection;
                          // "# ..." (with leading #) for Comment
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t offset = 0;
    std::size_t indent = 0;
    TokenStyle style = TokenStyle::Plain;
    bool isInline = false;  // Comment: true if trailing on a value's own line
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

        // ZE_READ: read a quoted/plain scalar or flow collection, propagating
        // any UnclosedQuote/BadEscape/UnclosedFlow error up through scanAll's
        // Result<vector<Token>>. Each use site gets a uniquely-named result
        // variable to avoid shadowing warnings under /W4 /WX.
#define ZE_READ(name, expr) \
    auto _r_##name = (expr); \
    if (!_r_##name) return _r_##name.error(); \
    std::string name = std::move(*_r_##name)

// ZE_EMIT_ANCHOR_ALIAS: at a value position, if the next char is '&', '*',
// or '!', emit an Anchor/Alias/Tag token and skip spaces. Returns true if
// the line ended (no inline value follows — caller should skip to next
// iteration). The caller checks the return and continues the loop.
#define ZE_EMIT_ANCHOR_ALIAS(tokIndent, _didContinue) \
    _didContinue = false; \
    if (peek() == '&' || peek() == '*') { \
        auto _aa = readAnchorOrAlias(); \
        Token _at{(_aa.first ? TokenType::Alias : TokenType::Anchor), \
                  std::move(_aa.second), line_, column_, pos_, (tokIndent)}; \
        tokens.push_back(_at); \
        skipSpaces(); \
        if (atEnd() || peek() == '\n' || peek() == '\r') { \
            emitTrailingComment(tokens, (tokIndent)); \
            inlineIndent.reset(); \
            _didContinue = true; \
        } \
    } else if (peek() == '!') { \
        std::string _tg = readTag(); \
        Token _tt{TokenType::Tag, std::move(_tg), line_, column_, pos_, (tokIndent)}; \
        tokens.push_back(_tt); \
        skipSpaces(); \
        if (atEnd() || peek() == '\n' || peek() == '\r') { \
            emitTrailingComment(tokens, (tokIndent)); \
            inlineIndent.reset(); \
            _didContinue = true; \
        } \
    }

        while (!atEnd()) {
            skipBlankLines();
            if (atEnd()) break;
            std::size_t indent = currentLineIndent();
            if (inlineIndent) indent = *inlineIndent;
            skipSpaces();
            // Block comment line (a '#' at the start of the content column).
            // Emitted as a Comment token; the parser binds it to the next
            // node's `pre` list.
            if (peek() == '#') {
                std::string c = readComment();
                Token t{TokenType::Comment, std::move(c), line_, column_, pos_, indent};
                t.isInline = false;
                tokens.push_back(t);
                skipToEndOfLine();
                inlineIndent.reset();
                continue;
            }
            // Document start marker ---.
            if (peek() == '-' && peekAt(1) == '-' && peekAt(2) == '-') {
                advance(); advance(); advance();
                skipToEndOfLine();
                Token t{TokenType::DocStart, {}, line_, column_, pos_, indent};
                tokens.push_back(t);
                inlineIndent.reset();
                continue;
            }
            // Block sequence entry: "- " (or "-" at EOL).
            if (peek() == '-' && (peekAt(1) == ' ' || peekAt(1) == '\t' ||
                                  peekAt(1) == '\n' || peekAt(1) == '\r' || atEndAfter(1))) {
                advance();  // consume '-'
                inlineIndent = column_;
                skipSpaces();
                Token t{TokenType::SeqEntry, {}, line_, column_, pos_, indent};
                tokens.push_back(t);
                // Optional inline value on the same line.
                if (!atEnd() && peek() != '\n' && peek() != '\r') {
                    bool _dc; ZE_EMIT_ANCHOR_ALIAS(*inlineIndent, _dc);
                    if (_dc) continue;
                    if (peek() == '|' || peek() == '>') {
                        ZE_READ(bs, readBlockScalar(indent));
                        Token v{TokenType::Scalar, std::move(bs), line_, column_, pos_, indent};
                        tokens.push_back(v);
                        inlineIndent.reset();
                        continue;
                    }
                    // Nested seq: "- - a" — the second '-' is another SeqEntry
                    // at the inlineIndent. Emit it; the parser handles it as
                    // a nested block.
                    if (peek() == '-' && (peekAt(1) == ' ' || peekAt(1) == '\t' ||
                                          peekAt(1) == '\n' || peekAt(1) == '\r' || atEndAfter(1))) {
                        // Don't consume here — let the main loop handle it on
                        // the next iteration with inlineIndent active.
                        // The current SeqEntry token is already pushed; the
                        // parser's parseBlock will see the next SeqEntry at
                        // deeper indent and treat it as a nested block.
                        // For this to work, we need to emit a SeqEntry token
                        // for the nested dash. Fall through to the main loop
                        // by NOT consuming and continuing.
                        // Actually, we need to advance and emit it:
                        advance();  // consume second '-'
                        std::size_t nestedIndent = column_;
                        skipSpaces();
                        Token nt{TokenType::SeqEntry, {}, line_, column_, pos_, *inlineIndent};
                        tokens.push_back(nt);
                        // Read inline value for the nested seq entry.
                        if (!atEnd() && peek() != '\n' && peek() != '\r') {
                            ZE_READ(ns, readScalarValue());
                            Token nv{TokenType::Scalar, std::move(ns), line_, column_, pos_, nestedIndent};
                            tokens.push_back(nv);
                        }
                        emitTrailingComment(tokens, *inlineIndent);
                        inlineIndent.reset();
                        continue;
                    }
                    ZE_READ(scalar, readScalarValue());
                    skipSpaces();
                    if (peek() == ':') {
                        advance();
                        Token me{TokenType::MapEntry, std::move(scalar), line_, column_, pos_, *inlineIndent};
                        tokens.push_back(me);
                        skipSpaces();
                        if (!atEnd() && peek() != '\n' && peek() != '\r') {
                            bool _dc2; ZE_EMIT_ANCHOR_ALIAS(*inlineIndent, _dc2);
                            if (_dc2) continue;
                            ZE_READ(val, readScalarValue());
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
                emitTrailingComment(tokens, indent);
                inlineIndent.reset();
                continue;
            }
            // Otherwise: a scalar, a flow collection, or a standalone value.
            if (peek() == '[' || peek() == '{') {
                const char open = peek();
                TokenStyle style = (open == '[') ? TokenStyle::FlowSeq : TokenStyle::FlowMap;
                ZE_READ(raw, readFlowCollection());
                Token t{.type = TokenType::FlowCollection, .text = std::move(raw),
                        .line = line_, .column = column_, .offset = pos_, .indent = indent,
                        .style = style};
                tokens.push_back(t);
                skipToEndOfLine();
                inlineIndent.reset();
                continue;
            }
            ZE_READ(scalar, readScalarValue());
            skipSpaces();
            if (peek() == ':') {
                advance();
                Token t{TokenType::MapEntry, std::move(scalar), line_, column_, pos_, indent};
                tokens.push_back(t);
                skipSpaces();
                if (!atEnd() && peek() != '\n' && peek() != '\r') {
                    bool _dc3; ZE_EMIT_ANCHOR_ALIAS(indent, _dc3);
                    if (_dc3) continue;
                    if (peek() == '|' || peek() == '>') {
                        ZE_READ(bs, readBlockScalar(indent));
                        Token v{TokenType::Scalar, std::move(bs), line_, column_, pos_, indent};
                        tokens.push_back(v);
                        inlineIndent.reset();
                        continue;
                    }
                    if (peek() == '[' || peek() == '{') {
                        const char open = peek();
                        TokenStyle vstyle = (open == '[') ? TokenStyle::FlowSeq : TokenStyle::FlowMap;
                        ZE_READ(raw, readFlowCollection());
                        Token v{.type = TokenType::FlowCollection, .text = std::move(raw),
                                .line = line_, .column = column_, .offset = pos_, .indent = indent,
                                .style = vstyle};
                        tokens.push_back(v);
                    } else {
                        ZE_READ(val, readScalarValue());
                        if (!val.empty()) {
                            Token v{TokenType::Scalar, std::move(val), line_, column_, pos_, indent};
                            tokens.push_back(v);
                        }
                    }
                }
                emitTrailingComment(tokens, indent);
            } else {
                Token t{TokenType::Scalar, std::move(scalar), line_, column_, pos_, indent};
                tokens.push_back(t);
                emitTrailingComment(tokens, indent);
            }
            inlineIndent.reset();
        }
#undef ZE_READ
#undef ZE_EMIT_ANCHOR_ALIAS
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

    // Skip truly blank lines (empty or whitespace-only). Comment lines and
    // content lines are left for the caller to handle — M6 emits Comment
    // tokens so the parser can bind them to nodes.
    void skipBlankLines() {
        while (!atEnd()) {
            std::size_t save = pos_;
            std::size_t saveline = line_, savecol = column_;
            while (!atEnd() && (peek() == ' ' || peek() == '\t')) advance();
            if (atEnd()) return;
            if (peek() == '\n') { advance(); continue; }
            if (peek() == '\r' && peekAt(1) == '\n') { advance(); advance(); continue; }
            // Skip ... (doc end) markers.
            if (peek() == '.' && peekAt(1) == '.' && peekAt(2) == '.') {
                advance(); advance(); advance();
                skipToEndOfLine();
                continue;
            }
            // Non-blank content (including '#' and '---') — restore and stop.
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

    // Read a comment from '#' to end of line. Returns text including the '#'.
    // Caller has already positioned at '#'; this consumes through newline.
    [[nodiscard]] std::string readComment() {
        std::size_t start = pos_;
        while (!atEnd() && peek() != '\n' && peek() != '\r') advance();
        std::string s(source_.substr(start, pos_ - start));
        // Trim trailing spaces/tabs (the comment text itself, not the newline).
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        return s;
    }

    // After a value token, skip spaces; if a '#' follows, emit it as an
    // inline Comment (bound to the just-emitted value node), then consume
    // the rest of the line. If no '#', just consume to end of line.
    void emitTrailingComment(std::vector<Token>& tokens, std::size_t indent) {
        skipSpaces();
        if (peek() == '#') {
            std::string c = readComment();
            Token t{TokenType::Comment, std::move(c), line_, column_, pos_, indent};
            t.isInline = true;
            tokens.push_back(t);
        }
        skipToEndOfLine();
    }

    // Read a block scalar (| literal or > folded). Caller positioned at | or >.
    // parentIndent is the indent of the key/entry this block belongs to.
    // The block content lives on subsequent lines at indent > parentIndent.
    // Returns the decoded content. Advances past the entire block.
    [[nodiscard]] Result<std::string> readBlockScalar(std::size_t parentIndent) {
        const bool folded = (peek() == '>');
        advance();  // consume | or >

        // Parse chomping indicator + optional explicit indent.
        char chomp = 'c';  // 'c'=clip, 's'=strip, 'k'=keep
        std::size_t explicitIndent = 0;
        bool hasExplicitIndent = false;
        while (!atEnd()) {
            const char c = peek();
            if (c == '-') { chomp = 's'; advance(); continue; }
            if (c == '+') { chomp = 'k'; advance(); continue; }
            if (c >= '0' && c <= '9') { explicitIndent = explicitIndent * 10 + (c - '0'); hasExplicitIndent = true; advance(); continue; }
            break;
        }
        // Consume rest of indicator line (may have inline comment).
        skipSpaces();
        if (peek() == '#') {
            // Skip inline comment on the indicator line — don't emit it
            // (block scalar comment handling is a later refinement).
            skipToEndOfLine();
        } else {
            skipToEndOfLine();
        }

        // Read content lines until we hit a line at indent <= parentIndent.
        // The block indent is determined by the first non-empty content line
        // (or the explicit indent if given).
        std::size_t blockIndent = hasExplicitIndent ? (parentIndent + explicitIndent) : 0;
        bool indentDetermined = hasExplicitIndent;
        std::vector<std::string> lines;  // raw lines with indent stripped

        while (!atEnd()) {
            // Peek the line's indent.
            std::size_t li = 0;
            while (pos_ + li < source_.size() && source_[pos_ + li] == ' ') ++li;

            // Check if line is empty/blank (only spaces or starts with \n).
            bool isBlank = (pos_ + li >= source_.size()) ||
                           source_[pos_ + li] == '\n' || source_[pos_ + li] == '\r';
            if (isBlank) {
                // Blank lines are part of the block if we haven't determined
                // the end yet. But if the line has fewer spaces than blockIndent
                // AND it's truly empty (just \n), it might end the block.
                if (indentDetermined) {
                    lines.push_back("");  // blank line
                    // Consume the blank line.
                    while (!atEnd() && peek() != '\n') advance();
                    if (!atEnd() && peek() == '\n') advance();
                } else {
                    // Skip blank lines before content starts.
                    while (!atEnd() && peek() != '\n') advance();
                    if (!atEnd() && peek() == '\n') advance();
                }
                continue;
            }

            if (!indentDetermined) {
                blockIndent = li;
                indentDetermined = true;
            }

            if (li <= parentIndent) break;  // dedent → end of block

            // Read the content of this line (after blockIndent spaces).
            std::string line = std::string(source_.substr(pos_ + blockIndent));
            // Truncate at newline.
            auto nlPos = line.find('\n');
            if (nlPos != std::string::npos) line = line.substr(0, nlPos);
            // Handle \r\n.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);

            // Advance past this line.
            while (!atEnd() && peek() != '\n') advance();
            if (!atEnd() && peek() == '\n') advance();
        }

        // Apply folding or literal.
        std::string result;
        if (folded) {
            // Fold: consecutive non-empty lines joined by ' '; blank line → '\n'.
            for (std::size_t k = 0; k < lines.size(); ++k) {
                if (k > 0) {
                    if (lines[k].empty()) {
                        // Blank line → paragraph break (newline).
                        // But don't double up if previous was also blank.
                        if (result.empty() || result.back() != '\n') {
                            result += '\n';
                        }
                    } else if (!lines[k - 1].empty()) {
                        result += ' ';
                    } else {
                        // After blank line, start new paragraph — no space needed.
                    }
                }
                result += lines[k];
            }
        } else {
            // Literal: preserve newlines.
            for (const auto& l : lines) {
                result += l;
                result += '\n';
            }
        }

        // Apply chomping (trailing newlines).
        // Count trailing newlines.
        std::size_t trailingNl = 0;
        while (trailingNl < result.size() && result[result.size() - 1 - trailingNl] == '\n') {
            ++trailingNl;
        }
        // Strip non-newline trailing chars? No — just handle trailing \n count.
        if (chomp == 's') {
            // Strip: remove ALL trailing newlines.
            result.erase(result.size() - trailingNl);
        } else if (chomp == 'k') {
            // Keep: all trailing newlines are preserved (already in result).
        } else {
            // Clip: keep exactly one trailing newline.
            result.erase(result.size() - trailingNl);
            if (!result.empty() || trailingNl > 0) {
                result += '\n';
            }
        }

        return result;
    }

    // Read an anchor (&name) or alias (*name). Caller positioned at & or *.
    // Returns (isAlias, name). Advances past the name.
    [[nodiscard]] std::pair<bool, std::string> readAnchorOrAlias() {
        const bool isAlias = (peek() == '*');
        advance();  // consume & or *
        std::size_t start = pos_;
        while (!atEnd()) {
            const char c = peek();
            if (c == ':' || c == ',' || c == ']' || c == '}' || c == '\n' ||
                c == '\r' || c == ' ' || c == '\t' || c == '#') break;
            advance();
        }
        return {isAlias, std::string(source_.substr(start, pos_ - start))};
    }

    // Read a tag (!name). Caller positioned at '!'. Returns the tag text
    // (without the leading '!'). Advances past the tag name + any spaces.
    [[nodiscard]] std::string readTag() {
        advance();  // consume '!'
        // Handle !!shorthand (double bang) — keep both bangs in the text.
        std::size_t start = pos_ - 1;  // include the '!'
        if (peek() == '!') advance();  // second '!'
        while (!atEnd()) {
            const char c = peek();
            if (c == ':' || c == ',' || c == ']' || c == '}' || c == '\n' ||
                c == '\r' || c == ' ' || c == '\t' || c == '#') break;
            advance();
        }
        return std::string(source_.substr(start, pos_ - start));
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
    // Double-quoted honors \n \t \r \\ \" \' \0 \a \b \f \v \e; unknown
    // escapes are a BadEscape error (YAML spec). Reaching EOF or newline
    // without a closing quote is an UnclosedQuote error — the legacy
    // library silently returned partial text.
    [[nodiscard]] Result<std::string> readQuoted() {
        const std::size_t startLine = line_;
        const std::size_t startCol = column_;
        const std::size_t startOffset = pos_;
        const char quote = peek();
        advance();  // consume opening quote
        std::string out;
        bool closed = false;
        while (!atEnd()) {
            const char c = peek();
            if (c == '\n' || c == '\r') break;  // unterminated on this line
            if (quote == '"') {
                if (c == '"') { advance(); closed = true; break; }
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
                        case 'a': out += '\a'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'v': out += '\v'; break;
                        case 'e': out += '\x1b'; break;
                        case '/': out += '/'; break;
                        case ' ': out += ' '; break;
                        default:
                            return YamlError{YamlErrorCode::BadEscape,
                                             {line_, column_, pos_},
                                             std::string("unknown escape \\") + e};
                    }
                    advance();
                    continue;
                }
                out += c;
                advance();
            } else {
                if (c == '\'') {
                    if (peekAt(1) == '\'') { out += '\''; advance(); advance(); continue; }
                    advance();  // consume closing '
                    closed = true;
                    break;
                }
                out += c;
                advance();
            }
        }
        if (!closed) {
            return YamlError{YamlErrorCode::UnclosedQuote,
                             {startLine, startCol, startOffset},
                             quote == '"' ? "unterminated double-quoted scalar"
                                          : "unterminated single-quoted scalar"};
        }
        return out;
    }

    // Read a scalar value: if it starts with a quote, readQuoted; else plain.
    // Plain scalars never fail; quoted scalars can fail with UnclosedQuote /
    // BadEscape. Callers must thread the error up through scanAll's Result.
    [[nodiscard]] Result<std::string> readScalarValue() {
        if (peek() == '"' || peek() == '\'') {
            return readQuoted();
        }
        return readPlainScalar();
    }

    // Read a complete flow collection starting at the current '[' or '{',
    // including nested collections and quoted strings. Returns the raw text
    // with the outer brackets/braces included. A flow collection that
    // reaches EOF or a newline without a matching close is an UnclosedFlow
    // error — previously it returned the truncated text and the parser
    // silently accepted it as an empty/partial collection.
    [[nodiscard]] Result<std::string> readFlowCollection() {
        const std::size_t startLine = line_;
        const std::size_t startCol = column_;
        const std::size_t startOffset = pos_;
        std::size_t start = pos_;
        const char open = peek();
        const char close = (open == '[') ? ']' : '}';
        advance();  // consume the outer open; it's not a nested opener
        int depth = 0;  // count of NESTED openers still unmatched
        char quote = 0;
        bool closed = false;
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
                if (depth > 0) { --depth; advance(); continue; }  // closes a nested opener
                if (c == close) { advance(); closed = true; break; }  // closes the outer
                break;  // mismatched close (e.g. ']' when '}' expected)
            }
            if (c == '\n' || c == '\r') break;  // single-line flow only
            advance();
        }
        if (!closed) {
            return YamlError{YamlErrorCode::UnclosedFlow,
                             {startLine, startCol, startOffset},
                             open == '[' ? "unclosed flow sequence '['"
                                         : "unclosed flow map '{'"};
        }
        return std::string(source_.substr(start, pos_ - start));
    }
};

} // namespace zyaml
