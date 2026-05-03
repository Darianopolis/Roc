#include "internal.hpp"

auto seat_manager_create() -> Ref<SeatManager>
{
    return ref_create<SeatManager>();
}

// -----------------------------------------------------------------------------

auto seat_create(SeatManager* manager, std::string_view name, SeatKeyboard* keyboard, SeatPointer* pointer) -> Ref<Seat>
{
    auto seat = ref_create<Seat>();

    seat->name = name;

    seat->manager = manager;
    manager->seats.emplace_back(seat.get());

    seat->keyboard = keyboard;
    keyboard->seat = seat.get();

    seat->pointer = pointer;
    pointer->seat = seat.get();

    return seat;
}

Seat::~Seat()
{
    std::erase(manager->seats, this);
}

auto seat_get_name(Seat* seat) -> const char*
{
    return seat->name.c_str();
}

auto seat_get_keyboard(Seat* seat) -> SeatKeyboard*
{
    return seat->keyboard.get();
}

auto seat_get_pointer(Seat* seat) -> SeatPointer*
{
    return seat->pointer.get();
}

// -----------------------------------------------------------------------------

auto seat_add_event_filter(Seat* seat, std::move_only_function<SeatEventFilterResult(SeatEvent*)> fn) -> Ref<SeatEventFilter>
{
    auto filter = ref_create<SeatEventFilter>();
    filter->seat = seat;
    filter->filter = std::move(fn);
    seat->event_filters.emplace_back(filter.get());
    return filter;
}

SeatEventFilter::~SeatEventFilter()
{
    if (seat) {
        std::erase(seat->event_filters, this);
    }
}

void seat_post_event(Seat* seat, SeatClient* client, SeatEvent* event)
{
    for (auto* filter : seat->event_filters) {
        if (filter->filter(event) == SeatEventFilterResult::capture) {
            return;
        }
    }

    client->event_handler(event);
}

auto seat_post_input_event(Weak<SeatInputDevice> device, SeatEvent* event) -> bool
{
    for (auto* filter : device->seat->event_filters) {
        if (filter->filter(event) == SeatEventFilterResult::capture) {
            return false;
        }
    }

    if (!device) return false;
    if (device->focus) {
        device->focus->client->event_handler(event);
        return true;
    }

    return false;
}
