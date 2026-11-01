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
  Bitmask m_owned = Bitmask{0};
  Bitmask m_exclude = Bitmask{0};
  std::size_t m_size = 0;
  // Owned pools, one per owned component
  std::vector<SparseSet *> m_pools;

  inline bool matches(Bitmask record) const
  {
    return (record & m_owned) == m_owned && (record & m_exclude) == 0;
  }
};

} // namespace reg
