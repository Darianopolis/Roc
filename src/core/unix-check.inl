#pragma once
#include "debug.hpp"

// File IO

CORE_UNIX_ERROR_BEHAVIOUR(open,  negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(close, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(read,  negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(write, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(pipe,  negative_one)

// Memory

CORE_UNIX_ERROR_BEHAVIOUR(memfd_create, negative_one)

CORE_UNIX_ERROR_BEHAVIOUR(malloc, null)

CORE_UNIX_ERROR_BEHAVIOUR(mmap,   negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(munmap, negative_one)

CORE_UNIX_ERROR_BEHAVIOUR(ftruncate, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(fstat,     negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(fcntl,     negative_one)

CORE_UNIX_ERROR_BEHAVIOUR(shm_open, negative_one)

// Event / Timers / Polling

CORE_UNIX_ERROR_BEHAVIOUR(timerfd_create,  negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(timerfd_settime, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(epoll_create1,   negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(epoll_wait,      negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(epoll_ctl,       negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(poll,            negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(eventfd,         negative_one)

// Capabilities

CORE_UNIX_ERROR_BEHAVIOUR(cap_get_proc, null)
CORE_UNIX_ERROR_BEHAVIOUR(cap_free,     negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(cap_set_proc, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(cap_get_flag, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(cap_set_flag, negative_one)

// DRM

CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjFDToHandle,     negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjHandleToFD,     negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjCreate,         negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjDestroy,        negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjImportSyncFile, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjExportSyncFile, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjTimelineSignal, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSyncobjTransfer,       negative_one)

CORE_UNIX_ERROR_BEHAVIOUR(drmGetDeviceFromDevId, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmIoctl,              negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmGetMagic,           negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmAuthMagic,          negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmSetClientCap,       negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmGetCap,             negative_one)

CORE_UNIX_ERROR_BEHAVIOUR(drmModeGetResources,        null)
CORE_UNIX_ERROR_BEHAVIOUR(drmModeGetPlaneResources,   null)
CORE_UNIX_ERROR_BEHAVIOUR(drmPrimeFDToHandle,         negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmModeAddFB2WithModifiers, negative_one)
CORE_UNIX_ERROR_BEHAVIOUR(drmModeAtomicCommit,        negative_one)

// libseat

CORE_UNIX_ERROR_BEHAVIOUR(libseat_open_seat, null)
CORE_UNIX_ERROR_BEHAVIOUR(libseat_get_fd,    negative_one)

// libinput

CORE_UNIX_ERROR_BEHAVIOUR(libinput_dispatch,         negative_errno)
CORE_UNIX_ERROR_BEHAVIOUR(libinput_udev_assign_seat, negative_errno)
