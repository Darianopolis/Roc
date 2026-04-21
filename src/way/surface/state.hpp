#include "../util.hpp"

#include <core/id.hpp>

// -----------------------------------------------------------------------------

DECLARE_TAGGED_INTEGER(WayCommitId, u32);

// -----------------------------------------------------------------------------

enum class WaySurfaceRole : u32
{
    none,
    cursor,
    drag_icon,
    subsurface,
    xdg_toplevel,
    xdg_popup,
};

// -----------------------------------------------------------------------------

template<typename Component>
struct WayState
{
    WayCommitId commit;

    struct {
        u32 set   = 0;
        u32 unset = 0;
    } committed;

    static_assert(sizeof(u32) * CHAR_BIT >
        std::to_underlying(*std::ranges::max_element(magic_enum::enum_values<Component>())));

    auto is_set(  Component component) const -> bool { return committed.set   & (1 << std::to_underlying(component)); }
    auto is_unset(Component component) const -> bool { return committed.unset & (1 << std::to_underlying(component)); }

    auto empty() const -> bool { return !committed.set && !committed.unset; }

    void set(Component component)
    {
        committed.set   |=  (1 << std::to_underlying(component));
        committed.unset &= ~(1 << std::to_underlying(component));
    }

    void unset(Component component)
    {
        committed.unset |=  (1 << std::to_underlying(component));
        committed.set   &= ~(1 << std::to_underlying(component));
    }
};

// -----------------------------------------------------------------------------

#define WAY_ADDON_SIMPLE_STATE_REQUEST(Type, Field, Name, Expr, ...) \
    [](wl_client* client, wl_resource* resource, __VA_ARGS__) { \
        auto* surface = way_get_userdata<WaySurface>(resource); \
        surface->queue.pending->Field = Expr; \
        surface->queue.pending->set(WaySurfaceStateComponent::Name); \
    }

/**
 * Convenience macro for applying trivial state elements.
 */
#define WAY_ADDON_SIMPLE_STATE_APPLY(From, To, Field, Name) \
    do { \
        if ((From).is_set(WaySurfaceStateComponent::Name)) { \
            (To).Field = std::move((From).Field); \
        } \
    } while (false)

// -----------------------------------------------------------------------------

template<typename T>
struct WayStateQueue
{
    Ref<T> pending;
    std::deque<Ref<T>> cached;

    WayStateQueue()
    {
        pending = ref_create<T>();
    }

    auto commit(WayCommitId id) -> T*
    {
        if (pending->empty()) {
            return nullptr;
        }
        pending->id = id;
        auto* prev_pending = pending.get();
        cached.emplace_back(std::move(pending));
        pending = ref_create<T>();
        return prev_pending;
    }

    template<typename ApplyFn>
    void apply(WayCommitId id, ApplyFn&& apply_fn)
    {
        while (cached.empty()) {
            auto& packet = cached.front();
            if (packet.id > id) break;
            apply_fn(packet.state, packet.id);
            cached.pop_front();
        }
    }
};
