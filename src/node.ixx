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

// Bool conversion (YAML 1.2 core + 1.1 spellings). Defined before Node
// so the as<bool>() instantiation point sees it. Accepts true/false/yes/
// no/on/off/y/n (case-insensitive). Non-bool strings return an error —
// they do NOT silently coerce to false (the legacy library's bug).
[[nodiscard]] inline Result<bool> convertBool(const std::string& s) {
    auto eq = [&](const char* lit) {
        std::size_t i = 0;
        for (; i < s.size() && lit[i]; ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != lit[i]) return false;
        }
        return i == s.size() && lit[i] == '\0';
    };
    if (eq("true") || eq("yes") || eq("on") || eq("y")) return true;
    if (eq("false") || eq("no") || eq("off") || eq("n")) return false;
    return YamlError{YamlErrorCode::ScalarConversion, {},
                     "scalar is not a bool: \"" + s + "\""};
}

class Node {
public:
    Node() = default;

    // Nodes are NOT copyable by default — map/sequence storage lives in a
    // shared_ptr, so an implicit copy would alias the same backing storage
    // and a mutation through one Node would silently affect its copies.
    // Use clone() for an independent deep copy, or move for ownership
    // transfer. This makes the shared-ownership semantics explicit.
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) noexcept = default;
    Node& operator=(Node&&) noexcept = default;

    // Deep copy: the scalar, and any map/sequence children (recursively),
    // are duplicated. The result is independent of the original.
    [[nodiscard]] Node clone() const {
        Node n;
        n.type_ = type_;
        n.scalar_ = scalar_;
        if (map_) {
            n.map_ = std::make_shared<MapStorage>();
            n.map_->entries.reserve(map_->entries.size());
            for (const auto& [k, v] : map_->entries) {
                n.map_->entries.emplace_back(k, v.clone());
            }
        }
        if (seq_) {
            n.seq_ = std::make_shared<SeqStorage>();
            n.seq_->entries.reserve(seq_->entries.size());
            for (const auto& e : seq_->entries) {
                n.seq_->entries.push_back(e.clone());
            }
        }
        return n;
    }

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

    // Parse-time helper: a plain scalar whose text is a null spelling
    // (null/Null/NULL/~ or empty) becomes a Null node rather than a Scalar.
    [[nodiscard]] static Node makeScalarOrNull(std::string value) {
        if (value.empty() || value == "null" || value == "Null" ||
            value == "NULL" || value == "~") {
            return makeNull();
        }
        return makeScalar(std::move(value));
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
    // M1: int, long long, std::string. M4: bool, float, double.
    template <class T>
    [[nodiscard]] Result<T> as() const {
        if (type_ != NodeType::Scalar) {
            return YamlError{
                YamlErrorCode::TypeMismatch, {}, "as<T>() on non-scalar node"};
        }
        if constexpr (std::is_same_v<T, int>) {
            // std::from_chars accepts leading '-' but MSVC's int overload
            // rejects leading '+'. Strip it so "+7" parses as 7.
            std::string stripped;
            const std::string* parseStr = &scalar_;
            if (!scalar_.empty() && scalar_[0] == '+') {
                stripped = scalar_.substr(1);
                parseStr = &stripped;
            }
            int v = 0;
            auto [ptr, ec] = std::from_chars(parseStr->data(), parseStr->data() + parseStr->size(), v);
            if (ec != std::errc{} || ptr != parseStr->data() + parseStr->size()) {
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
        } else if constexpr (std::is_same_v<T, float>) {
            float v = 0.0f;
            auto [ptr, ec] = std::from_chars(scalar_.data(), scalar_.data() + scalar_.size(), v);
            if (ec != std::errc{} || ptr != scalar_.data() + scalar_.size()) {
                return YamlError{YamlErrorCode::ScalarConversion, {},
                                  "scalar is not a float: \"" + scalar_ + "\""};
            }
            return v;
        } else if constexpr (std::is_same_v<T, double>) {
            double v = 0.0;
            auto [ptr, ec] = std::from_chars(scalar_.data(), scalar_.data() + scalar_.size(), v);
            if (ec != std::errc{} || ptr != scalar_.data() + scalar_.size()) {
                return YamlError{YamlErrorCode::ScalarConversion, {},
                                  "scalar is not a double: \"" + scalar_ + "\""};
            }
            return v;
        } else if constexpr (std::is_same_v<T, bool>) {
            return convertBool(scalar_);
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
