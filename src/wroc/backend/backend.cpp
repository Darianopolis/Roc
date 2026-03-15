#include "direct/backend.hpp"
#include "wayland/backend.hpp"

core::Ref<wroc_backend> wroc_backend_create(wroc_backend_type type)
{
    core::Ref<wroc_backend> backend = nullptr;
    switch (type) {
        break;case wroc_backend_type::layered:
            backend = core::create<wroc_wayland_backend>();
        break;case wroc_backend_type::direct:
            backend = core::create<wroc_direct_backend>();
        break;default:
            core::debugkill();
    }
    backend->type = type;
    return backend;
}
