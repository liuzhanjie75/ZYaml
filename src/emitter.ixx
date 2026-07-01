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
    // Leading/trailing whitespace would be stripped by the scanner on
    // re-parse, losing data. Quote so it round-trips.
    const char first = s.front();
    const char last = s.back();
    if (first == ' ' || first == '\t' || last == ' ' || last == '\t') return true;
    if (first == '-' || first == '?' || first == ':' || first == ',' ||
        first == '[' || first == ']' || first == '{' || first == '}' ||
        first == '#' || first == '&' || first == '*' || first == '!' ||
        first == '|' || first == '>' || first == '\'' || first == '"' ||
        first == '%' || first == '@' || first == '`') {
        return true;
    }
    // Any ':' makes the scanner stop mid-scalar (readPlainScalar breaks on
    // ':' unconditionally), so quote anything containing a colon. Also
    // quote if a space-then-# looks like an inline comment. Quote any
    // control char (tab, newline, etc.) or DEL — they'd either break
    // indentation or be unverifiable on re-parse. Quote any ' or " — in
    // flow context the scanner's readFlowCollection tracks quotes and
    // would misread a plain scalar containing them (eating the closing
    // bracket as a "quoted" char). Over-quotes slightly in block context
    // but guarantees round-trip safety.
    for (char c : s) {
        if (c == ':') return true;
        if (c == '\'' || c == '"') return true;
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return true;
    }
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == ' ' && s[i + 1] == '#') return true;
    }
    return false;
}

// Emit a scalar value, quoting if necessary. Uses double quotes with
// escapes for control chars; otherwise plain.
void emitScalar(std::string& out, std::string_view s) {
    if (!needsQuoting(s)) {
        out.append(s);
        return;
    }
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '\\': out.append("\\\\"); break;
            case '"': out.append("\\\""); break;
            case '\n': out.append("\\n"); break;
            case '\t': out.append("\\t"); break;
            case '\r': out.append("\\r"); break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
}

// Emit `n` spaces.
void indentTo(std::string& out, std::size_t n) {
    out.append(n, ' ');
}

// Emit pre comments (each on its own line, indented to `indent`).
void emitPre(std::string& out, const Comments& c, std::size_t indent) {
    for (const auto& line : c.pre) {
        indentTo(out, indent);
        out.append(line);
        out.push_back('\n');
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

void emitNode(std::string& out, const Node& node, std::size_t indent);

// Emit a flow sequence: [a, b, c]
void emitFlowSeq(std::string& out, const Node& seq) {
    out.push_back('[');
    bool first = true;
    for (const auto& e : seq.elements()) {
        if (!first) out.append(", ");
        first = false;
        emitScalar(out, e.asString());
    }
    out.push_back(']');
}

// Emit a flow map: {k: v, k: v}
void emitFlowMap(std::string& out, const Node& map) {
    out.push_back('{');
    bool first = true;
    for (const auto& item : map.items()) {
        if (!first) out.append(", ");
        first = false;
        emitScalar(out, item.key);
        out.append(": ");
        emitScalar(out, item.value.asString());
    }
    out.push_back('}');
}

// Append an inline comment (the trailing "# ..." on a node's own line).
void emitInline(std::string& out, const std::optional<std::string>& ic) {
    if (ic) {
        out.append("  ");
        out.append(*ic);
    }
}

void emitNode(std::string& out, const Node& node, std::size_t indent) {
    emitPre(out, node.comments(), indent);
    // At the root level (indent 0), never emit flow — the parser can't
    // handle a top-level flow collection as the document root. Force block.
    const bool forceBlock = (indent == 0);
    auto shouldFlow = [&](const Node& n) {
        return !forceBlock && shouldEmitFlow(n);
    };
    // Empty collections: emit as [] / {} so they round-trip. Block style
    // can't represent an empty seq/map (no entries to emit), so flow is
    // the only option — allowed even at root for this case.
    if (node.isSequence() && node.size() == 0) {
        indentTo(out, indent);
        out.append("[]");
        emitInline(out, node.comments().inline_);
        out.push_back('\n');
        return;
    }
    if (node.isMap() && node.size() == 0) {
        indentTo(out, indent);
        out.append("{}");
        emitInline(out, node.comments().inline_);
        out.push_back('\n');
        return;
    }
    if (node.isNull()) {
        indentTo(out, indent);
        out.append("null");
        emitInline(out, node.comments().inline_);
        out.push_back('\n');
        return;
    }
    if (node.isScalar()) {
        indentTo(out, indent);
        emitScalar(out, node.asString());
        emitInline(out, node.comments().inline_);
        out.push_back('\n');
        return;
    }
    if (node.isSequence()) {
        if (shouldFlow(node)) {
            indentTo(out, indent);
            emitFlowSeq(out, node);
            emitInline(out, node.comments().inline_);
            out.push_back('\n');
            return;
        }
        for (const auto& e : node.elements()) {
            emitPre(out, e.comments(), indent);
            indentTo(out, indent);
            out.push_back('-');
            if (e.isNull()) { out.push_back('\n'); continue; }
            if (e.isScalar()) {
                out.push_back(' ');
                emitScalar(out, e.asString());
                emitInline(out, e.comments().inline_);
                out.push_back('\n');
            } else if (shouldFlow(e)) {
                // Flow collection inline after '- '.
                out.push_back(' ');
                if (e.isSequence()) emitFlowSeq(out, e);
                else emitFlowMap(out, e);
                emitInline(out, e.comments().inline_);
                out.push_back('\n');
            } else {
                // Nested block under '-'. Emit on the same line as '-' for
                // a map's first entry (matching scene-file style), then the
                // rest at indent+2.
                out.push_back(' ');
                if (e.isMap() && !shouldFlow(e)) {
                    bool first = true;
                    for (const auto& item : e.items()) {
                        if (!first) { out.push_back('\n'); indentTo(out, indent + 2); }
                        first = false;
                        emitScalar(out, item.key);
                        out.append(": ");
                        const Node& v = item.value;
                        if (v.isScalar()) {
                            emitScalar(out, v.asString());
                            emitInline(out, v.comments().inline_);
                        } else if (v.isNull()) {
                            out.append("null");
                        } else {
                            // Nested deeper — newline + recursive at indent+2.
                            out.push_back('\n');
                            emitNode(out, v, indent + 4);
                            continue;
                        }
                    }
                    out.push_back('\n');
                } else {
                    out.push_back('\n');
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
            emitInline(out, node.comments().inline_);
            out.push_back('\n');
            return;
        }
        for (const auto& item : node.items()) {
            emitPre(out, item.value.comments(), indent);
            indentTo(out, indent);
            emitScalar(out, item.key);
            out.append(": ");
            const Node& v = item.value;
            if (v.isScalar()) {
                emitScalar(out, v.asString());
                emitInline(out, v.comments().inline_);
                out.push_back('\n');
            } else if (v.isNull()) {
                out.append("null");
                emitInline(out, v.comments().inline_);
                out.push_back('\n');
            } else if (shouldFlow(v)) {
                // Flow collection inline on the key's line (matches how
                // scene files write vectors: position: [1.0, 2.0, 3.0]).
                if (v.isSequence()) emitFlowSeq(out, v);
                else emitFlowMap(out, v);
                emitInline(out, v.comments().inline_);
                out.push_back('\n');
            } else {
                out.push_back('\n');
                emitNode(out, v, indent + 2);
            }
        }
        return;
    }
}

} // namespace emit_detail

// Public entry: emit a Node tree as YAML text.
[[nodiscard]] std::string emit(const Node& root) {
    // Reserve a reasonable starting buffer; std::string grows on demand.
    // A 1 KiB reserve covers small documents without a realloc.
    std::string out;
    out.reserve(1024);
    emit_detail::emitNode(out, root, 0);
    return out;
}

} // namespace zyaml
