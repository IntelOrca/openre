#pragma once

#include "ecs_components.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace openre::ecs
{
    /// Field type of a runtime (Lua-defined) component. Mirrors the four
    /// primitive types the Lua ECS supports.
    enum class RuntimeFieldType : uint8_t
    {
        Int = 0,
        Float = 1,
        Bool = 2,
        String = 3,
    };

    /// One field of a runtime component definition.
    struct RuntimeFieldDef
    {
        std::string name;
        RuntimeFieldType type = RuntimeFieldType::Int;
    };

    /// A typed value stored in a runtime component row.
    using RuntimeValue = std::variant<int32_t, float, bool, std::string>;

    /// A registered runtime component type: ordered fields + default values.
    struct RuntimeComponentDef
    {
        std::string name;
        std::vector<RuntimeFieldDef> fields;
        std::vector<RuntimeValue> defaults;
    };

    /// One runtime component row stored on a Scene entity: field values in
    /// definition order plus an insertion sequence number.
    struct RuntimeRow
    {
        std::vector<RuntimeValue> values;
        uint64_t seq = 0;
    };

    /// Registers a runtime component definition in the global registry.
    /// Returns false when a definition with this name already exists.
    bool defineRuntimeComponent(RuntimeComponentDef def);
    /// Looks up a registered runtime component definition, or nullptr.
    const RuntimeComponentDef* findRuntimeComponent(std::string_view name);
    /// Registered definitions in registration order.
    const std::vector<RuntimeComponentDef*>& runtimeComponents();
    /// Drops every runtime component definition (rows live on Scene and are
    /// dropped with it).
    void clearRuntimeComponents();
    /// Monotonic counter shared by every component_type_id<T> instantiation,
    /// so each distinct component type gets a unique id.
    inline uint32_t next_component_type_id()
    {
        static uint32_t next = 0;
        return next++;
    }

    /// Compile-time unique id for component type T.
    template<typename T> uint32_t component_type_id()
    {
        static uint32_t id = next_component_type_id();
        return id;
    }

    /// The ECS world: owns all entities and their components.
    ///
    /// Storage is a sparse set per component type, so components of different
    /// types never share memory and iterating over one type only touches its
    /// own dense array. Every entity has exactly one TransformComponent
    /// (managed by the scene), and at most one of every other component type.
    class Scene
    {
    public:
        /// The strict top-level sub-roots of the scene, all children of the
        /// hidden root. Each is a fixed container the engine and Lua organise
        /// content under.
        enum class RootKind : uint32_t
        {
            User = 0,    // what Lua game/plugin code has access to
            Game = 1,    // contains the legacy game entity
            Movie = 2,   // for the video component entity
            Fade = 3,    // for the fade black overlay
            Overlay = 4, // mostly for debug overlay text
        };

        Scene();
        ~Scene();

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        // ── Entities ─────────────────────────────────────────────────────
        /// Creates an entity as a child of `parent` (the scene root when
        /// NullEntity). The entity always receives a TransformComponent.
        Entity createEntity(Entity parent = NullEntity);
        /// Destroys `entity` and its whole subtree, removing every component.
        void destroyEntity(Entity entity);
        /// Reparents `entity` (and its subtree) under `parent`.
        void setParent(Entity entity, Entity parent);
        /// Destroys every entity except the hidden root.
        void clear();

        /// The hidden root entity; everything is transitively its child.
        Entity root() const
        {
            return m_root;
        }
        /// Returns one of the five strict top-level sub-roots (User, Game, Movie,
        /// Fade, Overlay). The root is created lazily on first access and given
        /// its canonical name, so this is safe to call even after clear().
        Entity getRoot(RootKind kind);
        /// Whether `entity` is a live handle in this scene.
        bool isValid(Entity entity) const;

        // ── Components ───────────────────────────────────────────────────
        /// Adds a new component of type T to `entity`. Asserts when the entity
        /// already has one (one component of each type per entity).
        template<typename T> T& addComponent(Entity entity)
        {
            static_assert(!std::is_same_v<T, TransformComponent>, "TransformComponent is managed by the scene");
            assert(isValid(entity));
            return storage<T>().add(entity);
        }

        /// Returns the component of type T on `entity`. Asserts when absent.
        template<typename T> T& getComponent(Entity entity)
        {
            assert(isValid(entity));
            return storage<T>().get(entity);
        }

        /// Returns the component of type T on `entity`, or nullptr when absent.
        template<typename T> T* tryGetComponent(Entity entity)
        {
            if (!isValid(entity))
                return nullptr;
            return storage<T>().tryGet(entity);
        }

        /// Whether `entity` has a component of type T.
        template<typename T> bool hasComponent(Entity entity)
        {
            if (!isValid(entity))
                return false;
            return storage<T>().contains(entity);
        }

        /// Removes the component of type T from `entity`. The transform cannot
        /// be removed (destroy the entity instead).
        template<typename T> void removeComponent(Entity entity)
        {
            static_assert(!std::is_same_v<T, TransformComponent>, "TransformComponent is managed by the scene");
            storage<T>().remove(entity);
        }

        /// Calls fn(entity, component&) for every entity with a T component.
        template<typename T, typename Fn> void each(Fn&& fn)
        {
            storage<T>().each(std::forward<Fn>(fn));
        }

        /// Calls fn(entity) for every live entity in the scene (including the
        /// hidden root). Used by the debug web UI to enumerate the tree.
        template<typename Fn> void eachEntity(Fn&& fn) const
        {
            for (uint32_t i = 0; i < m_records.size(); i++)
            {
                if (m_records[i].alive)
                    fn(make_entity(i, m_records[i].generation));
            }
        }

        // ── Runtime components ───────────────────────────────────────────
        // Lua-defined component types (see the registry above and the Lua ECS
        // in games/core/script/ecs.lua). Rows are typed field vectors keyed by
        // (component name, entity index); a per-row sequence number keeps
        // iteration in creation order. Accessors take full Entity handles and
        // validate them, so recycled slots can never alias a stale row.

        /// Adds a default-initialised runtime component row to `entity`.
        /// Returns false when the definition is unknown or the entity already
        /// has the component.
        bool addRuntimeComponent(Entity entity, const std::string& name);
        /// Removes the runtime component from `entity` (no-op when absent).
        void removeRuntimeComponent(Entity entity, const std::string& name);
        /// The runtime component row on `entity`, or nullptr when absent (or
        /// the entity is not live).
        RuntimeRow* tryGetRuntimeComponent(Entity entity, const std::string& name);
        /// Calls fn(entity, row&) for every entity carrying the named runtime
        /// component, oldest first.
        template<typename Fn> void eachRuntimeComponent(const std::string& name, Fn&& fn)
        {
            auto it = m_runtimeComponents.find(name);
            if (it == m_runtimeComponents.end())
                return;
            std::vector<std::pair<Entity, RuntimeRow*>> ordered;
            for (auto& [index, row] : it->second)
            {
                Entity entity = make_entity(index, m_records[index].generation);
                if (isValid(entity))
                    ordered.emplace_back(entity, &row);
            }
            std::sort(
                ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second->seq < b.second->seq; });
            for (auto& [entity, row] : ordered)
                fn(entity, *row);
        }
        /// Names of the runtime components present on `entity`, in the order
        /// their definitions were registered.
        std::vector<std::string> runtimeComponentsOf(Entity entity) const;

        /// The first live entity carrying a T component, or the null entity.
        template<typename T> Entity find()
        {
            Entity result = NullEntity;
            each<T>([&](Entity entity, T&) {
                if (result == NullEntity)
                    result = entity;
            });
            return result;
        }

    private:
        struct Record
        {
            uint32_t generation = 0;
            bool alive = false;
        };

        struct StorageBase
        {
            virtual ~StorageBase() = default;
            virtual void removeEntity(uint32_t index) = 0;
        };

        template<typename T> class ComponentStorage final : public StorageBase
        {
        public:
            T& add(Entity entity)
            {
                uint32_t index = entity_index(entity);
                if (m_sparse.size() <= index)
                    m_sparse.resize(index + 1, 0);
                assert(m_sparse[index] == 0 && "entity already has this component");
                m_sparse[index] = (uint32_t)m_dense.size() + 1;
                m_denseEntity.push_back(entity);
                m_dense.push_back(T{});
                return m_dense.back();
            }

            T& get(Entity entity)
            {
                T* component = tryGet(entity);
                assert(component != nullptr && "entity does not have this component");
                return *component;
            }

            T* tryGet(Entity entity)
            {
                uint32_t index = entity_index(entity);
                if (index >= m_sparse.size())
                    return nullptr;
                uint32_t slot = m_sparse[index];
                if (slot == 0)
                    return nullptr;
                return &m_dense[slot - 1];
            }

            bool contains(Entity entity)
            {
                return tryGet(entity) != nullptr;
            }

            void remove(Entity entity)
            {
                uint32_t index = entity_index(entity);
                if (index >= m_sparse.size())
                    return;
                uint32_t slot = m_sparse[index];
                if (slot == 0)
                    return;
                // Swap-remove with the dense tail, then fix the moved entry's
                // sparse slot.
                uint32_t last = (uint32_t)m_dense.size() - 1;
                if (slot - 1 != last)
                {
                    m_dense[slot - 1] = std::move(m_dense[last]);
                    m_denseEntity[slot - 1] = m_denseEntity[last];
                    m_sparse[entity_index(m_denseEntity[last])] = slot;
                }
                m_dense.pop_back();
                m_denseEntity.pop_back();
                m_sparse[index] = 0;
            }

            void removeEntity(uint32_t index) override
            {
                if (index >= m_sparse.size() || m_sparse[index] == 0)
                    return;
                remove(make_entity(index, 0));
            }

            template<typename Fn> void each(Fn&& fn)
            {
                for (size_t i = 0; i < m_dense.size(); i++)
                    fn(m_denseEntity[i], m_dense[i]);
            }

        private:
            std::vector<T> m_dense;
            std::vector<Entity> m_denseEntity;
            std::vector<uint32_t> m_sparse;
        };

        template<typename T> ComponentStorage<T>& storage()
        {
            uint32_t id = component_type_id<T>();
            auto it = m_storages.find(id);
            if (it == m_storages.end())
            {
                auto storage = std::make_unique<ComponentStorage<T>>();
                auto* raw = storage.get();
                m_storages.emplace(id, std::move(storage));
                return *raw;
            }
            return static_cast<ComponentStorage<T>&>(*it->second);
        }

        Entity allocEntity();
        void freeEntity(uint32_t index);

        void linkChild(Entity parent, Entity child);
        void unlinkChild(Entity parent, Entity child);
        void destroySubtree(Entity entity);

        std::vector<Record> m_records;
        std::vector<uint32_t> m_freeIndices;
        std::unordered_map<uint32_t, std::unique_ptr<StorageBase>> m_storages;
        std::unordered_map<std::string, std::unordered_map<uint32_t, RuntimeRow>> m_runtimeComponents;
        uint64_t m_runtimeSeq = 0;
        Entity m_root = NullEntity;
        Entity m_roots[5] = { NullEntity, NullEntity, NullEntity, NullEntity, NullEntity };
    };
}