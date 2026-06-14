# kernel/drivers/virtio VirtIO 传输层与核心层

file(GLOB_RECURSE VIRTIO_SOURCES "*.c")
add_library(virtio_objects OBJECT ${VIRTIO_SOURCES})
target_include_directories(virtio_objects PRIVATE .)

set(ALL_DRIVER_OBJECTS ${ALL_DRIVER_OBJECTS} $<TARGET_OBJECTS:virtio_objects> CACHE INTERNAL "" FORCE)