#include "marni_config.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"
#include "re2.h"
#include "str.h"
#include "system_config.h"

#include <cstdlib>
#include <cstring>

namespace openre::marni
{
    namespace
    {
        static const char* get_registry_path(const MarniConfig* self)
        {
            return self->path.data;
        }

        // 0x0050B450
        // Opens or creates a config group. With INI backend this is a no-op.
        // Returns: 1 on success.
        static uint32_t __stdcall MarniConfig_CreateKey(MarniConfig* self, uint32_t /*accessMode*/)
        {
            self->hKey = 1;
            self->ex_flag = 1;
            return 1;
        }

        // 0x0050B4B0
        // Closes the config group. With INI backend this is a no-op.
        static void __stdcall MarniConfig_CloseKey(MarniConfig* /*self*/) {}

        // 0x0050B500
        // Reads a string value from the config.
        static OldStdString* __stdcall
        MarniConfig_GetString(MarniConfig* self, OldStdString* out, const char* name, const char* defaultValue)
        {
            memset(out, 0, sizeof(OldStdString));

            const char* group = get_registry_path(self);
            auto value = system::config::get<std::string>(group, name, defaultValue ? defaultValue : "");
            str::string_assign_cstr(out, value.c_str());
            return out;
        }

        // 0x0050B620
        // Reads a DWORD value from the config.
        static uint32_t __stdcall MarniConfig_GetDword(MarniConfig* self, const char* name, uint32_t defaultValue)
        {
            const char* group = get_registry_path(self);
            auto value = system::config::get<int32_t>(group, name, static_cast<int32_t>(defaultValue));
            return static_cast<uint32_t>(value);
        }

        // 0x0050B730
        // Reads binary data from the config into a pre-allocated buffer.
        static void* __stdcall MarniConfig_QueryValue(MarniConfig* self, const char* name, void* buffer, uint32_t bufferSize)
        {
            void* resultBuffer = buffer;

            if (buffer == nullptr)
            {
                resultBuffer = std::malloc(bufferSize);
                if (resultBuffer)
                    std::memset(resultBuffer, 0, bufferSize);
            }

            const char* group = get_registry_path(self);
            system::config::get_binary(group, name, resultBuffer, bufferSize);
            return resultBuffer;
        }

        // 0x0050B7D0
        // Writes a string value to the config.
        static bool __stdcall MarniConfig_WriteString(MarniConfig* self, const char* name, const char* data)
        {
            const char* group = get_registry_path(self);
            system::config::set(group, name, data ? data : "");
            return true;
        }

        // 0x0050B820
        // Writes a DWORD value to the config.
        static bool __stdcall MarniConfig_WriteDword(MarniConfig* self, const char* name, uint32_t value)
        {
            const char* group = get_registry_path(self);
            system::config::set(group, name, static_cast<int32_t>(value));
            return true;
        }

        // 0x0050B8B0
        // Writes binary data to the config.
        static bool __stdcall MarniConfig_WriteBinary(MarniConfig* self, const char* name, const void* data, uint32_t size)
        {
            const char* group = get_registry_path(self);
            return system::config::set_binary(group, name, data, size);
        }
    }

    void marni_config_init_hooks()
    {
        interop::hookThisCall(0x0050B450, &MarniConfig_CreateKey);
        interop::hookThisCall(0x0050B4B0, &MarniConfig_CloseKey);
        interop::hookThisCall(0x0050B500, &MarniConfig_GetString);
        interop::hookThisCall(0x0050B620, &MarniConfig_GetDword);
        interop::hookThisCall(0x0050B730, &MarniConfig_QueryValue);
        interop::hookThisCall(0x0050B7D0, &MarniConfig_WriteString);
        interop::hookThisCall(0x0050B820, &MarniConfig_WriteDword);
        interop::hookThisCall(0x0050B8B0, &MarniConfig_WriteBinary);
    }
}
