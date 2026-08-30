#include "options.h"
#include "display.h"
#include "logger.h"
#include "system_config.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace openre::options
{
    namespace
    {
        std::vector<Def> s_defs;
        std::vector<int32_t> s_values;
        std::vector<bool> s_dirty;
        std::unordered_map<std::string, int> s_indexById;

        // Clamps a raw value into the option's legal state space. Returns
        // false for values that have no representation (external entries,
        // out-of-range enum indexes, non-binary toggles); range values are
        // always repaired in place.
        bool sanitize(const Def& def, int32_t& value)
        {
            switch (def.kind)
            {
            case OptionKind::Toggle: return value == 0 || value == 1;
            case OptionKind::Enum: return value >= 0 && value < static_cast<int32_t>(def.labels.size());
            case OptionKind::Range:
            {
                value = std::clamp(value, def.min, def.max);
                if (def.step > 1)
                    value = def.min + ((value - def.min) / def.step) * def.step;
                return true;
            }
            case OptionKind::External: return false;
            }
            return false;
        }

        // Fills in the fields every seed entry shares so the registrations
        // below only list what makes each option unique.
        Def make_def(
            const char* category, const char* subcategory, const char* group, const char* id, const char* title,
            const char* tooltip)
        {
            Def def;
            def.id = id;
            def.category = category;
            def.subcategory = subcategory;
            def.group = group;
            def.title = title;
            def.tooltip = tooltip;
            return def;
        }

        void add_toggle(Def def, int32_t defaultValue = 0)
        {
            def.kind = OptionKind::Toggle;
            def.labels = { "Off", "On" };
            def.defaultValue = defaultValue;
            register_option(def);
        }

        void add_enum(Def def, std::initializer_list<const char*> labels, int32_t defaultValue)
        {
            def.kind = OptionKind::Enum;
            def.labels.assign(labels.begin(), labels.end());
            def.defaultValue = defaultValue;
            register_option(def);
        }

        void add_range(Def def, int32_t min, int32_t max, int32_t step, int32_t defaultValue)
        {
            def.kind = OptionKind::Range;
            def.min = min;
            def.max = max;
            def.step = step;
            def.defaultValue = defaultValue;
            register_option(def);
        }

        void add_external(Def def)
        {
            def.kind = OptionKind::External;
            register_option(def);
        }

        // Real option definitions mirroring the RE2 remake options tree.
        // Registration order here is the order the menu lists them in.
        void register_seed_data()
        {
            // GAMEPLAY / General
            add_toggle(
                make_def(
                    "GAMEPLAY",
                    "General",
                    "Mechanics",
                    "gameplay_auto_aim",
                    "Auto Aim",
                    "Automatically lines the weapon up with the nearest target in front of the character."),
                1);
            add_toggle(
                make_def(
                    "GAMEPLAY",
                    "General",
                    "Mechanics",
                    "gameplay_quick_turn",
                    "Quick Turn",
                    "Performs an instant 180 degree turn when moving backwards while turning."),
                1);
            add_toggle(
                make_def(
                    "GAMEPLAY",
                    "General",
                    "Mechanics",
                    "gameplay_tactical_reload",
                    "Tactical Reload",
                    "Reloads the current weapon while the inventory or map is open."),
                1);
            add_toggle(
                make_def(
                    "GAMEPLAY",
                    "General",
                    "Cheats",
                    "gameplay_infinite_ammo",
                    "Infinite Ammo",
                    "Weapons never consume ammunition while this is enabled."),
                0);
            add_toggle(
                make_def(
                    "GAMEPLAY",
                    "General",
                    "Cheats",
                    "gameplay_invincibility",
                    "Invincibility",
                    "The player character takes no damage from any source."),
                0);
            add_toggle(
                make_def(
                    "GAMEPLAY",
                    "General",
                    "Cheats",
                    "gameplay_invisibility",
                    "Invisibility",
                    "Enemies are unable to detect the player character."),
                0);
            // CONTROLS / Controller
            add_enum(
                make_def(
                    "CONTROLS",
                    "Controller",
                    "Button Settings",
                    "controls_control_type",
                    "Control Type",
                    "Switches between the Type A and Type B button layouts."),
                { "Type A", "Type B" },
                0);
            add_external(make_def(
                "CONTROLS",
                "Controller",
                "Button Settings",
                "controls_button_configuration",
                "Button Configuration",
                "Opens the screen where controller buttons can be remapped."));
            add_enum(
                make_def(
                    "CONTROLS",
                    "Controller",
                    "Button Settings",
                    "controls_button_icon_type",
                    "Button Icon Type",
                    "Chooses the controller family shown by the on-screen button icons."),
                { "Xbox", "PlayStation", "Nintendo" },
                0);
            add_enum(
                make_def(
                    "CONTROLS",
                    "Controller",
                    "Function Settings",
                    "controls_controller_vibration",
                    "Controller Vibration",
                    "Strength of controller vibration feedback during gameplay."),
                { "Off", "Weak", "Normal", "Strong" },
                2);
            add_external(make_def(
                "CONTROLS",
                "Controller",
                "Function Settings",
                "controls_deadzone_sensitivity",
                "Deadzone and Sensitivity",
                "Opens the screen for adjusting stick deadzones and camera sensitivity."));
            // CONTROLS / Keyboard and Mouse
            add_external(make_def(
                "CONTROLS",
                "Keyboard and Mouse",
                "Keyboard and Mouse Settings",
                "controls_key_configuration",
                "Key Configuration",
                "Opens the screen where keyboard and mouse actions can be remapped."));
            add_enum(
                make_def(
                    "CONTROLS",
                    "Keyboard and Mouse",
                    "Keyboard and Mouse Settings",
                    "controls_keyboard_type",
                    "Keyboard Type",
                    "Keyboard layout used for the displayed key labels."),
                { "QWERTY (English)" },
                0);
            // GRAPHICS / Display
            {
                Def def = make_def(
                    "GRAPHICS",
                    "Display",
                    "Display",
                    "fullscreen",
                    "Display Mode",
                    "Runs the game in a window or switches to a borderless fullscreen presentation on the selected monitor.");
                def.section = "video";
                def.kind = OptionKind::Enum;
                def.labels = { "Window", "Fullscreen" };
                def.defaultValue = 0;
                def.onChanged = [](int32_t value) { display::set_fullscreen(value); };
                register_option(def);
            }
            {
                Def def = make_def(
                    "GRAPHICS",
                    "Display",
                    "Display",
                    "widescreen",
                    "Widescreen",
                    "Switches the picture between the 4:3 and 16:9 aspect ratios, snapping the resolution to match.");
                def.section = "video";
                def.kind = OptionKind::Toggle;
                def.labels = { "Off", "On" };
                def.defaultValue = 0;
                def.onChanged = [](int32_t value) { display::set_widescreen(value); };
                register_option(def);
            }
            {
                Def def = make_def(
                    "GRAPHICS",
                    "Display",
                    "Display",
                    "monitor",
                    "Output Display Settings",
                    "Selects which connected monitor the game is presented on.");
                def.section = "video";
                def.kind = OptionKind::Enum;
                def.defaultValue = 0;
                // Labels enumerate the connected monitors each time the menu lists them.
                def.refresh = [](Def& d) { d.labels = display::monitor_labels(); };
                // The [video] key is a 1-based display index.
                def.encode = [](int32_t value) { return std::to_string(value + 1); };
                def.decode = [](const std::string& text) {
                    int32_t value = 0;
                    if (std::sscanf(text.c_str(), "%d", &value) == 1 && value >= 1)
                        return value - 1;
                    return 0;
                };
                def.onChanged = [](int32_t value) { display::set_monitor(value); };
                register_option(def);
            }
            add_enum(
                make_def(
                    "GRAPHICS",
                    "Display",
                    "Display",
                    "font",
                    "Font",
                    "Switches UI text between the original sprite fonts and a bundled TTF font."),
                { "Retro", "Modern" },
                0);
            // GRAPHICS / Image
            {
                Def def = make_def(
                    "GRAPHICS",
                    "Image",
                    "General",
                    "screen_resolution",
                    "Screen Resolution",
                    "Resolution the game renders the scene at before it is scaled to the display. Lists follow the current "
                    "aspect ratio.");
                def.section = "video";
                def.kind = OptionKind::Enum;
                def.defaultValue = 0;
                // Labels follow the current widescreen aspect-ratio list.
                def.refresh = [](Def& d) { d.labels = display::resolution_labels(); };
                // The [video] key is the literal "1280x720" string.
                def.encode = [](int32_t value) { return display::resolution_string(value); };
                def.decode = [](const std::string& text) { return display::resolution_index(text); };
                def.onChanged = [](int32_t value) { display::set_screen_resolution(value); };
                register_option(def);
            }
            // AUDIO / General
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Volume",
                    "audio_master_volume",
                    "Master Volume",
                    "Overall loudness applied after the individual channels are mixed."),
                0,
                10,
                1,
                10);
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Volume",
                    "audio_sound_effects",
                    "Sound Effects",
                    "Loudness of footsteps, gunfire, doors and creature noises."),
                0,
                10,
                1,
                10);
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Volume",
                    "audio_background_music",
                    "Background Music",
                    "Loudness of the streamed soundtrack and ambient loops."),
                0,
                10,
                1,
                10);
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Volume",
                    "audio_menu_sound_effects",
                    "Menu Sound Effects",
                    "Loudness of interface and menu feedback sounds."),
                0,
                10,
                1,
                10);
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Volume",
                    "audio_dialogue_voices",
                    "Dialogue Voices",
                    "Loudness of voiced dialogue during cutscenes and gameplay."),
                0,
                10,
                1,
                10);
            add_enum(
                make_def("AUDIO", "General", "Output", "audio_device", "Audio Device", "Playback device used for game audio."),
                { "System Default" },
                0);
            // LANGUAGE / General
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Language",
                    "language_voice",
                    "Voice Language",
                    "Selects the voiced soundtrack language."),
                { "English", "Japanese" },
                0);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Language",
                    "language_display",
                    "Display Language",
                    "Language used for menus, item names and subtitles."),
                { "English", "Japanese", "French", "German", "Italian", "Spanish" },
                0);
            add_toggle(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Subtitles",
                    "language_subtitles",
                    "Subtitles",
                    "Displays dialogue text during voiced scenes."),
                1);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Subtitles",
                    "language_subtitle_language",
                    "Subtitle Language",
                    "Language used for subtitle text."),
                { "English", "Japanese", "French", "German", "Italian", "Spanish" },
                0);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Subtitles",
                    "language_subtitle_size",
                    "Subtitle Size",
                    "On-screen size of subtitle text."),
                { "Standard" },
                0);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Subtitles",
                    "language_subtitle_background",
                    "Subtitle Background",
                    "Draws a backing box behind subtitle text for readability."),
                { "Off", "On" },
                0);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Subtitles",
                    "language_subtitle_color",
                    "Subtitle Color",
                    "Text color used for subtitle lines."),
                { "White", "Yellow" },
                0);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Subtitles",
                    "language_speaker_name_display",
                    "Speaker Name Display",
                    "Shows the speaking character's name before each subtitle line."),
                { "Off", "On" },
                0);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Subtitles",
                    "language_speaker_name_color",
                    "Speaker Name Color",
                    "Text color used for the speaker name."),
                { "White", "Yellow" },
                0);
        }
    }

    void init()
    {
        if (!s_defs.empty())
            return;

        register_seed_data();
        refresh();

        // Apply persisted values so restarts round-trip. Values written by us
        // are already valid, but hand-edited INIs fall back to the default.
        // register_option seeded every slot with its default value already.
        for (size_t i = 0; i < s_defs.size(); i++)
        {
            int32_t value;
            if (s_defs[i].decode)
            {
                value = s_defs[i].decode(system::config::get<std::string>(s_defs[i].section, s_defs[i].id, ""));
            }
            else
            {
                value = system::config::get<int32_t>(s_defs[i].section, s_defs[i].id, s_defs[i].defaultValue);
            }
            if (!sanitize(s_defs[i], value))
                value = s_defs[i].defaultValue;
            s_values[i] = value;
        }

        logging::logInfo("Options registry initialized with {} entries", s_defs.size());
    }

    int register_option(const Def& def)
    {
        if (def.id.empty() || s_indexById.find(def.id) != s_indexById.end())
            return -1;

        auto index = static_cast<int>(s_defs.size());
        s_indexById.emplace(s_defs.emplace_back(def).id, index);
        s_values.push_back(def.defaultValue);
        s_dirty.push_back(false);
        return index;
    }

    int count()
    {
        return static_cast<int>(s_defs.size());
    }

    const Def& def(int index)
    {
        return s_defs.at(index);
    }

    void refresh()
    {
        for (auto& def : s_defs)
        {
            if (def.refresh)
                def.refresh(def);
        }
    }

    int index_of(std::string_view id)
    {
        auto it = s_indexById.find(std::string(id));
        return it == s_indexById.end() ? -1 : it->second;
    }

    int32_t get(std::string_view id)
    {
        auto index = index_of(id);
        return index < 0 ? 0 : get_by_index(index);
    }

    int32_t get_by_index(int index)
    {
        if (index < 0 || index >= count())
            return 0;
        return s_values[index];
    }

    bool set(std::string_view id, int32_t value)
    {
        auto index = index_of(id);
        if (index < 0)
            return false;
        auto& def = s_defs[index];
        if (def.refresh)
            def.refresh(def);
        if (!sanitize(def, value))
            return false;
        if (s_values[index] != value)
        {
            s_values[index] = value;
            s_dirty[index] = true;
        }
        if (def.onChanged)
            def.onChanged(s_values[index]);
        return true;
    }

    bool dirty(int index)
    {
        if (index < 0 || index >= count())
            return false;
        return s_dirty[index];
    }

    void flush()
    {
        bool wroteAny = false;
        for (size_t i = 0; i < s_defs.size(); i++)
        {
            if (!s_dirty[i])
                continue;
            if (s_defs[i].encode)
                system::config::set<std::string>(s_defs[i].section, s_defs[i].id, s_defs[i].encode(s_values[i]));
            else
                system::config::set<int32_t>(s_defs[i].section, s_defs[i].id, s_values[i]);
            s_dirty[i] = false;
            wroteAny = true;
        }
        if (wroteAny)
            system::config::save();
    }
}
