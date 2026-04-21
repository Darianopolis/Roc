#pragma once

#include "util.hpp"

#include <scene/scene.hpp>
#include <seat/seat.hpp>

// -----------------------------------------------------------------------------

struct WayServer;
struct WaySurface;
struct WaySeatClient;

struct WayClient : WayObject
{
    WayServer* server;

    wl_client* wl_client;

    Ref<SeatClient> scene;

    std::vector<WaySurface*> surfaces;
    std::vector<WaySeatClient*> seat_clients;

    ~WayClient();
};

void way_on_client_create(wl_listener*, void* data);

auto way_client_from(WayServer*, const wl_client*) -> WayClient*;

auto way_client_is_behind(WayClient*) -> bool;
