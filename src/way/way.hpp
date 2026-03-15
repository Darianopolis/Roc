#pragma once

#include "scene/scene.hpp"

struct way_server;

auto way_create(core::EventLoop*, gpu::Context*, scene_context*) -> core::Ref<way_server>;
