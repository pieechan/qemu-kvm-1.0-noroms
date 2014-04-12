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
 * dsm_lib.c:
 *        implementation of DSM API functions defined in "dsm_api.h".  most API
 *        functions are implemented to contact with DSM virtio backend module.
 *        interface between DSM APIs and virtio backend module will be defined
 *        in "dsm_virtio.h".
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/virtio_net.h>

//#include "config.h"
#include "qemu-common.h"
#include "kvm.h"
#include "dsm_api.h"
#include "dsm_virtio.h"
#include "lms_defs.h"

#define INIT_LEVEL4_PGT    0xffffffff81c05000UL

uint64_t init_level4_pgt = (INIT_LEVEL4_PGT & 0x000000007fffffffUL);

extern void vcon_injection(const char *str, const int cnt);

/*
 * DSM device I/O observation hook APIs.
 *
 * I/O hook functions are recorded into dsm_iohr structure and linked into
 * dsm_iohr[] array.
 */
dsm_iohr_t *dsm_iohr[DIDX_LAST];

/*
 * dsm_register_io_hook():
 *        register device I/O hook function for console, network, and block
 *        devices.  device type, I/O direction, and instance index should be
 *        specified as arguments of this function.
 */
int
dsm_register_io_hook(dsm_dev_t dt, dsm_io_t it, int idx, void *hook)
{
    const dsm_didx_t didx = DIDX_IDX(dt, it);
    dsm_iohr_t *new_iohr;

    if (DIDX_LAST <= didx) {
        /*
         * DIDX index out of range.  wrong device type or I/O type might be
         * specified.
         */
        verbose("dsm_register_io_hook: invalid device and/or I/O type\n");
        return -1;
    }
    if (hook == NULL) {
        /* we can't allow NULL hook function. */
        verbose("dsm_register_io_hook: invalid hook function\n");
        return -1;
    }

    /* allocate new iohr record and fill it with the parameter. */
    new_iohr = malloc(sizeof(dsm_iohr_t));
    if (new_iohr == NULL) {
        verbose("dsm_register_io_hook: insufficient memroy\n");
        return -1;
    }
    new_iohr->next = NULL;
    new_iohr->prev = NULL;
    new_iohr->index = idx;
    new_iohr->hook = hook;

    /* insert new record at the tail of the list. */
    if (dsm_iohr[didx] == NULL) {
        dsm_iohr[didx] = new_iohr;
    }
    else {
        dsm_iohr_t *iohr = dsm_iohr[didx];
        while (iohr->next != NULL) {
            iohr = iohr->next;
        }
        iohr->next = new_iohr;
        new_iohr->prev = iohr;
    }
    return 0;
}

/*
 * dsm_unregister_io_hook():
 *        unregister device I/O hook function specified by device type, I/O
 *        direction, and instance index number.
 */
void
dsm_unregister_io_hook(dsm_dev_t dt, dsm_io_t it, int idx)
{
    const dsm_didx_t didx = DIDX_IDX(dt, it);
    dsm_iohr_t *iohr;

    if (DIDX_LAST <= didx) {
        /*
         * DIDX index out of range.  wrong device type or I/O type might be
         * specified.
         */
        verbose("dsm_unregister_io_hook: invalid device and/or I/O type\n");
        return;
    }

    for (iohr = dsm_iohr[didx]; iohr != NULL; iohr = iohr->next) {
        if (iohr->index == idx) {
            if (iohr->next != NULL)
                iohr->next->prev = iohr->prev;
            if (iohr->prev != NULL)
                iohr->prev->next = iohr->next;
            if (iohr == dsm_iohr[didx])
                dsm_iohr[didx] = iohr->next;

            iohr->next = NULL;
            iohr->prev = NULL;
            free(iohr);
            return;
        }
    }
    return;
}


/*
 * DSM device I/O injection APIs.
 *
 * information required to implement device I/O injection function are held
 * into dsm_ioir structure defined in "dsm_virtio.h".  an array of pointer
 * to the dsm_ioir structure, dsm_ioir[] is used to record all combination of
 * device type, I/O direction, and instance index.
 */
dsm_ioir_t *dsm_ioir[DIDX_LAST];

/*
 * here we defined some internal functions which might be called from virtio
 * backend module.  the interface used by virtio backend module for device I/O
 * injection have been implemented as convenient macro, which construct a DIDX
 * value from passed arguments, then call appropriate functions defined here.
 *
 * dsm__ioij_lookup() is a support function to find out correct DSM injection
 * record from dsm_ioir[] lists.  either 'index' or 'tgtfd' can be specified
 * as search key.
 *
 * dsm__ioij_setup() is an real function for IOIJ_SETUP() interface.  it will
 * create an injection pipe and record it into corresponding dsm_ioir[] list.
 *
 * dsm__ioij_wait() is an real function for IOIJ_WAIT() interface.  additional
 * argument of file descriptor, which will be added to the FD set of select()
 * system-call together with the reader end of the pipe, should be specified.
 * this function returns non-zero if injection pipe can be read.  otherwise,
 * it returns zero to indicate additional FD can be read.
 *
 * dsm__ioij_readv() is an real function for IOIJ_READV() interface.  this
 * function call readv() on the reader end of injection pipe to get injected
 * data.
 */
dsm_ioir_t *
dsm__ioij_lookup(dsm_didx_t didx, int index, int tgtfd)
{
    dsm_ioir_t *ioir;

    for (ioir = dsm_ioir[didx]; ioir != NULL; ioir = ioir->next) {
        if (index >= 0 && ioir->index == index) {
            /* found, index match */
            return ioir;
        }
        if (tgtfd >= 0 && ioir->tgtfd == tgtfd) {
            /* found, target FD match */
            return ioir;
        }
    }
    return NULL;
}

void
dsm__ioij_setup(dsm_didx_t didx, int index, int tgtfd, void *vcon)
{
    dsm_ioir_t *new_ioir;

    new_ioir = malloc(sizeof(dsm_ioir_t));
    if (new_ioir == NULL) {
        perror("dsm_io_injection_setup: memory allocation");
        exit(3);
    }
    new_ioir->next = NULL;
    new_ioir->prev = NULL;
    new_ioir->valid = false;
    new_ioir->index = index;
    new_ioir->tgtfd = tgtfd;

    if (pipe(new_ioir->fds) < 0) {
        perror("dsm_io_injection_setup: creating pipe");
        exit(3);
    }

    /* insert newly allocated record into list. */
    if (dsm_ioir[didx] == NULL) {
        dsm_ioir[didx] = new_ioir;
    }
    else {
        dsm_ioir_t *ioir = dsm_ioir[didx];
        while (ioir->next != NULL) {
            ioir = ioir->next;
        }
        ioir->next = new_ioir;
        new_ioir->prev = ioir;
    }
    new_ioir->valid = true;
    new_ioir->vcon = vcon;
    return;
}

bool
dsm__ioij_wait(dsm_didx_t didx, int index, int dev_fd)
{
    dsm_ioir_t *ioir;
    int pipe_fd, nfds, ret;
    fd_set fdset;

    ioir = dsm__ioij_lookup(didx, index, -1);
    if (ioir == NULL) {
        /*
         * there is no appropriate record found in the list.  in this case we
         * considered so that injection pipe is not ready for reading.
         */
        return false;
    }
    pipe_fd = ioir->fds[0];

retry:
    /*
     * wait for incoming data either specified dev_fd or reader end of the
     * injection pipe.
     */
    FD_ZERO(&fdset);
    FD_SET(dev_fd, &fdset);
    FD_SET(pipe_fd, &fdset);
    nfds = (dev_fd > pipe_fd) ? dev_fd : pipe_fd;
    ret = select(nfds + 1, &fdset, NULL, NULL, NULL);
    if (ret <= 0) {
        /* something is wrong... */
        fprintf(stderr, "dsm__ioij_wait: select returns %d", ret);
        goto retry;
    }

    /* return true if injection pipe can be read, otherwise return false. */
    return FD_ISSET(pipe_fd, &fdset) ? true : false;
}

ssize_t
dsm__ioij_readv(dsm_didx_t didx, int index, struct iovec iov[], int num_iov)
{
    dsm_ioir_t *ioir;
    int pipe_fd, ret;

    ioir = dsm__ioij_lookup(didx, index, -1);
    if (ioir == NULL) {
        /*
         * there is no appropriate record found in the list.  in this case we
         * considered so that no injection data is ready for reading.
         */
        return 0;
    }
    pipe_fd = ioir->fds[0];

    ret = readv(pipe_fd, iov, num_iov);
    return ret;
}

/*
 * __io_injection_console() and __io_injection_tunnet() are the sub-functions
 * called from dsm_io_injection() API function to handle device type specific
 * data injection process.
 *
 * note that for the tunnet device, we must adjust caller supplied iovec[] to
 * be prepended by an extra element, which holds VNET_HDR header information.
 * empirically, VNET_HDR can be all zero if no packet offloading are required.
 */
static int
__io_injection_console(dsm_ioir_t *ioir, struct iovec iov[], int num_iov)
{
    //const int pipe_fd = ioir->fds[1];
    int ret;

    /*
     * for the console device, simply write all data chunks passed from the
     * caller.
     */
    //ret = writev(pipe_fd, iov, num_iov);
    vcon_injection(iov[0].iov_base, iov[0].iov_len);
    ret = iov[0].iov_len;
    return ret;
}

static int
__io_injection_tunnet(dsm_ioir_t *ioir, struct iovec __iov[], int __num_iov)
{
    const int pipe_fd = ioir->fds[1];
    const int num_iov = (__num_iov + 1);
    struct iovec iov[num_iov];
    struct virtio_net_hdr vnet_hdr;
    int idx, ret;

    /*
     * for the virtio-net, 1st element of iovec[] returned from TUN device is 
     * an extra header of type struct virtio_net_hder.  we would like to fake
     * the header and prepend it to the data chunks passed from the caller.
     */
    memset(&vnet_hdr, 0, sizeof(vnet_hdr));
    iov[0].iov_base = &vnet_hdr;
    iov[0].iov_len = sizeof(vnet_hdr);

    for (idx = 0; idx < __num_iov; idx++) {
        iov[idx + 1] = __iov[idx];
    }

    /*
     * now we inject the whole data with prepended faked header into writer
     * end of the pipe.
     */
    ret = writev(pipe_fd, iov, num_iov);

    /*
     * number of bytes written should be returned.  here we must subtract the
     * length of extra header prepended to get correct number of bytes to
     * be returned.
     */
    return (ret - sizeof(vnet_hdr));
}

/*
 * dsm_io_injection():
 *        DSM-API function to inject some data chunks into virtio devices.
 *        device type and I/O direction should be specified by dsm_dev_t
 *        and dsm_io_t pair.  injected data chunks should be specified as
 *        iovec form by iov[] and len arguments.
 */
int
dsm_io_injection(dsm_dev_t dt, dsm_io_t it, int index,
                                struct iovec iov[], size_t len)
{
    const dsm_didx_t didx = DIDX_IDX(dt, it);
    dsm_ioir_t *ioir;
    int xlen, iov_cnt, ret;

    if (DIDX_LAST <= didx) {
        /*
         * DIDX index out of range.  wrong device type or I/O type might be
         * specified.
         */
        return -1;
    }

    ioir = dsm__ioij_lookup(didx, index, -1);
    if (ioir == NULL) {
        /*
         * there is no appropriate injection record found in the list.  wrong
         * instance index number might be specified.
         */
        return -1;
    }

    switch (didx) {
    case DIDX_CNS_IN:
    case DIDX_NET_IN:
        if (! ioir->valid) {
            /*
             * pipe channel used for I/O injection is not yet initialized.
             * we can't inject passed data chunks anyway.
             */
            return -1;
        }
        break;

    default:
        /* unsupported combination of device type and I/O direction. */
        return -1;
    }

    /* figure out number of valid elements of iov[] array. */
    xlen = len;
    for (iov_cnt = 0; xlen > 0; iov_cnt++) {
        xlen -= iov[iov_cnt].iov_len;
    }

    /* now we would like to call device specific injection sub-function. */
    switch (didx) {
    case DIDX_CNS_IN:
        ret = __io_injection_console(ioir, iov, iov_cnt);
        break;

    case DIDX_NET_IN:
        ret = __io_injection_tunnet(ioir, iov, iov_cnt);
        break;

    default:
        /* should not happen. */
        return -1;
    }
    return ret;
}


/*
 * DSM User Guest OS memory contents observation APIs.
 *
 * following three parameters, describing physical memory location of User
 * Guest OS and mapped address of them into process address space, are defined
 * here and initialized by the virtio backend module.
 */
upaddr_t dsm_user_phys_base;
upaddr_t dsm_user_phys_size;
void *dsm_user_map_base;

/*
 * PAGE_OFFSET is the base address of kernel virtual address space.  typically
 * it is defined to 0xC0000000, first address of last 1GB of 32 bits address
 * space.
 *
 * LOWMEM_SIZE is maximum map size of physical low-memory region into kernel
 * virtual address space.  typically it is defined to 896MB (1GB - 128MB).
 */
//#if !defined(TARGET_X86_64)
//static const ukvaddr_t PAGE_OFFSET = 0xC0000000UL;
//static const ukvaddr_t LOWMEM_SIZE = (896 * 1024 * 1024);
//#else
static const ukvaddr_t PAGE_OFFSET = 0xffff800000000000UL;
static const ukvaddr_t LOWMEM_SIZE = 0x00007fffffffffffUL;
//#endif

void *cpu_physical_memory_map(u_int64_t addr, u_int64_t *plen, int is_write);
uint64_t cpu_get_phys_addr(uint64_t level4_pgt, uint64_t addr);

/*
 * dsm_map_ukva():
 *        get corresponding local process address of specified kernel virtul
 *        address of User Guest OS.
 */
void *
dsm_map_ukva(ukvaddr_t ukva)
{
#if 0
    const ukvaddr_t adj = ukva - PAGE_OFFSET - dsm_user_phys_base;
    return (void *)(dsm_user_map_base + adj);
#elif 0
    u_int64_t len = 4096;
    const u_int64_t adj = ukva - PAGE_OFFSET;
    void *p = (void *)cpu_physical_memory_map(adj, &len, 0);
    return p;
#else
    u_int64_t len = 4096;
    u_int64_t paddr = cpu_get_phys_addr(init_level4_pgt, ukva);
    u_int64_t vaddr = (u_int64_t)cpu_physical_memory_map(paddr, &len, 0);
    void *p = (void *)vaddr;
    return p;
#endif
}

/*
 * dsm_map_upa():
 *        get corresponding local process address of specified physical address
 *        of User Guest OS context.
 */
void *
dsm_map_upa(upaddr_t upa)
{
    return (void *)(dsm_user_map_base - dsm_user_phys_base);
}

/*
 * dsm_check_ukva():
 *        do simple range check of specified kernel virtual address of User
 *        Guest OS.  it must be within the range started at PAGE_OFFSET, size
 *        LOWMEM_SIZE.
 */
bool
dsm_check_ukva(ukvaddr_t ukva)
{
    //return (PAGE_OFFSET <= ukva && ukva < PAGE_OFFSET + LOWMEM_SIZE);
    return true;
}

/*
 * dsm_check_upa():
 *        do simple range check of specified physical address in the User Guest
 *        OS context.  it must be within the range started at user_phys_base,
 *        size user_phys_size.
 */
bool
dsm_check_upa(upaddr_t upa)
{
    return (dsm_user_phys_base <= upa &&
                        upa < dsm_user_phys_base + dsm_user_phys_size);
}


/*
 * DSM User Guest OS kernel activity observation APIs.
 *
 * User Guest OS kernel activity can be read from dedicated SysFS node provided
 * by the kernel module of watcher OS.  SYSFS_KERNEL_ACTIVITY defines SysFS
 * path with CPUID part parameterized.  you should replace '%d' with the
 * correct CPUID value for observing User Guest OS.
 */
#define SYSFS_KERNEL_ACTIVITY        "/sys/kernel/veidt/users/%d/activity"

/*
 * currently 'activity' SysFS node shows following 4 counters in 64 bits
 * unsigned integer form as following.
 *
 *        <u64 upcalls> <u64 sysiret> <u64 kenter> <u64 kexit>
 *
 * where each u64 integer values means,
 *
 * upcalls:
 *        total number of upcalles invoked by VMM for the guest OS.
 *
 * sysiret:
 *        total number of SYSIRET hypercalls takne by the guest OS.
 *
 * kenter:
 *        number of upcalles which causes guest OS privilege state transition
 *        from user mode to kernel mode.
 *
 * kexit:
 *        number of SYSIRET hypercalls which causes guest OS privilege state
 *        transision from kernel mode to user mode.
 *
 * note that those 4 counter values will be atomically examined by the VMM
 * layer to prevent inconsistency.
 *
 * DV86-NOTE: at this point, we know that 'sysiret' is always greater than
 * 'upcalls' by some constant number (i.e. 564).  I don't know why this
 * inconsistency occures within the VMM.
 */
extern int dsm_upcalls;

int
dsm_get_kernel_activity(struct dsm_kernel_activity *act)
{
    u_int64_t upcalls;

    if (act == NULL)
        return -1;

    upcalls = dsm_upcalls;
    if (kvm_enabled()) {
        upcalls = 0;
    } else {
        upcalls = dsm_upcalls;
    }
    act->exception_count = upcalls;
    return 0;
}

