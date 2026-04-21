#include "internal.hpp"

auto seat_manager_create() -> Ref<SeatManager>
{
    return ref_create<SeatManager>();
}

// -----------------------------------------------------------------------------

auto seat_create(SeatManager* manager, SeatKeyboard* keyboard, SeatPointer* pointer) -> Ref<Seat>
{
    auto seat = ref_create<Seat>();

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

auto seat_get_keyboard(Seat* seat) -> SeatKeyboard*
{
    return seat->keyboard.get();
}

auto seat_get_pointer(Seat* seat) -> SeatPointer*
{
    return seat->pointer.get();
}

// -----------------------------------------------------------------------------

auto seat_add_input_event_filter(Seat* seat, std::move_only_function<SeatEventFilterResult(SeatEvent*)> fn) -> Ref<SeatEventFilter>
{
    auto filter = ref_create<SeatEventFilter>();
    filter->seat = seat;
    filter->filter = std::move(fn);
    seat->input_event_filters.emplace_back(filter.get());
    return filter;
}

SeatEventFilter::~SeatEventFilter()
{
    if (seat) {
        std::erase(seat->input_event_filters, this);
    }
}

auto seat_post_input_event(Weak<SeatInputDevice> device, SeatEvent* event) -> bool
{
    for (auto* filter : device->seat->input_event_filters) {
        if (filter->filter(event) == SeatEventFilterResult::capture) {
            return false;
        }
    }

    if (!device) return false;
    if (device->focus) {
        seat_client_post_event(device->focus->client, event);
        return true;
    }

    return false;
}
