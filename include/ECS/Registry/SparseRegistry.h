#pragma once
#include "Bitmask.h"
#ifndef NDEBUG
#include "Core.h"
#include "CoreFiles/LogWrapper.h"
#endif
#include "Entity.h"
#include "SparseSet.h"
#include <queue>

namespace reg
{

template <typename ComponentManagerT>
  requires(is_compile_time_bitmask_v<ComponentManagerT>)
class SparseRegistry
{
private:
  template <typename...> friend class SimpleView;

  //
  // List of EntityIndexes already created, but no longer in use; to be recycled
  //
  std::queue<Entity> m_availableEntities = {};

  //
  // Maps Entity -> Bitmask (1:1)
  // - Make sure info here is needed per-entity; if data is sparse, use a
  // SparseSet for that info instead.
  //
  std::vector<Bitmask> m_records;

  // Holds generic pointers to specific component sparse sets.
  std::map<Bitmask, SparseSet> m_sparseSets;

  //
  // Highest recorded EntityIndexm should not exceed MAX_ENTITIES
  //
  std::int32_t numberOfEntities = Entity{-1};

public:
  SparseRegistry() : m_sparseSets() {};
  Entity createEntity(Bitmask bitmask = reg::Bitmask{-1})
  {
    Entity id = Entity{-1};
    if (m_availableEntities.empty()) {
      id = Entity(++numberOfEntities);
      addRecord(bitmask);
    } else {
      id = m_availableEntities.back();
      m_availableEntities.pop();
    }
    return id;
  }
  // ---------------------------------------------------- //
  // create
  // ---------------------------------------------------- //
  // Add an entity with N different components, passing using N arguments
  // This is useful to create all components at once, instead of one by one with
  // createComponent (singular)
  template <ECSComponent... Components>
  std::tuple<Components &...> createComponents(Entity entity,
                                               Components &&...comps)
  {
    Bitmask bitMask = getMultipleBitmask<Components...>();

    P_ASSERT(m_records.at(entity) == bitMask || m_records.at(entity) == -1,
             "Component reset isn't implemented yet. TODO: need to remove "
             "component of old archetype");

    // Insert each component into its corresponding sparse set.
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (std::get<Is>(m_sparseSets[getSingleBitmask<Components>()])
           .pushComponent(entity, std::forward<Components>(comps)),
       ...);
    }(std::index_sequence_for<Components...>{});

    updateRecord(entity, bitMask);

    // Return references to the inserted components.
    return std::tie(
        std::get<SparseSet &>(m_sparseSets[getBitmask<Components>()])
            .template fetchComponent<Components>(entity)...);
  }
  //
  // Attaches a component to an entity
  // Old add
  template <ECSComponent Component>
  Component &createComponent(Entity entity, Component &&comp)
  {
    Bitmask &targetBitMask = getEntityBitmask(entity);
    SparseSet &sparseSet = m_sparseSets[targetBitMask];

    sparseSet.pushComponent<Component>(entity, std::forward<Component>(comp));
    updateRecord(entity, targetBitMask);
  }

  // ==================================================== //
  // get
  // ==================================================== //
  //
  // Retrieves a reference to the specified component for the given entity
  //
  // Usage: Transform& transform = ecs.get<Transform>(player);
  //
  template <typename T> T &getComponent(Entity entity)
  {
    Bitmask &targetBitMask = getEntityBitmask(entity);
    SparseSet &sparseSet = m_sparseSets[targetBitMask];
    return sparseSet.fetchComponent<T>(entity);
  }
  template <ECSComponent... TargetComponents>
  std::tuple<TargetComponents &...> getComponents(Entity entity)
  {
    static_assert(hasAllBitmask<TargetComponents...>(),
                  "When using getComponent some component didn't exist inside  "
                  "the CompileTimeBitMask");
    P_ASSERT((m_records[entity.size_type()] &
              getMultipleBitmask<TargetComponents...>()) ==
                 getMultipleBitmask<TargetComponents...>(),
             "Entity {} does not have all components requested", entity);
    return std::tie(
        std::get<SparseSet &>(m_sparseSets[getBitmask<TargetComponents>()])
            .template fetchComponent<TargetComponents>(entity)...);
  }
  template <ECSComponent... TargetComponents>
  std::tuple<const TargetComponents &...> getComponents(Entity entity) const
  {
    static_assert(hasAllBitmask<TargetComponents...>(),
                  "When using getComponent some component didn't exist inside  "
                  "the CompileTimeBitMask");
    P_ASSERT((m_records[entity.size_type()] &
              getMultipleBitmask<TargetComponents...>()) ==
                 getMultipleBitmask<TargetComponents...>(),
             "Entity {} does not have all components requested", entity);
    return std::tie(std::get<const SparseSet &>(
                        m_sparseSets[getBitmask<TargetComponents>()])
                        .template fetchComponent<TargetComponents>(entity)...);
  }
  // ==================================================== //
  // add
  // ==================================================== //
  // Expand the components of an entity, changing its "type". This is to make
  // the objects more flexible but can also be used as a feature of the engine.
  template <ECSComponent... Components>
  std::tuple<Components &...> addComponents(Entity entity,
                                            Components &&...comps)
  {
    const Bitmask oldBitmask = m_records[entity.size_type()];
    const Bitmask newBitMask = getMultipleBitmask<Components...>();

    // De Morgan's Law (intersection, A^~B, ~A^B)
    // const Bitmask componentsToUpdate = oldBitmask & newBitMask;
    // const Bitmask newComponents = newBitMask & ~oldBitmask;
    const Bitmask removables = oldBitmask & ~newBitMask;

    auto processComponent = [&]<ECSComponent C>(C &&comp) {
      if ((getSingleBitmask<C>() & removables) != 0) {
        m_sparseSets[getSingleBitmask<C>()].remove(entity);
      } else {
        m_sparseSets[getSingleBitmask<C>()].pushComponent(
            entity, std::forward<C>(comp));
      }
    };

    (processComponent(std::forward<Components>(comps)), ...);

    return std::tie(
        std::get<SparseSet &>(m_sparseSets[getBitmask<Components>()])
            .template fetchComponent<Components>(entity)...);
  }

  /// Assuming bitmask is known, you can manually push components into the
  /// archetype. Only use this for binding outside c++, like lua
  template <ECSComponent C>
  void manualPush(Entity entity, Bitmask bitmask, C &&comp)
  {
    P_ASSERT((bitmask & getSingleBitmask<C>()) == getSingleBitmask<C>(),
             "manual push require the same bitmasks");
    m_sparseSets[getSingleBitmask<C>()].pushComponent(entity,
                                                      std::forward<C>(comp));
  }

public:
  // ==================================================== //
  // iterate chunks
  // ==================================================== //

public:
  // ==================================================== //
  // has
  // ==================================================== //
  // Are all components inside the targetBitMask?
  template <ECSComponent... ObjectComponents>
  constexpr bool containsAll(Entity entity) const
  {
    Bitmask targetBitMask = m_records[entity.size_type()];
    int objectBitMask = getMultipleBitmask<ObjectComponents...>();
    return (targetBitMask | objectBitMask) == objectBitMask;
  }
  // Targeted has at least one component that a specific archetype also has
  template <ECSComponent... TargetComponents> bool hasAny(Entity entity) const
  {
    Bitmask specificBitMask = m_records[entity.size_type()];
    if constexpr (sizeof...(TargetComponents) == 1) {
      int targetBitMask = getSingleBitmask<TargetComponents...>();
      return (targetBitMask & specificBitMask) != 0;
    } else {
      int targetBitMask = getMultipleBitmask<TargetComponents...>();
      return (targetBitMask & specificBitMask) != 0;
    }
  }

  //
  // Queries whether the entity is currently 'alive' and can be operated on by
  // the ECS.
  //
  // It does this by comparing the version encoded in the Entity ID with the
  // latest recorded version stored in m_entity_info
  //
  bool is_alive(Entity entity) const
  {
    return m_records[entity.size_type()] != -1;
  }
  // ==================================================== //
  // remove
  // ==================================================== //

  // Deletes an active entity and its associated components.
  void remove(Entity &entity)
  {
    Bitmask &targetBitMask = getEntityBitmask(entity);
    SparseSet &sparseSet = m_sparseSets.at(targetBitMask);

    sparseSet.remove(entity);
    m_records[entity.size_type()] = Bitmask{-1};
    m_availableEntities.push(entity);
  }

  // ==================================================== //
  // iterate chunks
  // ==================================================== //

private:
  // Bitmask related
  template <ECSComponent... Components>
  static constexpr Bitmask getMultipleBitmask()
  {
    return ComponentManagerT::template multiComponentBitmask<Components...>();
  }
  template <ECSComponent Component> static constexpr Bitmask getSingleBitmask()
  {
    return ComponentManagerT::template singleComponentBitmask<Component>();
  }
  template <ECSComponent... Component> static constexpr bool hasAllBitmask()
  {
    return ComponentManagerT::template allRegistered<Component...>();
  }
  template <ECSComponent Component> static constexpr bool hasBitmask()
  {
    return ComponentManagerT::template isRegistered<Component>();
  }

  // Record related
  void addRecord(Bitmask bitmask) { m_records.emplace_back(bitmask); }
  void updateRecord(Entity entity, Bitmask bitmask)
  {
    m_records[static_cast<unsigned>(entity)] = {bitmask};
  }
  Bitmask &getEntityBitmask(Entity entity)
  {
    return m_records[entity.size_type()];
  }
};

} // namespace reg
