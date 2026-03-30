#pragma once
#include "debug.hpp"

// File IO

UNIX_ERROR_BEHAVIOUR(open,  negative_one)
UNIX_ERROR_BEHAVIOUR(close, negative_one)
UNIX_ERROR_BEHAVIOUR(read,  negative_one)
UNIX_ERROR_BEHAVIOUR(write, negative_one)
UNIX_ERROR_BEHAVIOUR(pipe,  negative_one)

// Memory

UNIX_ERROR_BEHAVIOUR(memfd_create, negative_one)

UNIX_ERROR_BEHAVIOUR(malloc, null)

UNIX_ERROR_BEHAVIOUR(mmap,   negative_one)
UNIX_ERROR_BEHAVIOUR(munmap, negative_one)

UNIX_ERROR_BEHAVIOUR(ftruncate, negative_one)
UNIX_ERROR_BEHAVIOUR(fstat,     negative_one)
UNIX_ERROR_BEHAVIOUR(fcntl,     negative_one)

UNIX_ERROR_BEHAVIOUR(shm_open, negative_one)

// Event / Timers / Polling

UNIX_ERROR_BEHAVIOUR(timerfd_create,  negative_one)
UNIX_ERROR_BEHAVIOUR(timerfd_settime, negative_one)
UNIX_ERROR_BEHAVIOUR(epoll_create1,   negative_one)
UNIX_ERROR_BEHAVIOUR(epoll_wait,      negative_one)
UNIX_ERROR_BEHAVIOUR(epoll_ctl,       negative_one)
UNIX_ERROR_BEHAVIOUR(poll,            negative_one)
UNIX_ERROR_BEHAVIOUR(eventfd,         negative_one)
UNIX_ERROR_BEHAVIOUR(eventfd_write,   negative_one)
UNIX_ERROR_BEHAVIOUR(eventfd_read,    negative_one)

// Capabilities

UNIX_ERROR_BEHAVIOUR(cap_get_proc, null)
UNIX_ERROR_BEHAVIOUR(cap_free,     negative_one)
UNIX_ERROR_BEHAVIOUR(cap_set_proc, negative_one)
UNIX_ERROR_BEHAVIOUR(cap_get_flag, negative_one)
UNIX_ERROR_BEHAVIOUR(cap_set_flag, negative_one)

// DRM

UNIX_ERROR_BEHAVIOUR(drmSyncobjFDToHandle,     negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjHandleToFD,     negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjCreate,         negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjDestroy,        negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjImportSyncFile, negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjExportSyncFile, negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjTimelineSignal, negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjTransfer,       negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjQuery,          negative_one)
UNIX_ERROR_BEHAVIOUR(drmSyncobjTimelineWait,   negative_one)

UNIX_ERROR_BEHAVIOUR(drmGetDeviceFromDevId, negative_one)
UNIX_ERROR_BEHAVIOUR(drmIoctl,              negative_one)
UNIX_ERROR_BEHAVIOUR(drmGetMagic,           negative_one)
UNIX_ERROR_BEHAVIOUR(drmAuthMagic,          negative_one)
UNIX_ERROR_BEHAVIOUR(drmSetClientCap,       negative_one)
UNIX_ERROR_BEHAVIOUR(drmGetCap,             negative_one)

UNIX_ERROR_BEHAVIOUR(drmModeGetResources,        null)
UNIX_ERROR_BEHAVIOUR(drmModeGetPlaneResources,   null)
UNIX_ERROR_BEHAVIOUR(drmPrimeFDToHandle,         negative_one)
UNIX_ERROR_BEHAVIOUR(drmModeAddFB2WithModifiers, negative_one)
UNIX_ERROR_BEHAVIOUR(drmModeAtomicCommit,        negative_one)

// udev

UNIX_ERROR_BEHAVIOUR(udev_new, null)

// evdev

UNIX_ERROR_BEHAVIOUR(libevdev_new_from_fd, negative_errno)
UNIX_ERROR_BEHAVIOUR(libevdev_next_event,  negative_errno)

// libseat

UNIX_ERROR_BEHAVIOUR(libseat_open_seat, null)
UNIX_ERROR_BEHAVIOUR(libseat_get_fd,    negative_one)
UNIX_ERROR_BEHAVIOUR(libseat_dispatch,  negative_one)

// libinput

UNIX_ERROR_BEHAVIOUR(libinput_dispatch,         negative_errno)
UNIX_ERROR_BEHAVIOUR(libinput_udev_assign_seat, negative_errno)
