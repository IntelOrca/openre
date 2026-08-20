#pragma once

#include "resmgr.h"

#include <string_view>

namespace openre
{
    /**
     * Loads a texture asset and registers it with the resource manager,
     * returning a ResourceCookie holding one reference (0 on failure).
     *
     * `path` is a logical asset path relative to the game module directory
     * (e.g. "common/data/gw2"). Resolution order:
     *   1. <moduleDir>/<path>  (probing "", ".tim", ".adt")
     *   2. data://<path>       (probing "", ".tim", ".adt")
     *
     * ADT data is decoded via graphics::decodeAdt + reorgAdt (320x240 RGB555);
     * TIM data via tim::decodeTim. The decoded image is uploaded through the
     * render2d sprite renderer and the resulting GPU handle is stored in the
     * TextureResource.
     *
     * `timIndex` selects which TIM to decode when the file packs several TIMs
     * back to back (e.g. tmoji.bin); 0 decodes the first.
     */
    ResourceCookie loadTextureResource(std::string_view path, int width, int height, int timIndex = 0);
}