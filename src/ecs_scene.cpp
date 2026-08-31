#include "ecs_scene.h"

#include <algorithm>
#include <cassert>

namespace openre::ecs
{
    namespace
    {
        // Global registry of runtime component definitions. Only the active
        // game module registers definitions (via the Lua ECS), so a single
        // global list is sufficient; Scene::clear() drops it alongside rows.
        // Indirect storage keeps definition pointers stable across
        // registrations (g_runtimeOrder hands out raw pointers).
        std::vector<std::unique_ptr<RuntimeComponentDef>> g_runtimeComponents;
        std::vector<RuntimeComponentDef*> g_runtimeOrder;
    }

    bool defineRuntimeComponent(RuntimeComponentDef def)
    {
        for (const auto& existing : g_runtimeComponents)
        {
            if (existing->name == def.name)
                return false;
        }
        g_runtimeOrder.push_back(g_runtimeComponents.emplace_back(std::make_unique<RuntimeComponentDef>(std::move(def))).get());
        return true;
    }

    const RuntimeComponentDef* findRuntimeComponent(std::string_view name)
    {
        for (const auto& def : g_runtimeComponents)
        {
            if (def->name == name)
                return def.get();
        }
        return nullptr;
    }

    const std::vector<RuntimeComponentDef*>& runtimeComponents()
    {
        return g_runtimeOrder;
    }

    void clearRuntimeComponents()
    {
        g_runtimeComponents.clear();
        g_runtimeOrder.clear();
    }

    namespace
    {
        constexpr const char* rootName(Scene::RootKind kind)
        {
            switch (kind)
            {
            case Scene::RootKind::User: return "user";
            case Scene::RootKind::Game: return "game";
            case Scene::RootKind::Overlay: return "overlay";
            }
            return "";
        }
    }

    Scene::Scene()
    {
        m_root = allocEntity();
        storage<TransformComponent>().add(m_root);

        // Create the three strict sub-roots beneath the hidden root.
        for (uint32_t i = 0; i < 3; i++)
        {
            auto kind = static_cast<RootKind>(i);
            Entity e = createEntity(m_root);
            getComponent<TransformComponent>(e).name = rootName(kind);
            m_roots[i] = e;
        }
    }

    Scene::~Scene() = default;

    Entity Scene::allocEntity()
    {
        uint32_t index;
        if (!m_freeIndices.empty())
        {
            index = m_freeIndices.back();
            m_freeIndices.pop_back();
        }
        else
        {
            index = (uint32_t)m_records.size();
            m_records.emplace_back();
        }
        auto& record = m_records[index];
        record.generation++;
        record.alive = true;
        return make_entity(index, record.generation);
    }

    void Scene::freeEntity(uint32_t index)
    {
        assert(index < m_records.size());
        auto& record = m_records[index];
        assert(record.alive);
        record.alive = false;
        for (auto& [id, storage] : m_storages)
            storage->removeEntity(index);
        for (auto& [name, rows] : m_runtimeComponents)
            rows.erase(index);
        m_freeIndices.push_back(index);
    }

    bool Scene::isValid(Entity entity) const
    {
        if (entity == NullEntity)
            return false;
        uint32_t index = entity_index(entity);
        if (index >= m_records.size())
            return false;
        const auto& record = m_records[index];
        return record.alive && record.generation == entity_generation(entity);
    }

    Entity Scene::createEntity(Entity parent)
    {
        if (parent == NullEntity)
            parent = m_root;
        assert(isValid(parent));
        Entity entity = allocEntity();
        storage<TransformComponent>().add(entity);
        linkChild(parent, entity);
        return entity;
    }

    void Scene::destroyEntity(Entity entity)
    {
        assert(isValid(entity) && entity != m_root);
        destroySubtree(entity);
    }

    Entity Scene::getRoot(RootKind kind)
    {
        const uint32_t index = static_cast<uint32_t>(kind);
        assert(index < 5);
        Entity& root = m_roots[index];
        if (root == NullEntity || !isValid(root))
        {
            root = createEntity(m_root);
            getComponent<TransformComponent>(root).name = rootName(kind);
        }
        return root;
    }

    void Scene::destroySubtree(Entity entity)
    {
        auto* transform = tryGetComponent<TransformComponent>(entity);
        if (transform != nullptr)
        {
            Entity child = transform->firstChild;
            while (child != NullEntity)
            {
                Entity next = getComponent<TransformComponent>(child).nextSibling;
                destroySubtree(child);
                child = next;
            }
            if (transform->parent != NullEntity)
                unlinkChild(transform->parent, entity);
        }
        freeEntity(entity_index(entity));
    }

    void Scene::linkChild(Entity parent, Entity child)
    {
        auto& childTransform = getComponent<TransformComponent>(child);
        childTransform.parent = parent;
        childTransform.nextSibling = NullEntity;

        auto& parentTransform = getComponent<TransformComponent>(parent);
        if (parentTransform.firstChild == NullEntity)
        {
            parentTransform.firstChild = child;
            return;
        }
        Entity tail = parentTransform.firstChild;
        while (getComponent<TransformComponent>(tail).nextSibling != NullEntity)
            tail = getComponent<TransformComponent>(tail).nextSibling;
        getComponent<TransformComponent>(tail).nextSibling = child;
    }

    void Scene::unlinkChild(Entity parent, Entity child)
    {
        auto& parentTransform = getComponent<TransformComponent>(parent);
        if (parentTransform.firstChild == child)
        {
            parentTransform.firstChild = getComponent<TransformComponent>(child).nextSibling;
            return;
        }
        Entity cur = parentTransform.firstChild;
        while (cur != NullEntity && getComponent<TransformComponent>(cur).nextSibling != child)
            cur = getComponent<TransformComponent>(cur).nextSibling;
        assert(cur != NullEntity && "child not found in parent's child list");
        getComponent<TransformComponent>(cur).nextSibling = getComponent<TransformComponent>(child).nextSibling;
    }

    void Scene::setParent(Entity entity, Entity parent)
    {
        assert(isValid(entity) && entity != m_root);
        assert(isValid(parent));
        if (entity == parent)
            return;
        auto& transform = getComponent<TransformComponent>(entity);
        if (transform.parent == parent)
            return;
        if (transform.parent != NullEntity)
            unlinkChild(transform.parent, entity);
        linkChild(parent, entity);
    }

    void Scene::clear()
    {
        for (uint32_t i = 0; i < m_records.size(); i++)
        {
            if (m_records[i].alive && make_entity(i, m_records[i].generation) != m_root)
                destroySubtree(make_entity(i, m_records[i].generation));
        }
        // Runtime component rows die with their entities above; drop the
        // definitions too so a reloaded game module re-registers cleanly.
        m_runtimeComponents.clear();
        clearRuntimeComponents();
    }

    bool Scene::addRuntimeComponent(Entity entity, const std::string& name)
    {
        const RuntimeComponentDef* def = findRuntimeComponent(name);
        if (def == nullptr || !isValid(entity))
            return false;
        auto& rows = m_runtimeComponents[name];
        if (!rows.emplace(entity_index(entity), RuntimeRow{}).second)
            return false;
        auto& row = rows[entity_index(entity)];
        row.seq = m_runtimeSeq++;
        row.values = def->defaults;
        return true;
    }

    void Scene::removeRuntimeComponent(Entity entity, const std::string& name)
    {
        auto it = m_runtimeComponents.find(name);
        if (it != m_runtimeComponents.end())
            it->second.erase(entity_index(entity));
    }

    RuntimeRow* Scene::tryGetRuntimeComponent(Entity entity, const std::string& name)
    {
        if (!isValid(entity))
            return nullptr;
        auto it = m_runtimeComponents.find(name);
        if (it == m_runtimeComponents.end())
            return nullptr;
        auto row = it->second.find(entity_index(entity));
        return row != it->second.end() ? &row->second : nullptr;
    }

    std::vector<std::string> Scene::runtimeComponentsOf(Entity entity) const
    {
        std::vector<std::string> names;
        for (const auto* def : runtimeComponents())
        {
            auto it = m_runtimeComponents.find(def->name);
            if (it != m_runtimeComponents.end() && isValid(entity) && it->second.find(entity_index(entity)) != it->second.end())
            {
                names.push_back(def->name);
            }
        }
        return names;
    }
}