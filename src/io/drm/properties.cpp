#include "drm.hpp"

#include <core/log.hpp>

IoDrmResources::IoDrmResources(fd_t drm_fd)
{
    auto mode_res = unix_check<drmModeGetResources>(drm_fd).value;
    if (!mode_res) {
        log_warn("Failed to get mode resources");
        return;
    }
    defer { drmModeFreeResources(mode_res); };

    auto plane_res = unix_check<drmModeGetPlaneResources>(drm_fd).value;
    if (!plane_res) {
        log_warn("Failed to get plane resources");
        return;
    }
    defer { drmModeFreePlaneResources(plane_res); };

#define DO(Type, Name, Res) \
    for (decltype(Res->count_##Name) i = 0; i < Res->count_ ## Name; ++i) { \
        Name.emplace_back(drmModeGet##Type(drm_fd, Res->Name[i])); \
    }

    DO(Connector, connectors, mode_res)
    DO(Encoder, encoders, mode_res)
    DO(Crtc, crtcs, mode_res)
    DO(Plane, planes, plane_res)

#undef DO
}

#define DO(Type, Name) \
    drmMode##Type* IoDrmResources::find_##Name(u32 id) \
    { \
        for (auto* c : Name##s) if (c->Name##_id == id) return c;\
        return nullptr; \
    }

DO(Connector, connector)
DO(Encoder, encoder)
DO(Crtc, crtc)
DO(Plane, plane)

#undef DO

IoDrmResources::~IoDrmResources()
{
    for (auto* c : connectors) drmModeFreeConnector(c);
    for (auto* e : encoders)   drmModeFreeEncoder(e);
    for (auto* c : crtcs)      drmModeFreeCrtc(c);
    for (auto* p : planes)     drmModeFreePlane(p);
}

// -----------------------------------------------------------------------------

IoDrmPropertyMap::IoDrmPropertyMap(fd_t drm, u32 object_id, u32 object_type)
    : drm(drm)
    , props(PropPtr(drmModeObjectGetProperties(drm, object_id, object_type)))
{
    for (u32 i = 0; i < props->count_props; ++i) {
        auto* prop = drmModeGetProperty(drm, props->props[i]);
        properties[prop->name] = prop;
    }
}

IoDrmPropertyMap::IoDrmPropertyMap(IoDrmPropertyMap&& other)
    : drm(other.drm)
    , props(std::move(other.props))
    , properties(std::move(other.properties))
{}

IoDrmPropertyMap::~IoDrmPropertyMap()
{
    for (auto[_, prop] : properties) drmModeFreeProperty(prop);
}

auto IoDrmPropertyMap::get_prop_id(std::string_view prop_name) -> u32
{
    return properties.at(prop_name)->prop_id;
}

auto IoDrmPropertyMap::get_prop_value(std::string_view prop_name) -> u64
{
    auto id = get_prop_id(prop_name);
    for (u32 i = 0; i < props->count_props; ++i) {
        if (props->props[i] == id) {
            return props->prop_values[i];
        }
    }
    debug_assert_fail("Failed to find property");
}

auto IoDrmPropertyMap::get_enum_value(std::string_view prop_name, std::string_view enum_name) -> int
{
    auto* prop = properties.at(prop_name);

    debug_assert(prop->flags & DRM_MODE_PROP_ENUM);
    for (int e = 0; e < prop->count_enums; ++e) {
        if (enum_name == prop->enums[e].name) {
            return prop->enums[e].value;
        }
    }

    log_error("Failed to find enum value: {}.{}", prop_name, enum_name);
    debug_kill();
}

// -----------------------------------------------------------------------------

auto parse_plane_formats(IoContext* io, IoDrmResources* resources, drmModePlane* plane) -> GpuFormatSet
{
    auto drm = io->drm->fd;

    IoDrmPropertyMap props{drm, plane->plane_id, DRM_MODE_OBJECT_PLANE};
    auto blob_id = props.get_prop_value("IN_FORMATS");
    if (!blob_id) {
        log_error("Plane has no IN_FORMATS property");
        return {};
    }

    auto blob = drmModeGetPropertyBlob(drm, blob_id);
    defer { drmModeFreePropertyBlob(blob); };

    auto* header = static_cast<drm_format_modifier_blob*>(blob->data);

    auto* formats = byte_offset_pointer<GpuDrmFormat>(blob->data, header->formats_offset);
    auto* modifiers = byte_offset_pointer<drm_format_modifier>(blob->data, header->modifiers_offset);

    GpuFormatSet set;
    for (auto mod : std::span(modifiers, header->count_modifiers)) {
        u32 index = mod.offset;
        while (mod.formats) {
            auto bit_idx = std::countr_zero(mod.formats);
            index += bit_idx;

            auto format = gpu_format_from_drm(formats[index]);
            if (format) set.add(format, mod.modifier);

            mod.formats >>= bit_idx + 1;
            index++;
        }
    }

    return set;
}
