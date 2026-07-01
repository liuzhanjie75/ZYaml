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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ZYaml:scanner;

import :error;

// Internal helper: apply a double-quoted escape sequence. `e` is the char
// after '\'. Writes the decoded char(s) to `out`. Returns an error for
// unknown escapes. Not exported — visible only within the module
// (scanner/parser partitions), not to consumers of `import ZYaml`.
namespace zyaml::quote_detail {
[[nodiscard]] inline std::optional<YamlError>
applyDoubleEscape(char e, std::string& out) {
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
            return YamlError{YamlErrorCode::BadEscape, {},
                             std::string("unknown escape \\") + e};
    }
    return std::nullopt;
}
} // namespace zyaml::quote_detail

export namespace zyaml {

// ── Shared quoted-scalar decoders ───────────────────────────────────
// Single source of truth for escape processing and multi-line folding.
// Used by both Scanner::readQuoted (block context) and stripFlowQuotes
// (flow context) so the two paths can't drift.

// Decode a double-quoted scalar's content (the text between the quotes):
// process escapes and fold multi-line breaks per YAML 1.2. A trailing
// backslash before a line break continues the line (folds to nothing);
// a bare line break folds to a single space. Unknown escapes are a
// BadEscape error. `line`/`column`/`offset` locate the scalar start for
// error messages.
[[nodiscard]] inline Result<std::string> decodeDoubleQuoted(std::string_view s,
                                                             std::size_t line,
                                                             std::size_t column,
                                                             std::size_t offset) {
    std::string out;
    std::size_t k = 0;
    while (k < s.size()) {
        const char c = s[k];
        if (c == '\n' || c == '\r') {
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
            if (c == '\r' && k + 1 < s.size() && s[k + 1] == '\n') k += 2;
            else ++k;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        if (c == '\\' && k + 1 < s.size()) {
            const char e = s[k + 1];
            if (e == '\n' || e == '\r') {
                if (e == '\r' && k + 2 < s.size() && s[k + 2] == '\n') k += 3;
                else k += 2;
                while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
                continue;
            }
            if (auto err = quote_detail::applyDoubleEscape(e, out)) {
                err->where = {line, column, offset};
                return *err;
            }
            k += 2;
            continue;
        }
        out += c;
        ++k;
    }
    return out;
}

// Decode a single-quoted scalar's content: `''` → `'`, bare line breaks
// fold to a single space. No escape sequences (single-quoted scalars have
// none per YAML 1.2).
[[nodiscard]] inline Result<std::string> decodeSingleQuoted(std::string_view s) {
    std::string out;
    std::size_t k = 0;
    while (k < s.size()) {
        const char c = s[k];
        if (c == '\'') {
            if (k + 1 < s.size() && s[k + 1] == '\'') {
                out += '\'';
                k += 2;
                continue;
            }
            ++k;  // lone trailing quote (shouldn't happen — caller strips outer pair)
            continue;
        }
        if (c == '\n' || c == '\r') {
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
            if (c == '\r' && k + 1 < s.size() && s[k + 1] == '\n') k += 2;
            else ++k;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        out += c;
        ++k;
    }
    return out;
}

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

struct Token {
    TokenType type = TokenType::EndOfInput;
    std::string text;     // scalar text; key text for MapEntry; empty for SeqEntry;
                          // raw "[...]" / "{...}" for FlowCollection;
                          // "# ..." (with leading #) for Comment
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t offset = 0;
    std::size_t indent = 0;
    bool isInline = false;  // Comment: true if trailing on a value's own line
};

class Scanner {
public:
    explicit Scanner(std::string_view source) : source_(source) {}

    [[nodiscard]] Result<std::vector<Token>> scanAll() {
        std::vector<Token> tokens;
        // Rough estimate: one token per ~16 source bytes (a typical YAML
        // line). Avoids repeated realloc during large parses.
        tokens.reserve(source_.size() / 16);
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

        while (!atEnd()) {
            skipBlankLines();
            if (atEnd()) break;
            // YAML 1.2 forbids tabs for indentation. A tab at the start of
            // a content line is a BadIndent error.
            {
                std::size_t p = pos_;
                while (p < source_.size() && source_[p] == ' ') ++p;
                if (p < source_.size() && source_[p] == '\t') {
                    return YamlError{YamlErrorCode::BadIndent,
                                     {line_, column_, pos_},
                                     "tab character used for indentation"};
                }
            }
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
            // Document start marker ---. Must be at column 0 (indent == 0)
            // AND followed by whitespace/EOL — "---foo" is a plain scalar,
            // not a doc marker. Otherwise the marker silently eats content.
            if (indent == 0 && peek() == '-' && peekAt(1) == '-' && peekAt(2) == '-') {
                const char after = peekAt(3);
                if (after == ' ' || after == '\t' || after == '\n' ||
                    after == '\r' || after == '\0') {
                    advance(); advance(); advance();
                    skipToEndOfLine();
                    Token t{TokenType::DocStart, {}, line_, column_, pos_, indent};
                    tokens.push_back(t);
                    inlineIndent.reset();
                    continue;
                }
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
                    if (emitAnchorAliasTag(tokens, *inlineIndent, inlineIndent)) continue;
                    if (peek() == '|' || peek() == '>') {
                        ZE_READ(bs, readBlockScalar(indent));
                        Token v{TokenType::Scalar, std::move(bs), line_, column_, pos_, indent};
                        tokens.push_back(v);
                        inlineIndent.reset();
                        continue;
                    }
                    // Inline flow collection as the seq element value.
                    if (peek() == '[' || peek() == '{') {
                        ZE_READ(raw, readFlowCollection());
                        Token v{TokenType::FlowCollection, std::move(raw),
                                line_, column_, pos_, *inlineIndent};
                        tokens.push_back(v);
                        emitTrailingComment(tokens, indent);
                        inlineIndent.reset();
                        continue;
                    }
                    // Nested seq: "- - a" — the second '-' is another SeqEntry
                    // at the inlineIndent. Emit it; the parser handles it as
                    // a nested block. The inline value dispatch mirrors the
                    // outer seq-entry path so flow/block/anchor values are
                    // recognized (previously this called readScalarValue()
                    // directly, mis-reading "- - [1, 2]" as a plain scalar).
                    if (peek() == '-' && (peekAt(1) == ' ' || peekAt(1) == '\t' ||
                                          peekAt(1) == '\n' || peekAt(1) == '\r' || atEndAfter(1))) {
                        advance();  // consume second '-'
                        std::size_t nestedIndent = column_;
                        skipSpaces();
                        Token nt{TokenType::SeqEntry, {}, line_, column_, pos_, *inlineIndent};
                        tokens.push_back(nt);
                        // Read inline value for the nested seq entry.
                        if (!atEnd() && peek() != '\n' && peek() != '\r') {
                            if (emitAnchorAliasTag(tokens, nestedIndent, inlineIndent)) continue;
                            if (peek() == '|' || peek() == '>') {
                                ZE_READ(bs, readBlockScalar(*inlineIndent));
                                Token nv{TokenType::Scalar, std::move(bs), line_, column_, pos_, nestedIndent};
                                tokens.push_back(nv);
                            } else if (peek() == '[' || peek() == '{') {
                                ZE_READ(raw, readFlowCollection());
                                Token nv{TokenType::FlowCollection, std::move(raw),
                                         line_, column_, pos_, nestedIndent};
                                tokens.push_back(nv);
                            } else {
                                ZE_READ(ns, readScalarValue());
                                if (!ns.empty()) {
                                    Token nv{TokenType::Scalar, std::move(ns), line_, column_, pos_, nestedIndent};
                                    tokens.push_back(nv);
                                }
                            }
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
                            if (emitAnchorAliasTag(tokens, *inlineIndent, inlineIndent)) continue;
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
                ZE_READ(raw, readFlowCollection());
                Token t{TokenType::FlowCollection, std::move(raw),
                        line_, column_, pos_, indent};
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
                    if (emitAnchorAliasTag(tokens, indent, inlineIndent)) continue;
                    if (peek() == '|' || peek() == '>') {
                        ZE_READ(bs, readBlockScalar(indent));
                        Token v{TokenType::Scalar, std::move(bs), line_, column_, pos_, indent};
                        tokens.push_back(v);
                        inlineIndent.reset();
                        continue;
                    }
                    if (peek() == '[' || peek() == '{') {
                        ZE_READ(raw, readFlowCollection());
                        Token v{TokenType::FlowCollection, std::move(raw),
                                line_, column_, pos_, indent};
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
            // Skip ... (doc end) markers. Must be followed by whitespace/EOL
            // — "...foo" is a plain scalar, not a doc-end marker.
            if (peek() == '.' && peekAt(1) == '.' && peekAt(2) == '.') {
                const char after = peekAt(3);
                if (after == ' ' || after == '\t' || after == '\n' ||
                    after == '\r' || after == '\0') {
                    advance(); advance(); advance();
                    skipToEndOfLine();
                    continue;
                }
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

    // At a value position, if the next char is '&'/'*'/'!', emit an Anchor
    // / Alias / Tag token, skip spaces, and if the line then ends, emit the
    // trailing comment and clear inlineIndent. Returns true if the line
    // ended (caller should `continue` the main loop); false if a value may
    // follow on the same line.
    bool emitAnchorAliasTag(std::vector<Token>& tokens, std::size_t indent,
                            std::optional<std::size_t>& inlineIndent) {
        if (peek() != '&' && peek() != '*' && peek() != '!') return false;
        if (peek() == '&' || peek() == '*') {
            auto aa = readAnchorOrAlias();
            tokens.push_back(Token{aa.first ? TokenType::Alias : TokenType::Anchor,
                                    std::move(aa.second), line_, column_, pos_, indent});
        } else {
            std::string tg = readTag();
            tokens.push_back(Token{TokenType::Tag, std::move(tg),
                                    line_, column_, pos_, indent});
        }
        skipSpaces();
        if (atEnd() || peek() == '\n' || peek() == '\r') {
            emitTrailingComment(tokens, indent);
            inlineIndent.reset();
            return true;
        }
        return false;
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
        // Consume rest of indicator line (may have an inline comment, which
        // we don't emit — block scalar comment handling is a later refinement).
        skipSpaces();
        skipToEndOfLine();

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
                // Once the block indent is known, blank lines are part of the
                // block (kept as empty entries for folding). Before that,
                // leading blank lines are skipped.
                if (indentDetermined) {
                    lines.push_back("");  // blank line
                    while (!atEnd() && peek() != '\n') advance();
                    if (!atEnd() && peek() == '\n') advance();
                } else {
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
            // The final line's newline is part of the content — chomping
            // decides what to do with it (clip keeps one, strip removes,
            // keep preserves). Add it before chomping so the logic is
            // uniform with the literal path.
            if (!lines.empty()) result += '\n';
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
            // ':' ends a plain scalar only when followed by space, tab, EOL,
            // or a flow indicator (YAML 1.2). Otherwise it's part of the
            // scalar text (e.g. "a:b", "https://x").
            if (c == ':') {
                const char n = peekAt(1);
                if (n == ' ' || n == '\t' || n == '\n' || n == '\r' ||
                    n == ',' || n == ']' || n == '}' || n == '\0') break;
                advance();
                continue;
            }
            // '#' ends a plain scalar only when preceded by whitespace (it's
            // an inline comment then). A '#' embedded mid-token stays.
            if (c == '#') {
                if (pos_ == start || source_[pos_ - 1] == ' ' || source_[pos_ - 1] == '\t') break;
                advance();
                continue;
            }
            if (c == '\n' || c == '\r') break;
            advance();
        }
        std::string s(source_.substr(start, pos_ - start));
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        return s;
    }

    // Read a quoted scalar starting at the current '"' or '\''. Returns the
    // decoded content. The raw text between the quotes is collected first
    // (tracking line/column for unclosed-quote errors), then decoded via
    // the shared decodeDoubleQuoted/decodeSingleQuoted — the same path
    // flow context uses, so block and flow quoted scalars stay consistent.
    [[nodiscard]] Result<std::string> readQuoted() {
        const std::size_t startLine = line_;
        const std::size_t startCol = column_;
        const std::size_t startOffset = pos_;
        const char quote = peek();
        advance();  // consume opening quote
        std::string raw;
        bool closed = false;
        while (!atEnd()) {
            const char c = peek();
            // For double quotes, an escape sequence `\<x>` must not be
            // mistaken for a closing quote — keep both chars verbatim
            // in raw and let the decoder process them.
            if (quote == '"' && c == '\\' && !atEndAfter(1)) {
                raw += c;
                raw += peekAt(1);
                advance(); advance();
                continue;
            }
            if (c == quote) {
                if (quote == '\'' && peekAt(1) == '\'') {
                    // '' escape — keep both quotes in raw; the decoder
                    // collapses them to one '.
                    raw += '\'';
                    raw += '\'';
                    advance(); advance();
                    continue;
                }
                advance();  // consume closing quote
                closed = true;
                break;
            }
            raw += c;
            advance();
        }
        if (!closed) {
            return YamlError{YamlErrorCode::UnclosedQuote,
                             {startLine, startCol, startOffset},
                             quote == '"' ? "unterminated double-quoted scalar"
                                          : "unterminated single-quoted scalar"};
        }
        if (quote == '"') {
            return decodeDoubleQuoted(raw, startLine, startCol, startOffset);
        }
        return decodeSingleQuoted(raw);
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
    // including nested collections, quoted strings, and line breaks (YAML
    // 1.2 allows multi-line flow). Returns the raw text with the outer
    // brackets/braces included. A flow collection that reaches EOF without a
    // matching close is an UnclosedFlow error. Mismatched close brackets
    // (e.g. '[1, {a: 2]]') are rejected via an opener stack rather than a
    // bare depth counter, so the error type points at the real problem.
    [[nodiscard]] Result<std::string> readFlowCollection() {
        const std::size_t startLine = line_;
        const std::size_t startCol = column_;
        const std::size_t startOffset = pos_;
        std::size_t start = pos_;
        const char open = peek();
        const char close = (open == '[') ? ']' : '}';
        advance();  // consume the outer open; it's not a nested opener
        // Opener stack: each entry is the expected close for that level.
        // Grows with nesting depth — no semantic cap (YAML allows arbitrary
        // nesting). reserve(8) covers the common case without heap traffic.
        std::vector<char> stack;
        stack.reserve(8);
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
            if (c == '[' || c == '{') {
                stack.push_back((c == '[') ? ']' : '}');
                advance();
                continue;
            }
            if (c == ']' || c == '}') {
                if (!stack.empty()) {
                    // Closing a nested opener — must match the expected type.
                    if (stack.back() != c) break;  // mismatched close
                    stack.pop_back();
                    advance();
                    continue;
                }
                if (c == close) { advance(); closed = true; break; }  // closes the outer
                break;  // mismatched close at the outer level
            }
            // Newlines and other characters are part of the raw text —
            // multi-line flow collections are valid YAML 1.2.
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
