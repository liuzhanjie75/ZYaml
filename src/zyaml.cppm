// ZYaml — primary module unit.
//
// Exposes the public API. Consumers write `import ZYaml;`. Internals are
// partitioned (:node, :error, :scanner, :parser, ...). Partitions are
// re-exported here so the consumer never imports a partition directly.
//
// `export import :partition` makes the partition's exported declarations
// visible to anyone who imports ZYaml — this avoids the incomplete-type
// issues that `using` aliases can hit on MSVC when the aliased type's
// members reference standard-library templates.

export module ZYaml;

export import :error;
export import :node;
export import :parser;
export import :emitter;
