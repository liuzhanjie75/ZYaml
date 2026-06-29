// ZYaml — primary module unit.
//
// Exposes the public API of the library. Consumers write `import ZYaml;` and
// get the full surface. Internals are partitioned (:node, :scanner, :parser,
// :emitter, :comment, :error) but those partitions are re-exported here so
// the consumer never imports a partition directly.
//
// M0 milestone: only :node exists, with a minimal Node (makeNull + isNull)
// to validate the build pipeline. Subsequent milestones expand the API.

export module ZYaml;

import :node;

export namespace zyaml {

using NodeType = ::zyaml::node::NodeType;
using Node     = ::zyaml::node::Node;

} // namespace zyaml
