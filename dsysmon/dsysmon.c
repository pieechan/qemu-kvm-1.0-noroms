/*
 * Copyright (c) 2013 JST DEOS R&D Center
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "qemu-common.h"
#include "dsysmon.h"
#include "monitor.h"
#include "buffered_file.h"
#include "sysemu.h"
#include "block.h"
#include "qemu_socket.h"
#include "block-migration.h"
#include "qemu-objects.h"
#include "lms_defs.h"
#include "dsm_main.h"

#ifdef DEBUG_DSYSMON
#define dprintf(fmt, ...) \
    do { printf("migration: " fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

extern uint64_t init_level4_pgt;

int do_dsysmon(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const uint64_t pgt = qdict_get_try_int(qdict, "level4_pgt", 0);
    const uint64_t task = qdict_get_try_int(qdict, "init_task", 0);
    const uint64_t runq = qdict_get_try_int(qdict, "runqueues", 0);
    const uint64_t offset = qdict_get_try_int(qdict, "per_cpu_offset", 0);
    const uint64_t ids = qdict_get_try_int(qdict, "nr_cpu_ids", 0);
    const char *argv[6];
    int argc;

    if (pgt != 0) {
        init_level4_pgt = (pgt & 0x000000007fffffffUL);
    }
    if (task != 0) {
        init_task_ukva = task;
    }
    if (runq != 0) {
        per_cpu_rq_ukva = runq;
    }
    if (offset != 0) {
        per_cpu_offset_ukva = offset;
    }
    if (ids != 0) {
       nr_cpu_ids_ukva = ids;
    }

    argv[0] = "--fkbd";
    argv[1] = "--netlog0";
    argv[2] = "--rtkl";
    argv[3] = "--lms";
    argv[4] = NULL;
    argc = 4;

    dxfeed_init();
    dsm_init(argc, argv);
    monitor_printf(mon, "start dsysmon init_level4_pgt = %p init_task = %p runqueues = %p per_cpu_offset = %p nr_cpu_ids = %p\n",
            (void*)init_level4_pgt, (void*)init_task_ukva, (void*)per_cpu_rq_ukva, (void*)per_cpu_offset_ukva, (void*)nr_cpu_ids_ukva);

    return 0;
}

int do_dsysmon_exit(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    dsm_terminate();

    return 0;
}
