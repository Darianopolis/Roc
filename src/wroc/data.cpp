#include "protocol.hpp"

#define WROC_NOISY_DRAG 1

const u32 wroc_wl_data_device_manager_version = 3;

static
void wroc_wl_data_device_manager_create_data_source(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &wl_data_source_interface, wl_resource_get_version(resource), id);
    auto* data_source = core::create_unsafe<wroc_data_source>();
    data_source->resource = new_resource;
    server->data_manager.sources.emplace_back(data_source);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_data_source_impl, data_source);
}

static
void wroc_wl_data_device_manager_get_data_device(wl_client* client, wl_resource* resource, u32 id, wl_resource* seat)
{
    auto* new_resource = wroc_resource_create(client, &wl_data_device_interface, wl_resource_get_version(resource), id);
    auto* data_device = core::create_unsafe<wroc_data_device>();
    data_device->resource = new_resource;
    data_device->seat = wroc_get_userdata<wroc_seat>(seat);
    server->data_manager.devices.emplace_back(data_device);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_data_device_impl, data_device);
}

const struct wl_data_device_manager_interface wroc_wl_data_device_manager_impl = {
    .create_data_source = wroc_wl_data_device_manager_create_data_source,
    .get_data_device = wroc_wl_data_device_manager_get_data_device,
};

void wroc_wl_data_device_manager_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto new_resource = wroc_resource_create(client, &wl_data_device_manager_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_wl_data_device_manager_impl, nullptr);
}

// -----------------------------------------------------------------------------

static
void wroc_wl_data_offer_receive(wl_client* client, wl_resource* resource, const char* mime_type, int fd)
{
    log_debug("<- wl_data_offer.receive(mime = \"{}\", fd = {})", mime_type, fd);

    auto* offer = wroc_get_userdata<wroc_data_offer>(resource);

    if (offer->source) {
        wroc_send(wl_data_source_send_send, offer->source->resource, mime_type, fd);
    } else {
        log_error("Data offer receive failed: source {} was destroyed", (void*)offer->source.get());
    }

    close(fd);
}

static
void wroc_wl_data_offer_set_actions(wl_client* client, wl_resource* resource, u32 dnd_actions, u32 preferred_action)
{
    auto* data_offer = wroc_get_userdata<wroc_data_offer>(resource);

#if WROC_NOISY_DRAG
    log_trace("<- wl_data_offer.set_actions({}, {})",
        core::to_string(core::Flags(wl_data_device_manager_dnd_action(dnd_actions))),
        core::to_string(wl_data_device_manager_dnd_action(preferred_action)));
#endif

    if (!data_offer->source) {
        log_error("wl_data_offer.set_actions failed: source {} was destroyed", (void*)data_offer->source.get());
    }

    wl_data_device_manager_dnd_action new_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    if (preferred_action && data_offer->source->dnd_actions.contains(wl_data_device_manager_dnd_action(preferred_action))) {
        new_action = wl_data_device_manager_dnd_action(preferred_action);
    } else {
        auto matched = data_offer->source->dnd_actions & wl_data_device_manager_dnd_action(preferred_action);
        if      (matched.contains(WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK))  matched = WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
        else if (matched.contains(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY)) matched = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
        else if (matched.contains(WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)) matched = WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
        new_action = matched.get();
    }

    if (new_action != data_offer->action) {
        data_offer->action = new_action;
        log_debug("-> wl_data_offer.send_action({}, {})", (void*)data_offer->resource, core::to_string(new_action));
        wroc_send(wl_data_offer_send_action, data_offer->resource, data_offer->action);
    }
}

static
void wroc_wl_data_offer_accept(wl_client* client, wl_resource* resource, u32 serial, const char* mime_type)
{
    // TODO: Validate serial

#if WROC_NOISY_DRAG
    log_trace("<- wl_data_offer.accept({})", mime_type ? mime_type : "nullptr");
#endif

    auto* offer = wroc_get_userdata<wroc_data_offer>(resource);

    if (!offer->source) {
        log_error("    wl_data_offer.accept failed: source {} was destroyed", (void*)offer->source.get());
        return;
    }

    auto& drag = server->data_manager.drag;

    if (offer->source.get() != drag.source.get()) {
        log_warn("   not active drag, ignoring...");
        return;
    }

    bool matches = true;
    if (!mime_type) {
        matches = false;
    } else if (std::ranges::find_if(offer->source->mime_types, [&](auto& t) { return t == mime_type; }) == offer->source->mime_types.end()) {
        log_error("    wl_data_offer.accept - mime type \"{}\" not supported", mime_type);
        matches = false;
    }

    std::string new_target;

    if (matches) {
        new_target = mime_type;
        if (offer->mime_type != mime_type) {
            offer->mime_type = mime_type;
        }

        if (drag.offer.get() != offer) {
            log_debug("    drag.offer = {}", (void*)offer);
            drag.offer = offer;
        }
    } else if (drag.offer.get() == offer) {
        log_debug("    drag.offer = nullptr");
        drag.offer = nullptr;
    }

    if (drag.source && new_target != drag.source->target) {
        drag.source->target = new_target;
        log_debug("-> wl_data_source.send_target({}, {})", (void*)drag.source->resource, new_target.empty() ? "nullptr" : new_target.c_str());
        wroc_send(wl_data_source_send_target, drag.source->resource, new_target.empty() ? nullptr : new_target.c_str());
    }
}

static
void wroc_data_manager_end_grab()
{
    server->data_manager.drag = {};
}

static
void wroc_wl_data_offer_finish(wl_client* client, wl_resource* resource)
{
    log_debug("<- wl_data_offer.finish()");

    auto* offer = wroc_get_userdata<wroc_data_offer>(resource);

    if (offer->source) {
        wroc_send(wl_data_source_send_dnd_finished, offer->source->resource);
    }

    wroc_data_manager_end_grab();
}

const struct wl_data_offer_interface wroc_wl_data_offer_impl = {
    .accept      = wroc_wl_data_offer_accept,
    .receive     = wroc_wl_data_offer_receive,
    .destroy     = wroc_simple_resource_destroy_callback,
    .finish      = wroc_wl_data_offer_finish,
    .set_actions = wroc_wl_data_offer_set_actions,
};


wroc_data_offer::~wroc_data_offer()
{
    log_debug("<- wl_data_offer.destroyed");
}

// -----------------------------------------------------------------------------

static
void wroc_wl_data_source_offer(wl_client* client, wl_resource* resource, const char* mime_type)
{
    log_debug("<- wl_data_source.offer({})", mime_type);
    auto* data_source = wroc_get_userdata<wroc_data_source>(resource);
    data_source->mime_types.emplace_back(mime_type);
}

static
void wroc_wl_data_source_set_actions(wl_client* client, wl_resource* resource, u32 dnd_actions)
{
    log_debug("<- wl_data_source.set_actions()");
    if (dnd_actions >= WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) log_debug(" - copy");
    if (dnd_actions >= WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) log_debug(" - move");
    if (dnd_actions >= WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)  log_debug(" - ask");

    auto* data_source = wroc_get_userdata<wroc_data_source>(resource);
    data_source->dnd_actions = wl_data_device_manager_dnd_action(dnd_actions);
}

static
void wroc_wl_data_source_destroy(wl_client* client, wl_resource* resource)
{
    auto* source = wroc_get_userdata<wroc_data_source>(resource);
    log_debug("<- wl_data_source.destroy({})", (void*)source);

    // Immediately end drag if we were the current drag source to avoid ghost drag events
    if (server->data_manager.drag.source.get() == source) {
        wroc_data_manager_end_grab();
    }

    wl_resource_destroy(source->resource);
}

const struct wl_data_source_interface wroc_wl_data_source_impl = {
    .offer       = wroc_wl_data_source_offer,
    .destroy     = wroc_wl_data_source_destroy,
    .set_actions = wroc_wl_data_source_set_actions,
};

wroc_data_source::~wroc_data_source()
{
    log_debug("wroc_data_source::destroyed");
    if (server->data_manager.drag.source.get() == this) {
        wroc_data_manager_end_grab();
    }
    std::erase(server->data_manager.sources, this);
}

static
void wroc_data_source_cancel(wroc_data_source* data_source)
{
    if (!data_source) return;
    if (data_source->cancelled) return;
    log_debug("Cancelling data source: {}", (void*)data_source);
    if (data_source->resource) {
        wroc_send(wl_data_source_send_cancelled, data_source->resource);
    }
    data_source->cancelled = true;
    if (data_source == server->data_manager.drag.source.get()) {
        wroc_data_manager_end_grab();
    }
}

// -----------------------------------------------------------------------------

static
wl_resource* wroc_data_device_offer(wroc_data_device* device, wroc_data_source* source)
{
    auto* offer_resource = wroc_resource_create(wroc_resource_get_client(device->resource), &wl_data_offer_interface, wl_resource_get_version(device->resource), 0);
    auto* data_offer = core::create_unsafe<wroc_data_offer>();
    data_offer->resource = offer_resource;
    data_offer->source = source;
    data_offer->device = device;
    wroc_resource_set_implementation_refcounted(offer_resource, &wroc_wl_data_offer_impl, data_offer);

    wroc_send(wl_data_device_send_data_offer, device->resource, offer_resource);

    for (auto& mime_type : source->mime_types) {
        wroc_send(wl_data_offer_send_offer, offer_resource, mime_type.c_str());
    }

    if (wl_resource_get_version(offer_resource) >= WL_DATA_OFFER_ACTION_SINCE_VERSION) {
        wroc_send(wl_data_offer_send_source_actions, offer_resource, source->dnd_actions.get());
    }

    return offer_resource;
}

void wroc_data_manager_offer_selection(wl_client* client)
{
    if (!server->data_manager.selection) return;
    auto* selection = server->data_manager.selection.get();
    for (auto* device : server->data_manager.devices) {
        if (wroc_resource_get_client(device->resource) != client) continue;
        // TODO: We should also be filtering per seat when/if we support multiple of those
        log_debug("Offering selection {} to {}", (void*)selection, (void*)device);
        auto* offer_resource = wroc_data_device_offer(device, selection);
        wroc_send(wl_data_device_send_selection, device->resource, offer_resource);
    }
}

static
void wroc_wl_data_device_set_selection(wl_client* client, wl_resource* resource, wl_resource* source, u32 serial)
{
    // TODO: Validate serial

    auto* data_source = wroc_get_userdata<wroc_data_source>(source);

    // Cancel all previous data sources

    for (auto* old_source : server->data_manager.sources) {
        if (old_source == data_source) continue;
        wroc_data_source_cancel(old_source);
    }

    server->data_manager.selection = data_source;

    log_debug("<- wl_data_device.set_selection({})", (void*)data_source);

    if (auto* surface = server->seat->keyboard->focused_surface.get(); surface && surface->resource) {
        if (auto* focused_client = wroc_resource_get_client(surface->resource)) {
            wroc_data_manager_offer_selection(focused_client);
        }
    }
}

void wroc_data_manager_update_drag(wroc_surface* target_surface)
{
    auto& drag = server->data_manager.drag;

    if (!drag.source || !drag.device) return;

    if (drag.offered_surface.get() == target_surface) {
        if (!target_surface) return;

        auto time = wroc_get_elapsed_milliseconds();

        for (auto* device : server->data_manager.devices) {
            if (wroc_resource_get_client(device->resource) != wroc_resource_get_client(target_surface->resource)) continue;

            if (wroc_is_client_behind(wroc_resource_get_client(device->resource))) {
                log_warn("[{}], client is running behind, skipping drag motion event...", time);
                continue;
            }

            auto pos = wroc_surface_pos_from_global(target_surface, server->seat->pointer->position);
#if WROC_NOISY_DRAG
            log_trace("-> wl_data_device.motion({}, {}, {:.2f}, {:.2f})", time, (void*)target_surface, pos.x, pos.y);
#endif
            wroc_send(wl_data_device_send_motion, device->resource,
                time,
                wl_fixed_from_double(pos.x),
                wl_fixed_from_double(pos.y));
        }
        return;
    }

    if (drag.offered_surface) {
        for (auto* device : server->data_manager.devices) {
            if (wroc_resource_get_client(device->resource) != wroc_resource_get_client(drag.offered_surface->resource)) continue;

            log_debug("-> wl_data_device.leave({})", (void*)target_surface);
            wroc_send(wl_data_device_send_leave, device->resource);
        }
    }

    drag.offered_surface = target_surface;
    drag.offer = nullptr;
    if (target_surface) {
        for (auto* device : server->data_manager.devices) {
            if (wroc_resource_get_client(device->resource) != wroc_resource_get_client(target_surface->resource)) continue;

            auto pos = wroc_surface_pos_from_global(target_surface, server->seat->pointer->position);
            log_debug("-> wl_data_device.enter({}, {:.2f}, {:.2f})", (void*)target_surface, pos.x, pos.y);
            auto* offer_resource = wroc_data_device_offer(device, drag.source.get());
            wroc_send(wl_data_device_send_enter, device->resource,
                wl_display_next_serial(server->display),
                target_surface->resource,
                wl_fixed_from_double(pos.x),
                wl_fixed_from_double(pos.y),
                offer_resource);
        }
    }
}

void wroc_data_manager_finish_drag()
{
    auto& drag = server->data_manager.drag;

    // End drag operation regardless of operation
    defer { wroc_data_manager_end_grab(); };

    if (!drag.source || !drag.device) {
        return;
    }

    if (drag.offer && drag.offer->device && drag.offer->source) {
        if (drag.offer->action && drag.source->dnd_actions.contains(drag.offer->action)) {
            log_debug("Drag completed with offer");
            log_debug("  action = {}", core::to_string(drag.offer->action));
            log_debug("  mime_type = {}", drag.offer->mime_type);
            wroc_send(wl_data_device_send_drop, drag.offer->device->resource);
            wroc_send(wl_data_source_send_dnd_drop_performed, drag.source->resource);
        } else {
            log_warn("Drag completed with no matching action, cancelling");
            wroc_data_source_cancel(drag.source.get());
        }
    } else {
        log_warn("Drag completed with no offered device (or source was destroyed), cancelling");
        wroc_data_source_cancel(drag.source.get());
    }
}

static
void wroc_wl_data_device_start_drag(wl_client* client, wl_resource* resource, wl_resource* source,
                                    wl_resource* origin, wl_resource* icon, u32 serial)
{
    // TODO: Validate serial

    auto* data_device = wroc_get_userdata<wroc_data_device>(resource);
    // TODO: Handle drag with null source
    auto* data_source = wroc_get_userdata<wroc_data_source>(source);
    // auto* origin_surface = wroc_get_userdata<wroc_surface>(origin);
    auto* drag_surface = wroc_get_userdata<wroc_surface>(icon);

    auto drag_icon = wroc_surface_get_or_create_addon<wroc_drag_icon>(drag_surface);
    drag_surface->buffer_dst.origin = {};

    wroc_surface_update_map_state(drag_surface);

    log_debug("Drag started (device = {}, source = {}, surface = {})", (void*)data_device, (void*)data_source, (void*)drag_surface);

    server->data_manager.drag.device = data_device;
    server->data_manager.drag.source = data_source;
    server->data_manager.drag.icon = std::move(drag_icon);
    server->data_manager.drag.offered_surface = nullptr;
    server->data_manager.drag.offer = nullptr;

    auto surface_under_cursor = wroc_get_surface_under_cursor();

    wroc_data_manager_update_drag(surface_under_cursor);
}

const struct wl_data_device_interface wroc_wl_data_device_impl = {
    .start_drag    = wroc_wl_data_device_start_drag,
    .set_selection = wroc_wl_data_device_set_selection,
    .release       = wroc_simple_resource_destroy_callback,
};

wroc_data_device::~wroc_data_device()
{
    std::erase(server->data_manager.devices, this);
}
