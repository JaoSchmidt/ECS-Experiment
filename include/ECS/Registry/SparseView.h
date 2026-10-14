#pragma once
#include "Bitmask.h"
#include "Entity.h"
#include "SparseSet.h"
//
// A SimpleView is a basic implementation of a view, allowing iteration based
// on the passed in Component parameter pack.
//
// This allows us to operate on entities with the given components using
// lambdas.
//
// This type is ephemeral, and not meant to be persistently stored.
//
namespace reg
{

template <ECSComponent... Components> class View
{
  using ForEachFunc = std::function<void(Components &...)>;
  using ForEachFuncWithEntity = std::function<void(Entity, Components &...)>;

public:
  View(const std::vector<Bitmask> &records,
       std::array<SparseSet *, sizeof...(Components)> &&arr,
       Bitmask excludeBitMask)
      : m_viewPools{std::move(arr)}, m_records(records),
        m_excludeBitMask(excludeBitMask)
  {
    SEECS_ASSERT(sizeof...(Components) == m_viewPools.size(),
                 "Component type list and pool array size mismatch");

    auto smallest_pool = std::min_element(
        m_viewPools.begin(), m_viewPools.end(),
        [](SparseSet *a, SparseSet *b) { return a->size() < b->size(); });

    SEECS_ASSERT(smallest_pool != m_viewPools.end(),
                 "Initializing invalid/empty view");

    m_smallest = *smallest_pool;
  }

  //
  // Executes a passed lambda on all the entities that match the
  // passed parameter pack.
  //
  // Provided function should follow one of two forms:
  // [](Component& c1, Component& c2);
  // OR
  // [](Entity entity, Component& c1, Component& c2);
  //
  void forEach(ForEachFunc func) { forEachImpl(func); }

  void forEach(ForEachFuncWithEntity func) { forEachWithEntityImpl(func); }

private:
  // Stores component pools associated with the view via their component index
  // from ComponentTypes
  std::array<SparseSet *, sizeof...(Components)> m_viewPools;
  const std::vector<Bitmask> &m_records;
  Bitmask m_excludeBitMask = Bitmask{-1};
  //
  // Pointer to the smallest component pool that we use as our basis for
  // iteration
  //
  // SimpleView<A, B, C>:
  //		Pool(A) x x x x x x x
  //		Pool(B) x x  <----- Pool B will be used to iterate the view
  //		Pool(C) x x x x x
  //
  SparseSet *m_smallest = nullptr;

  //
  // Returns true iff all the pools in the view contain the given EntityIndex
  //
  bool allContain(Entity entity)
  {
    return std::all_of(
        m_viewPools.begin(), m_viewPools.end(),
        [entity](SparseSet *pool) { return pool->containsIndex(entity); });
  }
  bool IsExcluded(Entity entity)
  {
    return (m_records[entity] & m_excludeBitMask) != 0;
  }

  //
  //	Retrieve a given component pool at an Index in m_view_pools
  //
  template <size_t Index> SparseSet *getPool() { return m_viewPools[Index]; }

  template <typename Component>
  inline Component &processPool(SparseSet *s, Entity entity)
  {
    return s->fetchComponent<Component>(entity);
  }
  template <std::size_t... Is>
  auto componentTuple(Entity entity, std::index_sequence<Is...>)
  {
    return std::forward_as_tuple(
        processPool<Components>(m_viewPools[Is], entity)...);
  }

  /*
   *  Provided the function arguments are valid, this function will iterate over
   * the smallest pool and run the lambda on all entities that contain all the
   * components in the view.
   *
   *  Note: This is the internal implementation: opt for the more user friendly
   * functional ones in the public interface.
   */
  void forEachImpl(ForEachFunc &&func)
    requires(std::is_invocable_v<ForEachFunc, Components &...>())
  {
    constexpr auto inds = std::index_sequence_for<Components...>{};
    // Iterate smallest component pool and compare against other pools in view
    // Note this list is a COPY, allowing safe deletion during iteration.
    for (Entity entity : m_smallest->getEntities()) {
      if (allContain(entity)) {
        std::apply(func, componentTuple(entity, inds));
      }
    }
  }
  /* Same as forEachImpl but with entity as the first argument  */
  void forEachWithEntityImpl(ForEachFuncWithEntity &&func)
    requires(
        std::is_invocable_v<ForEachFuncWithEntity, Entity, Components &...>())
  {
    constexpr auto inds = std::index_sequence_for<Components...>{};
    // Iterate smallest component pool and compare against other pools in view
    // Note this list is a COPY, allowing safe deletion during iteration.
    for (Entity entity : m_smallest->getEntities()) {
      if (allContain(entity) && !IsExcluded(entity)) {
        std::apply(func, std::tuple_cat(std::make_tuple(entity),
                                        componentTuple(entity, inds)));
      }
    }
  }
};
} // namespace reg
