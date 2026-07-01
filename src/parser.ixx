// :parser partition — token stream → Node tree.
//
// M2: recursive-descent over the indent-annotated token stream. Handles
// block maps, block sequences, and arbitrary nesting. The rule:
//
//   parseBlock(minIndent):
//     consume all tokens at indent == minIndent.
//     - MapEntry: key is taken from the token; value is either the inline
//       Scalar on the same line, or a nested block (parseBlock at the
//       first child's deeper indent), or Null.
//     - SeqEntry: element value is the inline Scalar, or a nested block.
//     A block can switch from map to sequence mid-stream only at the same
//       indent — handled by tracking the "current container type".
//
// Mixed map+seq at the same indent is allowed (some YAML allows this) but
// rare; we track the container by the first entry's kind and stop on the
// opposite kind so a sibling-map after a seq is treated as the parent's
// property, not a continuation.

module;

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module ZYaml:parser;

import :error;
import :node;
import :scanner;

export namespace zyaml {

class YamlDoc {
public:
    YamlDoc() = default;
    explicit YamlDoc(Node root) : root_(std::move(root)) {}
    YamlDoc(YamlDoc&&) = default;
    YamlDoc& operator=(YamlDoc&&) = default;
    YamlDoc(const YamlDoc&) = delete;
    YamlDoc& operator=(const YamlDoc&) = delete;

    [[nodiscard]] const Node& root() const noexcept { return root_; }
    [[nodiscard]] Node& root() noexcept { return root_; }

private:
    Node root_;
};

namespace detail {

// Document-level anchor table: name → owned copy of the anchored node.
// Aliases resolve through this table by cloning the stored node (independent
// copy, no shared mutation). Node is move-only, so entries are emplaced
// via try_emplace with std::move.
using AnchorTable = std::unordered_map<std::string, Node>;

// Forward decls.
[[nodiscard]] Result<Node> parseBlock(const std::vector<Token>& tokens,
                                       std::size_t& i,
                                       AnchorTable& anchors);
[[nodiscard]] Result<Node> parseFlowCollection(std::string_view raw,
                                                std::size_t line,
                                                std::size_t column,
                                                std::size_t offset);

// parseValue: unified value reader. Called at every value position in the
// parser (MapEntry value, SeqEntry inline, standalone). Handles Anchor
// (&name), Alias (*name), Scalar, FlowCollection, and nested block values.
// `indent` is the logical indent of the key/entry this value belongs to;
// a nested block value must be at a deeper indent (checked below).
[[nodiscard]] Result<Node> parseValue(const std::vector<Token>& tokens,
                                      std::size_t& i,
                                      std::size_t indent,
                                      AnchorTable& anchors) {
    if (i >= tokens.size() || tokens[i].type == TokenType::EndOfInput) {
        return Node::makeNull();
    }
    const Token& t = tokens[i];

    // Tag: !name before a value. Record the tag, then parse the value
    // that follows and attach the tag to it.
    if (t.type == TokenType::Tag) {
        std::string tagName = t.text;
        i++;  // consume Tag
        auto result = parseValue(tokens, i, indent, anchors);
        if (!result) return result.error();
        Node value = std::move(*result);
        value.setTag(tagName);
        return value;
    }

    // Anchor: &name before a value. Register, then parse the value
    // by recursing into parseValue (handles Scalar/Flow/nested uniformly).
    if (t.type == TokenType::Anchor) {
        std::string name = t.text;
        i++;  // consume Anchor
        auto result = parseValue(tokens, i, indent, anchors);
        if (!result) return result.error();
        if (anchors.count(name)) {
            return YamlError{YamlErrorCode::DuplicateAnchor,
                             {t.line, t.column, t.offset},
                             "duplicate anchor: &" + name};
        }
        anchors.emplace(std::move(name), result->clone());
        return std::move(*result);
    }

    // Alias: *name — resolve from the anchor table.
    if (t.type == TokenType::Alias) {
        std::string name = t.text;
        i++;
        auto it = anchors.find(name);
        if (it == anchors.end()) {
            return YamlError{YamlErrorCode::UnknownAnchor,
                             {t.line, t.column, t.offset},
                             "unknown anchor: *" + name};
        }
        return it->second.clone();
    }

    // Scalar.
    if (t.type == TokenType::Scalar && t.indent == indent) {
        Node v = Node::makeScalarOrNull(t.text);
        i++;
        return v;
    }

    // Flow collection.
    if (t.type == TokenType::FlowCollection && t.indent == indent) {
        auto node = parseFlowCollection(t.text, t.line, t.column, t.offset);
        if (!node) return node.error();
        i++;
        return std::move(*node);
    }

    // Nested block (deeper indent). Any non-EndOfInput token at a deeper
    // indent is a nested block — includes Scalar (standalone), MapEntry,
    // SeqEntry, Anchor. parseBlock handles dispatching. DocStart ends the
    // document — not a nested block.
    if (t.type != TokenType::EndOfInput && t.type != TokenType::DocStart
        && t.indent > indent) {
        auto child = parseBlock(tokens, i, anchors);
        if (!child) return child.error();
        return std::move(*child);
    }

    // No inline value, no nested block → null.
    return Node::makeNull();
}

// Decode a double-quoted scalar's content (the text between the quotes):
// process escapes and fold multi-line breaks per YAML 1.2. A trailing
// backslash before a line break continues the line (folds to nothing);
// a bare line break folds to a single space. Unknown escapes are a
// BadEscape error. This is the single source of truth for double-quoted
// decoding — the scanner's readQuoted produces the same result for the
// same input, so flow and block context stay consistent.
[[nodiscard]] Result<std::string> decodeDoubleQuoted(std::string_view s,
                                                       std::size_t line,
                                                       std::size_t column,
                                                       std::size_t offset) {
    std::string out;
    std::size_t k = 0;
    while (k < s.size()) {
        const char c = s[k];
        // Line break: fold per YAML 1.2 — bare break folds to a single
        // space (trim trailing spaces on this line, leading on next).
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
            // Trailing backslash at end of line → line continuation.
            // Fold to nothing: skip the backslash, the line break, and the
            // leading whitespace of the next line.
            if (e == '\n' || e == '\r') {
                if (e == '\r' && k + 2 < s.size() && s[k + 2] == '\n') k += 3;
                else k += 2;
                while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
                continue;
            }
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
                    return YamlError{YamlErrorCode::BadEscape, {line, column, offset},
                                     std::string("unknown escape \\") + e};
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
[[nodiscard]] Result<std::string> decodeSingleQuoted(std::string_view s) {
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

// Strip surrounding quotes and decode the content of a flow-collection item
// or key. Shared between the flow seq and flow map paths. Uses the same
// decoders as block context so a quoted scalar parses identically whether
// it appears at top level or inside a flow collection.
[[nodiscard]] Result<std::string> stripFlowQuotes(std::string_view s,
                                                    std::size_t line,
                                                    std::size_t column,
                                                    std::size_t offset) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return decodeDoubleQuoted(s.substr(1, s.size() - 2), line, column, offset);
    }
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        return decodeSingleQuoted(s.substr(1, s.size() - 2));
    }
    return std::string(s);
}

// Split a flow collection's inner text on top-level commas, respecting
// nested brackets/braces and quotes. Single pass — O(n). Returns one
// string_view per item, with surrounding whitespace (incl. newlines from
// multi-line flow collections) already trimmed.
[[nodiscard]] std::vector<std::string_view> splitFlowItems(std::string_view inner) {
    std::vector<std::string_view> items;
    std::size_t segStart = 0;
    int depth = 0;
    char quote = 0;
    auto isWs = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    for (std::size_t k = 0; k <= inner.size(); ++k) {
        if (k == inner.size()) {
            if (k > segStart) {
                std::string_view item = inner.substr(segStart, k - segStart);
                while (!item.empty() && isWs(item.front())) item.remove_prefix(1);
                while (!item.empty() && isWs(item.back())) item.remove_suffix(1);
                if (!item.empty()) items.push_back(item);
            }
            break;
        }
        const char c = inner[k];
        if (quote) {
            if (c == quote) quote = 0;
            else if (quote == '"' && c == '\\' && k + 1 < inner.size()) ++k;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '[' || c == '{') { ++depth; continue; }
        if (c == ']' || c == '}') { --depth; continue; }
        if (c == ',' && depth == 0) {
            std::string_view item = inner.substr(segStart, k - segStart);
            while (!item.empty() && isWs(item.front())) item.remove_prefix(1);
            while (!item.empty() && isWs(item.back())) item.remove_suffix(1);
            if (!item.empty()) items.push_back(item);
            segStart = k + 1;
        }
    }
    return items;
}

// Find the top-level ':' that separates a flow map key from its value,
// ignoring colons inside quoted strings or nested flow collections, AND
// ignoring colons that are part of a plain scalar (e.g. "a:b" in
// {a:b: 1}). A ':' is a separator only when followed by whitespace, EOL,
// or a flow terminator — the same rule the scanner's readPlainScalar uses
// for block context. Returns npos if none.
[[nodiscard]] std::size_t findTopLevelColon(std::string_view s) {
    int depth = 0;
    char quote = 0;
    for (std::size_t k = 0; k < s.size(); ++k) {
        const char c = s[k];
        if (quote) {
            if (c == quote) quote = 0;
            else if (quote == '"' && c == '\\' && k + 1 < s.size()) ++k;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '[' || c == '{') { ++depth; continue; }
        if (c == ']' || c == '}') { --depth; continue; }
        if (c == ':' && depth == 0) {
            // Only a separator if followed by whitespace/EOL/flow-end/EOF —
            // mirrors readPlainScalar's colon rule so "a:b" stays one token.
            const char n = (k + 1 < s.size()) ? s[k + 1] : '\0';
            if (n == ' ' || n == '\t' || n == '\n' || n == '\r' ||
                n == ',' || n == ']' || n == '}' || n == '\0') {
                return k;
            }
        }
    }
    return std::string_view::npos;
}

// Parse a flow collection's raw text (e.g. "[a, b, c]" or "{x: 1, y: 2}")
// into a Node. Nested flow collections (e.g. "{x: [1, 2]}") recurse. Trims
// whitespace around each item.
[[nodiscard]] Result<Node> parseFlowCollection(std::string_view raw,
                                                std::size_t line,
                                                std::size_t column,
                                                std::size_t offset) {
    if (raw.size() < 2 || (raw.front() != '[' && raw.front() != '{')) {
        return YamlError{YamlErrorCode::UnexpectedToken, {line, column, offset},
                         "flow collection missing opening bracket"};
    }
    const bool isSeq = (raw.front() == '[');
    const char expectedClose = isSeq ? ']' : '}';
    // Defensive: scanner's readFlowCollection already guarantees a matching
    // close, but a mismatched close (e.g. "[a}") reaches here as a truncated
    // raw — reject rather than silently stripping a wrong close.
    if (raw.back() != expectedClose) {
        return YamlError{YamlErrorCode::UnclosedFlow, {line, column, offset},
                         isSeq ? "flow sequence missing closing ']'"
                               : "flow map missing closing '}'"};
    }
    std::string_view inner = raw.substr(1, raw.size() - 2);
    auto items = splitFlowItems(inner);

    if (isSeq) {
        Node seq = Node::makeSequence();
        for (auto item : items) {
            if (item.front() == '[' || item.front() == '{') {
                auto nested = parseFlowCollection(item, line, column, offset);
                if (!nested) return nested.error();
                seq.push(std::move(*nested));
            } else {
                auto decoded = stripFlowQuotes(item, line, column, offset);
                if (!decoded) return decoded.error();
                seq.push(Node::makeScalarOrNull(std::move(*decoded)));
            }
        }
        return seq;
    }
    Node map = Node::makeMap();
    for (auto item : items) {
        const auto colonPos = findTopLevelColon(item);
        if (colonPos == std::string_view::npos) {
            return YamlError{YamlErrorCode::UnexpectedToken, {line, column, offset},
                             "flow map entry missing ':'"};
        }
        std::string_view key = item.substr(0, colonPos);
        std::string_view val = item.substr(colonPos + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.remove_suffix(1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.remove_suffix(1);
        auto decodedKey = stripFlowQuotes(key, line, column, offset);
        if (!decodedKey) return decodedKey.error();
        if (!val.empty() && (val.front() == '[' || val.front() == '{')) {
            auto nested = parseFlowCollection(val, line, column, offset);
            if (!nested) return nested.error();
            map.appendMapEntry(std::move(*decodedKey), std::move(*nested));
        } else {
            auto decodedVal = stripFlowQuotes(val, line, column, offset);
            if (!decodedVal) return decodedVal.error();
            map.appendMapEntry(std::move(*decodedKey),
                                Node::makeScalarOrNull(std::move(*decodedVal)));
        }
    }
    return map;
}

// Parse a block: a run of tokens at the same indent (== blockIndent), as
// either a map or a sequence. Advances i past the block. Returns the Node.
// At the top level, blockIndent is taken from the first token's indent.
[[nodiscard]] Result<Node> parseBlock(const std::vector<Token>& tokens,
                                       std::size_t& i,
                                       AnchorTable& anchors) {
    // Skip leading block comments — they bind to the first entry's `pre`.
    std::vector<std::string> currentPre;
    while (i < tokens.size() && tokens[i].type == TokenType::Comment
           && !tokens[i].isInline) {
        currentPre.push_back(tokens[i].text);
        i++;
    }
    if (i >= tokens.size() || tokens[i].type == TokenType::EndOfInput) {
        return Node::makeNull();
    }
    const std::size_t blockIndent = tokens[i].indent;
    const TokenType firstKind = tokens[i].type;
    Node container = (firstKind == TokenType::SeqEntry)
                          ? Node::makeSequence()
                          : Node::makeMap();

    // Helper: attach `currentPre` and any following inline Comment to a
    // value node, then clear currentPre. Used after building each entry's
    // value so comments travel with the value, not the container.
    auto finalizeValue = [&](Node& value) {
        value.mutableComments().pre = std::move(currentPre);
        currentPre.clear();
        if (i < tokens.size() && tokens[i].type == TokenType::Comment
            && tokens[i].isInline) {
            value.mutableComments().inline_ = tokens[i].text;
            i++;
        }
    };

    while (i < tokens.size() && tokens[i].type != TokenType::EndOfInput) {
        const Token& t = tokens[i];
        // DocStart (---) ends this document's block.
        if (t.type == TokenType::DocStart) break;
        // A block comment at this indent is a `pre` for the next entry.
        if (t.type == TokenType::Comment && !t.isInline) {
            if (t.indent < blockIndent) break;
            currentPre.push_back(t.text);
            i++;
            continue;
        }
        if (t.indent < blockIndent) break;
        if (t.indent > blockIndent) {
            return YamlError{YamlErrorCode::BadIndent,
                             {t.line, t.column, t.offset},
                             "unexpected deeper indentation"};
        }

        if (t.type == TokenType::SeqEntry) {
            if (firstKind != TokenType::SeqEntry) break;
            i++;  // consume SeqEntry
            // SeqEntry special case: a scalar followed by a deeper nested block
            // (e.g. "- name: floor" then "  path: ..." at indent+2). parseValue
            // handles scalar; the nested block follows as a second step.
            if (i < tokens.size() && tokens[i].type == TokenType::Scalar
                && tokens[i].indent == t.indent) {
                Node value = Node::makeScalarOrNull(tokens[i].text);
                i++;
                // Check for a deeper block extending the seq element.
                if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                    && tokens[i].indent > t.indent
                    && (tokens[i].type == TokenType::MapEntry
                        || tokens[i].type == TokenType::SeqEntry
                        || tokens[i].type == TokenType::Anchor)) {
                    auto child = parseBlock(tokens, i, anchors);
                    if (!child) return child.error();
                    value = std::move(*child);
                }
                finalizeValue(value);
                container.push(std::move(value));
            } else {
                // No inline scalar — parseValue handles anchor/alias/flow/nested.
                auto value = parseValue(tokens, i, t.indent, anchors);
                if (!value) return value.error();
                finalizeValue(*value);
                container.push(std::move(*value));
            }
            continue;
        }

        if (t.type == TokenType::MapEntry) {
            if (firstKind != TokenType::MapEntry) break;
            std::string key = t.text;
            i++;  // consume MapEntry
            auto value = parseValue(tokens, i, t.indent, anchors);
            if (!value) return value.error();
            finalizeValue(*value);

            // Merge key: "<<: *anchor" splices the referenced map's entries
            // into this map WITHOUT overwriting keys already set explicitly.
            if (key == "<<" && value->isMap()) {
                for (const auto& item : value->items()) {
                    if (container.find(item.key) == nullptr) {
                        container.appendMapEntry(std::string(item.key),
                                                 item.value.clone());
                    }
                }
            } else {
                container.appendMapEntry(std::move(key), std::move(*value));
            }
            continue;
        }

        if (t.type == TokenType::Scalar) {
            Node scalar = Node::makeScalarOrNull(t.text);
            i++;
            finalizeValue(scalar);
            return scalar;
        }

        if (t.type == TokenType::FlowCollection) {
            auto node = parseFlowCollection(t.text, t.line, t.column, t.offset);
            if (!node) return node.error();
            i++;
            finalizeValue(*node);
            return std::move(*node);
        }

        return YamlError{YamlErrorCode::UnexpectedToken,
                         {t.line, t.column, t.offset},
                         "unexpected token in block"};
    }

    return container;
}

} // namespace detail

[[nodiscard]] Result<YamlDoc> parse(std::string_view source) {
    if (source.empty()) {
        return YamlDoc(Node::makeNull());
    }

    Scanner scanner(source);
    auto tokens = scanner.scanAll();
    if (!tokens) return tokens.error();

    std::size_t i = 0;
    detail::AnchorTable anchors;
    // Skip leading DocStart (---) tokens — they're optional document markers.
    while (i < tokens->size() && (*tokens)[i].type == TokenType::DocStart) {
        i++;
    }
    auto root = detail::parseBlock(*tokens, i, anchors);
    if (!root) return root.error();

    // A well-formed document has exactly one root block. If parseBlock
    // returned before consuming everything, the remaining tokens were a
    // same-indent switch of container type (map→seq or seq→map) that the
    // block parser bailed on — that's silently dropped data. Reject it.
    if (i < tokens->size() && (*tokens)[i].type != TokenType::EndOfInput) {
        const Token& t = (*tokens)[i];
        return YamlError{YamlErrorCode::UnexpectedToken,
                         {t.line, t.column, t.offset},
                         "unexpected token after document root (mixed map/seq "
                         "at the same indent is not a valid single document)"};
    }

    return YamlDoc(std::move(*root));
}

// Split a multi-document stream on top-level --- markers and parse each
// segment independently. ... (doc end) markers are skipped by the scanner.
// Returns a vector of YamlDoc, one per document.
//
// API note: parseMultiDoc returns std::optional<YamlError> with an out-param
// instead of Result<std::vector<YamlDoc>> because YamlDoc is move-only (Node
// is non-copyable), and std::variant (which Result wraps) requires its value
// type to be copyable. The out-param form sidesteps that constraint cleanly.
[[nodiscard]] std::optional<YamlError> parseMultiDoc(std::string_view source,
                                                      std::vector<YamlDoc>& docs) {
    if (source.empty()) {
        docs.push_back(YamlDoc(Node::makeNull()));
        return std::nullopt;
    }

    Scanner scanner(source);
    auto tokens = scanner.scanAll();
    if (!tokens) return tokens.error();

    std::size_t i = 0;
    while (i < tokens->size() && (*tokens)[i].type == TokenType::DocStart) {
        i++;
    }

    while (i < tokens->size() && (*tokens)[i].type != TokenType::EndOfInput) {
        detail::AnchorTable anchors;
        auto root = detail::parseBlock(*tokens, i, anchors);
        if (!root) return root.error();
        docs.push_back(YamlDoc(std::move(*root)));
        // After a document, only DocStart (---) or EndOfInput is valid. A
        // stray token here means parseBlock bailed early on a mixed-container
        // document (e.g. "a: 1\n- lost\n") — same check parse() makes.
        if (i < tokens->size() && (*tokens)[i].type != TokenType::EndOfInput
            && (*tokens)[i].type != TokenType::DocStart) {
            const Token& t = (*tokens)[i];
            return YamlError{YamlErrorCode::UnexpectedToken,
                             {t.line, t.column, t.offset},
                             "unexpected token after document (mixed map/seq "
                             "at the same indent is not a valid single document)"};
        }
        while (i < tokens->size() && (*tokens)[i].type == TokenType::DocStart) {
            i++;
        }
    }
    if (docs.empty()) {
        docs.push_back(YamlDoc(Node::makeNull()));
    }
    return std::nullopt;
}

} // namespace zyaml
