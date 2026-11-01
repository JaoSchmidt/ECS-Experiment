#pragma once
#include "ECS/Concepts.h"
#include "ECS/Entity.h"
#include "SparseSet.h"
#include <algorithm>
#include <type_traits>
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

template <typename... Components> class View
{
  using ForEachFunc = std::function<void(Components &...)>;
  using ForEachFuncWithEntity = std::function<void(Entity, Components &...)>;

  static constexpr bool isConstView =
      (std::is_const_v<std::remove_reference_t<Components>> && ...);
  using PoolType =
      std::conditional_t<isConstView, const SparseSet *, SparseSet *>;

public:
  View(const std::vector<Bitmask> &records,
       std::array<PoolType, sizeof...(Components)> &&arr,
       Bitmask requiredBitMask, Bitmask excludeBitMask)
      : m_viewPools{std::move(arr)},      //
        m_records(records),               //
        m_excludeBitMask(excludeBitMask), //
        m_bitmask(requiredBitMask)
  {
    P_ASSERT(sizeof...(Components) == m_viewPools.size(),
             "Component type list and pool array size mismatch");

    auto smallest_pool =
        std::min_element(m_viewPools.begin(), m_viewPools.end(),
                         [](auto a, auto b) { return a->size() < b->size(); });

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
  void forEach(ForEachFunc func) { forEachImpl(std::move(func)); }

  void forEach(ForEachFuncWithEntity func)
  {
    forEachWithEntityImpl(std::move(func));
  }

private:
  // Stores component pools associated with the view via their component index
  // from ComponentTypes
  std::array<PoolType, sizeof...(Components)> m_viewPools;
  const std::vector<Bitmask> &m_records;
  Bitmask m_excludeBitMask = Bitmask{-1};
  // Union of the bitmasks of every component in this view. Passed in by the
  // registry, which is the only place that knows how a component type maps to
  // a bit. allContain() tests against this, so without it a view would only
  // match entities that own every component registered in the manager.
  Bitmask m_bitmask = Bitmask{-1};
  //
  // Pointer to the smallest component pool that we use as our basis for
  // iteration
  //
  PoolType m_smallest = nullptr;

  //
  // Returns true iff all the pools in the view contain the given EntityIndex
  //
  inline bool allContain(Entity entity)
  {
    return (m_records[entity] & m_bitmask) == m_bitmask;
  }
  inline bool IsExcluded(Entity entity)
  {
    return (m_records[entity] & m_excludeBitMask) != 0;
  }

  //
  //	Retrieve a given component pool at an Index in m_view_pools
  //
  template <size_t Index> auto getPool() { return m_viewPools[Index]; }

  template <typename Component, typename PoolT>
  inline auto &processPool(PoolT *s, Entity entity)
  {
    if constexpr (std::is_const_v<Component>)
      return s->template fetchComponent<std::remove_const_t<Component>>(entity);
    else
      return s->template fetchComponent<Component>(entity);
  }
  template <std::size_t... Is>
  auto componentTuple(Entity entity, std::index_sequence<Is...>)
  {
    using RawTuple = std::tuple<Components...>;
    return std::forward_as_tuple(
        processPool<std::tuple_element_t<Is, RawTuple>>(m_viewPools[Is],
                                                        entity)...);
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
    requires(std::is_invocable_v<ForEachFunc, Components &...>)
  {
    constexpr auto inds = std::index_sequence_for<Components...>{};
    // Iterate smallest component pool and compare against other pools in view
    // Note this list is a COPY, allowing safe deletion during iteration.
    for (Entity entity : m_smallest->getEntities()) {
      if (allContain(entity) && !IsExcluded(entity)) {
        std::apply(func, componentTuple(entity, inds));
      }
    }
  }
  /* Same as forEachImpl but with entity as the first argument  */
  void forEachWithEntityImpl(ForEachFuncWithEntity &&func)
    requires(
        std::is_invocable_v<ForEachFuncWithEntity, Entity, Components &...>)
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
