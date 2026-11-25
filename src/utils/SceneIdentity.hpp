#pragma once

#include <Geode/Geode.hpp>

namespace paimon {

// bookkeeping must not keep nodes alive: GD loads the next game layer only
// after the outgoing editor/game layer is destroyed.
template<class Node>
class NodeIdentity {
public:
    NodeIdentity() = default;
    explicit NodeIdentity(Node* node)
        : m_node(node), m_objectID(node ? node->m_uID : 0) {}

    // the candidate is live; the stored pointer is only compared, never dereferenced.
    bool matches(Node* candidate) const {
        return candidate && candidate == m_node && candidate->m_uID == m_objectID;
    }

private:
    Node* m_node = nullptr;
    unsigned m_objectID = 0;
};

using SceneIdentity = NodeIdentity<cocos2d::CCScene>;

}
