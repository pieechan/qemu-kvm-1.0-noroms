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
#ifndef QEMU_DSYSMON_H
#define QEMU_DSYSMON_H

#include "qdict.h"
#include "qemu-common.h"

int do_dsysmon(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_dsysmon_exit(Monitor *mon, const QDict *qdict, QObject **ret_data);

#endif /* QEMU_DSYSMON_H */
