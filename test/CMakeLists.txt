
add_executable(ipc-server)
target_sources(ipc-server PRIVATE ipc/server.cpp)
target_link_libraries(ipc-server PUBLIC ${LIBNAME})

add_executable(ipc-client)
target_sources(ipc-client PRIVATE ipc/client.cpp)
target_link_libraries(ipc-client PUBLIC ${LIBNAME})
