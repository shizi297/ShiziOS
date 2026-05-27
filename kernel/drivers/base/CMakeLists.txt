# kernel/drivers/base 驱动框架

file(GLOB_RECURSE BASE_SOURCES "*.c")
add_library(base_objects OBJECT ${BASE_SOURCES})

set(ALL_DRIVER_OBJECTS ${ALL_DRIVER_OBJECTS} $<TARGET_OBJECTS:base_objects> CACHE INTERNAL "" FORCE)