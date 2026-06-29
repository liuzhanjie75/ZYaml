// M0 smoke test — validates the build pipeline end to end.
//
// Asserts the minimal Node API compiles and links through `import ZYaml`.
// Subsequent milestones replace this with feature-specific tests.

#include <iostream>

import ZYaml;

int main() {
    using namespace zyaml;

    const Node n = Node::makeNull();
    if (!n.isNull()) {
        std::cerr << "FAIL: makeNull().isNull() expected true, got false\n";
        return 1;
    }
    if (n.type() != NodeType::Null) {
        std::cerr << "FAIL: makeNull().type() expected Null\n";
        return 1;
    }
    if (n.isScalar() || n.isSequence() || n.isMap()) {
        std::cerr << "FAIL: null node reports a non-null type\n";
        return 1;
    }

    std::cout << "zyaml smoke test passed\n";
    return 0;
}
