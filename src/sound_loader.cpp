#include "sound_loader.h"

#include "game_module.h"
#include "logger.h"
#include "resmgr.h"
#include "system_audio.h"
#include "system_filesystem.h"

#include <filesystem>
#include <string>
#include <typeindex>
#include <vector>

namespace openre
{
    namespace
    {
        // Reads the first candidate that exists: module dir first, then the
        // original RE2 data set via data://, mirroring the audio emitter's
        // source resolution so both see the same files.
        bool readSoundBytes(const std::string& path, std::vector<uint8_t>& outBytes)
        {
            if (!path.empty() && path.find("data://") != 0)
            {
                const auto modulePath = game_module_dir() / path;
                if (system::fs::info(modulePath.string().c_str()).kind == system::fs::FileKind::file)
                    outBytes = system::fs::readAllBytes(modulePath.string().c_str());
            }
            if (outBytes.empty())
            {
                const auto resolved = system::fs::resolve(("data://" + path).c_str());
                if (!resolved.empty() && system::fs::info(resolved.c_str()).kind == system::fs::FileKind::file)
                    outBytes = system::fs::readAllBytes(resolved.c_str());
            }
            return !outBytes.empty();
        }
    }

    ResourceCookie loadSoundResource(std::string_view path, int sub)
    {
        // The resource key includes the sub index so different sounds from one
        // multi-sound .sap container stay separate entries.
        const std::string key = sub >= 0 ? (std::string(path) + "#" + std::to_string(sub)) : std::string(path);

        // Reuse an already-loaded sound instead of decoding it again. Returns
        // 0 when nothing is registered under this key yet.
        auto& resmgr = ResourceManager::get();
        const ResourceCookie existing = resmgr.addRef<SoundResource>(key);
        if (existing != 0)
            return existing;

        std::vector<uint8_t> bytes;
        if (!readSoundBytes(std::string(path), bytes))
        {
            logging::logError("[sound_loader] sound '{}' not found (module dir or data://)", path);
            return 0;
        }

        std::vector<int16_t> pcm;
        int frequency = 0;
        int channels = 0;
        if (!system::audio::decode_sap(bytes.data(), (int)bytes.size(), sub, pcm, frequency, channels))
        {
            logging::logError("[sound_loader] failed to decode sound '{}'", key);
            return 0;
        }

        // Buffer group 4 ("core") has spare capacity for resident UI effects;
        // take any free slot so we never collide with emitter sources.
        const uint32_t handle = system::audio::create_buffer_unused(4, pcm.data(), (int)pcm.size(), frequency, channels);
        if (handle == 0)
        {
            logging::logError("[sound_loader] no free audio slot for '{}'", key);
            return 0;
        }

        auto resource = std::make_unique<SoundResource>();
        resource->audioHandle = handle;
        const ResourceCookie cookie = resmgr.addFirstRef<SoundResource>(key, std::move(resource));
        logging::logInfo(
            "[sound_loader] loaded '{}' -> cookie {}, handle {}, {} Hz, {} ch", key, cookie, handle, frequency, channels);
        return cookie;
    }
}
