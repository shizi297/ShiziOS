# kernel/drivers/acpi ACPI驱动模块

file(GLOB_RECURSE ACPI_SOURCES "*.c")
add_library(acpi_objects OBJECT ${ACPI_SOURCES})
target_sources(kernel_drivers PRIVATE $<TARGET_OBJECTS:acpi_objects>)