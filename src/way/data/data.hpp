#pragma once

#include "../util.hpp"
#include "../server.hpp"

#include <seat/seat.hpp>

struct WayClient;
struct WayClientSeat;
struct WaySurface;

struct WayDataSource : SeatDataSourceInterface
{
    WayClient* client;

    WayResource resource;

    Flags<SeatDndAction> supported_actions;
    std::flat_set<std::string> offered;

    Ref<SeatDataSource> source;

    std::optional<SeatDndAction> last_action;

    virtual void cancel() final override;
    virtual void send(std::string_view mime_type, fd_t target) final override;

    virtual void action_update(SeatDndAction) final override;
    virtual void dnd_drop_performed() final override;
    virtual void dnd_finished() final override;

    ~WayDataSource();
};

struct WayDataOffer
{
    WayClientSeat* client_seat;

    WayResource resource;

    std::optional<SeatDndAction> last_action;

    Ref<SeatDataOffer> offer;
};

void way_data_offer_selection(WayClientSeat*);

void way_handle_data_event(WayClientSeat*, SeatDataEvent*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_data_device_manager, 3);
WAY_INTERFACE_DECLARE(wl_data_offer);
WAY_INTERFACE_DECLARE(wl_data_source);
WAY_INTERFACE_DECLARE(wl_data_device);
