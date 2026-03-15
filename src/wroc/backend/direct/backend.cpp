#include "backend.hpp"

#define WROC_DIRECT_BACKEND_TIME_LIMIT 0

void wroc_direct_backend::init()
{
#if WROC_DIRECT_BACKEND_TIME_LIMIT
    log_warn("Direct backend is highly experimental and will self terminate after {} seconds to prevent system lockout", WROC_DIRECT_BACKEND_TIME_LIMIT);
    std::thread{[] {
        std::this_thread::sleep_for(operator""s(WROC_DIRECT_BACKEND_TIME_LIMIT));
        core::debugkill();
    }}.detach();
#endif

    wroc_backend_init_session(this);
    wroc_backend_init_drm(this);
}

void wroc_direct_backend::start()
{
    wroc_backend_start_drm(this);
}

wroc_direct_backend::~wroc_direct_backend()
{
    wroc_backend_close_session(this);

    outputs.clear();
}
