#pragma once

#include "NodeTypes.h"

namespace Forge {

// Material Output Node - the final output for material rendering
struct MaterialOutputNode : public Node {
    MaterialOutputNode(int nodeId, ImVec2 pos);
};

} // namespace Forge
