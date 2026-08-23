#pragma once

#include "resmgr.h"

#include <string_view>

namespace openre
{
    /**
     * Loads a .sap sound file and registers it with the resource manager,
     * returning a ResourceCookie holding one reference (0 on failure). Small
     * sounds are decoded fully into memory as always resident one-shots; large
     * whole-file sources (>= 1 MiB, e.g. BGM tracks) are streamed from disk
     * instead. The cookie resolves to a SoundResource whose audioHandle is
     * playable via system::audio either way.
     *
     * `path` is a logical asset path relative to the game module directory
     * (e.g. "common/sound/core/core22.sap"). Resolution order:
     *   1. <moduleDir>/<path>
     *   2. data://<path>
     *
     * `sub` selects one sub-sound of a multi-sound .sap container (one RIFF
     * WAV chunk per set bit after the 8-byte mask header), e.g. sub 2 of
     * core22.sap. -1 decodes the whole file as a single sound.
     */
    ResourceCookie loadSoundResource(std::string_view path, int sub = -1);
}
