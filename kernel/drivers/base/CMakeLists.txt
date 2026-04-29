# kernel/drivers/base 驱动框架

file(GLOB_RECURSE BASE_SOURCES "*.c")
add_library(base_objects OBJECT ${BASE_SOURCES})
target_sources(kernel_drivers PRIVATE $<TARGET_OBJECTS:base_objects>)