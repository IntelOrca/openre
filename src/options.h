#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openre::options
{
    // How an option presents and validates its value.
    enum class OptionKind : uint8_t
    {
        // Boolean stored as 0/1 and shown with two labels (Off/On).
        Toggle,
        // Stored value is an index into Def::labels.
        Enum,
        // Integer clamped to [min, max] and snapped onto the step grid.
        Range,
        // A button-style entry with no value of its own; set() rejects it.
        External,
    };

    // Static description of one option. Strings are copied on registration,
    // so callers may pass temporaries.
    struct Def
    {
        // Stable persistence key inside the [options] INI section.
        std::string id;
        // Outer tab, inner tab and section header driving menu layout.
        // Options list in registration order; a changed group value starts a
        // new section.
        std::string category;
        std::string subcategory;
        std::string group;
        std::string title;
        std::string tooltip;
        OptionKind kind = OptionKind::Toggle;
        // Value labels for Toggle/Enum kinds (e.g. {"Off", "On"}).
        std::vector<std::string> labels;
        // Bounds for the Range kind (step <= 1 means unstepped).
        int32_t min = 0;
        int32_t max = 0;
        int32_t step = 1;
        // Value applied on first run and when nothing valid was persisted.
        int32_t defaultValue = 0;
    };

    // Registers the seed data and applies persisted values from the
    // [options] INI section. Must run before any consumer reads values.
    void init();

    // Adds a definition (and its starting value) to the registry. Returns
    // the stable registration index, or -1 when the id is empty or already
    // taken. Registration order defines listing order forever after.
    int register_option(const Def& def);

    // Number of registered options.
    int count();

    // Definition in registration order; index must be within [0, count()).
    const Def& def(int index);

    // Registration index for an option id, or -1 when unknown.
    int index_of(std::string_view id);

    // Current value by id (canonical) or by registration index. Returns 0
    // for unknown ids and out-of-range indexes.
    int32_t get(std::string_view id);
    int32_t get_by_index(int index);

    // Validates and stores a new value, marking the option dirty when the
    // value changed. Returns false for unknown ids and for values rejected
    // by the option's kind: enum indexes outside the label list, toggles
    // other than 0/1 and external entries. Range values are clamped to
    // [min, max] and snapped down onto the step grid first.
    bool set(std::string_view id, int32_t value);

    // True while the option has changes not yet written to disk.
    bool dirty(int index);

    // Debounced persistence: writes every dirty option into the [options]
    // INI section and saves once afterwards. No-op when nothing is dirty.
    void flush();
}
