#pragma once
#include "debug.hpp"

// File IO

UNIX_FUNCTION(open,   UnixErrorBehavior::negative_one)
UNIX_FUNCTION(close,  UnixErrorBehavior::negative_one)
UNIX_FUNCTION(read,   UnixErrorBehavior::negative_one)
UNIX_FUNCTION(write,  UnixErrorBehavior::negative_one)
UNIX_FUNCTION(pipe,   UnixErrorBehavior::negative_one)
UNIX_FUNCTION(dup2,   UnixErrorBehavior::negative_one)
UNIX_FUNCTION(fchdir, UnixErrorBehavior::negative_one)

UNIX_FUNCTION(fdopen,  UnixErrorBehavior::null)
UNIX_FUNCTION(freopen, UnixErrorBehavior::null)

// Process

UNIX_FUNCTION(sigfillset,  UnixErrorBehavior::negative_one)
UNIX_FUNCTION(sigprocmask, UnixErrorBehavior::negative_one)
UNIX_FUNCTION(execveat,    UnixErrorBehavior::negative_one)

// Memory

UNIX_FUNCTION(memfd_create, UnixErrorBehavior::negative_one)

UNIX_FUNCTION(malloc, UnixErrorBehavior::null)

UNIX_FUNCTION(mmap, ([]<auto Function>(auto... args) -> UnixResult<decltype(Function(args...))> {
    auto res = Function(args...);
    if (res == MAP_FAILED) return { nullptr, errno };
    return { res, 0 };
}))
UNIX_FUNCTION(munmap, UnixErrorBehavior::negative_one)

UNIX_FUNCTION(ftruncate, UnixErrorBehavior::negative_one)
UNIX_FUNCTION(fstat,     UnixErrorBehavior::negative_one)
UNIX_FUNCTION(fcntl,     UnixErrorBehavior::negative_one)

UNIX_FUNCTION(shm_open, UnixErrorBehavior::negative_one)

// Event / Timers / Polling

UNIX_FUNCTION(timerfd_create,  UnixErrorBehavior::negative_one)
UNIX_FUNCTION(timerfd_settime, UnixErrorBehavior::negative_one)
UNIX_FUNCTION(epoll_create1,   UnixErrorBehavior::negative_one)
UNIX_FUNCTION(epoll_wait,      UnixErrorBehavior::negative_one)
UNIX_FUNCTION(epoll_ctl,       UnixErrorBehavior::negative_one)
UNIX_FUNCTION(poll,            UnixErrorBehavior::negative_one)
UNIX_FUNCTION(eventfd,         UnixErrorBehavior::negative_one)
UNIX_FUNCTION(eventfd_write,   UnixErrorBehavior::negative_one)
UNIX_FUNCTION(eventfd_read,    UnixErrorBehavior::negative_one)
UNIX_FUNCTION(signalfd,        UnixErrorBehavior::negative_one)

// Capabilities

UNIX_FUNCTION(cap_get_proc, UnixErrorBehavior::null)
UNIX_FUNCTION(cap_free,     UnixErrorBehavior::negative_one)
UNIX_FUNCTION(cap_set_proc, UnixErrorBehavior::negative_one)
UNIX_FUNCTION(cap_get_flag, UnixErrorBehavior::negative_one)
UNIX_FUNCTION(cap_set_flag, UnixErrorBehavior::negative_one)

// DRM

UNIX_FUNCTION(drmIoctl, UnixErrorBehavior::negative_one)

UNIX_FUNCTION(drmSyncobjFDToHandle,     UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjHandleToFD,     UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjCreate,         UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjDestroy,        UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjImportSyncFile, UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjExportSyncFile, UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjTransfer,       UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjQuery,          UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjTimelineSignal, UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmSyncobjTimelineWait,   UnixErrorBehavior::negative_errno)

UNIX_FUNCTION(drmGetDevices2,        UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(drmGetDeviceFromDevId, UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(drmGetMagic,           UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(drmAuthMagic,          UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(drmSetClientCap,       UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmGetCap,             UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmHandleEvent,        UnixErrorBehavior::negative_one)

UNIX_FUNCTION(drmModeGetResources,        UnixErrorBehavior::null)
UNIX_FUNCTION(drmModeGetPlaneResources,   UnixErrorBehavior::null)
UNIX_FUNCTION(drmModeGetProperty,         UnixErrorBehavior::null)
UNIX_FUNCTION(drmModeGetPropertyBlob,     UnixErrorBehavior::null)
UNIX_FUNCTION(drmModeObjectGetProperties, UnixErrorBehavior::null)
UNIX_FUNCTION(drmPrimeFDToHandle,         UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmCloseBufferHandle,       UnixFunction<drmIoctl>::behavior)
UNIX_FUNCTION(drmModeAddFB2WithModifiers, UnixErrorBehavior::negative_errno /* DRM_IOCTL (negative_errno and sets errno) */)
UNIX_FUNCTION(drmModeCloseFB,             UnixErrorBehavior::negative_errno /* DRM_IOCTL */)
UNIX_FUNCTION(drmModeAtomicAlloc,         UnixErrorBehavior::null)
UNIX_FUNCTION(drmModeAtomicCommit,        UnixErrorBehavior::negative /* negative_one or DRM_IOCTL */)
UNIX_FUNCTION(drmModeAtomicAddProperty,   UnixErrorBehavior::negative_errno)

// udev

UNIX_FUNCTION(udev_new, UnixErrorBehavior::null)

// evdev

UNIX_FUNCTION(libevdev_new_from_fd, UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(libevdev_next_event,  UnixErrorBehavior::negative_errno)

// libseat

UNIX_FUNCTION(libseat_open_seat,      UnixErrorBehavior::null)
UNIX_FUNCTION(libseat_get_fd,         UnixErrorBehavior::negative_one)
UNIX_FUNCTION(libseat_dispatch,       UnixErrorBehavior::negative_one)
UNIX_FUNCTION(libseat_switch_session, UnixErrorBehavior::negative_one)

// libinput

UNIX_FUNCTION(libinput_dispatch,         UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(libinput_udev_assign_seat, UnixErrorBehavior::negative_errno)
