#pragma once

#include "wroc/wroc.hpp"

// -----------------------------------------------------------------------------

struct wroc_direct_backend;
struct wroc_input_device;

struct wroc_device : wroc_object
{
    int dev_id;
    int fd;
};

struct wroc_drm_output_state;

struct wroc_drm_buffer
{
    core::Weak<gpu_image> image;
    u32 fb2_handle;
};

struct wroc_drm_output : wroc_output
{
    wroc_drm_output_state* state;

    ~wroc_drm_output();

    virtual wroc_output_commit_id commit(gpu_image*, gpu_syncpoint acquire, gpu_syncpoint release, core::Flags<wroc_output_commit_flag>) final override;
};

struct wroc_libinput_keyboard : wroc_keyboard
{
    wroc_input_device* base;

    virtual void update_leds(libinput_led) final override;
};

struct wroc_libinput_pointer : wroc_pointer
{
    wroc_input_device* base;
};

struct wroc_input_device : wroc_object
{
    wroc_direct_backend* backend;

    libinput_device* handle;

    core::Ref<wroc_libinput_keyboard> keyboard;
    core::Ref<wroc_libinput_pointer> pointer;

    ~wroc_input_device();
};

struct wroc_direct_backend : wroc_backend
{
    core::Fd drm_fd;

    struct libseat* seat;
    const char* seat_name;
    struct udev* udev;
    struct libinput* libinput;

    std::vector<core::Ref<wroc_device>> devices;

    std::vector<core::Ref<wroc_input_device>> input_devices;

    std::vector<core::Ref<wroc_drm_output>> outputs;

    std::vector<wroc_drm_buffer> buffer_cache;

    core::Fd libseat_fd = {};
    core::Fd libinput_fd = {};

    virtual void init() final override;
    virtual void start() final override;

    gpu_format_set format_set;

    virtual const gpu_format_set& get_output_format_set() final override
    {
        return format_set;
    }

    virtual void create_output() final override;
    virtual void destroy_output(wroc_output*) final override;

    ~wroc_direct_backend();
};

inline
wroc_direct_backend* wroc_get_direct_backend()
{
    return static_cast<wroc_direct_backend*>(server->backend.get());
}

wroc_device* wroc_open_restricted(wroc_direct_backend*, const char* name);

void wroc_backend_init_session(wroc_direct_backend*);
void wroc_backend_close_session(wroc_direct_backend*);

void wroc_backend_handle_libinput_event(wroc_direct_backend*, libinput_event*);

void wroc_backend_init_drm(wroc_direct_backend*);
void wroc_backend_start_drm(wroc_direct_backend*);