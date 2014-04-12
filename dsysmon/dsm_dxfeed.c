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
 * dsm_dxfeed.c:
 *        implementation of DXFEED direct hypervisor call.  this function is
 *        intended to be used by the special user-land process which run on
 *        the target guest OS kernel, to pass some information collected by the
 *        process to the observer entity bypassing potentially untrusted User
 *        Guset OS kernel.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "veidtdefs.h"
#include "dsm_api.h"
#include "exception.h"


/*
 * dsm_dxfeed_maxsize():
 *        return pre-defined maximum data buffer size in bytes which can be
 *        passed from user-land to the observer entity.
 */
size_t
dsm_dxfeed_maxsize(void)
{
    return VEIDT_DXFEED_MAXDATASIZE;
}


#if 0
/*
 * dsm_dxfeed_write():
 *        issue an DXFEED hypervisor call to pass specified data buffer, pointed
 *        by 'buf' and size 'len', to the observer entity.  it returnes zero
 *        if data buffer is passed successfully.  return some negative integer
 *        value if some error has been detected.
 */
int
dsm_dxfeed_write(char *buf, size_t len)
{
    const unsigned long magic = VEIDT_DXFEED_MAGIC;
    int retval;

    asm volatile (
                "int %b1"
                : "=a" (retval)
                : "Nd" (VEIDT_VECTOR_DXFEED),
                  "a" (magic),
                  "b" (buf),
                  "c" (len)
                : "memory", "cc");
    return retval;
}
#endif

/*
 * dsm_dxfeed_read():
 *        read out data chunks written by dsm_dxfeed_write() function by special
 *        user-land process on the User Guest OS.
 */
#define SYSFS_DXFEED_READ        "/sys/kernel/veidt/users/%d/dxfeed"

int
dsm_dxfeed_read(char *buf, size_t len)
{
#if 0
    const int cpuid = 1;                /* cpuid of our User Guest OS */
    char sysfs[128];
    int fd, retval;

    (void)snprintf(sysfs, sizeof(sysfs), SYSFS_DXFEED_READ, cpuid);

    if ((fd = open(sysfs, O_RDONLY)) < 0)
        return fd;

    retval = read(fd, buf, len);
    close(fd);

    return retval;
#else
    int retval = veidtcall_util_read_dxfeed(NULL, 1, buf, len);
    return retval;
#endif
}
