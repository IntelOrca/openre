#include "system_config.h"
#include "logger.h"
#include "system_filesystem.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace openre::system::config
{
    namespace
    {
        // Key: "group.key" → value string
        std::unordered_map<std::string, std::string> s_config;
        bool s_loaded = false;

        // Track section order for deterministic save output
        struct SectionKey
        {
            std::string section;
            std::string key;
        };
        std::vector<SectionKey> s_keyOrder;

        void addKey(const std::string& section, const std::string& key, const std::string& value)
        {
            std::string fullKey = section + "." + key;
            if (s_config.find(fullKey) == s_config.end())
            {
                s_keyOrder.push_back({ section, key });
            }
            s_config[fullKey] = value;
        }

        // Trim leading/trailing whitespace
        std::string trim(const std::string& s)
        {
            size_t start = 0;
            while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
                start++;
            size_t end = s.size();
            while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
                end--;
            return s.substr(start, end - start);
        }

        // Encode binary data as hex string
        std::string hexEncode(const void* data, uint32_t size)
        {
            if (data == nullptr || size == 0)
                return {};
            const auto* bytes = static_cast<const uint8_t*>(data);
            std::string result;
            result.reserve(size * 2);
            static const char hexChars[] = "0123456789ABCDEF";
            for (uint32_t i = 0; i < size; i++)
            {
                result.push_back(hexChars[bytes[i] >> 4]);
                result.push_back(hexChars[bytes[i] & 0x0F]);
            }
            return result;
        }

        // Decode hex string to binary
        uint32_t hexDecode(const std::string& hex, void* buffer, uint32_t maxSize)
        {
            if (hex.size() % 2 != 0)
                return 0;
            auto* out = static_cast<uint8_t*>(buffer);
            uint32_t count = 0;
            for (size_t i = 0; i < hex.size() && count < maxSize; i += 2)
            {
                auto val = std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16);
                out[count++] = static_cast<uint8_t>(val);
            }
            return count;
        }

        const Resolution kDefaultWindowSize{ 800, 600 };
        const Resolution kDefaultRenderResolution{ 640, 480 };

        // Parse a "WxH" value (e.g. "800x600") into a Resolution. Returns
        // false when the value is malformed (non-numeric, missing separator,
        // zero/negative, out of int32 range, ...).
        bool parse_resolution(const std::string& value, Resolution& out)
        {
            auto xPos = value.find('x');
            if (xPos == std::string::npos)
                xPos = value.find('X');
            if (xPos == std::string::npos)
                return false;

            auto widthStr = trim(value.substr(0, xPos));
            auto heightStr = trim(value.substr(xPos + 1));
            if (widthStr.empty() || heightStr.empty())
                return false;

            char* end = nullptr;
            errno = 0;
            auto width = std::strtol(widthStr.c_str(), &end, 10);
            if (errno == ERANGE || end == widthStr.c_str() || *end != '\0' || width <= 0 || width > INT32_MAX)
                return false;
            errno = 0;
            end = nullptr;
            auto height = std::strtol(heightStr.c_str(), &end, 10);
            if (errno == ERANGE || end == heightStr.c_str() || *end != '\0' || height <= 0 || height > INT32_MAX)
                return false;

            out.width = static_cast<int32_t>(width);
            out.height = static_cast<int32_t>(height);
            return true;
        }

        // Ensure the [video] section carries both resolution keys, defaulting
        // when absent or malformed, so any save() round-trips them.
        void ensure_video_defaults()
        {
            auto ensure = [](const char* key, const Resolution& defaultValue) {
                Resolution parsed;
                auto it = s_config.find(std::string("video.") + key);
                if (it == s_config.end() || !parse_resolution(it->second, parsed))
                {
                    addKey("video", key, std::to_string(defaultValue.width) + "x" + std::to_string(defaultValue.height));
                }
            };
            ensure("window_size", kDefaultWindowSize);
            ensure("render_resolution", kDefaultRenderResolution);
        }
    }

    void load()
    {
        s_config.clear();
        s_keyOrder.clear();
        s_loaded = true;

        // Seed the [video] resolution keys up front so a save() still
        // round-trips them when no config file exists yet (or nothing has
        // read the keys).
        ensure_video_defaults();

        auto info = system::fs::info("user://openre.ini");
        if (info.kind == system::fs::FileKind::none)
        {
            logging::logInfo("Config not found, creating new config at {}", info.physicalPath);
            return;
        }

        auto data = system::fs::readAllBytes("user://openre.ini");
        if (data.empty())
        {
            return;
        }

        std::string content(reinterpret_cast<const char*>(data.data()), data.size());
        std::istringstream stream(content);
        std::string line;
        std::string currentSection;

        while (std::getline(stream, line))
        {
            // Trim trailing \r
            auto trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                continue;

            if (trimmed[0] == '[')
            {
                auto endBracket = trimmed.find(']');
                if (endBracket != std::string::npos)
                {
                    currentSection = trimmed.substr(1, endBracket - 1);
                }
                continue;
            }

            auto eqPos = trimmed.find('=');
            if (eqPos != std::string::npos)
            {
                auto key = trim(trimmed.substr(0, eqPos));
                auto value = trim(trimmed.substr(eqPos + 1));
                if (!currentSection.empty() && !key.empty())
                {
                    addKey(currentSection, key, value);
                }
            }
        }

        // Re-seed after parsing so malformed values in the file fall back to
        // (and are persisted as) the defaults.
        ensure_video_defaults();

        logging::logInfo("Config loaded from {}", info.physicalPath);
    }

    void save()
    {
        // Group keys by section
        std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> sections;
        std::vector<std::string> sectionOrder;

        for (auto& entry : s_config)
        {
            // Parse "section.key" back apart
            auto dotPos = entry.first.find('.');
            if (dotPos == std::string::npos)
                continue;
            auto section = entry.first.substr(0, dotPos);
            auto key = entry.first.substr(dotPos + 1);

            if (sections.find(section) == sections.end())
            {
                // Use the order from s_keyOrder if possible
                sectionOrder.push_back(section);
            }
            sections[section].push_back({ key, entry.second });
        }

        // Sort section order to match s_keyOrder ordering
        std::unordered_map<std::string, int> sectionRank;
        for (size_t i = 0; i < s_keyOrder.size(); i++)
        {
            if (sectionRank.find(s_keyOrder[i].section) == sectionRank.end())
            {
                sectionRank[s_keyOrder[i].section] = static_cast<int>(i);
            }
        }
        for (auto& s : sectionOrder)
        {
            if (sectionRank.find(s) == sectionRank.end())
            {
                sectionRank[s] = INT_MAX;
            }
        }
        std::sort(sectionOrder.begin(), sectionOrder.end(), [&](const std::string& a, const std::string& b) {
            return sectionRank[a] < sectionRank[b];
        });

        // Sort keys within each section by first-seen order
        std::unordered_map<std::string, int> keyRank;
        for (size_t i = 0; i < s_keyOrder.size(); i++)
        {
            keyRank[s_keyOrder[i].key] = static_cast<int>(i);
        }

        // Build INI content
        std::ostringstream out;
        for (auto& section : sectionOrder)
        {
            out << "[" << section << "]\n";
            auto& keys = sections[section];
            std::sort(keys.begin(), keys.end(), [&](const auto& a, const auto& b) {
                int ra = keyRank.count(a.first) ? keyRank[a.first] : INT_MAX;
                int rb = keyRank.count(b.first) ? keyRank[b.first] : INT_MAX;
                return ra < rb;
            });
            for (auto& kv : keys)
            {
                out << kv.first << " = " << kv.second << "\n";
            }
            out << "\n";
        }

        auto content = out.str();
        auto info = system::fs::info("user://openre.ini");
        auto result = system::fs::writeAllBytes("user://openre.ini", content.data(), content.size());
        if (result == 0)
        {
            logging::logInfo("Config saved to {}", info.physicalPath);
        }
        else
        {
            logging::logError("[system::config] Failed to save config");
        }
    }

    bool remove_group(const std::string& group)
    {
        if (!s_loaded)
            load();

        std::string prefix = group + ".";
        bool removed = false;
        for (auto it = s_config.begin(); it != s_config.end();)
        {
            if (it->first.compare(0, prefix.size(), prefix) == 0)
            {
                it = s_config.erase(it);
                removed = true;
            }
            else
            {
                ++it;
            }
        }
        s_keyOrder.erase(
            std::remove_if(s_keyOrder.begin(), s_keyOrder.end(), [&](const SectionKey& sk) { return sk.section == group; }),
            s_keyOrder.end());
        return removed;
    }

    bool remove_key(const std::string& group, const std::string& name)
    {
        if (!s_loaded)
            load();

        std::string fullKey = group + "." + name;
        auto it = s_config.find(fullKey);
        if (it == s_config.end())
            return false;

        s_config.erase(it);
        s_keyOrder.erase(
            std::remove_if(
                s_keyOrder.begin(),
                s_keyOrder.end(),
                [&](const SectionKey& sk) { return sk.section == group && sk.key == name; }),
            s_keyOrder.end());
        return true;
    }

    template<> std::string get<std::string>(const std::string& group, const std::string& name, std::string default_value)
    {
        if (!s_loaded)
            load();

        std::string fullKey = group + "." + name;
        auto it = s_config.find(fullKey);
        if (it != s_config.end())
        {
            return it->second;
        }
        return default_value;
    }

    template<> int32_t get<int32_t>(const std::string& group, const std::string& name, int32_t default_value)
    {
        if (!s_loaded)
            load();

        std::string fullKey = group + "." + name;
        auto it = s_config.find(fullKey);
        if (it != s_config.end())
        {
            return static_cast<int32_t>(std::strtol(it->second.c_str(), nullptr, 10));
        }
        return default_value;
    }

    template<typename T> void set(std::string_view group, std::string_view name, T value)
    {
        if (!s_loaded)
            load();

        std::string groupStr(group);
        std::string nameStr(name);
        addKey(groupStr, nameStr, std::to_string(value));
    }

    // Explicit specialization for string
    template<> void set<std::string>(std::string_view group, std::string_view name, std::string value)
    {
        if (!s_loaded)
            load();

        std::string groupStr(group);
        std::string nameStr(name);
        addKey(groupStr, nameStr, value);
    }

    // Explicit specialization for string_view
    template<> void set<std::string_view>(std::string_view group, std::string_view name, std::string_view value)
    {
        if (!s_loaded)
            load();

        std::string groupStr(group);
        std::string nameStr(name);
        addKey(groupStr, nameStr, std::string(value));
    }

    // Explicit specialization for const char*
    template<> void set<const char*>(std::string_view group, std::string_view name, const char* value)
    {
        if (!s_loaded)
            load();

        std::string groupStr(group);
        std::string nameStr(name);
        addKey(groupStr, nameStr, std::string(value ? value : ""));
    }

    // Explicit specialization for int32_t
    template<> void set<int32_t>(std::string_view group, std::string_view name, int32_t value)
    {
        if (!s_loaded)
            load();

        std::string groupStr(group);
        std::string nameStr(name);
        addKey(groupStr, nameStr, std::to_string(value));
    }

    // Explicit specialization for uint32_t
    template<> void set<uint32_t>(std::string_view group, std::string_view name, uint32_t value)
    {
        if (!s_loaded)
            load();

        std::string groupStr(group);
        std::string nameStr(name);
        addKey(groupStr, nameStr, std::to_string(value));
    }

    void* get_binary(const std::string& group, const std::string& name, void* buffer, uint32_t size)
    {
        if (!s_loaded)
            load();

        if (buffer == nullptr || size == 0)
            return buffer;

        std::string fullKey = group + "." + name;
        auto it = s_config.find(fullKey);
        if (it != s_config.end())
        {
            auto decoded = hexDecode(it->second, buffer, size);
            if (decoded > 0)
            {
                return buffer;
            }
        }
        // Return the buffer even on failure to match original behavior
        return buffer;
    }

    bool set_binary(const std::string& group, const std::string& name, const void* data, uint32_t size)
    {
        if (!s_loaded)
            load();

        if (data == nullptr || size == 0)
            return false;

        auto hex = hexEncode(data, size);
        addKey(group, name, hex);
        return true;
    }

    Resolution get_resolution(std::string_view group, std::string_view name, const Resolution& default_value)
    {
        if (!s_loaded)
            load();

        auto fullKey = std::string(group) + "." + std::string(name);
        auto it = s_config.find(fullKey);
        if (it != s_config.end())
        {
            Resolution parsed;
            if (parse_resolution(it->second, parsed))
                return parsed;
        }

        // Absent or malformed: fall back to the default and store it so a
        // subsequent save() round-trips a valid value.
        set_resolution(group, name, default_value);
        return default_value;
    }

    void set_resolution(std::string_view group, std::string_view name, const Resolution& value)
    {
        if (!s_loaded)
            load();

        addKey(std::string(group), std::string(name), std::to_string(value.width) + "x" + std::to_string(value.height));
    }

    Resolution get_window_size()
    {
        return get_resolution("video", "window_size", kDefaultWindowSize);
    }

    Resolution get_render_resolution()
    {
        return get_resolution("video", "render_resolution", kDefaultRenderResolution);
    }
}
