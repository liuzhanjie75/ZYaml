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
#include <string>
#include <string_view>
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

// Parse a flow collection's raw text (e.g. "[a, b, c]" or "{x: 1, y: 2}")
// into a Node. M3: single-line, comma-separated, no nested flow collections
// (those would have been read by the scanner as one raw string and we
// recurse here only one level deep — sufficient for scene float3s and
// simple maps). Trims whitespace around each item.
[[nodiscard]] Result<Node> parseFlowCollection(std::string_view raw,
                                                std::size_t line,
                                                std::size_t column,
                                                std::size_t offset) {
    if (raw.empty() || (raw.front() != '[' && raw.front() != '{')) {
        return YamlError{YamlErrorCode::UnexpectedToken, {line, column, offset},
                         "flow collection missing opening bracket"};
    }
    const bool isSeq = (raw.front() == '[');
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
                                       std::size_t& i) {
    // Determine this block's indent from the first token.
    if (i >= tokens.size() || tokens[i].type == TokenType::EndOfInput) {
        return Node::makeNull();
    }
    const std::size_t blockIndent = tokens[i].indent;

    // Decide container type by the first token kind.
    const TokenType firstKind = tokens[i].type;
    Node container = (firstKind == TokenType::SeqEntry)
                          ? Node::makeSequence()
                          : Node::makeMap();

    while (i < tokens.size() && tokens[i].type != TokenType::EndOfInput) {
        const Token& t = tokens[i];
        // A token at indent < blockIndent ends this block (return to parent).
        // A token at indent > blockIndent shouldn't appear here (parseBlock
        // is always entered at the first token of a block); but guard anyway.
        if (t.indent < blockIndent) break;
        if (t.indent > blockIndent) {
            // Unexpected deeper indent without a container-opening token —
            // treat as a parse error rather than silently misnesting.
            return YamlError{YamlErrorCode::BadIndent,
                             {t.line, t.column, t.offset},
                             "unexpected deeper indentation"};
        }

        if (t.type == TokenType::SeqEntry) {
            if (firstKind != TokenType::SeqEntry) {
                // Switched from map to seq at same indent — end this map block.
                break;
            }
            i++;  // consume SeqEntry
            // Inline value?
            if (i < tokens.size() && tokens[i].type == TokenType::Scalar
                && tokens[i].indent == t.indent) {
                Node value = Node::makeScalarOrNull(tokens[i].text);
                i++;  // consume the inline scalar
                if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                    && tokens[i].indent > t.indent
                    && (tokens[i].type == TokenType::MapEntry
                        || tokens[i].type == TokenType::SeqEntry)) {
                    auto child = parseBlock(tokens, i);
                    if (!child) return child.error();
                    value = std::move(*child);
                }
                container.push(std::move(value));
            } else if (i < tokens.size() && tokens[i].type == TokenType::FlowCollection
                       && tokens[i].indent == t.indent) {
                auto node = parseFlowCollection(tokens[i].text, tokens[i].line,
                                                tokens[i].column, tokens[i].offset);
                if (!node) return node.error();
                i++;
                container.push(std::move(*node));
            } else {
                // No inline value — the element is a nested block (or null).
                if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                    && tokens[i].indent > t.indent) {
                    auto child = parseBlock(tokens, i);
                    if (!child) return child.error();
                    container.push(std::move(*child));
                } else {
                    container.push(Node::makeNull());
                }
            }
            continue;
        }

        if (t.type == TokenType::MapEntry) {
            if (firstKind != TokenType::MapEntry) {
                break;  // switched from seq to map at same indent
            }
            std::string key = t.text;
            i++;  // consume MapEntry
            // Inline value on the same line?
            if (i < tokens.size() && tokens[i].type == TokenType::Scalar
                && tokens[i].indent == t.indent) {
                Node value = Node::makeScalarOrNull(tokens[i].text);
                i++;
                container.appendMapEntry(std::move(key), std::move(value));
            } else if (i < tokens.size() && tokens[i].type == TokenType::FlowCollection
                       && tokens[i].indent == t.indent) {
                auto node = parseFlowCollection(tokens[i].text, tokens[i].line,
                                                tokens[i].column, tokens[i].offset);
                if (!node) return node.error();
                i++;
                container.appendMapEntry(std::move(key), std::move(*node));
            } else if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                       && tokens[i].indent > t.indent) {
                // Nested block value.
                auto child = parseBlock(tokens, i);
                if (!child) return child.error();
                container.appendMapEntry(std::move(key), std::move(*child));
            } else {
                container.appendMapEntry(std::move(key), Node::makeNull());
            }
            continue;
        }

        if (t.type == TokenType::Scalar) {
            // A standalone scalar document (no map/seq wrapper). Return it
            // directly as the root — only valid at the top level.
            Node scalar = Node::makeScalarOrNull(t.text);
            i++;
            return scalar;
        }

        // Unexpected token type.
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
    auto root = detail::parseBlock(*tokens, i);
    if (!root) return root.error();

    return YamlDoc(std::move(*root));
}

} // namespace zyaml
