#include "../internal.hpp"

struct io_session
{
};

void io_session_init(io_context*)
{
    log_error("IO - session backend not implemented");
}
