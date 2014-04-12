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
/*
 * exception.h:
 *        definition of exception handling related data types, structures, and
 *        function prototypes.
 */
#ifndef _DSYSMON_EXCEPTION_H
#define _DSYSMON_EXCEPTION_H

#include <sys/types.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
typedef unsigned int cpuid_t;
typedef int bool_t;


/* 'regs_t' represent a saved processor context on the stack. */
typedef struct __attribute__((packed)) {
    u16 gs;        u16 _pad_gs;
    u16 fs;        u16 _pad_fs;
    u16 es;        u16 _pad_es;
    u16 ds;        u16 _pad_ds;
    u64 edi;
    u64 esi;
    u64 ebp;
    u64 _esp;
    u64 ebx;
    u64 edx;
    u64 ecx;
    u64 eax;
    u64 vector;
    u64 error_code;
    u64 eip;
    u16 cs;        u16 _pad_cs;
    u64 eflags;
    u64 esp;
    u16 ss;        u16 _pad_ss;
} regs_t;


/* exception handler functions defined in 'dxfeed.c'. */
extern void dxfeed_exception(regs_t *, void *arg);

extern int veidtcall_util_read_dxfeed(regs_t *, cpuid_t, char *, size_t);

#endif /* _DSYSMON_EXCEPTION_H */
