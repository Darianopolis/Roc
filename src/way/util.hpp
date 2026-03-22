#pragma once

#include <wayland-server-core.h>

#include "core/util.hpp"
#include "core/object.hpp"

// -----------------------------------------------------------------------------

template<typename T>
std::span<T> way_to_span(wl_array* array)
{
    usz count = array->size / sizeof(T);
    return std::span<T>(static_cast<T*>(array->data), count);
}

template<typename T>
wl_array way_to_wl_array(std::span<T> span)
{
    return wl_array {
        .size = span.size_bytes(),
        .alloc = span.size_bytes(),
        .data = const_cast<void*>(static_cast<const void*>(span.data())),
    };
}

// -----------------------------------------------------------------------------

struct way_object
{
    virtual ~way_object() = default;
};

template<typename T>
T* way_get_userdata(void* data)
{
    auto* base = static_cast<way_object*>(data);
    if (!base) return nullptr;
    auto* derived = dynamic_cast<T*>(base);
    if (!derived) {
        log_error("way_get_userdata<{}> failed, got {}", typeid(T).name(), typeid(*base).name());
        core_debugkill();
    }
    return derived;
}

template<typename T>
T* way_get_userdata(wl_resource* resource)
{
    return resource ? way_get_userdata<T>(wl_resource_get_user_data(resource)) : nullptr;
}

// -----------------------------------------------------------------------------

class way_resource
{
    wl_resource* resource = {};
    wl_listener destroy_listener {
        .notify = on_destroy,
    };

public:
    way_resource()
    {
        wl_list_init(&destroy_listener.link);
    }

    way_resource(wl_resource* resource)
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

    way_resource& operator=(wl_resource* other)
    {
        reset(other);
        return *this;
    }

    static void on_destroy(wl_listener* listener, void* data)
    {
        way_resource* self = wl_container_of(listener, self, destroy_listener);
        self->resource = nullptr;
        wl_list_init(&self->destroy_listener.link);

        // log_debug("core_resource<{}>: resource destroyed, clearing..", (void*)self);
    }

    ~way_resource()
    {
        wl_list_remove(&destroy_listener.link);
    }

    CORE_DELETE_COPY_MOVE(way_resource)

    operator wl_resource*() const { return resource; }
};

// -----------------------------------------------------------------------------

class way_resource_list
{
    struct list_node
    {
        wl_resource* resource = nullptr;
        wl_listener destroy_listener {
            .notify = on_destroy,
        };
        list_node* prev = nullptr;
        list_node* next = nullptr;

        list_node()
        {
            wl_list_init(&destroy_listener.link);
        }

        list_node(wl_resource* resource)
            : resource(resource)
        {
            wl_resource_add_destroy_listener(resource, &destroy_listener);
        }

        static void on_destroy(wl_listener* listener, void* data)
        {
            list_node* self = wl_container_of(listener, self, destroy_listener);

            // log_debug("cleaning up list_node: {}", (void*)self);
            wl_list_init(&self->destroy_listener.link);

            if (self->prev) self->prev->next = self->next;
            if (self->next) self->next->prev = self->prev;

            delete self;
        }

        ~list_node()
        {
            // log_debug("List node destroyed: {}", (void*)this);

            wl_list_remove(&destroy_listener.link);
        }

        CORE_DELETE_COPY_MOVE(list_node)
    };

    list_node root;

    struct iterator
    {
        const list_node* current;

        iterator& operator++()
        {
            current = current->next;
            return *this;
        }

        bool operator==(const iterator& other) const
        {
            return current == other.current;
        }

        wl_resource* operator*() const
        {
            return current->resource;
        }
    };

public:
    way_resource_list()
    {
        root.next = &root;
        root.prev = &root;
    }

    void emplace_back(wl_resource* resource)
    {
        if (!resource) return;

        auto* node = new list_node{resource};

        node->next = &root;
        node->prev = root.prev;

        root.prev->next = node;
        root.prev = node;
    }

    void clear()
    {
        list_node* next = nullptr;
        for (auto* node = root.next; node != &root; node = next) {
            next = node->next;
            delete node;
        }
        root.next = &root;
        root.prev = &root;
    }

    void take_and_append_all(way_resource_list&& other)
    {
        if (other.root.next == &other.root) return;

        other.root.next->prev = root.prev;
        other.root.prev->next = &root;

        root.prev->next = other.root.next;
        root.prev       = other.root.prev;

        other.root.next = &other.root;
        other.root.prev = &other.root;
    }

    wl_resource* front() const
    {
        return root.next ? root.next->resource : nullptr;
    }

    iterator begin() const
    {
        return iterator{root.next};
    }

    iterator end() const
    {
        return iterator{&root};
    }

    ~way_resource_list()
    {
        clear();
    }

    CORE_DELETE_COPY_MOVE(way_resource_list)
};

// -----------------------------------------------------------------------------

struct way_listener
{
    void* data;
    wl_listener listener;

    way_listener() = default;

    CORE_DELETE_COPY_MOVE(way_listener);

    ~way_listener()
    {
        wl_list_remove(&listener.link);
    }

    template<typename T>
    T* get() const
    {
        return way_get_userdata<T>(data);
    }

    static
    way_listener& from(wl_listener* listener)
    {
        way_listener* way_listener = wl_container_of(listener, way_listener, listener);
        return *way_listener;
    }
};

template<typename T>
T* way_get_userdata(wl_listener* listener)
{
    return way_listener::from(listener).get<T>();
}
