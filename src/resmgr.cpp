#include "resmgr.h"

#include "font.h"
#include "logger.h"
#include "marni_renderer.h"
#include "render2d.h"
#include "system_audio.h"

#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace openre
{
    ResourceManager& ResourceManager::get()
    {
        // Intentionally leaked (process-lifetime singleton). Function-local
        // statics are destroyed in an unspecified order at process exit, but
        // the Lua VM's __gc finalizers may still call into the resource
        // manager during lua_close(). Keeping the instance alive avoids using
        // an already-destroyed object in that "late release" path.
        static ResourceManager* instance = new ResourceManager();
        return *instance;
    }

    std::vector<ResourceManager::ResourceInfo> ResourceManager::list() const
    {
        // One entry per live RESOURCE (not per cookie): several cookies may
        // reference the same resource, and reporting each would list the same
        // asset repeatedly. All live cookies are reported so callers can use
        // any of them with fromCookie()/preview endpoints.
        std::vector<ResourceInfo> result;
        for (ResourceHandle handle = 1; handle <= this->resources.size(); handle++)
        {
            const auto& entry = this->resources[handle - 1];
            if (!entry.resource)
                continue;

            ResourceInfo info;
            info.resource = handle;
            for (size_t i = 0; i < this->cookies.size(); i++)
            {
                if (this->cookies[i] == handle)
                    info.cookies.push_back(static_cast<ResourceCookie>(i + 1));
            }

            info.path = entry.path;
            if (entry.typeIndex == std::type_index(typeid(TextureResource)))
            {
                info.type = "texture";
                if (auto* tex = static_cast<TextureResource*>(entry.resource.get()))
                {
                    info.width = tex->rgbaWidth;
                    info.height = tex->rgbaHeight;
                }
            }
            else if (entry.typeIndex == std::type_index(typeid(MarniTextureResource)))
            {
                info.type = "texture";
                if (auto* tex = static_cast<MarniTextureResource*>(entry.resource.get()))
                {
                    info.width = tex->rgbaWidth;
                    info.height = tex->rgbaHeight;
                }
            }
            else if (entry.typeIndex == std::type_index(typeid(FontResource)))
            {
                info.type = "font";
                if (auto* font = static_cast<FontResource*>(entry.resource.get()))
                {
                    info.width = font->width;
                    info.height = font->height;
                }
            }
            else if (entry.typeIndex == std::type_index(typeid(SoundResource)))
            {
                info.type = "sound";
                info.streaming = static_cast<SoundResource*>(entry.resource.get())->streaming;
            }
            else if (entry.typeIndex == std::type_index(typeid(MovieResource)))
            {
                info.type = "movie";
                info.streaming = true;
            }
            else
            {
                info.type = "unknown";
            }
            result.push_back(std::move(info));
        }
        return result;
    }

    TextureResource::~TextureResource()
    {
        if (textureHandle != 0)
            render2d::SpriteRenderer::get().unloadTexture(textureHandle);
    }

    MarniTextureResource::~MarniTextureResource()
    {
        if (textureHandle != 0 && marni::g_renderer)
            marni::g_renderer->unloadTexture(textureHandle);
    }

    SoundResource::~SoundResource()
    {
        if (audioHandle != 0)
            system::audio::unload(audioHandle);
    }

    ResourceHandle
    ResourceManager::addImpl(std::string_view path, std::unique_ptr<Resource> resource, std::type_index typeIndex)
    {
        // One resource per (path, type): a repeated load reuses the existing
        // entry instead of piling up duplicates.
        auto existingHandle = find(path, typeIndex);
        if (existingHandle != 0)
        {
            logging::logDebug("[resmgr] resource '{}' already loaded, reusing handle", std::string(path));
            return existingHandle;
        }

        logging::logInfo(
            "[resmgr] load {} handle = {}, path = '{}'", resource->getName(), this->resources.size() + 1, std::string(path));

        auto& resourceEntry = this->resources.emplace_back();
        resourceEntry.handle = static_cast<ResourceHandle>(this->resources.size());
        resourceEntry.path = std::string(path);
        resourceEntry.resource = std::move(resource);
        resourceEntry.typeIndex = typeIndex;
        return resourceEntry.handle;
    }

    ResourceCookie ResourceManager::addRef(ResourceHandle handle)
    {
        if (handle == 0)
            return 0;

        ResourceCookie cookie;
        if (!this->spareCookies.empty())
        {
            cookie = this->spareCookies.back();
            this->spareCookies.pop_back();
            this->cookies[cookie - 1] = handle;
        }
        else
        {
            this->cookies.push_back(handle);
            cookie = static_cast<ResourceCookie>(this->cookies.size());
        }

        auto& resourceEntry = this->resources[handle - 1];
        resourceEntry.count++;
        logging::logDebug(
            "[resmgr] ref {}, cookie = {}, path = '{}'", resourceEntry.resource->getName(), cookie, resourceEntry.path);
        return cookie;
    }

    ResourceCookie ResourceManager::addRefImpl(std::string_view path, std::type_index typeIndex)
    {
        return addRef(find(path, typeIndex));
    }

    ResourceCookie ResourceManager::dupRef(ResourceCookie cookie)
    {
        if (cookie == 0)
            return 0;
        return addRef(this->cookies[cookie - 1]);
    }

    void ResourceManager::release(ResourceCookie cookie)
    {
        if (cookie == 0 || cookie > this->cookies.size())
            return;

        auto& dst = this->cookies[cookie - 1];
        if (dst == 0)
            return;

        auto& resourceEntry = this->resources[dst - 1];
        logging::logDebug(
            "[resmgr] unref {}, cookie = {}, path = '{}'", resourceEntry.resource->getName(), cookie, resourceEntry.path);

        if (resourceEntry.count <= 1)
        {
            logging::logInfo(
                "[resmgr] unload {} handle = {}, path = '{}'",
                resourceEntry.resource->getName(),
                resourceEntry.handle,
                resourceEntry.path);
            resourceEntry = {};
        }
        else
        {
            resourceEntry.count--;
        }

        dst = 0;
        this->spareCookies.push_back(cookie);
    }

    void ResourceManager::releaseAll()
    {
        for (size_t i = 0; i < this->cookies.size(); i++)
        {
            const auto handle = this->cookies[i];
            if (handle == 0 || handle > this->resources.size())
                continue;

            auto& entry = this->resources[handle - 1];
            if (entry.resource && entry.count <= 1)
            {
                logging::logInfo(
                    "[resmgr] unload {} handle = {}, path = '{}'", entry.resource->getName(), entry.handle, entry.path);
            }
            this->cookies[i] = 0;
        }

        // resources.clear() destroys any remaining TextureResource here, while
        // the renderer (and GPU) are still alive.
        this->resources.clear();
        this->cookies.clear();
        this->spareCookies.clear();
    }

    ResourceHandle ResourceManager::getFirst(std::type_index typeIndex) const
    {
        for (const auto& entry : this->resources)
        {
            if (entry.typeIndex && *entry.typeIndex == typeIndex)
                return entry.handle;
        }
        return 0;
    }

    ResourceHandle ResourceManager::getNext(ResourceHandle handle) const
    {
        for (auto i = handle; i < this->resources.size(); i++)
        {
            if (this->resources[i].handle > handle)
                return this->resources[i].handle;
        }
        return 0;
    }

    ResourceHandle ResourceManager::find(std::string_view path, std::type_index typeIndex) const
    {
        for (const auto& entry : this->resources)
        {
            if (entry.typeIndex && *entry.typeIndex == typeIndex && entry.path == path)
                return entry.handle;
        }
        return 0;
    }
}