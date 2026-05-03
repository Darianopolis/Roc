#pragma once

#include "util.hpp"

#include <wm/wm.hpp>

// -----------------------------------------------------------------------------

struct WayServer;
struct WaySurface;
struct WayClientSeat;

struct WayClient
{
    WayServer* server;
    Ref<WmClient> wm;

    wl_client* wl_client;

    std::vector<WaySurface*> surfaces;
    std::vector<WayClientSeat*> seats;
};

void way_on_client_create(wl_listener*, void* data);

auto way_client_from(const wl_client*) -> WayClient*;

auto way_client_is_behind(WayClient*) -> bool;

void way_client_queue_flush(WayClient*);
