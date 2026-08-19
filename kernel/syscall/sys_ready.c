/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */
 
#include <task.h>

int sys_ready(void) {
    task_wakeup(current->father);
    return 0;
}