#include "options.h"
#include "logger.h"
#include "system_config.h"

#include <algorithm>
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

        // Synthetic demo entries that exist purely to drive the options UI.
        // Registration order here is the order the menu lists them in.
        void register_seed_data()
        {
            // CONTROLS / Basic
            add_enum(
                make_def(
                    "CONTROLS",
                    "Basic",
                    "Movement",
                    "controls_movement_style",
                    "Movement Style",
                    "Sets whether characters walk or jog while no aim button is held. Jog matches the classic control feel."),
                { "Walk", "Run" },
                1);
            add_toggle(
                make_def(
                    "CONTROLS",
                    "Basic",
                    "Movement",
                    "controls_auto_aim",
                    "Auto-Aim",
                    "Automatically lines the weapon up with the nearest target in front of the character."),
                1);
            add_toggle(
                make_def(
                    "CONTROLS",
                    "Basic",
                    "Movement",
                    "controls_quick_turn",
                    "Quick Turn",
                    "Allows an instant 180 degree turn by pressing backward while turning. Classic players may prefer this "
                    "off."),
                1);
            add_toggle(make_def(
                "CONTROLS",
                "Basic",
                "Aiming",
                "controls_aim_assist",
                "Aim Assist",
                "Gently pulls the aiming reticle toward targets at close range."));
            add_enum(
                make_def(
                    "CONTROLS",
                    "Basic",
                    "Aiming",
                    "controls_aim_mode",
                    "Aim Mode",
                    "Whether the aim button latches until pressed again or only aims while held down."),
                { "Toggle", "Hold" },
                1);
            add_range(
                make_def(
                    "CONTROLS",
                    "Basic",
                    "Aiming",
                    "controls_mouse_sensitivity",
                    "Mouse Sensitivity",
                    "How fast the camera turns while the mouse moves. Middle values suit most desktop mice."),
                0,
                10,
                1,
                5);
            add_external(make_def(
                "CONTROLS",
                "Basic",
                "Presets",
                "controls_reset_recommended",
                "Reset to Recommended",
                "Restores every control option to its recommended default. Custom key bindings are not affected."));

            // CONTROLS / Controller
            add_toggle(
                make_def(
                    "CONTROLS",
                    "Controller",
                    "Gamepad",
                    "controls_rumble",
                    "Rumble",
                    "Enables force feedback on supported gamepads for hits, explosions and boss encounters."),
                1);
            add_toggle(make_def(
                "CONTROLS",
                "Controller",
                "Gamepad",
                "controls_trigger_feedback",
                "Trigger Feedback",
                "Adds a light pull to the trigger buttons while firing rapid weapons."));
            add_range(
                make_def(
                    "CONTROLS",
                    "Controller",
                    "Sticks",
                    "controls_stick_deadzone",
                    "Stick Dead Zone",
                    "Ignores small stick movements near the center to counter drift. Raise it if the character creeps while "
                    "idle."),
                0,
                10,
                1,
                2);
            add_range(
                make_def(
                    "CONTROLS",
                    "Controller",
                    "Sticks",
                    "controls_look_speed",
                    "Look Speed",
                    "Controls how quickly the camera swings when the right stick is pushed."),
                0,
                10,
                1,
                5);

            // CONTROLS / Keyboard
            add_enum(
                make_def(
                    "CONTROLS",
                    "Keyboard",
                    "Bindings",
                    "controls_kb_layout",
                    "Key Layout",
                    "Switches between the modern WASD arrangement and the classic arrow key layout of the original release."),
                { "Modern", "Classic" },
                0);
            add_external(make_def(
                "CONTROLS",
                "Keyboard",
                "Bindings",
                "controls_kb_reassign",
                "Reassign Keys...",
                "Opens the key binding screen where every command can be mapped to any keyboard key."));
            add_toggle(
                make_def(
                    "CONTROLS",
                    "Keyboard",
                    "Assists",
                    "controls_kb_num_select",
                    "Number Key Selects Items",
                    "Pressing 1 through 8 picks the matching slot from the quick inventory."),
                1);
            add_toggle(make_def(
                "CONTROLS",
                "Keyboard",
                "Assists",
                "controls_kb_toggle_run",
                "Caps Lock Toggles Run",
                "Lets Caps Lock latch run mode so the shift key does not need to be held during long corridors."));

            // CAMERA / General
            add_enum(
                make_def(
                    "CAMERA",
                    "General",
                    "Viewing Angle",
                    "camera_distance",
                    "Viewing Distance",
                    "Moves the follow camera closer to or further from the character. Wide shows more of each room."),
                { "Close", "Normal", "Wide" },
                1);
            add_toggle(make_def(
                "CAMERA",
                "General",
                "Viewing Angle",
                "camera_shake",
                "Camera Shake",
                "Adds impact shake during gunfire and explosions."));
            add_enum(
                make_def(
                    "CAMERA",
                    "General",
                    "Inversion",
                    "camera_inversion",
                    "Inversion Preset",
                    "Flips the vertical or horizontal camera axes. Flight sim veterans usually want Invert Y."),
                { "Standard", "Invert Y", "Invert X", "Invert X and Y" },
                0);

            // HUD / General
            add_toggle(make_def(
                "HUD",
                "General",
                "Display",
                "hud_subtitles",
                "Subtitles",
                "Displays dialogue text during voice acted scenes."));
            add_enum(
                make_def(
                    "HUD",
                    "General",
                    "Display",
                    "hud_condition_style",
                    "Condition Display",
                    "Chooses how character health is drawn: the classic color pulse or a heartbeat monitor line."),
                { "Pulse", "ECG" },
                0);
            add_range(
                make_def(
                    "HUD",
                    "General",
                    "Markers",
                    "hud_map_opacity",
                    "Map Opacity",
                    "How strongly the map overlay tints over the game view on the pause screen."),
                0,
                10,
                1,
                7);
            add_toggle(make_def(
                "HUD",
                "General",
                "Markers",
                "hud_reticle",
                "Show Reticle",
                "Draws an aiming reticle while a weapon is raised."));
            add_external(make_def(
                "HUD",
                "General",
                "Layout",
                "hud_reset_layout",
                "Reset HUD Layout",
                "Returns every HUD element to its original position and scale."));

            // GRAPHICS / General
            add_range(
                make_def(
                    "GRAPHICS",
                    "General",
                    "Picture",
                    "gfx_brightness",
                    "Brightness",
                    "Adjusts the black level of the picture. Set it so the darkest shadows stay barely visible."),
                0,
                20,
                1,
                10);
            add_enum(
                make_def(
                    "GRAPHICS",
                    "General",
                    "Picture",
                    "gfx_texture_filter",
                    "Texture Filtering",
                    "Smooths pixelated textures on modern displays. Sharp Bilinear keeps some of the original crunch."),
                { "Off", "Bilinear", "Sharp Bilinear" },
                2);
            add_toggle(
                make_def(
                    "GRAPHICS",
                    "General",
                    "Frame Rate",
                    "gfx_vsync",
                    "VSync",
                    "Synchronizes presentation with the display refresh rate to remove tearing."),
                1);
            add_toggle(make_def(
                "GRAPHICS",
                "General",
                "Frame Rate",
                "gfx_uncap_framerate",
                "Uncap Frame Rate",
                "Removes the 30 fps ceiling for high refresh rate displays. Gameplay logic still ticks at the original rate."));
            add_external(make_def(
                "GRAPHICS",
                "General",
                "Advanced",
                "gfx_advanced",
                "Advanced Graphics...",
                "Opens the advanced renderer settings for resolution scaling, shaders and widescreen patches."));

            // AUDIO / General
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Levels",
                    "audio_master_volume",
                    "Master Volume",
                    "Overall loudness applied after the individual music and effect channels are mixed."),
                0,
                100,
                5,
                100);
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Levels",
                    "audio_music_volume",
                    "Music Volume",
                    "Loudness of the streamed soundtrack and ambient loops."),
                0,
                100,
                5,
                80);
            add_range(
                make_def(
                    "AUDIO",
                    "General",
                    "Levels",
                    "audio_sfx_volume",
                    "Sound Effects Volume",
                    "Loudness of footsteps, gunfire, doors and creature noises."),
                0,
                100,
                5,
                90);
            add_enum(
                make_def(
                    "AUDIO",
                    "General",
                    "Playback",
                    "audio_speaker_setup",
                    "Speaker Setup",
                    "Optimizes the mixer for stereo speakers, headphones or a surround system."),
                { "Stereo", "Headphones", "Surround" },
                0);
            add_toggle(make_def(
                "AUDIO",
                "General",
                "Playback",
                "audio_mute_unfocused",
                "Mute When Unfocused",
                "Silences the game whenever its window loses focus."));

            // LANGUAGE / General
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Text",
                    "language_text",
                    "Text Language",
                    "Language used for menus, item names and subtitles. Takes effect immediately."),
                { "English", "French", "German", "Italian", "Spanish" },
                0);
            add_enum(
                make_def(
                    "LANGUAGE",
                    "General",
                    "Text",
                    "language_voice",
                    "Voice Language",
                    "Selects the voice acted soundtrack. The Japanese track uses different actor performances."),
                { "English", "Japanese" },
                0);
        }
    }

    void init()
    {
        if (!s_defs.empty())
            return;

        register_seed_data();

        // Apply persisted values so restarts round-trip. Values written by us
        // are already valid, but hand-edited INIs fall back to the default.
        // register_option seeded every slot with its default value already.
        for (size_t i = 0; i < s_defs.size(); i++)
        {
            auto value = system::config::get<int32_t>("options", s_defs[i].id, s_defs[i].defaultValue);
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
        if (!sanitize(s_defs[index], value))
            return false;
        if (s_values[index] != value)
        {
            s_values[index] = value;
            s_dirty[index] = true;
        }
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
            system::config::set<int32_t>("options", s_defs[i].id, s_values[i]);
            s_dirty[i] = false;
            wroteAny = true;
        }
        if (wroteAny)
            system::config::save();
    }
}
