#include "drm.hpp"

#include <core/log.hpp>

inline
auto io_drm_object_type_get_name(IoDrmObjectType type) -> const char*
{
    switch (type) {
        break;case DRM_MODE_OBJECT_CRTC:      return "crtc";
        break;case DRM_MODE_OBJECT_CONNECTOR: return "connector";
        break;case DRM_MODE_OBJECT_ENCODER:   return "encoder";
        break;case DRM_MODE_OBJECT_MODE:      return "mode";
        break;case DRM_MODE_OBJECT_PROPERTY:  return "property";
        break;case DRM_MODE_OBJECT_FB:        return "framebuffer";
        break;case DRM_MODE_OBJECT_BLOB:      return "blob";
        break;case DRM_MODE_OBJECT_PLANE:     return "plane";
        break;case DRM_MODE_OBJECT_COLOROP:   return "color-op";
    }
    return "invalid";
}

inline
void io_drm_object_dump_properties(IoDrmObject* object)
{
    log_warn("drm.{}{{{}}}.properties:", io_drm_object_type_get_name(object->type), object->id);

    for (auto[id, value] : object->property_values) {
        auto prop = io_drm_get_property(object->io, id);
        if (drmModeGetPropertyType(prop->info.get()) == DRM_MODE_PROP_ENUM) {
            log_warn("  [{}] = {}", prop->name(), prop->enum_name(value));
        } else {
            log_warn("  [{}] = {}", prop->name(), value);
        }
    }
}

inline
void io_drm_dump_planes(IoContext* io)
{
    for (auto* plane : io->drm->planes) {
        auto plane_type_prop = io_drm_get_property(io, plane->property_name_lookup.at("type"));
        log_warn("drm.plane{{{:3}:{:7}}} -> crtc{{{:3}}} {}",
            plane->id,
            plane_type_prop->enum_name(plane->get("type")),
            plane->get("CRTC_ID"),
            io->drm->crtcs
                | std::views::enumerate
                | std::views::filter([&](auto e) { return (1u << std::get<0>(e)) & plane->info->possible_crtcs; })
                | std::views::transform([&](auto e) { return std::get<1>(e)->id; }));
    }
}
