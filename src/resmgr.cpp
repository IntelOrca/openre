#include "resmgr.h"
#include "logger.h"

using namespace openre::logging;

namespace openre
{
    ResourceManager::ResourceManager(Logger& logger)
        : logger(logger)
    {
    }

    ResourceManager::~ResourceManager() {}

    ResourceHandle ResourceManager::add(std::string_view path, std::unique_ptr<Resource> resource, std::type_index typeIndex)
    {
        auto typeId = &typeid(resource);
        auto existingHandle = find(path, typeIndex);
        if (existingHandle != 0)
        {
            logger.log(LogVerbosity::warning, "Resource already loaded for path '%s'", std::string(path).c_str());
        }
        else if (logger.isLogging(LogVerbosity::info))
        {
            logger.log(
                LogVerbosity::info,
                "Load %s handle = %d, path = '%s'",
                resource->getName(),
                this->resources.size() + 1,
                std::string(path).c_str());
        }

        auto& resourceEntry = this->resources.emplace_back();
        resourceEntry.handle = this->resources.size();
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
        if (this->spareCookies.size() > 0)
        {
            cookie = this->spareCookies.back();
            this->spareCookies.pop_back();
            this->cookies[cookie - 1] = handle;
        }
        else
        {
            this->cookies.push_back(handle);
            cookie = this->cookies.size();
        }

        auto& resourceEntry = this->resources[handle - 1];
        resourceEntry.count++;
        if (logger.isLogging(LogVerbosity::debug))
        {
            logger.log(
                LogVerbosity::debug,
                "Ref %s, cookie = %d, handle = %d, path = '%s'",
                resourceEntry.resource->getName(),
                cookie - 1,
                resourceEntry.handle,
                resourceEntry.path.c_str());
        }
        return cookie;
    }

    ResourceCookie ResourceManager::addRef(std::string_view path, std::type_index typeIndex)
    {
        return addRef(find(path, typeIndex));
    }

    void ResourceManager::release(ResourceCookie cookie)
    {
        if (cookie == 0 || cookie > this->cookies.size())
            return;

        auto& dst = this->cookies[cookie - 1];
        if (dst != 0)
        {
            auto& resourceEntry = this->resources[dst - 1];
            if (logger.isLogging(LogVerbosity::debug))
            {
                logger.log(
                    LogVerbosity::debug,
                    "Unref %s, cookie = %d, handle = %d, path = '%s'",
                    resourceEntry.resource->getName(),
                    cookie,
                    resourceEntry.handle,
                    resourceEntry.path.c_str());
            }

            if (resourceEntry.count <= 1)
            {
                if (logger.isLogging(LogVerbosity::info))
                {
                    logger.log(
                        LogVerbosity::info,
                        "Unload %s, handle = %d, path = '%s'",
                        resourceEntry.resource->getName(),
                        resourceEntry.handle,
                        resourceEntry.path.c_str());
                }
                resourceEntry = {};
            }
            else
            {
                resourceEntry.count--;
            }

            dst = 0;
            this->spareCookies.push_back(cookie);
        }
    }

    ResourceHandle ResourceManager::getFirst(std::type_index typeIndex) const
    {
        for (size_t i = 0; i < this->resources.size(); i++)
        {
            if (this->resources[i].typeIndex == typeIndex)
            {
                return static_cast<ResourceHandle>(i + 1);
            }
        }
        return 0;
    }

    ResourceHandle ResourceManager::getNext(ResourceHandle handle) const
    {
        if (handle == 0 || handle > this->resources.size())
            return 0;

        auto typeIndex = this->resources[handle - 1].typeIndex;
        for (size_t i = handle + 1; i < this->resources.size(); i++)
        {
            if (this->resources[i].typeIndex == typeIndex)
            {
                return static_cast<ResourceHandle>(i + 1);
            }
        }
        return 0;
    }

    ResourceHandle ResourceManager::find(std::string_view path, std::type_index typeIndex) const
    {
        auto result = std::find_if(this->resources.begin(), this->resources.end(), [path, typeIndex](const ResourceEntry& r) {
            return r.typeIndex == typeIndex && r.path == path;
        });
        if (result != this->resources.end())
        {
            return result->handle;
        }
        return 0;
    }
}
