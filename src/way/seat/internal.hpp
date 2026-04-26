#pragma once

#include "seat.hpp"

#include "../data/data.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"
#include "../shell/shell.hpp"

static
auto find_surface(WayClient* client, SeatInputRegion* region) -> WaySurface*
{
    if (!region) return nullptr;
    if (region->client != client->scene.get()) return nullptr;
    for (auto* surface : client->surfaces) {
        if (surface->scene.input_region.get() == region) return surface;
    }
    return nullptr;
}
