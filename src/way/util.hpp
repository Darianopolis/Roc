#pragma once

#include <wayland-server-core.h>

#include <core/util.hpp>
#include <core/object.hpp>

// -----------------------------------------------------------------------------

template<typename T>
auto way_to_span(wl_array* array) -> std::span<T>
{
    usz count = array->size / sizeof(T);
    return std::span<T>(static_cast<T*>(array->data), count);
}

template<typename T>
auto way_to_wl_array(std::span<T> span) -> wl_array
{
    return wl_array {
        .size = span.size_bytes(),
        .alloc = span.size_bytes(),
        .data = const_cast<void*>(static_cast<const void*>(span.data())),
    };
}

// -----------------------------------------------------------------------------

struct WayObject
{
    virtual ~WayObject() = default;
};

template<typename T>
auto way_get_userdata(void* data) -> T*
{
    auto* base = static_cast<WayObject*>(data);
    if (!base) return nullptr;
    auto* derived = dynamic_cast<T*>(base);
    if (!derived) {
        log_error("way_get_userdata<{}> failed, got {}", typeid(T).name(), typeid(*base).name());
        debug_kill();
    }
    return derived;
}

template<typename T>
auto way_get_userdata(wl_resource* resource) -> T*
{
    return resource ? way_get_userdata<T>(wl_resource_get_user_data(resource)) : nullptr;
}

// -----------------------------------------------------------------------------

class WayResource
{
    wl_resource* resource = {};
    wl_listener destroy_listener {
        .notify = on_destroy,
    };

public:
    WayResource()
    {
        wl_list_init(&destroy_listener.link);
    }

    WayResource(wl_resource* resource)
        : resource(resource)
    {
        wl_resource_add_destroy_listener(resource, &destroy_listener);
    }

    void reset(wl_resource* new_resource)
    {
        if (resource == new_resource) return;

        resource = new_resource;
        wl_list_remove(&destroy_listener.link);
        wl_list_init(&destroy_listener.link);

        if (resource) {
            wl_resource_add_destroy_listener(new_resource, &destroy_listener);
        }
    }

    auto& operator=(wl_resource* other)
    {
        reset(other);
        return *this;
    }

    static void on_destroy(wl_listener* listener, void* data)
    {
        WayResource* self = wl_container_of(listener, self, destroy_listener);
        self->resource = nullptr;
        wl_list_init(&self->destroy_listener.link);
    }

    ~WayResource()
    {
        wl_list_remove(&destroy_listener.link);
    }

    DELETE_COPY_MOVE(WayResource)

    operator wl_resource*() const { return resource; }
};

// -----------------------------------------------------------------------------

class WayResourceList
{
    struct ListNode
    {
        wl_resource* resource = nullptr;
        wl_listener destroy_listener {
            .notify = on_destroy,
        };
        ListNode* prev = nullptr;
        ListNode* next = nullptr;

        ListNode()
        {
            wl_list_init(&destroy_listener.link);
        }

        ListNode(wl_resource* resource)
            : resource(resource)
        {
            wl_resource_add_destroy_listener(resource, &destroy_listener);
        }

        static void on_destroy(wl_listener* listener, void* data)
        {
            ListNode* self = wl_container_of(listener, self, destroy_listener);

            // log_debug("cleaning up ListNode: {}", (void*)self);
            wl_list_init(&self->destroy_listener.link);

            if (self->prev) self->prev->next = self->next;
            if (self->next) self->next->prev = self->prev;

            delete self;
        }

        ~ListNode()
        {
            // log_debug("List node destroyed: {}", (void*)this);

            wl_list_remove(&destroy_listener.link);
        }

        DELETE_COPY_MOVE(ListNode)
    };

    ListNode root;

    struct Iterator
    {
        const ListNode* current;

        auto operator++() -> Iterator&
        {
            current = current->next;
            return *this;
        }

        auto operator==(const Iterator& other) const -> bool
        {
            return current == other.current;
        }

        auto operator*() const -> wl_resource*
        {
            return current->resource;
        }
    };

public:
    WayResourceList()
    {
        root.next = &root;
        root.prev = &root;
    }

    void emplace_back(wl_resource* resource)
    {
        if (!resource) return;

        auto* node = new ListNode{resource};

        node->next = &root;
        node->prev = root.prev;

        root.prev->next = node;
        root.prev = node;
    }

    void clear()
    {
        ListNode* next = nullptr;
        for (auto* node = root.next; node != &root; node = next) {
            next = node->next;
            delete node;
        }
        root.next = &root;
        root.prev = &root;
    }

    void take_and_append_all(WayResourceList&& other)
    {
        if (other.root.next == &other.root) return;

        other.root.next->prev = root.prev;
        other.root.prev->next = &root;

        root.prev->next = other.root.next;
        root.prev       = other.root.prev;

        other.root.next = &other.root;
        other.root.prev = &other.root;
    }

    auto front() const -> wl_resource*
    {
        return root.next ? root.next->resource : nullptr;
    }

    auto begin() const -> Iterator
    {
        return Iterator{root.next};
    }

    auto end() const -> Iterator
    {
        return Iterator{&root};
    }

    ~WayResourceList()
    {
        clear();
    }

    DELETE_COPY_MOVE(WayResourceList)
};

// -----------------------------------------------------------------------------

struct WayListener
{
    void* data;
    wl_listener listener;

    WayListener() = default;

    DELETE_COPY_MOVE(WayListener);

    ~WayListener()
    {
        wl_list_remove(&listener.link);
    }

    template<typename T>
    auto get() const -> T*
    {
        return way_get_userdata<T>(data);
    }

    static
    auto from(wl_listener* listener) -> WayListener&
    {
        WayListener* WayListener = wl_container_of(listener, WayListener, listener);
        return *WayListener;
    }
};

template<typename T>
auto way_get_userdata(wl_listener* listener) -> T*
{
    return WayListener::from(listener).get<T>();
}

// -----------------------------------------------------------------------------

void way_simple_destroy(wl_client* client, wl_resource* resource);

// -----------------------------------------------------------------------------

#define WAY_STUB(Name) \
    .Name = [](wl_client*, wl_resource* resource, auto...) { \
        log_error("TODO - {}{{{}}}::" #Name, wl_resource_get_interface(resource)->name, (void*)resource); \
    }
#define WAY_STUB_QUIET(Name) \
    .Name = [](auto...) {}

#define WAY_INTERFACE(Name) \
    const struct Name##_interface way_##Name##_impl

struct WayBindGlobalData
{
    wl_client* client;
    void*      data;
    u32        version;
    u32        id;
};

#define WAY_BIND_GLOBAL(Name, Data) \
    static void way_##Name##_bind_global_impl(const WayBindGlobalData& Data); \
           void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    { \
        way_##Name##_bind_global_impl({client, data, version, id}); \
    } \
    static void way_##Name##_bind_global_impl(const WayBindGlobalData& Data)

#define WAY_INTERFACE_DECLARE(Name, ...) \
    extern WAY_INTERFACE(Name) \
    __VA_OPT__(; \
        static_assert(std::same_as<decltype(__VA_ARGS__), int>); \
        constexpr u32 way_##Name##_version = __VA_ARGS__; \
        void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    )

// -----------------------------------------------------------------------------

auto way_resource_create(wl_client*, const wl_interface*, int version, int id, const void* impl, WayObject*, bool refcount) -> wl_resource*;

inline
auto way_resource_create(wl_client* client, const wl_interface* interface, wl_resource* parent, int id, const void* impl, WayObject* object, bool refcount) -> wl_resource*
{
    return way_resource_create(client, interface, wl_resource_get_version(parent), id, impl, object, refcount);
}

#define way_resource_create_unsafe(Name, Client, Version, IdOrResource, Object) \
    way_resource_create(Client, &Name##_interface, Version, IdOrResource, &way_##Name##_impl, Object, false)

#define way_resource_create_refcounted(Name, Client, Version, IdOrResource, Object) \
    way_resource_create(Client, &Name##_interface, Version, IdOrResource, &way_##Name##_impl, Object, true)
