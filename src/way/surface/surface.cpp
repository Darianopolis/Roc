#include "surface.hpp"

#include "../shell/shell.hpp"
#include "../buffer/buffer.hpp"
#include "../client.hpp"

WAY_INTERFACE(wl_region) = {
    .destroy = way_simple_destroy,
    .add = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way_get_userdata<WayRegion>(resource)->region.add({{f32(x), f32(y)}, {f32(w), f32(h)}, xywh});
    },
    .subtract = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way_get_userdata<WayRegion>(resource)->region.subtract({{f32(x), f32(y)}, {f32(w), f32(h)}, xywh});
    }
};

static
void create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto region = ref_create<WayRegion>();
    region->resource = way_resource_create_refcounted(wl_region, client, resource, id, region.get());
}

// -----------------------------------------------------------------------------

static
void create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto surface = ref_create<WaySurface>();

    surface->client = way_client_from(client);
    surface->client->surfaces.emplace_back(surface.get());

    auto* server = surface->client->server;

    surface->scene.tree = scene_tree_create();
    surface->scene.tree->userdata = {server->userdata_id, surface.get()};
    scene_tree_set_enabled(surface->scene.tree.get(), false);

    surface->scene.texture = scene_texture_create();
    scene_tree_place_above(surface->scene.tree.get(), nullptr, surface->scene.texture.get());

    surface->scene.input_region = scene_input_region_create();
    surface->scene.focus = seat_focus_create(wm_get_seat_client(surface->client->wm.get()), surface->scene.input_region.get());
    scene_tree_place_above(surface->scene.tree.get(), nullptr, surface->scene.input_region.get());

    surface->resource = way_resource_create_refcounted(wl_surface, client, resource, id, surface.get());
}

WAY_INTERFACE(wl_compositor) = {
    .create_surface = create_surface,
    .create_region  = create_region,
};

WAY_BIND_GLOBAL(wl_compositor, bind)
{
    way_resource_create_unsafe(wl_compositor, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void offset(wl_client* client, wl_resource* resource, i32 dx, i32 dy)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    surface->pending->surface.offset += vec2i32{dx, dy};
}

static
void attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 dx, i32 dy)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    auto* pending = surface->pending.get();

    debug_assert(!pending->image);

    if (!wl_buffer) {
        pending->buffer = nullptr;
        pending->set |= WaySurfaceStateComponent::buffer;
        return;
    }

    pending->buffer = way_get_userdata<WayBuffer>(wl_buffer);
    pending->set |= WaySurfaceStateComponent::buffer;

    pending->surface.offset += vec2i32{dx, dy};
}

static
void damage(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    surface->pending->surface.damage.damage({{x, y}, {width, height}, xywh});
}

static
void damage_buffer(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    surface->pending->buffer_damage.damage({{x, y}, {width, height}, xywh});
}

static
void frame(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    auto callback = way_resource_create(client, &wl_callback_interface, resource, id, nullptr, nullptr, false);

    surface->pending->surface.frame_callbacks.emplace_back(callback);
}

void way_surface_on_frame(WaySurface* surface, WmOutput* output, u64 frame_id)
{
    auto* server = surface->client->server;

    // Check if there are any outstanding frame callbacks

    if (!surface->current.surface.frame_callbacks.front()
        &&  !std::ranges::any_of(surface->cached, [&](auto& pending) {
                return pending->surface.frame_callbacks.front();
            })) {
        return;
    }

    // Check that this surface intersects with the given output

    {
        auto dst = surface->scene.texture->dst;
        dst.origin += scene_tree_get_position(surface->scene.tree.get());

        if (!rect_intersects(wm_output_get_viewport(output), dst)) {
            return;
        }
    }

    // Require that we either
    //   a) haven't seen this frame request before OR
    //   b) a specified interval has elapsed and we can re-submit the frame callback
    //
    // HACK: Certain applications continuously send frame callbacks even when they have no new content to commit
    //       To avoid a feedback loop forcing CPU usage to 100% we need to either:
    //         a) Throttle repeated surface frame callbacks to a given interval independently from output frame requests
    //         b) Always perform an output commit after a surface frame request even if there is no scene damage
    //             - This would force VRR monitors to their maximum refresh rate when such an application is unoccluded
    //       We are going with option (a) currently as it is the least disruptive option for our output implementations
    //
    // TODO: It may be preferable to tune the throttle interval automatically based on relevant output refresh rates
    //        - E.g. selecting the min refresh interval of all outputs that the surface is currently unoccluded on

    auto now = std::chrono::steady_clock::now();
    static constexpr auto throttle_interval = std::chrono::steady_clock::duration(1000ms) / 60;

    if (frame_id <= surface->frame.last_seen_frame_id) {
        if (now - surface->frame.last_sent_time < throttle_interval) {
            if (!surface->frame.waiting_for_timer) {
                surface->frame.waiting_for_timer = true;
                timer_enqueue(server->timer.get(),
                    surface->frame.last_sent_time + throttle_interval,
                    [surface = Weak(surface), output = Weak(output)] {
                        if (!surface) return;
                        surface->frame.waiting_for_timer = false;
                        if (!output) return;
                        way_surface_on_frame(surface.get(), output.get(), surface->frame.last_seen_frame_id);
                    });
            }
            return;
        }
    }

    surface->frame.last_seen_frame_id = std::max(surface->frame.last_seen_frame_id, frame_id);
    surface->frame.last_sent_time = now;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(way_get_elapsed(server)).count();

    auto send_frame_callbacks = [&](WayResourceList& list) {
        while (auto callback = list.front()) {
            way_send<wl_callback_send_done>(callback, ms);
            wl_resource_destroy(callback);
        }
    };

    send_frame_callbacks(surface->current.surface.frame_callbacks);
    for (auto& pending : surface->cached) {
        if (!pending->commit) continue;
        send_frame_callbacks(pending->surface.frame_callbacks);
    }

    way_client_queue_flush(surface->client);
}

static
void set_input_region(wl_client* client, wl_resource* resource, wl_resource* region)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    auto* pending = surface->pending.get();

    pending->set |= WaySurfaceStateComponent::input_region;
    pending->surface.input_region = region
        ? way_get_userdata<WayRegion>(region)->region
        : region2f32{way_infinite_aabb};
}

WaySurfaceState::~WaySurfaceState()
{
    // TODO: Empty callbacks
}

static
void surface_set_mapped(WaySurface* surface, bool mapped)
{
    if (mapped == surface->mapped) return;
    surface->mapped = mapped;

    log_info("Surface {} was {}", (void*)surface, mapped ? "mapped" : "unmapped");

    scene_tree_set_enabled(surface->scene.tree.get(), mapped);

    if (surface->role == WaySurfaceRole::xdg_toplevel) {
        way_toplevel_on_map_change(surface, mapped);
    }
}

static
void update_map_state(WaySurface* surface)
{
    bool can_be_mapped =
           surface->current.buffer
        && surface->current.image
        && surface->resource;

    surface_set_mapped(surface, can_be_mapped);
}

static
void apply(WaySurface* surface, WaySurfaceState& from)
{
    auto& to = surface->current;

    to.commit = from.commit;

    if (from.set.contains(WaySurfaceStateComponent::buffer_transform)) {
        to.set |= WaySurfaceStateComponent::buffer_transform;
        to.buffer_transform = from.buffer_transform;
    }

    if (from.set.contains(WaySurfaceStateComponent::buffer_scale)) {
        to.set |= WaySurfaceStateComponent::buffer_scale;
        to.buffer_scale = from.buffer_scale;
    }

    to.surface.frame_callbacks.take_and_append_all(std::move(from.surface.frame_callbacks));

    // Offset

    to.surface.offset += from.surface.offset;

    // Buffer state

    if (from.set.contains(WaySurfaceStateComponent::buffer)) {
        if (from.buffer) {
            to.set |= WaySurfaceStateComponent::buffer;
            to.buffer = std::move(from.buffer);
            to.image  = std::move(from.image);

            scene_texture_set_image(surface->scene.texture.get(),
                to.image.get(),
                surface->client->server->sampler.get(),
                GpuBlendMode::premultiplied);

            if (from.buffer_damage) {
                scene_texture_damage(surface->scene.texture.get(), from.buffer_damage.bounds());
            }
        } else {
            to.set -= WaySurfaceStateComponent::buffer;
            to.buffer = nullptr;

            scene_texture_set_image(surface->scene.texture.get(), nullptr, nullptr, GpuBlendMode::none);
        }
    }

    // Buffer source / destination

    way_viewport_apply(surface, from);

    // Input regions

    if (from.set.contains(WaySurfaceStateComponent::input_region)) {
        scene_input_region_set_region(surface->scene.input_region.get(), std::move(from.surface.input_region));
    }

    scene_input_region_set_clip(surface->scene.input_region.get(), surface->scene.texture->dst);

    // Map state

    update_map_state(surface);

    // Component state

    for (auto* addon : surface->addons) {
        addon->apply(from.commit);
    }
}

WaySurfaceAddon::~WaySurfaceAddon()
{
    if (!surface) return;
    std::erase(surface->addons, this);
}

void way_surface_try_flush(WaySurface* surface)
{
    // TODO: Queued applications

    auto prev_applied_commit_id = surface->current.commit;

    while (!surface->cached.empty()) {
        auto& packet = *surface->cached.front().get();

        if (!std::ranges::all_of(surface->addons, [&](auto* addon) {
            return addon->test(packet.commit);
        })) {
            break;
        }

        // Check for buffer ready

        if (packet.buffer && !(packet.image = packet.buffer->acquire(surface, &packet))) {
            break;
        }

        apply(surface, packet);

        surface->cached.pop_front();
    }

    if (surface->current.commit == prev_applied_commit_id) return;

    // Flush subsurface state recursively

    auto* server = surface->client->server;

    for (auto* child : surface->scene.tree->children) {
        if (child->type != SceneNodeType::tree) continue;
        auto* tree = static_cast<SceneTree*>(child);
        if (tree->userdata.id != server->userdata_id) continue;
        way_surface_try_flush(way_get_userdata<WaySurface>(server, tree->userdata.data));
    }
}

static
void commit(wl_client* client, wl_resource* resource)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    auto pending = surface->pending;
    pending->commit = ++surface->last_commit_id;
    surface->cached.emplace_back(pending);
    surface->pending = ref_create<WaySurfaceState>();

    // Queue frame request for frame callbacks

    if (pending->surface.frame_callbacks.front()) {
        wm_request_frame(surface->client->server->wm);
    }

    for (auto* addon : surface->addons) {
        addon->commit(pending->commit);
    }

    if (!pending->set.contains(WaySurfaceStateComponent::buffer)) {
        debug_assert(!pending->buffer_damage,  "TODO: wl_surface::damage_buffer without attached buffer");
        debug_assert(!pending->surface.damage, "TODO: wl_surface::damage without attached buffer");
    }

    // Attempt to flush any state immediately

    way_surface_try_flush(surface);
}

WAY_INTERFACE(wl_surface) = {
    .destroy = way_simple_destroy,
    .attach = attach,
    .damage = damage,
    .frame = frame,
    WAY_STUB_QUIET(set_opaque_region),
    .set_input_region = set_input_region,
    .commit = commit,
    .set_buffer_transform = [](wl_client* client, wl_resource* resource, i32 bt) {
        auto* surface = way_get_userdata<WaySurface>(resource);
        surface->pending->buffer_transform = wl_output_transform(bt);
        surface->pending->set |= WaySurfaceStateComponent::buffer_transform;
    },
    .set_buffer_scale = [](wl_client* client, wl_resource* resource, i32 scale) {
        auto* surface = way_get_userdata<WaySurface>(resource);
        surface->pending->buffer_scale = scale;
        surface->pending->set |= WaySurfaceStateComponent::buffer_scale;
    },
    .damage_buffer = damage_buffer,
    .offset = offset,
};

// -----------------------------------------------------------------------------

WaySurface::~WaySurface()
{
    scene.tree->userdata = {};
    debug_assert(std::erase(client->surfaces, this));

    for (auto* addon : addons) {
        addon->surface = nullptr;
    }
}

void way_surface_addon_register(WaySurface* surface, WaySurfaceAddon* addon)
{
    addon->surface = surface;
    surface->addons.emplace_back(addon);
}

// -----------------------------------------------------------------------------

auto way_find_surface_for_focus(WayClient* client, SeatFocus* focus) -> WaySurface*
{
    for (auto* surface : client->surfaces) {
        if (surface->scene.focus.get() == focus) {
            return surface;
        }
    }
    return nullptr;
}
