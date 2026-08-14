#include "shell.hpp"

#include "../surface/surface.hpp"
#include "../client.hpp"

#define WAY_NOISY_ACTIVATION 0

static
void get_activation_token(wl_client* client, wl_resource* resource, u32 id)
{
#if WAY_NOISY_ACTIVATION
    log_warn("xdg_activation_v1.get_activation_token");
#endif
    way_resource_create_unsafe(xdg_activation_token_v1, client, resource, id, nullptr);
}

static
void activate(wl_client* client, wl_resource* resource, const char* token, wl_resource* wl_surface)
{
    auto* surface = way_get_userdata<WaySurface>(wl_surface);

#if WAY_NOISY_ACTIVATION
    log_warn("xdg_activation_v1.activate({}, {})", token, (void*)wl_surface);
#endif

    if (surface->toplevel && surface->mapped) {
#if WAY_NOISY_ACTIVATION
        log_warn("  activating!");
#endif
        wm_focus(surface->client->server->wm, surface->toplevel->window.get());
    }
}

WAY_INTERFACE(xdg_activation_v1) = {
    .destroy = way_simple_destroy,
    .get_activation_token = get_activation_token,
    .activate = activate,
};

WAY_BIND_GLOBAL(xdg_activation_v1, bind)
{
    way_resource_create_unsafe(xdg_activation_v1, bind.client, bind.version, bind.id, nullptr);
}

static
void set_serial(wl_client* client, wl_resource* resource, u32 serial, wl_resource* seat)
{
#if WAY_NOISY_ACTIVATION
    log_warn("xdg_activation_token_v1.set_serial({}, {})", serial, (void*)seat);
#endif
}

static
void set_app_id(wl_client* client, wl_resource* resource, const char* app_id)
{
#if WAY_NOISY_ACTIVATION
    log_warn("xdg_activation_token_v1.set_app_id({})", app_id);
#endif
}

static
void set_surface(wl_client* client, wl_resource* resource, wl_resource* surface)
{
#if WAY_NOISY_ACTIVATION
    log_warn("xdg_activation_token_v1.set_surface({})", (void*)surface);
#endif
}

static
void commit(wl_client* client, wl_resource* resource)
{
#if WAY_NOISY_ACTIVATION
    log_warn("xdg_activation_token_v1.commit");
#endif

    static u64 count = 0;
    auto token = std::format(PROGRAM_NAME "-activate-{}", ++count);
    way_send<xdg_activation_token_v1_send_done>(resource, token.c_str());
}

WAY_INTERFACE(xdg_activation_token_v1) = {
    .set_serial = set_serial,
    .set_app_id = set_app_id,
    .set_surface = set_surface,
    .commit = commit,
    .destroy = way_simple_destroy,
};
