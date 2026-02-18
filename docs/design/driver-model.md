Driver Model Design

This document describes the driver model design for our operating system. This model aims to decouple the kernel from drivers, enabling drivers to maintain binary compatibility across kernel version updates (including interface refactoring) while retaining high runtime efficiency.

1. Design Overview

Our driver model is based on the following core ideas:

· The kernel maintains a function table that contains pointers to all kernel functions available for drivers to call.
· The kernel provides a function pointer at a fixed virtual address, through which drivers can obtain a copy of the function table for a specified version.
· During initialization, the driver copies the function table into local storage; all subsequent kernel calls are made indirectly through this local table, thereby decoupling from the kernel's concrete implementation addresses.
· A versioning mechanism supports coexistence of multiple function table versions, allowing older drivers to run on newer kernels without recompilation.

2. Function Table Structure and Version Management

2.1 Function Table Structure

The function table for each version is a struct whose layout is defined in a public header. All versioned function table structs begin with a size field, indicating the number of function pointers in the table. Subsequent fields are the specific function pointers in a fixed order.

For example:

```c
struct ftable_v1 {
    uint64_t size;          // number of functions
    int (*printk)(const char *fmt, ...);
    void *(*kmalloc)(size_t size);
    // ... other function pointers
};
```

When the kernel adds new functionality without modifying existing interfaces, the function table version number remains unchanged, and new function pointers are simply appended at the end of the struct. Old drivers, which only copy the table size corresponding to their version (as known from the size field), will not perceive the newly added functions and can still operate normally.

2.2 Kernel Maintains Multiple Version Tables

During initialization, the kernel constructs a static function table struct for each supported version (e.g., ftable_v1, ftable_v2, etc.). These structs reside in the kernel data section and are pre-filled with the corresponding function addresses.

The kernel maintains a global pointer array static const void *ftables[MAX_VERSION], where the array index is the version number. Each non‑null element points to the static function table of that version. For example:

```c
static const struct ftable_v1 ftable_v1 = { ... };
static const struct ftable_v2 ftable_v2 = { ... };
static const void *ftables[] = {
    [1] = &ftable_v1,
    [2] = &ftable_v2,
    // ... higher versions
};
```

Version numbering rules:

· Incrementing the major version indicates an incompatible interface change (e.g., removal or modification of an old function). The old version's table is retained for use by older drivers.
· Adding new functions without breaking existing ones does not change the major version.

3. Mechanism for Drivers to Obtain the Function Table

3.1 Function Table Acquisition at a Fixed Address

The kernel places a function pointer at a fixed virtual address (defined in kernel/arch/${ARCH}/include/mm_addr.h), which points to the kernel function void *get_ftable(int version). At runtime, drivers can call get_ftable directly through this address.

Function prototype:

```c
void *get_ftable(int version);
```

Behavior description:

· The driver passes the version number it expects (i.e., the major version it was compiled against).
· The kernel checks whether version is within the valid range and whether ftables[version] is non‑null. If invalid, it returns NULL.
· If the version is valid, the kernel copies the content of the static function table for that version onto the current kernel stack (creating a temporary stack‑local copy) and returns a pointer to this copy.

Note: The returned pointer points to data on the stack, which becomes invalid as soon as get_ftable returns. Therefore, the driver must immediately copy the entire table into its own local storage.

3.2 Driver Saves a Copy of the Function Table

During initialization (e.g., in its entry function), the driver performs the following:

```c
static struct ftable_v1 local_ft;   // local static storage matching the required version

void driver_init(void)
{
    struct ftable_v1 *p = (struct ftable_v1 *)get_ftable(1); // assuming version 1
    if (!p) {
        // version not supported, handle error
        return;
    }
    // copy the table from the stack to local storage
    memcpy(&local_ft, p, sizeof(local_ft));
    // now local_ft contains all usable function pointers

    // subsequent kernel calls go through local_ft
    local_ft.printk("Driver initialized\n");
}
```

The driver must ensure that the lifetime of the local storage matches that of the driver (e.g., using global or static variables within the module) so that it remains available for all subsequent runtime operations.

4. Driver Loading and Initialization

4.1 Loading Process

Drivers exist as kernel modules and contain an embedded digital signature. Loading is performed via a dedicated system call (e.g., sys_load_driver).

Loading steps:

1. The kernel verifies the driver's digital signature. The signature algorithm is planned to be ed25519, with the public key embedded in the kernel image.
2. Upon successful verification, the kernel maps the driver code into the kernel address space and sets appropriate page table permissions (executable, readable, writable, depending on requirements).
3. The driver gains the ability to run in kernel mode, using the kernel stack and sharing all memory addresses with the kernel.

4.2 Driver Initialization Steps

After the driver entry function is invoked, it performs the following:

1. Calls get_ftable to obtain a copy of the required version's function table and copies it to local storage (as described above).
2. Registers itself with the kernel via a registration interface (e.g., register_driver) provided in the local table. During registration, it supplies necessary information such as driver type, supported device IDs, callback function pointers, etc. These callback functions are implemented by the driver and are passed to the kernel through the registration call; the kernel will invoke them using the pointers provided.
3. Initialization completes, and the driver enters the operational state.

Note that the registration interface itself is a function pointer in the function table, so the driver calls it as local_ft.register_driver(...).

5. Calling Kernel Functions at Runtime

During normal operation, any kernel functionality needed by the driver is invoked through the locally saved function table. For example:

```c
local_ft.kmalloc(size, flags);
local_ft.printk("Some message\n");
```

Because the local table is a private copy for the driver, these calls do not depend on the actual location of the function table in the kernel nor involve any relocation operations. Each call incurs only one extra memory access (to fetch the function pointer from the local table), resulting in minimal overhead compared to direct calls.

6. Driver Loading at Boot Time

During system startup, the kernel itself does not search for or load drivers from the file system. This responsibility is delegated to a user‑mode initialization program (e.g., the init process). The user‑mode program can read configuration files, enumerate devices, and then load the necessary driver modules into the kernel via the driver‑loading system call. This design decouples driver management from the core kernel, keeping the kernel simple and focused.