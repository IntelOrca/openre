#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace openre
{
    /**
     * A unique number for the resource.
     */
    using ResourceHandle = uint32_t;

    /**
     * A unique number for a resource consumer. Used instead of ref counting so
     * we know when a resource is no longer being used.
     */
    using ResourceCookie = uint32_t;

    class Resource
    {
    public:
        virtual ~Resource() = default;
        virtual const char* getName() const = 0;
    };

    /**
     * A texture owned by the engine's 2D sprite renderer (openre::render2d).
     * The textureHandle is opaque to the resource manager; it is resolved
     * against the render2d texture registry by whoever draws with it.
     */
    class TextureResource : public Resource
    {
    public:
        int textureHandle = 0;
        int width = 0;
        int height = 0;

        // Decoded RGBA pixels (per-pixel R,G,B,A), cached at load time so the
        // debug web UI can preview the texture without re-decoding the source.
        // Empty for resources created without pixel data.
        std::vector<uint8_t> rgba;
        int rgbaWidth = 0;
        int rgbaHeight = 0;

        ~TextureResource() override;

        const char* getName() const override
        {
            return "texture";
        }
    };

    /**
     * A decoded sound owned by the system::audio mixer. The audioHandle is
     * opaque; unloading happens in the destructor.
     */
    class SoundResource : public Resource
    {
    public:
        uint32_t audioHandle = 0;

        ~SoundResource() override;

        const char* getName() const override
        {
            return "sound";
        }
    };

    /**
     * Tracks loaded resources and their consumers. A resource lives as long
     * as at least one cookie points at it; release() drops a cookie and unloads
     * the resource once the last one is gone. Lua texture userdata release
     * their cookie in __gc.
     */
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

        std::vector<ResourceEntry> resources;
        std::vector<ResourceHandle> cookies;
        std::vector<ResourceCookie> spareCookies;

    public:
        static ResourceManager& get();

        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        template<typename T> ResourceHandle add(std::string_view path, std::unique_ptr<T> resource)
        {
            static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");
            return addImpl(path, std::unique_ptr<Resource>(std::move(resource)), std::type_index(typeid(T)));
        }

        template<typename T> ResourceCookie addRef(std::string_view path)
        {
            return addRefImpl(path, std::type_index(typeid(T)));
        }

        template<typename T> ResourceCookie addFirstRef(std::string_view path, std::unique_ptr<T> resource)
        {
            auto handle = add<T>(path, std::move(resource));
            return addRef(handle);
        }

        ResourceCookie addRef(ResourceHandle handle);
        ResourceCookie dupRef(ResourceCookie cookie);
        void release(ResourceCookie cookie);

        /// A snapshot of a single resource, used for enumeration/debugging.
        struct ResourceInfo
        {
            ResourceCookie cookie = 0;
            std::string type; // e.g. "texture", "font"
            std::string path; // resource key
            int width = 0;
            int height = 0;
        };

        /// Enumerates every live resource, in cookie order.
        std::vector<ResourceInfo> list() const;

        /// Releases every resource and clears the registry. Call before the
        /// renderer/GPU teardown so resource destructors (e.g. TextureResource
        /// unloading from the sprite renderer) run while the GPU is alive.
        /// After this, any stale Lua texture userdata's __gc is a safe no-op.
        void releaseAll();

        ResourceHandle getFirst(std::type_index typeIndex) const;
        ResourceHandle getNext(ResourceHandle handle) const;

        template<typename T> ResourceHandle getFirst() const
        {
            return getFirst(std::type_index(typeid(T)));
        }

        template<typename T> ResourceHandle getNext(ResourceHandle handle) const
        {
            return getNext(handle);
        }

        template<typename T> T* fromCookie(ResourceCookie cookie) const
        {
            static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");
            if (cookie == 0 || cookie > this->cookies.size())
                return nullptr;

            auto handle = this->cookies[cookie - 1];
            if (handle == 0 || handle > this->resources.size())
                return nullptr;

            auto& resource = this->resources[handle - 1];
            if (!resource.typeIndex || *resource.typeIndex != std::type_index(typeid(T)))
                return nullptr;

            return static_cast<T*>(resource.resource.get());
        }

        template<typename T> T* fromHandle(ResourceHandle handle) const
        {
            static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");
            if (handle == 0 || handle > this->resources.size())
                return nullptr;

            auto& resource = this->resources[handle - 1];
            if (!resource.typeIndex || *resource.typeIndex != std::type_index(typeid(T)))
                return nullptr;

            return static_cast<T*>(resource.resource.get());
        }

    private:
        ResourceManager() = default;
        ~ResourceManager() = default;

        ResourceHandle addImpl(std::string_view path, std::unique_ptr<Resource> resource, std::type_index typeIndex);
        ResourceCookie addRefImpl(std::string_view path, std::type_index typeIndex);
        ResourceHandle find(std::string_view path, std::type_index typeIndex) const;
    };
}