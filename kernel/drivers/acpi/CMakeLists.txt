# kernel/drivers/acpi ACPI驱动模块

file(GLOB_RECURSE ACPI_SOURCES "*.c")
add_library(acpi_objects OBJECT ${ACPI_SOURCES})

set(ALL_DRIVER_OBJECTS ${ALL_DRIVER_OBJECTS} $<TARGET_OBJECTS:acpi_objects> CACHE INTERNAL "" FORCE)