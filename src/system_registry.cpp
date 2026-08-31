#include "system_registry.h"

#include "logger.h"

#include <algorithm>

namespace openre
{
    SystemRegistry& systemRegistry()
    {
        static SystemRegistry registry;
        return registry;
    }

    SystemDef& SystemRegistry::registerNative(
        std::string name, std::vector<std::string> after, std::vector<std::string> before, SystemPlacement placement,
        SystemFn fn)
    {
        SystemDef def;
        def.name = std::move(name);
        def.kind = SystemKind::Native;
        def.after = std::move(after);
        def.before = std::move(before);
        def.placement = placement;
        def.nativeFn = fn;
        m_defs.push_back(std::move(def));
        m_dirty = true;
        return m_defs.back();
    }

    SystemDef& SystemRegistry::registerManaged(
        std::string name, std::vector<std::string> after, std::vector<std::string> before, SystemPlacement placement,
        int managedId)
    {
        SystemDef def;
        def.name = std::move(name);
        def.kind = SystemKind::Managed;
        def.after = std::move(after);
        def.before = std::move(before);
        def.placement = placement;
        def.managedId = managedId;
        m_defs.push_back(std::move(def));
        m_dirty = true;
        return m_defs.back();
    }

    bool SystemRegistry::removeManaged(const std::string& name)
    {
        for (auto it = m_defs.begin(); it != m_defs.end(); ++it)
        {
            if (it->kind == SystemKind::Managed && it->name == name)
            {
                m_defs.erase(it);
                m_dirty = true;
                return true;
            }
        }
        return false;
    }

    void SystemRegistry::setManagedRunner(void (*fn)(int managedId))
    {
        m_managedRunner = fn;
    }

    SystemDef* SystemRegistry::find(const std::string& name)
    {
        for (auto& def : m_defs)
        {
            if (def.name == name)
                return &def;
        }
        return nullptr;
    }

    const SystemDef* SystemRegistry::find(const std::string& name) const
    {
        for (const auto& def : m_defs)
        {
            if (def.name == name)
                return &def;
        }
        return nullptr;
    }

    const std::vector<SystemDef*>& SystemRegistry::ordered()
    {
        if (m_dirty)
            recompute();
        return m_order;
    }

    void SystemRegistry::run(AppContext& app)
    {
        // Reset the paced-frame signal: the frame producers (core.Module,
        // core.LegacyGame) set it when they advance the game on a throttled
        // frame, and only then are managed Lua systems dispatched (see below).
        app.framePaced = false;

        // Snapshot the dispatch list by value up front: Lua-managed systems
        // may call re.registerSystem()/resetManagedSystems() while the loop
        // runs, which can reallocate `m_defs` and invalidate every pointer in
        // `m_order`. Dispatching from the snapshot means systems added
        // mid-frame run on the next frame, and the in-flight loop can never
        // dereference a moved/freed SystemDef.
        struct Dispatch
        {
            SystemKind kind;
            SystemFn nativeFn = nullptr;
            int managedId = -1;
        };
        std::vector<Dispatch> dispatch;
        dispatch.reserve(m_defs.size());
        for (const SystemDef* def : ordered())
        {
            dispatch.push_back({ def->kind, def->nativeFn, def->managedId });
        }

        for (const Dispatch& d : dispatch)
        {
            if (d.kind == SystemKind::Native && d.nativeFn != nullptr)
            {
                d.nativeFn(app);
            }
            else if (d.kind == SystemKind::Managed && m_managedRunner != nullptr && app.framePaced)
            {
                // Managed Lua systems replace the old tick hooks, which ran
                // inside script::tick() on the frame-rate-throttled cadence
                // (in sync with the input walk). Only dispatch them on frames
                // where ModuleSystem/LegacyGameSystem actually advanced the
                // game (framePaced set by the producers); the uncapped
                // iterations between paced frames must not run them or their
                // input handling desyncs (one tap stays visible across several
                // update() calls).
                m_managedRunner(d.managedId);
            }
        }
    }

    bool SystemRegistry::reachesThroughBefore(const SystemDef* from, const SystemDef* to) const
    {
        // DFS over `before` edges: can `to` be reached from `from`?
        if (from == nullptr || to == nullptr)
            return false;
        std::vector<const SystemDef*> stack;
        std::vector<const SystemDef*> visited;
        stack.push_back(from);
        while (!stack.empty())
        {
            const SystemDef* cur = stack.back();
            stack.pop_back();
            if (cur == to)
                return true;
            if (std::find(visited.begin(), visited.end(), cur) != visited.end())
                continue;
            visited.push_back(cur);
            for (const auto& nextName : cur->before)
            {
                if (const SystemDef* next = find(nextName); next != nullptr)
                    stack.push_back(next);
            }
        }
        return false;
    }

    void SystemRegistry::recompute()
    {
        // Phase 1: insert systems in registration order. Each insertion finds
        // the feasible window [earliest, latest) among already-placed systems
        // (after all `after` targets, before all `before` targets) and picks a
        // slot by placement: earliest, midpoint, or latest.
        m_order.clear();
        m_order.reserve(m_defs.size());

        for (auto& def : m_defs)
        {
            size_t earliest = 0;
            size_t latest = m_order.size();
            for (const auto& name : def.after)
            {
                if (const SystemDef* target = find(name); target != nullptr)
                {
                    auto pos = std::find(m_order.begin(), m_order.end(), target);
                    if (pos != m_order.end())
                        earliest = std::max(earliest, static_cast<size_t>(std::distance(m_order.begin(), pos) + 1));
                }
            }
            for (const auto& name : def.before)
            {
                if (const SystemDef* target = find(name); target != nullptr)
                {
                    auto pos = std::find(m_order.begin(), m_order.end(), target);
                    if (pos != m_order.end())
                        latest = std::min(latest, static_cast<size_t>(std::distance(m_order.begin(), pos)));
                }
            }

            size_t insertAt = latest;
            if (earliest > latest)
            {
                // Empty window: before wins. Place immediately before the
                // earliest before-target so the before constraints hold; the
                // conflicting after constraints are ignored.
                insertAt = latest;
                logging::logWarning(
                    "[system_registry] system '{}' has conflicting after/before constraints; placing by before "
                    "constraints only",
                    def.name);
            }
            else if (earliest == latest)
            {
                insertAt = earliest;
            }
            else
            {
                switch (def.placement)
                {
                case SystemPlacement::Early: insertAt = earliest; break;
                case SystemPlacement::Late: insertAt = latest - 1; break;
                case SystemPlacement::Default: insertAt = earliest + (latest - earliest) / 2; break;
                }
            }

            m_order.insert(m_order.begin() + insertAt, &def);
        }

        // Phase 2: repair any before-edge that ended up inverted. Move the
        // system backwards until it runs before its before-target; when a
        // before-edge would close a true cycle, drop it and log which one.
        const size_t maxPasses = m_defs.size() * m_defs.size() + 2;
        for (size_t pass = 0; pass < maxPasses; ++pass)
        {
            bool changed = false;
            for (auto& def : m_defs)
            {
                for (auto it = def.before.begin(); it != def.before.end();)
                {
                    const SystemDef* target = find(*it);
                    auto posD = std::find(m_order.begin(), m_order.end(), &def);
                    auto posB = std::find(m_order.begin(), m_order.end(), target);
                    if (posD == m_order.end() || posB == m_order.end() || posD < posB)
                    {
                        ++it;
                        continue;
                    }
                    // Inverted: `def` must run before `target`. If moving it
                    // would re-break this same edge, the edge closes a cycle.
                    if (reachesThroughBefore(target, &def))
                    {
                        logging::logWarning(
                            "[system_registry] dropping before-constraint '{}' -> '{}' (would create a cycle)", def.name, *it);
                        it = def.before.erase(it);
                        changed = true;
                        continue;
                    }
                    // Move `def` to just before `target`.
                    m_order.erase(posD);
                    size_t posNow = static_cast<size_t>(
                        std::distance(m_order.begin(), std::find(m_order.begin(), m_order.end(), target)));
                    m_order.insert(m_order.begin() + posNow, &def);
                    changed = true;
                    break; // positions moved; re-check outer loop
                }
            }
            if (!changed)
                break;
        }
    }
}