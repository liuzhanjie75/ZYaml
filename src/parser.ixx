// :parser partition — token stream → Node tree.
//
// M1: parses a flat block map of plain scalars:
//   key1: value1
//   key2: value2
// Indentation must be uniform (all entries at the same column). Nested
// maps/sequences, flow, quotes, anchors arrive in later milestones.

module;

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

// Parse a YAML document. M1: only flat block maps of plain scalars.
[[nodiscard]] Result<YamlDoc> parse(std::string_view source) {
    if (source.empty()) {
        return YamlDoc(Node::makeNull());
    }

    Scanner scanner(source);
    auto tokens = scanner.scanAll();
    if (!tokens) return tokens.error();

    // M1: all MapEntry tokens must be at the same indent (the root map's
    // indent). A Scalar without a preceding MapEntry is the document root
    // itself (a single scalar doc).
    Node root = Node::makeMap();
    bool anyEntry = false;
    std::size_t rootIndent = static_cast<std::size_t>(-1);

    for (std::size_t i = 0; i < tokens->size(); ++i) {
        const Token& t = (*tokens)[i];
        if (t.type == TokenType::EndOfInput) break;
        if (t.type == TokenType::MapEntry) {
            if (rootIndent == static_cast<std::size_t>(-1)) {
                rootIndent = t.indent;
            } else if (t.indent != rootIndent) {
                return YamlError(YamlError{
                    YamlErrorCode::BadIndent,
                    {t.line, t.column, t.offset},
                    "inconsistent indentation — nested maps not yet supported (M1)"});
            }
            Node keyNode = Node::makeScalar(t.text);
            (void)keyNode;  // M1: key is used as the map key string directly
            // Look for a value token on the same line (next token, Scalar).
            Node value;
            if (i + 1 < tokens->size() && (*tokens)[i + 1].type == TokenType::Scalar
                && (*tokens)[i + 1].line == t.line) {
                value = Node::makeScalar((*tokens)[i + 1].text);
                ++i;  // consume the value token
            } else {
                value = Node::makeNull();
            }
            root.appendMapEntry(t.text, std::move(value));
            anyEntry = true;
        } else if (t.type == TokenType::Scalar) {
            // A standalone scalar doc (no map). The first scalar wins;
            // subsequent ones are an error in M1.
            if (!anyEntry) {
                return YamlDoc(Node::makeScalar(t.text));
            }
            return YamlError(YamlError{
                YamlErrorCode::UnexpectedToken,
                {t.line, t.column, t.offset},
                "unexpected scalar after map entries (M1 limitation)"});
        }
    }

    if (!anyEntry) {
        return YamlDoc(Node::makeNull());
    }
    return YamlDoc(std::move(root));
}

} // namespace zyaml
