#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "interop.hpp"
#include "logger.h"
#include "marni_config.h"
#include "openre.h"
#include "re2.h"
#include "str.h"

#include <cstring>

// Registry access rights
constexpr uint32_t REG_KEY_READ = KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY;
constexpr uint32_t REG_KEY_WRITE_ACCESS = KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK;

namespace openre::marni
{
    namespace
    {
        // Offset 0x04 in MarniConfig is an OldStdString storing the registry subkey path.
        static const char* get_registry_path(const MarniConfig* self)
        {
            return self->path.data;
        }

        // 0x0050B450
        // Opens or creates a registry key and stores the handle in MarniConfig::hKey.
        // accessMode: 0 = read-only, non-zero = read/write
        // Returns: 1 on success, 0 on failure.
        static uint32_t __stdcall MarniConfig_CreateKey(MarniConfig* self, uint32_t accessMode)
        {
            const char* path = get_registry_path(self);
            logging::logDebug("[REG CREATE] root=0x{}, path='{}'", (void*)(uintptr_t)self->root_key, path ? path : "(null)");

            HKEY hKeyResult = nullptr;
            DWORD disposition = 0;

            uint32_t samDesired = REG_KEY_READ;
            if (accessMode != 0)
                samDesired |= REG_KEY_WRITE_ACCESS;

            LONG result = RegCreateKeyExA(
                reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->root_key)),
                path,
                0,       // Reserved
                nullptr, // lpClass
                REG_OPTION_NON_VOLATILE,
                samDesired,
                nullptr, // lpSecurityAttributes
                &hKeyResult,
                &disposition);

            if (result == ERROR_SUCCESS)
            {
                self->hKey = reinterpret_cast<uint32_t>(hKeyResult);
                if (disposition == REG_CREATED_NEW_KEY)
                {
                    self->ex_flag = 0;
                    logging::logInfo("[REG CREATE] Created new key");
                }
                else
                {
                    self->ex_flag = 1;
                    logging::logInfo("[REG CREATE] Opened existing key");
                }
                return 1;
            }

            self->hKey = 0;
            self->ex_flag = 0;
            logging::logError("[REG CREATE] Failed to open/create key, error={}", static_cast<uint32_t>(result));
            return 0;
        }

        // 0x0050B4B0
        // Closes the registry key if one is open.
        static void __stdcall MarniConfig_CloseKey(MarniConfig* self)
        {
            if (self->hKey != 0)
            {
                RegCloseKey(reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)));
                self->hKey = 0;
                logging::logDebug("[REG CLOSE] Key closed");
            }
            else
            {
                logging::logDebug("[REG CLOSE] No key open");
            }
        }

        // 0x0050B500
        // Reads a REG_SZ string value from the registry.
        // Returns the output string pointer (dst parameter).
        static OldStdString* __stdcall
        MarniConfig_GetString(MarniConfig* self, OldStdString* out, const char* name, const char* defaultValue)
        {
            // Must zero output before writing, otherwise string_assign_cstr
            // will try to free uninitialized stack memory.
            memset(out, 0, sizeof(OldStdString));

            if (!MarniConfig_CreateKey(self, 0))
            {
                // Key creation/opening failed; use default value
                str::string_assign_cstr(out, defaultValue ? defaultValue : "");
                MarniConfig_CloseKey(self);
                logging::logWarning("[REG READ] name='{}' - using default value", name);
                return out;
            }

            DWORD type = REG_SZ;
            DWORD cbData = 0;

            // First call to get size
            LONG result = RegQueryValueExA(
                reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)),
                name,
                nullptr, // lpReserved
                &type,
                nullptr, // lpData
                &cbData);

            if (result == ERROR_SUCCESS && type == REG_SZ && cbData > 0)
            {
                auto* buffer = new char[cbData];
                std::memset(buffer, 0, cbData);

                result = RegQueryValueExA(
                    reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)),
                    name,
                    nullptr,
                    &type,
                    reinterpret_cast<LPBYTE>(buffer),
                    &cbData);

                if (result == ERROR_SUCCESS)
                {
                    str::string_assign_cstr(out, buffer);
                    logging::logDebug("[REG READ] name='{}', value='{}'", name, buffer);
                }
                else
                {
                    str::string_assign_cstr(out, defaultValue ? defaultValue : "");
                    logging::logWarning(
                        "[REG READ] name='{}' - second query failed, error={}", name, static_cast<uint32_t>(result));
                }

                delete[] buffer;
            }
            else
            {
                str::string_assign_cstr(out, defaultValue ? defaultValue : "");
                logging::logWarning(
                    "[REG READ] name='{}' - query failed or wrong type, error={}", name, static_cast<uint32_t>(result));
            }

            MarniConfig_CloseKey(self);
            return out;
        }

        // 0x0050B620
        // Reads a REG_DWORD value from the registry.
        // Returns the DWORD value (or default if not found).
        static uint32_t __stdcall MarniConfig_GetDword(MarniConfig* self, const char* name, uint32_t defaultValue)
        {
            if (!MarniConfig_CreateKey(self, 0))
            {
                MarniConfig_CloseKey(self);
                logging::logWarning("[REG READ] name='{}' - using default value ({})", name, defaultValue);
                return defaultValue;
            }

            DWORD type = REG_DWORD;
            DWORD data = defaultValue;
            DWORD cbData = sizeof(DWORD);

            // First call to check type and size
            LONG result = RegQueryValueExA(
                reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)), name, nullptr, &type, nullptr, &cbData);

            if (result == ERROR_SUCCESS && type == REG_DWORD && cbData <= sizeof(DWORD))
            {
                // Second call to read data
                result = RegQueryValueExA(
                    reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)),
                    name,
                    nullptr,
                    &type,
                    reinterpret_cast<LPBYTE>(&data),
                    &cbData);
            }

            MarniConfig_CloseKey(self);

            if (result == ERROR_SUCCESS)
            {
                logging::logDebug("[REG READ] name='{}', value={}", name, data);
            }
            else
            {
                data = defaultValue;
                logging::logWarning(
                    "[REG READ] name='{}' - failed, error={}, using default ({})",
                    name,
                    static_cast<uint32_t>(result),
                    defaultValue);
            }

            return data;
        }

        // 0x0050B730
        // Reads REG_BINARY data from the registry into a pre-allocated buffer.
        // buffer: pre-allocated buffer, or nullptr to allocate via malloc
        // bufferSize: size of buffer (or max size)
        // Returns the buffer pointer (or nullptr on failure).
        static void* __stdcall MarniConfig_QueryValue(MarniConfig* self, const char* name, void* buffer, uint32_t bufferSize)
        {
            void* resultBuffer = buffer;
            uint32_t maxSize = bufferSize;

            // If no buffer provided, allocate one
            if (buffer == nullptr)
            {
                resultBuffer = std::malloc(bufferSize);
                if (resultBuffer)
                    std::memset(resultBuffer, 0, bufferSize);
            }

            if (!MarniConfig_CreateKey(self, 0))
            {
                MarniConfig_CloseKey(self);
                logging::logWarning("[REG READ] name='{}' - CreateKey failed", name);
                return resultBuffer;
            }

            DWORD type = REG_BINARY;
            DWORD cbData = maxSize;

            LONG result = RegQueryValueExA(
                reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)), name, nullptr, &type, nullptr, &cbData);

            if (result == ERROR_SUCCESS && type == REG_BINARY && cbData <= maxSize)
            {
                result = RegQueryValueExA(
                    reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)),
                    name,
                    nullptr,
                    &type,
                    reinterpret_cast<LPBYTE>(resultBuffer),
                    &cbData);

                if (result == ERROR_SUCCESS)
                {
                    logging::logDebug("[REG READ] name='{}', size={}", name, cbData);
                }
                else
                {
                    logging::logWarning(
                        "[REG READ] name='{}' - second query failed, error={}", name, static_cast<uint32_t>(result));
                }
            }
            else
            {
                logging::logWarning(
                    "[REG READ] name='{}' - query failed or type mismatch, error={}", name, static_cast<uint32_t>(result));
            }

            MarniConfig_CloseKey(self);
            return resultBuffer;
        }

        // 0x0050B7D0
        // Writes a REG_SZ string value to the registry.
        // Returns true on success.
        static bool __stdcall MarniConfig_WriteString(MarniConfig* self, const char* name, const char* data)
        {
            if (!MarniConfig_CreateKey(self, 1))
            {
                MarniConfig_CloseKey(self);
                logging::logError("[REG WRITE] name='{}' - CreateKey failed", name);
                return false;
            }

            LONG result = RegSetValueExA(
                reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)),
                name,
                0, // Reserved
                REG_SZ,
                reinterpret_cast<const BYTE*>(data),
                static_cast<DWORD>(std::strlen(data) + 1));

            MarniConfig_CloseKey(self);

            if (result == ERROR_SUCCESS)
            {
                logging::logDebug("[REG WRITE] name='{}', value='{}'", name, data);
                return true;
            }

            logging::logError("[REG WRITE] name='{}' - failed, error={}", name, static_cast<uint32_t>(result));
            return false;
        }

        // 0x0050B820
        // Writes a REG_DWORD value to the registry.
        // Returns true on success.
        static bool __stdcall MarniConfig_WriteDword(MarniConfig* self, const char* name, uint32_t value)
        {
            if (!MarniConfig_CreateKey(self, 1))
            {
                MarniConfig_CloseKey(self);
                logging::logError("[REG WRITE] name='{}' - CreateKey failed", name);
                return false;
            }

            LONG result = RegSetValueExA(
                reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)),
                name,
                0, // Reserved
                REG_DWORD,
                reinterpret_cast<const BYTE*>(&value),
                sizeof(DWORD));

            MarniConfig_CloseKey(self);

            if (result == ERROR_SUCCESS)
            {
                logging::logDebug("[REG WRITE] name='{}', value={}", name, value);
                return true;
            }

            logging::logError("[REG WRITE] name='{}' - failed, error={}", name, static_cast<uint32_t>(result));
            return false;
        }

        // 0x0050B8B0
        // Writes REG_BINARY data to the registry.
        // Returns true on success.
        static bool __stdcall MarniConfig_WriteBinary(MarniConfig* self, const char* name, const void* data, uint32_t size)
        {
            if (!MarniConfig_CreateKey(self, 1))
            {
                MarniConfig_CloseKey(self);
                logging::logError("[REG WRITE] name='{}' - CreateKey failed", name);
                return false;
            }

            LONG result = RegSetValueExA(
                reinterpret_cast<HKEY>(static_cast<uintptr_t>(self->hKey)),
                name,
                0, // Reserved
                REG_BINARY,
                static_cast<const BYTE*>(data),
                size);

            MarniConfig_CloseKey(self);

            if (result == ERROR_SUCCESS)
            {
                logging::logDebug("[REG WRITE] name='{}', size={}", name, size);
                return true;
            }

            logging::logError("[REG WRITE] name='{}' - failed, error={}", name, static_cast<uint32_t>(result));
            return false;
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
