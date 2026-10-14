#pragma once
#include "Entity.h"

//
// A templated sparse set implementation, mapping EntityIndex -> T
//
// It uses a sparse list and a dense list, allowing us to store tightly
// packed sequential data that is indexed via sparse indices
//
// It does this via the following relationship: m_dense[m_sparse[EntityIndex]]
// == component(EntityIndex)
//
// The dense list is tightly packed since we want data close together as it's
// often accessed sequentially
//
// The sparse list often contains gaps, sacrificing data locality for O(1)
// lookups
//
namespace reg
{

class SparseSet
{

private:
  static constexpr Entity tombstone = Entity{-1};

  // Stores index into the m_dense array, where: (m_dense[m_sparse[EntityIndex]]
  // == T)
  // entity to dense map
  std::vector<Entity> m_sparse;

  using Deleter = void (*)(void *);
  // AutoDeleteVector is unique_ptr, they delete themselves automatically
  using AutoDeleteVector = std::unique_ptr<void, Deleter>;
  // Holds actual (component) data, tightly packed
  AutoDeleteVector m_dense;
  std::function<Entity(Entity)> m_remover;
  std::function<void(void)> m_clearer;
  std::function<size_t(void)> m_size;

  // map m_dense to entities
  std::vector<Entity> m_entities;

  //
  // Inserts an index into the sparse list that maps to the dense list,
  // associating m_dense[m_sparse[entity_index]] == m_dense[dense_index]
  //
  inline void setSparseIndex(Entity entity_index, size_t dense_index)
  {
    if (entity_index >= m_sparse.size())
      m_sparse.resize(entity_index + 1, tombstone);
    m_sparse[entity_index] = Entity(dense_index);
  }

  //
  // Returns the dense index for a given EntityIndex,
  // or a tombstone (null) value if non-existent
  //
  inline Entity getDenseIndex(Entity entity) const
  {
    if (entity.size_type() >= m_sparse.size())
      return tombstone;
    return m_sparse[entity.size_type()];
  }
  template <typename C> inline std::vector<C> &getDenseVec()
  {
    return *static_cast<std::vector<C> *>(m_dense);
  }

public:
  template <typename C>
  SparseSet()
      : m_remover(
            [this](Entity entity) { return removeFromComponent<C>(entity); }),
        m_clearer([this]() { getDenseVec<C>().clear(); }),
        m_size([this]() { return getDenseVec<C>().size(); })
  {
    auto deleter = [](void *vector) {
      delete static_cast<std::vector<C> *>(vector);
    };
    constexpr size_t BUFFER_ELEMENT_RESERVE_SIZE = 1000; // no. elements
    m_sparse.reserve(BUFFER_ELEMENT_RESERVE_SIZE);
    m_dense = AutoDeleteVector{new std::vector<C>(), deleter};

    getDense<C>().reserve(BUFFER_ELEMENT_RESERVE_SIZE);

    m_entities.reserve(BUFFER_ELEMENT_RESERVE_SIZE);
  }
  //
  // Upserts into the dense list such that:
  // m_dense[m_sparse[entity_index]] == C
  // Old "set"
  template <typename C, typename... Args>
  C &pushComponent(Entity entity, Args &&...args)
  {
    Entity index = getDenseIndex(entity);
    std::vector<C> &dense = getDenseVec<C>();
    // Overwrite if data exists
    if (index != tombstone) {
      dense[index] = C{std::forward<Args>(args)...};
      return dense[index];
    }

    // Map sparse index to dense index
    setSparseIndex(entity, dense.size());

    // Push data to the back of the dense list
    dense.emplace_back(std::forward<Args>(args)...);
    m_entities.push_back(entity);

    return dense.back();
  }

  //
  // Return C& if data exists in dense list, or nullptr if not.
  // Returns a reference to the data instead of a pointer
  // old get_ref
  template <typename C> C &fetchComponent(Entity entity)
  {
    Entity index = getDenseIndex(entity);
    std::vector<C> &dense = getDenseVec<C>();
    P_ASSERT((unsigned)index < dense.size(),
             "Entity index {} out of range on array of size {}", entity.value,
             dense.size());
    return dense[index];
  }
  Entity remove(Entity entity) { return m_remover(entity); }
  size_t size() { return m_size(); }

  //
  // Removes data from the dense list, and sets the sparse index to a tombstone
  // (null) value
  template <typename C> void removeFromComponent(Entity entity)
  {
    Entity deleted_dense_index = getDenseIndex(entity);
    std::vector<C> dense = getDenseVec<C>();

    if (dense.empty() || deleted_dense_index == tombstone)
      return;

    // Swap the back element's index in the dense list to the deleted element's
    // index and sex the deleted element's index to a tombstone (null)
    setSparseIndex(m_entities.back(), deleted_dense_index);
    setSparseIndex(entity, tombstone);

    // Swap and pop: Swap deleted index with back element, then pop off the back
    // of the dense list. An O(1) operation; at the cost of no strict dense
    // ordering
    std::swap(dense.back(), m_remover(deleted_dense_index));
    std::swap(m_entities.back(), m_entities[deleted_dense_index]);

    dense.pop_back();
    m_entities.pop_back();
  }

  //
  // This returns a copy of all EntityIndex that currently have data in
  // the dense list so we can safely delete from the vector while iterating.
  //
  std::vector<Entity> getIndexList() const { return m_entities; }

  //
  // Check if the EntityIndex currently has associated data in the dense list
  //
  bool contains_index(Entity entity_index) const
  {
    return getDenseIndex(entity_index) != tombstone;
  }
  std::vector<Entity> getEntities() { return m_entities; };

  void clear()
  {
    m_clearer();
    m_sparse.clear();
    m_entities.clear();
  }

  //
  // Read-only dense list
  //
  template <typename C> const std::vector<C> &data() const
  {
    return getDenseVec<C>().data();
  }
};
} // namespace reg
