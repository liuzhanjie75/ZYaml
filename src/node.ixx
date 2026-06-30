// :node partition — the Node value type.
//
// M1: Scalar + Map storage. Map is a vector of (key, Node) pairs to preserve
// insertion order (the legacy yaml.hpp used std::map which sorted keys).
// Sequence/anchor/comment support arrives in later milestones.
//
// Standard headers go in the GMF to avoid MSVC C5244.

module;

#include <charconv>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

export module ZYaml:node;

import :error;

export namespace zyaml {

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

    [[nodiscard]] static Node makeScalar(std::string value) {
        Node n;
        n.type_ = NodeType::Scalar;
        n.scalar_ = std::move(value);
        return n;
    }

    [[nodiscard]] static Node makeMap() {
        Node n;
        n.type_ = NodeType::Map;
        n.map_ = std::make_shared<MapStorage>();
        return n;
    }

    [[nodiscard]] static Node makeSequence() {
        Node n;
        n.type_ = NodeType::Sequence;
        n.seq_ = std::make_shared<SeqStorage>();
        return n;
    }

    [[nodiscard]] NodeType type() const noexcept { return type_; }
    [[nodiscard]] bool isNull() const noexcept { return type_ == NodeType::Null; }
    [[nodiscard]] bool isScalar() const noexcept { return type_ == NodeType::Scalar; }
    [[nodiscard]] bool isSequence() const noexcept { return type_ == NodeType::Sequence; }
    [[nodiscard]] bool isMap() const noexcept { return type_ == NodeType::Map; }

    // ── Scalar access ──────────────────────────────────────────
    // Returns the raw scalar text. Empty for non-scalars.
    [[nodiscard]] std::string_view asString() const noexcept {
        return scalar_;
    }

    // Typed conversion. Returns Result<T>; never throws.
    // M1: int, long long, std::string. M4 adds bool/float/double.
    template <class T>
    [[nodiscard]] Result<T> as() const {
        if (type_ != NodeType::Scalar) {
            return YamlError{
                YamlErrorCode::TypeMismatch, {}, "as<T>() on non-scalar node"};
        }
        if constexpr (std::is_same_v<T, int>) {
            int v = 0;
            auto [ptr, ec] = std::from_chars(scalar_.data(), scalar_.data() + scalar_.size(), v);
            if (ec != std::errc{} || ptr != scalar_.data() + scalar_.size()) {
                return YamlError{YamlErrorCode::ScalarConversion, {},
                                  "scalar is not an int: \"" + scalar_ + "\""};
            }
            return v;
        } else if constexpr (std::is_same_v<T, long long>) {
            long long v = 0;
            auto [ptr, ec] = std::from_chars(scalar_.data(), scalar_.data() + scalar_.size(), v);
            if (ec != std::errc{} || ptr != scalar_.data() + scalar_.size()) {
                return YamlError{YamlErrorCode::ScalarConversion, {},
                                  "scalar is not an integer: \"" + scalar_ + "\""};
            }
            return v;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return scalar_;
        } else {
            return YamlError{YamlErrorCode::ScalarConversion, {},
                             "as<T>() unsupported for this type"};
        }
    }

    // ── Map access ──────────────────────────────────────────────
    [[nodiscard]] std::size_t size() const noexcept {
        if (type_ == NodeType::Map && map_) return map_->entries.size();
        if (type_ == NodeType::Sequence && seq_) return seq_->entries.size();
        return 0;
    }

    [[nodiscard]] const Node* find(std::string_view key) const noexcept {
        if (type_ != NodeType::Map || !map_) return nullptr;
        for (const auto& [k, v] : map_->entries) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    [[nodiscard]] Node* find(std::string_view key) noexcept {
        if (type_ != NodeType::Map || !map_) return nullptr;
        for (auto& [k, v] : map_->entries) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    // Insertion-order-preserving iteration over (key, value) pairs.
    // Returns a view of references into the map storage.
    struct MapItem {
        std::string_view key;
        const Node& value;
    };

    // A lightweight range adapter — the map storage outlives this view only
    // as long as the Node does.
    class MapItemsView {
    public:
        MapItemsView(const std::vector<std::pair<std::string, Node>>* entries)
            : entries_(entries) {}

        struct Iterator {
            typename std::vector<std::pair<std::string, Node>>::const_iterator it;
            MapItem operator*() const { return {it->first, it->second}; }
            Iterator& operator++() { ++it; return *this; }
            bool operator!=(const Iterator& other) const { return it != other.it; }
        };

        [[nodiscard]] Iterator begin() const {
            return {entries_ ? entries_->begin() :
                               decltype(entries_->begin()){}};
        }
        [[nodiscard]] Iterator end() const {
            return {entries_ ? entries_->end() :
                               decltype(entries_->end()){}};
        }

    private:
        const std::vector<std::pair<std::string, Node>>* entries_;
    };

    [[nodiscard]] MapItemsView items() const noexcept {
        if (type_ == NodeType::Map && map_) {
            return MapItemsView(&map_->entries);
        }
        return MapItemsView(nullptr);
    }

    // ── Sequence access ─────────────────────────────────────────
    // Index access. Grows the sequence with Null nodes if i is past the end.
    Node& operator[](std::size_t i) {
        ensureSequence();
        if (i >= seq_->entries.size()) {
            seq_->entries.resize(i + 1);
        }
        return seq_->entries[i];
    }

    const Node& operator[](std::size_t i) const {
        // Returns a static null node if out of range (read-only safety).
        static const Node nullNode;
        if (type_ != NodeType::Sequence || !seq_ || i >= seq_->entries.size()) {
            return nullNode;
        }
        return seq_->entries[i];
    }

    // Range view over sequence elements (insertion order).
    class SeqElementsView {
    public:
        explicit SeqElementsView(const std::vector<Node>* entries) : entries_(entries) {}
        struct Iterator {
            typename std::vector<Node>::const_iterator it;
            const Node& operator*() const { return *it; }
            Iterator& operator++() { ++it; return *this; }
            bool operator!=(const Iterator& other) const { return it != other.it; }
        };
        [[nodiscard]] Iterator begin() const {
            return {entries_ ? entries_->begin() : decltype(entries_->begin()){}};
        }
        [[nodiscard]] Iterator end() const {
            return {entries_ ? entries_->end() : decltype(entries_->end()){}};
        }
    private:
        const std::vector<Node>* entries_;
    };

    [[nodiscard]] SeqElementsView elements() const noexcept {
        if (type_ == NodeType::Sequence && seq_) {
            return SeqElementsView(&seq_->entries);
        }
        return SeqElementsView(nullptr);
    }

    // Internal: used by the parser to append a sequence element.
    void appendSeqElement(Node value) {
        ensureSequence();
        seq_->entries.push_back(std::move(value));
    }

    void push(Node value) {
        ensureSequence();
        seq_->entries.push_back(std::move(value));
    }

    // Internal: used by the parser to append a map entry.
    void appendMapEntry(std::string key, Node value) {
        ensureMap();
        map_->entries.emplace_back(std::move(key), std::move(value));
    }

private:
    struct MapStorage {
        std::vector<std::pair<std::string, Node>> entries;
    };
    struct SeqStorage {
        std::vector<Node> entries;
    };

    void ensureMap() {
        if (type_ != NodeType::Map) {
            type_ = NodeType::Map;
            map_ = std::make_shared<MapStorage>();
        } else if (!map_) {
            map_ = std::make_shared<MapStorage>();
        }
    }

    void ensureSequence() {
        if (type_ != NodeType::Sequence) {
            type_ = NodeType::Sequence;
            seq_ = std::make_shared<SeqStorage>();
        } else if (!seq_) {
            seq_ = std::make_shared<SeqStorage>();
        }
    }

    NodeType type_ = NodeType::Null;
    std::string scalar_;
    std::shared_ptr<MapStorage> map_;
    std::shared_ptr<SeqStorage> seq_;
};

} // namespace zyaml
