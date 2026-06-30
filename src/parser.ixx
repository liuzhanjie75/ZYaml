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
#include <expected>
#include <memory>
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

    [[nodiscard]] const Node& root() const noexcept { return root_; }
    [[nodiscard]] Node& root() noexcept { return root_; }

private:
    Node root_;
};

namespace detail {

// Document-level anchor table: name → shared_ptr to the anchored node.
// Aliases resolve through this table. clone() gives the alias an independent
// copy (not shared mutation); the shared_ptr exists so the table can outlive
// the original tree node.
using AnchorTable = std::unordered_map<std::string, std::shared_ptr<Node>>;

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
// `indent` is the logical indent of the key/entry this value belongs to.
// `endIndent` is the block's indent — if the value is a nested block it must
// be deeper than `endIndent`.
[[nodiscard]] Result<Node> parseValue(const std::vector<Token>& tokens,
                                      std::size_t& i,
                                      std::size_t indent,
                                      AnchorTable& anchors) {
    if (i >= tokens.size() || tokens[i].type == TokenType::EndOfInput) {
        return Node::makeNull();
    }
    const Token& t = tokens[i];

    // Anchor: &name before a value. Register, then parse the value.
    if (t.type == TokenType::Anchor) {
        std::string name = t.text;
        i++;  // consume Anchor
        Node value;
        if (i < tokens.size() && tokens[i].type == TokenType::Scalar
            && tokens[i].indent == indent) {
            value = Node::makeScalarOrNull(tokens[i].text);
            i++;
        } else if (i < tokens.size() && tokens[i].type == TokenType::FlowCollection
                   && tokens[i].indent == indent) {
            auto node = parseFlowCollection(tokens[i].text, tokens[i].line,
                                            tokens[i].column, tokens[i].offset);
            if (!node) return node.error();
            i++;
            value = std::move(*node);
        } else if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                   && tokens[i].indent > indent) {
            auto child = parseBlock(tokens, i, anchors);
            if (!child) return child.error();
            value = std::move(*child);
        } else {
            value = Node::makeNull();
        }
        if (anchors.count(name)) {
            return YamlError{YamlErrorCode::DuplicateAnchor,
                             {t.line, t.column, t.offset},
                             "duplicate anchor: &" + name};
        }
        anchors[name] = std::make_shared<Node>(value.clone());
        return value;
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
        return it->second->clone();
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
    // SeqEntry, Anchor. parseBlock handles dispatching.
    if (t.type != TokenType::EndOfInput && t.indent > indent) {
        auto child = parseBlock(tokens, i, anchors);
        if (!child) return child.error();
        return std::move(*child);
    }

    // No inline value, no nested block → null.
    return Node::makeNull();
}

// Parse a flow collection's raw text (e.g. "[a, b, c]" or "{x: 1, y: 2}")
// into a Node. M3: single-line, comma-separated, no nested flow collections
// (those would have been read by the scanner as one raw string and we
// recurse here only one level deep — sufficient for scene float3s and
// simple maps). Trims whitespace around each item.
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
    // Strip outer brackets/braces.
    std::string_view inner = raw.substr(1, raw.size() - 2);

    if (isSeq) {
        Node seq = Node::makeSequence();
        // Split on top-level commas.
        std::size_t segStart = 0;
        for (std::size_t k = 0; k <= inner.size(); ++k) {
            const bool atEndOrComma = (k == inner.size() || inner[k] == ',');
            if (atEndOrComma) {
                std::string_view item = inner.substr(segStart, k - segStart);
                // Trim spaces.
                while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
                while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);
                if (!item.empty()) {
                    seq.push(Node::makeScalarOrNull(std::string(item)));
                }
                segStart = k + 1;
            }
        }
        return seq;
    } else {
        Node map = Node::makeMap();
        std::size_t segStart = 0;
        for (std::size_t k = 0; k <= inner.size(); ++k) {
            const bool atEndOrComma = (k == inner.size() || inner[k] == ',');
            if (atEndOrComma) {
                std::string_view item = inner.substr(segStart, k - segStart);
                while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
                while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);
                if (!item.empty()) {
                    const auto colonPos = item.find(':');
                    if (colonPos == std::string_view::npos) {
                        return YamlError{YamlErrorCode::UnexpectedToken, {line, column, offset},
                                         "flow map entry missing ':'"};
                    }
                    std::string_view key = item.substr(0, colonPos);
                    std::string_view val = item.substr(colonPos + 1);
                    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.remove_suffix(1);
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
                    while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.remove_suffix(1);
                    map.appendMapEntry(std::string(key),
                                        Node::makeScalarOrNull(std::string(val)));
                }
                segStart = k + 1;
            }
        }
        return map;
    }
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
            container.appendMapEntry(std::move(key), std::move(*value));
            continue;
        }

        if (t.type == TokenType::Scalar) {
            Node scalar = Node::makeScalarOrNull(t.text);
            i++;
            finalizeValue(scalar);
            return scalar;
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

} // namespace zyaml
