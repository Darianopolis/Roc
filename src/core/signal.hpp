#pragma once

#include "containers.hpp"
#include "stack.hpp"

template<typename Signature>
struct ListenerState;

template<typename R, typename ...Args>
struct ListenerState<R(Args...)>
{
    Link<ListenerState> link;
    R(*notify)(void*, Args...);
};

template<typename Signature>
struct Listener
{
    Ref<ListenerState<Signature>> state;
};

template<typename Signature>
struct Signal;

template<typename R, typename ...Args>
struct Signal<R(Args...)>
{
    Link<ListenerState<R(Args...)>> listeners;

    template<typename... Args2>
    void operator()(Args2&& ...args)
    {
        ThreadStack stack;

        auto* stored = stack.get_head<Weak<ListenerState<R(Args...)>>>();
        usz count = 0;
        auto* link = listeners.next;
        while (link != &listeners) {
            stored[count++] = CONTAINER_OF(ListenerState<R(Args...)>, link, link);
            link = link->next;
        }
        stack.set_head(stored + count);

        for (usz i = 0; i < count; ++i) {
            if (Ref listener = stored[i].get()) {
                listener->notify(listener.get(), std::forward<Args2>(args)...);
            }
        }
    }

    void insert(ListenerState<R(Args...)>* listener)
    {
        listeners.insert_after(&listener->link);
    }

    template<typename Fn>
    auto listen(Fn&& fn) -> Listener<R(Args...)>
    {
        struct LambdaListener : ListenerState<R(Args...)>
        {
            Fn fn;

            LambdaListener(Fn&& fn)
                : fn(std::move(fn))
            {}
        };
        auto listener = ref_create<LambdaListener>(std::move(fn));
        listener->notify = [](void* data, Args... args) {
            static_cast<LambdaListener*>(data)->fn(args...);
        };
        insert(listener.get());
        return {listener};
    }
};
