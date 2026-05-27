# kernel/drivers/bus/pci PCI总线驱动

file(GLOB_RECURSE PCI_SOURCES "*.c")
add_library(pci_objects OBJECT ${PCI_SOURCES})
target_include_directories(pci_objects PRIVATE .)

set(ALL_DRIVER_OBJECTS ${ALL_DRIVER_OBJECTS} $<TARGET_OBJECTS:pci_objects> CACHE INTERNAL "" FORCE)