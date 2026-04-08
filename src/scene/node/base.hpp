#include "../internal.hpp"

#define SCENE_NOISY_NODES 1

#if SCENE_NOISY_NODES
#define NODE_LOG(...) log_error(__VA_ARGS__)
#else
#define NODE_LOG(...)
#endif
