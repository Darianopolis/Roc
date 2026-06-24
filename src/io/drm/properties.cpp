#include "drm.hpp"
#include "debug.hpp"

#include <core/log.hpp>

template<typename T>
static
auto create_drm_object(IoContext* io, IoDrmObjectId id, u32 index) -> Ref<T>
{
    auto object = ref_create<T>();
    object->io = io;
    object->id = id;
    object->index = index;
    object->update_properties();
    object->update_info();
    io_drm_object_dump_properties(object.get());
    return object;
}

void io_drm_enumerate_static_objects(IoContext* io)
{
    auto resources = unix_check<drmModeGetResources>(io->drm->fd).value;
    defer { drmModeFreeResources(resources); };

    for (int i = 0; i < resources->count_crtcs; ++i) {
        io->drm->crtcs.emplace_back(create_drm_object<IoDrmCrtc>(io, resources->crtcs[i], i));
    }

    for (int i = 0; i < resources->count_encoders; ++i) {
        io->drm->encoders.emplace_back(create_drm_object<IoDrmEncoder>(io, resources->encoders[i], i));
    }

    auto planes = unix_check<drmModeGetPlaneResources>(io->drm->fd).value;
    defer { drmModeFreePlaneResources(planes); };

    for (u32 i = 0; i < planes->count_planes; ++i) {
        io->drm->planes.emplace_back(create_drm_object<IoDrmPlane>(io, planes->planes[i], i));
    }
}

void io_drm_enumerate_connectors(IoContext* io)
{
    ankerl::unordered_dense::map<IoDrmObjectId, u32> new_connectors;

    auto resources = unix_check<drmModeGetResources>(io->drm->fd).value;
    defer { drmModeFreeResources(resources); };
    for (int i = 0; i < resources->count_connectors; ++i) {
        new_connectors.emplace(resources->connectors[i], i);
    }

    io->drm->connectors.erase_if([&](IoDrmConnector* connector) {
        return !new_connectors.contains(connector->id);
    });

    for (auto* existing : io->drm->connectors) {
        new_connectors.erase(existing->id);
    }

    for (auto[id, index] : new_connectors) {
        io->drm->connectors.emplace_back(create_drm_object<IoDrmConnector>(io, id, index));
    }
}

auto io_drm_get_property(IoContext* io, IoDrmPropertyId id) -> IoDrmProperty*
{
    auto iter = io->drm->properties.find(id);
    if (iter != io->drm->properties.end()) return &iter->second;

    auto& properties = io->drm->properties[id];
    properties.info.reset(unix_check<drmModeGetProperty>(io->drm->fd, id).value);
    return &properties;
}

void IoDrmObject::update_properties()
{
    property_name_lookup.clear();
    property_values.clear();

    auto properties = unix_check<drmModeObjectGetProperties>(io->drm->fd, id, type).value;
    defer { drmModeFreeObjectProperties(properties); };

    if (!properties) return;

    for (u32 i = 0; i < properties->count_props; ++i) {
        auto prop_id = IoDrmPropertyId(properties->props[i]);
        auto prop = io_drm_get_property(io, prop_id);
        auto prop_name = prop->name();
        property_name_lookup[prop_name] = prop_id;
        property_values[prop_id] = properties->prop_values[i];
    }
}

auto IoDrmObject::get(std::string_view name) -> u64
{
    auto prop_id = property_name_lookup.at(name);
    return property_values.at(prop_id);
}

void IoDrmObject::set(drmModeAtomicReqPtr req, std::string_view name, u64 value)
{
    auto prop_id = property_name_lookup.at(name);
    unix_check<drmModeAtomicAddProperty>(req, id, prop_id, value);
}
