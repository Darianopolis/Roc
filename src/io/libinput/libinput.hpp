#include "../internal.hpp"

struct IoLibinputDevice;

struct IoLibinput
{
    struct libinput* libinput;

    RefVector<IoLibinputDevice> input_devices;
};

struct IoLibinputDevice : IoInputDeviceBase
{
    libinput_device* handle;

    virtual void update_leds(Flags<libinput_led> leds) final override;

    ~IoLibinputDevice();
};

void io_libinput_handle_event(IoContext*, libinput_event*);
