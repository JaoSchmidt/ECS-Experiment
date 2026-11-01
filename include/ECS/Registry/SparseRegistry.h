#pragma once
#include "Bitmask.h"
#ifndef NDEBUG
#include "Core.h"
#include "CoreFiles/LogWrapper.h"
#endif
#include "Entity.h"
#include "ExcludeComponents.h"
#include "SparseSet.h"
#include "SparseView.h"
#include <map>
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
private:
  SparseSet *getSparseSet(Bitmask b)
  {
    auto it = m_sparseSets.find(b);
    if (it != m_sparseSets.end()) {
      return &it->second;
    }
    return nullptr;
  }
  template <ECSComponent C> SparseSet &getOrCreateSparseSet()
  {
    Bitmask bm = getSingleBitmask<C>();
    return m_sparseSets.try_emplace(bm, std::type_identity<C>{}).first->second;
  }
  template <ECSComponent C>
  inline SparseSet &insertComp(Entity entity, C &&comp)
  {
    SparseSet &s = getOrCreateSparseSet<C>();
    s.pushComponent(entity, std::forward<C>(comp));
    return s;
  }

public:
  // Add an entity with N different components, passing using N arguments
  // This is useful to create all components at once, instead of one by one with
  // createComponent (singular)
  template <ECSComponent... Components>
  std::tuple<Components &...> createComponents(Entity entity,
                                               Components &&...comps)
  {
    constexpr Bitmask bitmask = getMultipleBitmask<Components...>();
    // constexpr auto inds = std::index_sequence_for<Components...>{};

    P_ASSERT(m_records.at(entity) == bitmask || m_records.at(entity) == -1,
             "Component reset isn't implemented yet. TODO: need to remove "
             "component of old archetype");

    // Insert each component into its corresponding sparse set.
    (insertComp(entity, std::forward<Components>(comps)), ...);

    updateRecord(entity, bitmask);

    // Return references to the inserted components.
    return std::tie<Components &...>(
        m_sparseSets.at(getSingleBitmask<Components>())
            .template fetchComponent<Components>(entity)...);
  }
  //
  // Create a single component attached into an entity
  // Old add
  template <ECSComponent Component>
  Component &createComponent(Entity entity, Component &&comp)
  {
    SparseSet &s = insertComp(entity, std::forward<Component>(comp));
    updateRecord(entity, getSingleBitmask<Component>());
    return s.fetchComponent<Component>(entity);
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
    P_ASSERT(
        m_records.at(entity) == getSingleBitmask<T>(),
        "in getComponent() record bitmask and asked bitmask are different");

    SparseSet &s = getOrCreateSparseSet<T>();
    return s.fetchComponent<T>(entity);
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
    return std::tie(m_sparseSets.at(getSingleBitmask<TargetComponents>())
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
    return std::tie(m_sparseSets.at(getSingleBitmask<TargetComponents>())
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
        getOrCreateSparseSet<C>().remove(entity);
      } else {
        getOrCreateSparseSet<C>().pushComponent(entity, std::forward<C>(comp));
      }
    };

    (processComponent(std::forward<Components>(comps)), ...);

    return std::tie(
        std::get<SparseSet &>(m_sparseSets.at(getSingleBitmask<Components>()))
            .template fetchComponent<Components>(entity)...);
  }

  /// Assuming bitmask is known, you can manually push components into the
  /// archetype. Only use this for binding outside c++, like lua
  template <ECSComponent C>
  void manualPush(Entity entity, Bitmask bitmask, C &&comp)
  {
#ifdef NDEBUG
    UNUSED(bitmask)
#endif
    P_ASSERT((bitmask & getSingleBitmask<C>()) == getSingleBitmask<C>(),
             "manual push require the same bitmasks");
    getOrCreateSparseSet<C>().pushComponent(entity, std::forward<C>(comp));
  }

  // ==================================================== //
  // iterate chunks
  // ==================================================== //
private:
  template <ECSComponent... Cs, std::size_t... Is>
  inline void createConstArr(std::array<const SparseSet *, sizeof...(Cs)> &arr,
                             std::index_sequence<Is...>)
  {
    ((arr[Is] =
          &getOrCreateSparseSet<std::tuple_element_t<Is, std::tuple<Cs...>>>()),
     ...);
  }
  template <ECSComponent... Cs, std::size_t... Is>
  inline void createArr(std::array<SparseSet *, sizeof...(Cs)> &arr,
                        std::index_sequence<Is...>)
  {
    ((arr[Is] =
          &getOrCreateSparseSet<std::tuple_element_t<Is, std::tuple<Cs...>>>()),
     ...);
  }

public:
  template <ECSComponent... Components, ECSComponent... ExcludeComponents>
  View<Components...> view(exclude_t<ExcludeComponents...> = {})
  {
    Bitmask excludeBitMask = getMultipleBitmask<ExcludeComponents...>();
    Bitmask requiredBitMask = getMultipleBitmask<Components...>();
    // for each component, extract the target sparse set

    constexpr auto indexes = std::index_sequence_for<Components...>{};
    std::array<SparseSet *, sizeof...(Components)> arr;
    createArr<Components...>(arr, indexes);

    return View<Components...>(m_records, std::move(arr), requiredBitMask,
                               excludeBitMask);
  }
  template <ECSComponent... Components, ECSComponent... ExcludeComponents>
  View<const Components...> viewConst(exclude_t<ExcludeComponents...> = {})
  {
    Bitmask excludeBitMask = getMultipleBitmask<ExcludeComponents...>();
    Bitmask requiredBitMask = getMultipleBitmask<Components...>();
    // for each component, extract the target sparse set

    constexpr auto indexes = std::index_sequence_for<Components...>{};
    std::array<const SparseSet *, sizeof...(Components)> arr;
    createConstArr<Components...>(arr, indexes);

    return View<const Components...>(m_records, std::move(arr), requiredBitMask,
                                     excludeBitMask);
  }

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
    return (targetBitMask & objectBitMask) == objectBitMask;
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
    for (Bitmask i = Bitmask{1}; i.value < getMaxComp();
         i.value = i.value << 1) {
      SparseSet *sparseSet = getSparseSet(i);
      if (sparseSet)
        sparseSet->remove(entity);
    }

    m_records[entity.size_type()] = Bitmask{-1};
    m_availableEntities.push(entity);
  }

  // ==================================================== //
  // iterate chunks
  // ==================================================== //

private:
  // Bitmask related
  static constexpr std::size_t getMaxComp()
  {
    return ComponentManagerT::getNumberOfRegisteredComponents();
  }
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
