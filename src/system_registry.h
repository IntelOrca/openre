#pragma once

#include "systems.h"

#include <string>
#include <vector>

namespace openre
{
    // Preferred placement of a system within the ordered list: as early as
    // possible, at the middle of its feasible window, or as late as possible.
    enum class SystemPlacement
    {
        Early = 0,
        Default = 1,
        Late = 2,
    };

    enum class SystemKind
    {
        Native = 0,  // C++ app-shell system
        Managed = 1, // Lua-managed ECS system
    };

    // One registered system. `after` lists systems that must run before this
    // one; `before` lists systems this one must run before. Native systems
    // carry a `core.` prefix and no "System" suffix in their canonical name.
    struct SystemDef
    {
        std::string name;
        SystemKind kind = SystemKind::Native;
        std::vector<std::string> after;
        std::vector<std::string> before;
        SystemPlacement placement = SystemPlacement::Default;
        SystemFn nativeFn = nullptr; // when kind == Native
        int managedId = -1;          // when kind == Managed (dispatch key)
    };

    // Single mixed registry for app-shell (native) and Lua-managed systems.
    // The dispatch order is derived from the after/before constraints plus an
    // early/default/late placement, never an authored priority. Ordering is
    // deterministic and recomputed lazily whenever the system set changes.
    class SystemRegistry
    {
    public:
        SystemRegistry() = default;

        SystemDef& registerNative(
            std::string name, std::vector<std::string> after, std::vector<std::string> before, SystemPlacement placement,
            SystemFn fn);
        SystemDef& registerManaged(
            std::string name, std::vector<std::string> after, std::vector<std::string> before, SystemPlacement placement,
            int managedId);

        bool removeManaged(const std::string& name);
        void clearManaged();

        // Managed entries dispatch back into Lua through this callback.
        void setManagedRunner(void (*fn)(int managedId));

        // Systems in resolved run order (recomputed when the set changes).
        const std::vector<SystemDef*>& ordered();

        // Run every system once, in order.
        void run(AppContext& app);

    private:
        void recompute();
        SystemDef* find(const std::string& name);
        const SystemDef* find(const std::string& name) const;
        bool reachesThroughBefore(const SystemDef* from, const SystemDef* to) const;

        std::vector<SystemDef> m_defs;
        std::vector<SystemDef*> m_order;
        bool m_dirty = true;
        void (*m_managedRunner)(int managedId) = nullptr;
    };

    // The process-wide system registry.
    SystemRegistry& systemRegistry();
}