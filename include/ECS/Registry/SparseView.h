#pragma once
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

template <typename... Components> class View
{
  using ForEachFunc = std::function<void(Components &...)>;
  using ForEachFuncWithEntity = std::function<void(Entity, Components &...)>;

public:
  SimpleView(ECS *ecs)
      : m_view_pools{ecs->get_component_pool_ptr<Components>()...}
  {
    SEECS_ASSERT(ComponentTypes::size == m_view_pools.size(),
                 "Component type list and pool array size mismatch");

    auto smallest_pool = std::min_element(
        m_view_pools.begin(), m_view_pools.end(),
        [](SparseSet *a, SparseSet *b) { return a->size() < b->size(); });

    SEECS_ASSERT(smallest_pool != m_view_pools.end(),
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
  bool all_contain(Entity entity)
  {
    return std::all_of(
        m_viewPools.begin(), m_viewPools.end(),
        [entity](SparseSet *pool) { return pool->contains_index(entity); });
  }
  bool not_excluded(Entity entity_index)
  {
    if (m_excluded_pools.empty())
      return true;

    return std::none_of(m_excluded_pools.begin(), m_excluded_pools.end(),
                        [entity_index](SparseSet *pool) {
                          return pool->contains_index(entity_index);
                        });
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
      if (all_contain(entity)) {
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
      if (all_contain(entity) && not_excluded(entity)) {
        std::apply(func, std::tuple_cat(std::make_tuple(entity),
                                        componentTuple(entity, inds)));
      }
    }
  }
};
} // namespace reg
