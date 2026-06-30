// :emitter partition — Node tree → YAML text (block style by default).
//
// M7: produces block-style YAML that re-parses to an equal tree. Flow
// collections re-emit as flow (short, single-line). Scalars are quoted
// only when necessary (contain ':' / '#' / leading special / empty).
// Comments (pre / inline) are re-emitted so they survive round-trip.
//
// The emitter does NOT preserve a per-node style hint from parse yet — it
// re-derives quoting heuristically. That's enough for round-trip equality
// (parse→emit→parse) but not for byte-identical round-trip (M7 scope is
// the former; byte fidelity is a later refinement).

module;

#include <sstream>
#include <string>
#include <string_view>

export module ZYaml:emitter;

import :node;

export namespace zyaml {

namespace emit_detail {

// True if a plain scalar's text would be ambiguous unquoted: it contains
// ':' followed by space/EOL, a '#' (comment), leading reserved chars,
// is empty, or parses as a null/bool keyword we'd want to preserve as a
// string. We're conservative — quoting is safe; the cost is aesthetics.
[[nodiscard]] bool needsQuoting(std::string_view s) {
    if (s.empty()) return true;
    const char first = s.front();
    if (first == '-' || first == '?' || first == ':' || first == ',' ||
        first == '[' || first == ']' || first == '{' || first == '}' ||
        first == '#' || first == '&' || first == '*' || first == '!' ||
        first == '|' || first == '>' || first == '\'' || first == '"' ||
        first == '%' || first == '@' || first == '`') {
        return true;
    }
    // Any ':' makes the scanner stop mid-scalar (readPlainScalar breaks on
    // ':' unconditionally), so quote anything containing a colon. Also quote
    // if a space-then-# looks like an inline comment.
    for (char c : s) {
        if (c == ':') return true;
        if (c == '\n' || c == '\r') return true;
    }
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == ' ' && s[i + 1] == '#') return true;
    }
    return false;
}

// Emit a scalar value, quoting if necessary. Uses double quotes with
// escapes for control chars; otherwise plain.
void emitScalar(std::ostringstream& out, std::string_view s) {
    if (!needsQuoting(s)) {
        out << s;
        return;
    }
    out << '"';
    for (char c : s) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\t': out << "\\t"; break;
            case '\r': out << "\\r"; break;
            default: out << c; break;
        }
    }
    out << '"';
}

// Emit `n` spaces.
void indentTo(std::ostringstream& out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) out << ' ';
}

// Emit pre comments (each on its own line, indented to `indent`).
void emitPre(std::ostringstream& out, const Comments& c, std::size_t indent) {
    for (const auto& line : c.pre) {
        indentTo(out, indent);
        out << line << '\n';
    }
}

// True if a node's children should emit inline (flow) vs block. M7 emits
// flow when the node was parsed as flow (we can't tell post-parse without a
// style hint), so we re-derive: emit flow for short scalar-only sequences
// and maps of scalar values. Conservative: only flow if all children are
// scalars and total length is small.
[[nodiscard]] bool shouldEmitFlow(const Node& n) {
    if (n.isSequence()) {
        if (n.size() == 0 || n.size() > 8) return false;
        for (const auto& e : n.elements()) {
            if (!e.isScalar()) return false;
            if (e.asString().size() > 16) return false;
        }
        return true;
    }
    if (n.isMap()) {
        if (n.size() == 0 || n.size() > 4) return false;
        for (const auto& item : n.items()) {
            if (!item.value.isScalar()) return false;
            if (item.value.asString().size() > 16) return false;
        }
        return true;
    }
    return false;
}

void emitNode(std::ostringstream& out, const Node& node, std::size_t indent);

// Emit a flow sequence: [a, b, c]
void emitFlowSeq(std::ostringstream& out, const Node& seq) {
    out << '[';
    bool first = true;
    for (const auto& e : seq.elements()) {
        if (!first) out << ", ";
        first = false;
        emitScalar(out, e.asString());
    }
    out << ']';
}

// Emit a flow map: {k: v, k: v}
void emitFlowMap(std::ostringstream& out, const Node& map) {
    out << '{';
    bool first = true;
    for (const auto& item : map.items()) {
        if (!first) out << ", ";
        first = false;
        emitScalar(out, item.key);
        out << ": ";
        emitScalar(out, item.value.asString());
    }
    out << '}';
}

void emitNode(std::ostringstream& out, const Node& node, std::size_t indent) {
    emitPre(out, node.comments(), indent);
    // At the root level (indent 0), never emit flow — the parser can't
    // handle a top-level flow collection as the document root. Force block.
    const bool forceBlock = (indent == 0);
    auto shouldFlow = [&](const Node& n) {
        return !forceBlock && shouldEmitFlow(n);
    };
    if (node.isNull()) {
        indentTo(out, indent);
        out << "null";
        if (node.comments().inline_) out << "  " << *node.comments().inline_;
        out << '\n';
        return;
    }
    if (node.isScalar()) {
        indentTo(out, indent);
        emitScalar(out, node.asString());
        if (node.comments().inline_) out << "  " << *node.comments().inline_;
        out << '\n';
        return;
    }
    if (node.isSequence()) {
        if (shouldFlow(node)) {
            indentTo(out, indent);
            emitFlowSeq(out, node);
            if (node.comments().inline_) out << "  " << *node.comments().inline_;
            out << '\n';
            return;
        }
        for (const auto& e : node.elements()) {
            emitPre(out, e.comments(), indent);
            indentTo(out, indent);
            out << '-';
            if (e.isNull()) { out << '\n'; continue; }
            if (e.isScalar()) {
                out << ' ';
                emitScalar(out, e.asString());
                if (e.comments().inline_) out << "  " << *e.comments().inline_;
                out << '\n';
            } else if (shouldFlow(e)) {
                // Flow collection inline after '- '.
                out << ' ';
                if (e.isSequence()) emitFlowSeq(out, e);
                else emitFlowMap(out, e);
                if (e.comments().inline_) out << "  " << *e.comments().inline_;
                out << '\n';
            } else {
                // Nested block under '-'. Emit on the same line as '-' for
                // a map's first entry (matching scene-file style), then the
                // rest at indent+2.
                out << ' ';
                if (e.isMap() && !shouldFlow(e)) {
                    bool first = true;
                    for (const auto& item : e.items()) {
                        if (!first) { out << '\n'; indentTo(out, indent + 2); }
                        first = false;
                        emitScalar(out, item.key);
                        out << ": ";
                        const Node& v = item.value;
                        if (v.isScalar()) {
                            emitScalar(out, v.asString());
                            if (v.comments().inline_) out << "  " << *v.comments().inline_;
                        } else if (v.isNull()) {
                            out << "null";
                        } else {
                            // Nested deeper — newline + recursive at indent+2.
                            out << '\n';
                            emitNode(out, v, indent + 4);
                            continue;
                        }
                    }
                    out << '\n';
                } else {
                    out << '\n';
                    emitNode(out, e, indent + 2);
                }
            }
        }
        return;
    }
    if (node.isMap()) {
        if (shouldFlow(node)) {
            indentTo(out, indent);
            emitFlowMap(out, node);
            if (node.comments().inline_) out << "  " << *node.comments().inline_;
            out << '\n';
            return;
        }
        for (const auto& item : node.items()) {
            emitPre(out, item.value.comments(), indent);
            indentTo(out, indent);
            emitScalar(out, item.key);
            out << ": ";
            const Node& v = item.value;
            if (v.isScalar()) {
                emitScalar(out, v.asString());
                if (v.comments().inline_) out << "  " << *v.comments().inline_;
                out << '\n';
            } else if (v.isNull()) {
                out << "null";
                if (v.comments().inline_) out << "  " << *v.comments().inline_;
                out << '\n';
            } else if (shouldFlow(v)) {
                // Flow collection inline on the key's line (matches how
                // scene files write vectors: position: [1.0, 2.0, 3.0]).
                if (v.isSequence()) emitFlowSeq(out, v);
                else emitFlowMap(out, v);
                if (v.comments().inline_) out << "  " << *v.comments().inline_;
                out << '\n';
            } else {
                out << '\n';
                emitNode(out, v, indent + 2);
            }
        }
        return;
    }
}

} // namespace emit_detail

// Public entry: emit a Node tree as YAML text.
[[nodiscard]] std::string emit(const Node& root) {
    std::ostringstream out;
    emit_detail::emitNode(out, root, 0);
    return out.str();
}

} // namespace zyaml
