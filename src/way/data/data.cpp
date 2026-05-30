#include "data.hpp"

#include "../seat/seat.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

static
auto to_wayland_dnd_action(Flags<SeatDndAction> action) -> wl_data_device_manager_dnd_action
{
    uint32_t wl = {};
    if (action.contains(SeatDndAction::copy)) wl |= WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    if (action.contains(SeatDndAction::move)) wl |= WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
    if (action.contains(SeatDndAction::ask))  wl |= WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
    return wl_data_device_manager_dnd_action(wl);
}

static
auto from_wayland_dnd_action(wl_data_device_manager_dnd_action wl) -> Flags<SeatDndAction>
{
    Flags<SeatDndAction> action = {};
    if (wl & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) action |= SeatDndAction::copy;
    if (wl & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) action |= SeatDndAction::move;
    if (wl & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)  action |= SeatDndAction::ask;
    return action;
}

// -----------------------------------------------------------------------------

static
void create_data_source(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client = way_client_from(wl_client);

    auto source = ref_create<WayDataSource>();
    source->client = client;

    source->resource = way_resource_create_refcounted(wl_data_source, wl_client, resource, id, source.get());

    log_debug("WayDataSource created {}", (void*)source.get());
}

void WayDataSource::cancel()
{
    if (resource) {
        way_send<wl_data_source_send_cancelled>(resource);
    }
}

void WayDataSource::send(std::string_view mime, fd_t fd)
{
    log_debug("WayDataSource::send({}, {})", mime, fd);
    way_send<wl_data_source_send_send>(resource, std::string(mime).c_str(), fd);
}

void WayDataSource::action_update(SeatDndAction action)
{
    if (last_action && *last_action == action) return;
    last_action = action;
    log_debug("WayDataSource::action({})", action);
    way_send<wl_data_source_send_action>(resource, to_wayland_dnd_action(action));
}

void WayDataSource::dnd_drop_performed()
{
    way_send<wl_data_source_send_dnd_drop_performed>(resource);
}

void WayDataSource::dnd_finished()
{
    way_send<wl_data_source_send_dnd_finished>(resource);
}

WayDataSource::~WayDataSource()
{
    log_debug("WayDataSource destroyed {}", (void*)this);
}

static
void get_data_device(wl_client* wl_client, wl_resource* resource, u32 id, wl_resource* wl_seat)
{
    auto* client_seat = way_get_userdata<WayClientSeat>(wl_seat);

    client_seat->data_devices.emplace_back(way_resource_create_refcounted(wl_data_device, wl_client, resource, id, client_seat));
}

WAY_INTERFACE(wl_data_device_manager) = {
    .create_data_source = create_data_source,
    .get_data_device = get_data_device,
};

WAY_BIND_GLOBAL(wl_data_device_manager, bind)
{
    way_resource_create_unsafe(wl_data_device_manager, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void accept(wl_client* client, wl_resource* resource, u32 serial, const char* mime_type)
{
    // log_trace("wl_data_offer.accept({})", mime_type ?: "NONE");

    auto* offer = way_get_userdata<WayDataOffer>(resource);

    seat_data_offer_accept(offer->offer.get(), mime_type);
}

static
void receive(wl_client* client, wl_resource* resource, const char* mime_type, fd_t fd)
{
    log_trace("wl_data_offer.receive({})", mime_type ?: "NONE");

    auto write = Fd(fd);

    auto* offer = way_get_userdata<WayDataOffer>(resource);
    seat_data_offer_receive(offer->offer.get(), mime_type, write.get());
}

static
void finish(wl_client* client, wl_resource* wl_data_offer)
{
    log_trace("wl_data_offer.finish()");

    auto* offer = way_get_userdata<WayDataOffer>(wl_data_offer);
    seat_data_offer_finish(offer->offer.get());
}

static
void offer_set_actions(wl_client* client, wl_resource* wl_data_offer, u32 actions, u32 preferred_action)
{
    // log_trace("wl_data_offer.set_actions({}, {})",
    //     Flags(wl_data_device_manager_dnd_action(actions)),
    //     wl_data_device_manager_dnd_action(preferred_action));

    auto* offer = way_get_userdata<WayDataOffer>(wl_data_offer);
    auto action = seat_data_offer_set_actions(offer->offer.get(),
        from_wayland_dnd_action(wl_data_device_manager_dnd_action(actions)),
        from_wayland_dnd_action(wl_data_device_manager_dnd_action(preferred_action)).get());

    if (offer->last_action && action == *offer->last_action) return;
    offer->last_action = action;

    way_send<wl_data_offer_send_action>(offer->resource, to_wayland_dnd_action(action));
}

WAY_INTERFACE(wl_data_offer) = {
    .accept = accept,
    .receive = receive,
    .destroy = way_simple_destroy,
    .finish = finish,
    .set_actions = offer_set_actions,
};

// -----------------------------------------------------------------------------

static
void offer(wl_client* client, wl_resource* resource, const char* mime_type)
{
    log_trace("wl_data_source.offer({})", mime_type ?: "NONE");

    auto* source = way_get_userdata<WayDataSource>(resource);
    source->offered.emplace(mime_type);
}

static
void source_set_actions(wl_client* client, wl_resource* wl_data_source, u32 actions)
{
    log_trace("wl_data_source.set_actions({})", Flags(from_wayland_dnd_action(wl_data_device_manager_dnd_action(actions))));

    auto* source = way_get_userdata<WayDataSource>(wl_data_source);
    source->supported_actions = from_wayland_dnd_action(wl_data_device_manager_dnd_action(actions));
}

WAY_INTERFACE(wl_data_source) = {
    .offer = offer,
    .destroy = way_simple_destroy,
    .set_actions = source_set_actions,
};

// -----------------------------------------------------------------------------

static
auto get_mime_types(WayDataSource* source)
{
    std::vector<std::string_view> mime_types;
    mime_types.insert_range(mime_types.begin(), source->offered);
    return mime_types;
}

static
void start_drag(
    wl_client*   wl_client,
    wl_resource* wl_data_device,
    wl_resource* wl_data_source,
    wl_resource* origin_surface,
    wl_resource* icon_surface,
    u32 serial)
{
    log_trace("wl_data_device.start_drag()");

    auto* client_seat = way_get_userdata<WayClientSeat>(wl_data_device);
    auto* source = way_get_userdata<WayDataSource>(wl_data_source);
    auto* drag_surface = way_get_userdata<WaySurface>(icon_surface);

    if (drag_surface) {
        scene_tree_set_translation(drag_surface->scene.tree.get(), {0, 0});

        if (drag_surface->role != WaySurfaceRole::drag_icon) {
            drag_surface->role = WaySurfaceRole::drag_icon;
            drag_surface->drag_role = ref_create<WayDragIcon>();
            way_surface_addon_register(drag_surface, drag_surface->drag_role.get());
            scene_node_unparent(drag_surface->scene.input_region.get());
        }
    }

    source->source = seat_start_drag(client_seat->seat->seat, {
        .interface = source,
        .mime_types = get_mime_types(source),
        .drag_actions = source->supported_actions,
        .drag_visual = drag_surface->scene.tree.get(),
    });

    if (!source->source) {
        way_send<wl_data_source_send_cancelled>(source->resource);
    }
}

void WayDragIcon::apply(WayCommitId)
{
    if (surface->current.surface.offset != vec2i32{}) {
        scene_tree_set_translation(surface->scene.tree.get(),
            surface->scene.tree->translation + vec_cast<f32>(surface->current.surface.offset));

        surface->current.surface.offset = {};
    }
}

static
void set_selection(wl_client* wl_client, wl_resource* wl_data_device, wl_resource* wl_data_source, u32 serial)
{
    auto* client_seat = way_get_userdata<WayClientSeat>(wl_data_device);
    auto* source = way_get_userdata<WayDataSource>(wl_data_source);

    if (!source) {
        seat_clear_selection(client_seat->seat->seat);
        return;
    }

    std::vector<std::string_view> mime_types;
    mime_types.insert_range(mime_types.begin(), source->offered);

    if (source->source) {
        log_warn("Client attempted to re-use wl_data_source, migrating to new WayDataSource");

        // WORKAROUND: This is a workaround for certain Wayland clients that re-submit the same data source
        //             to set_selection. In these cases, we want to suppress the emission of a cancel operation
        //             that is normally triggered when clearing the previous selection.
        //
        //             To do this, we simply temporarily extract the wl_resource (which prevents the cancel
        //             operation from being sent), destroy the old SeatDataSource, and re-insert the resource.

        auto resource = std::exchange(source->resource, nullptr);
        source->source.destroy();
        source->resource = resource;
    }

    source->source = seat_set_selection(client_seat->seat->seat, {
        .interface = source,
        .mime_types = get_mime_types(source),
    });
}

WAY_INTERFACE(wl_data_device) = {
    .start_drag = start_drag,
    .set_selection = set_selection,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
auto make_offer(WayClientSeat* client_seat, wl_resource* wl_data_device, SeatDataOffer* seat_offer) -> Ref<WayDataOffer>
{
    auto offer = ref_create<WayDataOffer>();
    offer->client_seat = client_seat;
    offer->offer = seat_offer;

    offer->resource = way_resource_create_refcounted(wl_data_offer, client_seat->client->wl_client, wl_data_device, 0, offer.get());

    way_send<wl_data_device_send_data_offer>(wl_data_device, offer->resource);
    for (auto& mime : seat_data_offer_get_mime_types(seat_offer)) {
        way_send<wl_data_offer_send_offer>(offer->resource, mime.c_str());
    }

    if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
        way_send<wl_data_offer_send_source_actions>(offer->resource,
            to_wayland_dnd_action(seat_data_offer_get_actions(seat_offer)));
    }

    return offer;
}

void way_data_offer_selection(WayClientSeat* client_seat)
{
    auto* seat = client_seat->seat;

    auto seat_offer = seat_get_selection(seat->seat);
    if (!seat_offer) return;

    if (!seat->focus.keyboard || !seat->focus.keyboard->client) return;

    for (auto* wl_data_device : client_seat->data_devices) {
        auto offer = make_offer(client_seat, wl_data_device, seat_offer.get());
        way_send<wl_data_device_send_selection>(wl_data_device, offer->resource);
    }
}

// -----------------------------------------------------------------------------

static
auto get_client_seat(WayClient* client, Seat* seat) -> WayClientSeat*
{
    // TODO: Deduplicate this
    for (auto* client_seat : client->seats) {
        if (client_seat->seat->seat == seat) {
            return client_seat;
        }
    }
    return nullptr;
}

static
auto to_fixed(vec2f32 v) -> Vec<2, wl_fixed_t>
{
    return {wl_fixed_from_double(v.x), wl_fixed_from_double(v.y)};
}

static
auto to_surface_pos(WaySurface* surface, vec2f32 global_pos)
{
    return global_pos - scene_tree_get_position(surface->scene.tree.get());
}

static
void drag_leave(WayClient* client, SeatDataEvent* event)
{
    log_trace("drag_leave");

    auto* client_seat = get_client_seat(client, event->seat);
    debug_assert(client_seat->drag_entered);

    for (auto* wl_data_device : client_seat->data_devices) {
        way_send<wl_data_device_send_leave>(wl_data_device);
    }

    client_seat->drag_entered = false;
}

static
void drag_enter(WayClient* client, SeatDataEvent* event)
{
    log_trace("drag_enter");

    auto* pointer = seat_get_pointer(event->seat);

    auto* client_seat = get_client_seat(client, event->seat);

    debug_assert(!client_seat->drag_entered);

    client_seat->drag_entered = true;

    auto serial = way_next_serial(client->server);
    auto* surface = way_find_surface_for_focus(client, event->drag.focus);
    auto pos = to_fixed(to_surface_pos(surface, seat_pointer_get_position(pointer)));

    for (auto* wl_data_device : client_seat->data_devices) {
        auto offer = make_offer(client_seat, wl_data_device, event->drag.offer);
        way_send<wl_data_device_send_enter>(wl_data_device, serial.value, surface->resource, pos.x, pos.y, offer->resource);
    }
}

static
void drag_motion(WayClient* client, SeatDataEvent* event)
{
    // log_trace("drag_motion");

    auto* pointer = seat_get_pointer(event->seat);

    auto* client_seat = get_client_seat(client, event->seat);
    auto elapsed = way_get_elapsed(client->server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto* surface = way_find_surface_for_focus(client, event->drag.focus);
    auto pos = to_fixed(to_surface_pos(surface, seat_pointer_get_position(pointer)));

    for (auto* wl_data_device : client_seat->data_devices) {
        way_send<wl_data_device_send_motion>(wl_data_device, time_ms, pos.x, pos.y);
    }
}

static
void drag_drop(WayClient* client, SeatDataEvent* event)
{
    log_trace("drag_drop");

    auto* client_seat = get_client_seat(client, event->seat);

    for (auto* wl_data_device : client_seat->data_devices) {
        wl_data_device_send_drop(wl_data_device);
    }

    client_seat->drag_entered = false;
}

void way_handle_data_event(WayClient* client, SeatDataEvent* event)
{
    switch (event->type) {
        break;case SeatEventType::selection:
            way_data_offer_selection(get_client_seat(client, event->seat));

        break;case SeatEventType::drag_enter:
            drag_enter(client, event);
        break;case SeatEventType::drag_leave:
            drag_leave(client, event);
        break;case SeatEventType::drag_motion:
            drag_motion(client, event);
        break;case SeatEventType::drag_drop:
            drag_drop(client, event);

        break;default:
            debug_unreachable();
    }
}
