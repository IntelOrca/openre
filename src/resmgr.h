#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace openre::logging
{
    class Logger;
}

namespace openre
{
    /**
     * A unique number for the resource.
     */
    using ResourceHandle = uint32_t;

    /**
     * A unique number for a resource consumer. Used instead
     * of ref counting so we know when a resource is no longer
     * being used.
     */
    using ResourceCookie = uint32_t;

    class Resource
    {
    public:
        virtual ~Resource() = default;
        virtual const char* getName() const = 0;
    };

    class ResourceManager
    {
    private:
        struct ResourceEntry
        {
            ResourceHandle handle = 0;
            uint32_t count = 0;
            std::string path;
            std::unique_ptr<Resource> resource;
            std::optional<std::type_index> typeIndex;
        };

        openre::logging::Logger& logger;
        std::vector<ResourceEntry> resources;
        std::vector<ResourceHandle> cookies;
        std::vector<ResourceCookie> spareCookies;

    public:
        ResourceManager(openre::logging::Logger& logger);
        ResourceManager(const ResourceManager&) = delete;
        ~ResourceManager();

        ResourceHandle add(std::string_view path, std::unique_ptr<Resource> resource, std::type_index typeIndex);
        ResourceCookie addRef(std::string_view path, std::type_index typeIndex);
        ResourceCookie addRef(ResourceHandle handle);
        void release(ResourceCookie cookie);

        ResourceHandle getFirst(std::type_index typeIndex) const;
        ResourceHandle getNext(ResourceHandle handle) const;

        template<typename T> ResourceHandle add(std::string_view path, std::unique_ptr<T> resource)
        {
            return this->add(path, std::move(resource), std::type_index(typeid(T)));
        }

        template<typename T> ResourceCookie addRef(std::string_view path)
        {
            return this->addRef(path, std::type_index(typeid(T)));
        }

        template<typename T> ResourceHandle addFirstRef(std::string_view path, std::unique_ptr<T> resource)
        {
            return this->addRef(this->add<T>(path, std::move(resource)));
        }

        template<typename T> T* fromCookie(ResourceCookie cookie) const
        {
            static_assert(std::is_base_of<Resource, T>::value);
            if (cookie == 0 || cookie > this->cookies.size())
                return nullptr;

            auto handle = this->cookies[cookie - 1];
            auto& resource = this->resources[handle - 1];
            if (resource.typeIndex != std::type_index(typeid(T)))
                return nullptr;

            return static_cast<T*>(resource.resource.get());
        }

        template<typename T> T* fromHandle(ResourceHandle handle) const
        {
            static_assert(std::is_base_of<Resource, T>::value);
            if (handle == 0 || handle > this->resources.size())
                return nullptr;

            auto& resource = this->resources[handle - 1];
            if (resource.typeIndex != std::type_index(typeid(T)))
                return nullptr;

            return static_cast<T*>(resource.resource.get());
        }

        template<typename T> ResourceHandle getFirst() const
        {
            return getFirst(std::type_index(typeid(T)));
        }

    private:
        ResourceHandle find(std::string_view path, std::type_index typeIndex) const;
    };
}
