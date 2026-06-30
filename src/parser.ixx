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
                Node value = Node::makeScalar(tokens[i].text);
                i++;  // consume the inline scalar
                // Are there deeper entries belonging to this seq element
                // (e.g. a map continuing under the "- ")? They appear at
                // indent > t.indent. They extend the element's value.
                if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                    && tokens[i].indent > t.indent
                    && (tokens[i].type == TokenType::MapEntry
                        || tokens[i].type == TokenType::SeqEntry)) {
                    // The element is a nested block. Replace the scalar value
                    // with the parsed block (a scalar + nested block is
                    // ambiguous in YAML; we treat the nested block as the
                    // element value and drop the inline scalar).
                    auto child = parseBlock(tokens, i);
                    if (!child) return child.error();
                    value = std::move(*child);
                }
                container.push(std::move(value));
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
                Node value = Node::makeScalar(tokens[i].text);
                i++;
                container.appendMapEntry(std::move(key), std::move(value));
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
            Node scalar = Node::makeScalar(t.text);
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
