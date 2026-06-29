// :node partition — the Node value type and its type tag.
//
// M0: minimal surface to validate the build. Node holds a type tag only;
// scalar/sequence/map storage arrives in later milestones.
//
// Standard headers go in the GMF (before `module ZYaml:node;`) so they're
// preprocessed as plain text — this avoids MSVC's C5244 "include in module
// purview should be a header unit" warning, and matches the pattern ZeroEngine
// already uses for its asset_document module.

module;

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

export module ZYaml:node;

export namespace zyaml::node {

enum class NodeType {
    Null,
    Scalar,
    Sequence,
    Map,
};

class Node {
public:
    Node() = default;

    [[nodiscard]] static Node makeNull() {
        Node n;
        n.type_ = NodeType::Null;
        return n;
    }

    [[nodiscard]] NodeType type() const noexcept { return type_; }
    [[nodiscard]] bool isNull() const noexcept { return type_ == NodeType::Null; }
    [[nodiscard]] bool isScalar() const noexcept { return type_ == NodeType::Scalar; }
    [[nodiscard]] bool isSequence() const noexcept { return type_ == NodeType::Sequence; }
    [[nodiscard]] bool isMap() const noexcept { return type_ == NodeType::Map; }

private:
    NodeType type_ = NodeType::Null;
};

} // namespace zyaml::node
