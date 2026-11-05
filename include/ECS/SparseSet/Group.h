#pragma once
#include "ECS/Entity.h"
#include "SparseSet.h"
#include <cstddef>
#include <vector>

namespace reg
{

//
// State of an owning group. Lives inside the registry.
//
// Invariant: for every pool in m_pools, the first m_size entries of its dense
// list are exactly the entities matching the group, in the same order across
// all pools.
//
struct GroupData {
  SparseSet *getSparseSet(Bitmask b)
  {
    auto it = m_pools.find(b);
    if (it != m_pools.end()) {
      return it->second;
    }
    return nullptr;
  }
  template <ECSComponent C> void performSwaps(Entity e)
  {
    Bitmask bm = getSingleBitmask<C>();
    SparseSet *s = getSparseSet<C>(bm);
    s->swapDense<C>(s->getDenseIndex(e), m_size);
  }

  inline bool matches(Bitmask record) const
  {
    return (record & m_owned) == m_owned && (record & m_exclude) == 0;
  }

  // entity just became a match: move it into slot [size] in every pool
  template <ECSComponent... Components> void enter(Entity e)
  {
    (performSwaps<Components>(e), ...);
    ++m_size;
  }
  // entity is about to stop matching: move it to slot [size-1], shrink
  template <ECSComponent... Components> void leave(Entity e)
  {
    --m_size;
    (performSwaps<Components>(e), ...);
  }
  // bool contains(Entity e) const
  // { // O(1)
  //   return m_pools[0]->containsIndex(e) &&
  //          m_pools[0]->getDenseIndex(e) < m_size;
  // }
  //
  Bitmask m_owned = Bitmask{0};
  Bitmask m_exclude = Bitmask{0};
  std::size_t m_size = 0;
  // Owned pools, one per owned component
  std::map<Bitmask, SparseSet *> m_pools;
};

} // namespace reg
