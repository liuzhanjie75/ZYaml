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
                                       std::size_t& i) {
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
            Node value;
            if (i < tokens.size() && tokens[i].type == TokenType::Scalar
                && tokens[i].indent == t.indent) {
                value = Node::makeScalarOrNull(tokens[i].text);
                i++;
                if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                    && tokens[i].indent > t.indent
                    && (tokens[i].type == TokenType::MapEntry
                        || tokens[i].type == TokenType::SeqEntry)) {
                    auto child = parseBlock(tokens, i);
                    if (!child) return child.error();
                    value = std::move(*child);
                }
            } else if (i < tokens.size() && tokens[i].type == TokenType::FlowCollection
                       && tokens[i].indent == t.indent) {
                auto node = parseFlowCollection(tokens[i].text, tokens[i].line,
                                                tokens[i].column, tokens[i].offset);
                if (!node) return node.error();
                i++;
                value = std::move(*node);
            } else if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                       && tokens[i].indent > t.indent) {
                auto child = parseBlock(tokens, i);
                if (!child) return child.error();
                value = std::move(*child);
            } else {
                value = Node::makeNull();
            }
            finalizeValue(value);
            container.push(std::move(value));
            continue;
        }

        if (t.type == TokenType::MapEntry) {
            if (firstKind != TokenType::MapEntry) break;
            std::string key = t.text;
            i++;  // consume MapEntry
            Node value;
            if (i < tokens.size() && tokens[i].type == TokenType::Scalar
                && tokens[i].indent == t.indent) {
                value = Node::makeScalarOrNull(tokens[i].text);
                i++;
            } else if (i < tokens.size() && tokens[i].type == TokenType::FlowCollection
                       && tokens[i].indent == t.indent) {
                auto node = parseFlowCollection(tokens[i].text, tokens[i].line,
                                                tokens[i].column, tokens[i].offset);
                if (!node) return node.error();
                i++;
                value = std::move(*node);
            } else if (i < tokens.size() && tokens[i].type != TokenType::EndOfInput
                       && tokens[i].indent > t.indent) {
                auto child = parseBlock(tokens, i);
                if (!child) return child.error();
                value = std::move(*child);
            } else {
                value = Node::makeNull();
            }
            finalizeValue(value);
            container.appendMapEntry(std::move(key), std::move(value));
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
    auto root = detail::parseBlock(*tokens, i);
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
