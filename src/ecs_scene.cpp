#include "ecs_scene.h"

#include <cassert>

namespace openre::ecs
{
    Scene::Scene()
    {
        m_root = allocEntity();
        storage<TransformComponent>().add(m_root);
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
    }
}