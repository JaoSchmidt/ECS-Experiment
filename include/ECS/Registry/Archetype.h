/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "Core.h"
#include "CoreFiles/LogWrapper.h"
#include "ECS/Registry/Entity.h"
#include <exception>
#include <typeindex>

namespace reg
{

// ---------------------------------------------------- //
// Container definition
// ---------------------------------------------------- //
class Archetype
{
public:
  using Deleter = void (*)(void *);
  using ErasedVector = std::unique_ptr<void, Deleter>;
  std::vector<std::function<Row(Row)>> m_removers;
  std::vector<reg::Entity> m_entities;

private:
  std::map<std::type_index, ErasedVector> m_componentMap;

public:
  // ---------------------------------------------------- //
  // create
  // ---------------------------------------------------- //

  // either finds or create a new component
  template <typename C> std::vector<C> &createComponent()
  {
    auto it = m_componentMap.find(std::type_index(typeid(C)));
    if (it != m_componentMap.end())
      return *static_cast<std::vector<C> *>(it->second.get());

    auto deleter = [](void *vector) {
      delete static_cast<std::vector<C> *>(vector);
    };
    m_removers.push_back(
        [this](reg::Row row) { return removeFromComponent<C>(row); });
    auto [newIt, isInserted] =
        m_componentMap.emplace(std::type_index(typeid(C)),
                               ErasedVector{new std::vector<C>(), deleter});
    P_ASSERT(isInserted, "Could not create new component vector");
    PLOG_I("New component bitmask added {}", typeid(C).name());

    // store the deleter to use inside the destructor
    return *static_cast<std::vector<C> *>(newIt->second.get());
  }
  template <typename... Components> Row pushComponents(Components &&...comps)
  {
    Row row = Row{-1};
    (...,
     (row = pushComponent<Components>(
          std::forward<Components>(comps)))); // and this should create its rows
    return row;
  }
  template <typename... Components>
  Row pushComponents(reg::Entity entity, Components &&...comps)
  {
    Row row = Row{-1};
    (...,
     (row = pushComponent<Components>(
          std::forward<Components>(comps)))); // and this should create its rows
    m_entities.emplace_back(entity);
    return row;
  }
  // directly add the component to the archetype, should be used N time per
  // entity, with N being the entity's number of components
  template <typename C, typename... Args> Row pushComponent(Args &&...args)
  {
    std::vector<C> &componentArray = createComponent<C>();
    Row index = Row(static_cast<int>(componentArray.size()));
    componentArray.emplace_back(std::forward<Args>(args)...);
    return index;
  }

  // ---------------------------------------------------- //
  // remove
  // ---------------------------------------------------- //

  std::pair<Row, Entity> remove(Row row)
  {
    Row lastRow = Row{-1};
    Entity swappedEntity = m_entities[m_entities.size() - 1];

    for (std::function<Row(Row)> &remover : m_removers) {
      lastRow = remover(row);
    }
    std::iter_swap(m_entities.begin() + row, m_entities.end() - 1);
    m_entities.pop_back();
    return std::make_pair(lastRow, swappedEntity);
  }

  template <typename... Components> std::pair<Row, Entity> remove(Row row)
  {
    static_assert(sizeof...(Components) != 0,
                  "Not allowed to remove from empty archetypes");
    Row lastRow = Row{-1};
    Entity swappedEntity = m_entities[m_entities.size() - 1];
    (..., (lastRow = removeFromComponent<Components>(row)));
    std::iter_swap(m_entities.begin() + row, m_entities.end() - 1);
    m_entities.pop_back();
    return std::make_pair(lastRow, swappedEntity);
  }

  // This is returning the swapped element's row. You need to use it to
  // update its entity record later
  template <typename C> Row removeFromComponent(Row index)
  {
    std::vector<C> &v = getComponent<C>();
    std::iter_swap(v.begin() + index, v.end() - 1);
    v.pop_back();
    return Row{static_cast<int>(v.size())};
  }

  // ---------------------------------------------------- //
  // get
  // ---------------------------------------------------- //
  // Extract a row of an archetype. I.e. the components of an entity
  template <typename... Components>
  std::tuple<const Components &...> extractRow(Row row) const
  {
    return std::tie<const Components &...>(fetchComponent<Components>(row)...);
  };
  template <typename... Components>
  std::tuple<Components &...> extractRow(Row row)
  {
    return std::tie<Components &...>(fetchComponent<Components>(row)...);
  };

  template <typename T> const T &fetchComponent(Row entityIndex) const
  {
    const std::vector<T> &componentArray = getComponent<T>();
    P_ASSERT((unsigned)entityIndex < componentArray.size(),
             "Entity index {} out of range on array of size {}",
             entityIndex.value, componentArray.size());
    return componentArray[static_cast<unsigned>(entityIndex)];
  }

  template <typename T> T &fetchComponent(Row entityIndex)
  {
    std::vector<T> &componentArray = getComponent<T>();
    P_ASSERT((unsigned)entityIndex < componentArray.size(),
             "Entity index {} out of range on array of size {}",
             entityIndex.value, componentArray.size());
    return componentArray[static_cast<unsigned>(entityIndex)];
  }

  template <typename C> const std::vector<C> &getComponent() const
  {
    auto it = m_componentMap.find(std::type_index(typeid(C)));
    if (it == m_componentMap.end()) {
      PLOG_E("Cannot find component vector inside Archetype");
      PLOG_E("You are probably trying to call getComponent but the component "
             "you want doesn't exist in the object.");
      PLOG_E("Missing component type: {}", typeid(C).name());
      std::terminate();
    }
    return *static_cast<const std::vector<C> *>(it->second.get());
  }

  template <typename C> std::vector<C> &getComponent()
  {
    auto it = m_componentMap.find(std::type_index(typeid(C)));
    if (it == m_componentMap.end()) {
      PLOG_E("Cannot find component vector inside Archetype");
      PLOG_E("You are probably trying to call getComponent but the component "
             "you want doesn't exist in the object.");
      PLOG_E("Missing component type: {}", typeid(C).name());
      std::terminate();
    }
    return *static_cast<std::vector<C> *>(it->second.get());
  }
  Row lastRow() const { return Row{static_cast<int>(m_entities.size())}; }
  reg::Entity getEntity(Row row) const
  {
    return m_entities[static_cast<unsigned>(row)];
  }
};

} // namespace reg
