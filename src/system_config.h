#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openre::system::config
{
    void load();
    void save();
    // Removes every key belonging to `group` (e.g. "input"), so the next save
    // drops the whole section. Returns true if any keys were removed. Used to
    // reset a stale/obsolete config section.
    bool remove_group(const std::string& group);

    template<typename T> T get(const std::string& group, const std::string& name, T default_value);

    // Explicit specializations
    template<> std::string get<std::string>(const std::string& group, const std::string& name, std::string default_value);

    template<> int32_t get<int32_t>(const std::string& group, const std::string& name, int32_t default_value);

    template<typename T> void set(std::string_view group, std::string_view name, T value);

    // Explicit specializations
    template<> void set<std::string>(std::string_view group, std::string_view name, std::string value);

    template<> void set<std::string_view>(std::string_view group, std::string_view name, std::string_view value);

    template<> void set<const char*>(std::string_view group, std::string_view name, const char* value);

    template<> void set<int32_t>(std::string_view group, std::string_view name, int32_t value);

    template<> void set<uint32_t>(std::string_view group, std::string_view name, uint32_t value);

    // Binary read/write
    void* get_binary(const std::string& group, const std::string& name, void* buffer, uint32_t size);
    bool set_binary(const std::string& group, const std::string& name, const void* data, uint32_t size);

    // A two-dimensional resolution, stored in the INI as a "WxH" string.
    struct Resolution
    {
        int32_t width = 0;
        int32_t height = 0;
    };

    // Parses the "WxH" value stored under `group`.`name`. When the key is
    // absent or malformed, `default_value` is returned and stored so a
    // subsequent save() round-trips the key.
    Resolution get_resolution(std::string_view group, std::string_view name, const Resolution& default_value);

    // Stores `value` under `group`.`name` as a "WxH" string.
    void set_resolution(std::string_view group, std::string_view name, const Resolution& value);

    // Convenience accessors for the [video] section (defaults "800x600" /
    // "640x480").
    Resolution get_window_size();
    Resolution get_render_resolution();
}
