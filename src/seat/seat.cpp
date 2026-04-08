#include "seat.hpp"

auto seat_create() -> Ref<Seat>
{
    return ref_create<Seat>();
}
