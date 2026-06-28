#pragma once

#include "../internal.hpp"

template<auto Fn>
struct IoDrmDeleter { void operator()(auto v) { Fn(v); } };

// -----------------------------------------------------------------------------

using IoDrmObjectId = u32;
using IoDrmPropertyId = u32;
using IoDrmObjectType = u32;

struct IoDrmProperty
{
    std::unique_ptr<drmModePropertyRes, IoDrmDeleter<drmModeFreeProperty>> info;

    auto id()   -> u32              { return info->prop_id; }
    auto name() -> std::string_view { return info->name;    }

    auto enum_name(u64 value) -> std::string_view
    {
        for (int e = 0; e < info->count_enums; ++e) {
            if (value == info->enums[e].value)  return info->enums[e].name;
        }
        return "";
    }
};

auto io_drm_get_property(IoContext*, IoDrmPropertyId) -> IoDrmProperty*;

struct IoDrmObject
{
    IoContext* io;
    IoDrmObjectId id;
    IoDrmObjectType type;

    u32 index;

    ankerl::unordered_dense::map<IoDrmPropertyId, u64> property_values;
    ankerl::unordered_dense::map<std::string_view, IoDrmPropertyId> property_name_lookup;

    void update_properties();

    auto get(std::string_view name) -> u64;
    void set(drmModeAtomicReqPtr, std::string_view name, u64);

    IoDrmObject() = default;
    DELETE_COPY_MOVE(IoDrmObject);
};

// -----------------------------------------------------------------------------

struct IoDrmCrtc;
struct IoDrmEncoder;
struct IoDrmConnector;
struct IoDrmPlane;

struct IoDrmOutput : IoOutputBase
{
    IoDrmPlane* primary_plane;
    IoDrmPlane* cursor_plane;
    IoDrmCrtc* crtc;
    Weak<IoDrmConnector> connector;

    Ref<GpuImage> current_image;
    Ref<GpuImage> pending_image;

    Ref<GpuImage> current_cursor_image;
    Ref<GpuImage> pending_cursor_image;

    std::chrono::steady_clock::time_point last_commit_time = {};

    GpuFormatSet formats;

    virtual auto info() -> IoOutputInfo final override
    {
        return {
            .size = size,
            .formats = &formats,
        };
    }

    virtual auto commit(const WmOutputCommitInfo&) -> bool final override;
};

// -----------------------------------------------------------------------------

struct IoDrmBuffer
{
    Weak<GpuImageBase> image;
    u32 framebuffer;
};

// -----------------------------------------------------------------------------

struct IoDrm
{
    fd_t fd;

    Listener<void(bool)> session_listener;

    ankerl::unordered_dense::segmented_map<IoDrmPropertyId, IoDrmProperty> properties;

    RefVector<IoDrmPlane> planes;
    RefVector<IoDrmCrtc> crtcs;
    RefVector<IoDrmEncoder> encoders;
    RefVector<IoDrmConnector> connectors;

    RefVector<IoDrmOutput> outputs;

    std::vector<IoDrmBuffer> buffer_cache;
};

void io_drm_enumerate_static_objects(IoContext*);
void io_drm_enumerate_connectors(IoContext*);
void io_drm_configure_outputs(IoContext*);

// -----------------------------------------------------------------------------

template<typename T>
auto io_drm_find_object(auto&& list, IoDrmObjectId id) -> T*
{
    if (!id) return nullptr;
    for (T* object : list) {
        if (object->id == id) return object;
    }
    return nullptr;
}

#define IO_DRM_DEFINE_OBJECT(T, L, U) \
    struct IoDrm ## T : IoDrmObject \
    { \
        std::unique_ptr<_drmMode ## T, IoDrmDeleter<drmModeFree ## T>> info; \
        static constexpr IoDrmObjectType Type = DRM_MODE_OBJECT_ ## U; \
        IoDrm ## T() { type = Type; } \
        void update_info() { info.reset(drmModeGet ## T(io->drm->fd, id)); } \
    }; \
    inline auto io_drm_find_ ## L (IoContext* io, IoDrmObjectId id) -> IoDrm ## T* \
    { \
        return io_drm_find_object<IoDrm ## T>(io->drm->L ## s, id); \
    }

IO_DRM_DEFINE_OBJECT(Crtc, crtc, CRTC)
IO_DRM_DEFINE_OBJECT(Plane, plane, PLANE)
IO_DRM_DEFINE_OBJECT(Connector, connector, CONNECTOR)
IO_DRM_DEFINE_OBJECT(Encoder, encoder, ENCODER)
