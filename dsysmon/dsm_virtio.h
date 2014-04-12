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
 * dsm_virtio.h:
 *        internal definition of virtio backend related macros and data types.
 */
#ifndef _DSM_VIRTIO_H
#define _DSM_VIRITO_H


/*
 * for the internal of DSM device I/O hook and data injection, we construct an
 * index value from device type and I/O direction specifiers.  this composite
 * value is represented as enumeration of dsm_didx_t.
 */
typedef enum dsm_dev_io_index {
    DIDX_CNS_IN                = ((DSM_DEV_CONSOLE << 1) | (DSM_IO_INPUT  << 0)),
    DIDX_CNS_OUT        = ((DSM_DEV_CONSOLE << 1) | (DSM_IO_OUTPUT << 0)),
    DIDX_NET_IN                = ((DSM_DEV_NETWORK << 1) | (DSM_IO_INPUT  << 0)),
    DIDX_NET_OUT        = ((DSM_DEV_NETWORK << 1) | (DSM_IO_OUTPUT << 0)),
    DIDX_BLK_IN                = ((DSM_DEV_BLOCK   << 1) | (DSM_IO_INPUT  << 0)),
    DIDX_BLK_OUT        = ((DSM_DEV_BLOCK   << 1) | (DSM_IO_OUTPUT << 0)),
    DIDX_LAST                = 8,
} dsm_didx_t;

#define DIDX_IDX(dev, dir)                (((dev) << 1) | ((dir) << 0))
#define DIDX_DEV(didx)                        (((didx) >> 1) & ((1 << 2) - 1))
#define DIDX_DIR(didx)                        (((didx) >> 0) & ((1 << 1) - 1))


/*
 * a registered device I/O hook will be recorded into dsm_iohr structure.
 * arbitrarily number of I/O hooks for certain device type and I/O direction
 * can be recorded by linked list of dsm_iohr structures.  an array of pointer
 * to the dsm_iohr structure, dsm_iohr[] will be used to hold lists of hooks
 * for all available combination of device types and I/O directions.
 *
 * note that 'index' member of dsm_iohr structure is used to specify the target
 * instance of same device type and I/O direction pair.  if '-1' is specified,
 * all existing instances of matched devices can be considered as the target.
 */
typedef struct dsm_io_hook_record {
    struct dsm_io_hook_record *next;                /* linked list of the record */
    struct dsm_io_hook_record *prev;                /* linked list of the record */
    int index;                                        /* target instance index */
    dsm_io_hook_t hook;                                /* hook function */
} dsm_iohr_t;
extern dsm_iohr_t *dsm_iohr[DIDX_LAST];

/*
 * two macros, IOHK_CHECK() and IOHK_CALL() are defined so that easy I/O hook
 * handling can be achieved.  IOHK_CHECK() will check existance of regitstered
 * hook function, and IOHK_CALL() will call specified hook functions with
 * appropriate arguments.
 *
 * note that device type and I/O direction arguments for these macros can be
 * specified without their prefix, DSM_DEV_ and DSM_IO_.
 */
#define __DEV(dev)                        (DSM_DEV_ ## dev)
#define __DIR(dir)                        (DSM_IO_ ## dir)

#define IOHK_CHECK(dev, dir)                \
        (dsm_iohr[DIDX_IDX(__DEV(dev), __DIR(dir))])

#define IOHK_CALL(dev, dir, idx, iov, len, args...)        \
        ({  dsm_iohr_t *_iohr = IOHK_CHECK(dev, dir); \
            int _len = (len); \
            while (_iohr != NULL) { \
                if (_iohr->index == -1 || _iohr->index == (idx)) { \
                    _len = _iohr->hook(__DEV(dev), __DIR(dir), \
                                    (idx), (iov), _len, ##args); \
                } \
                _iohr = _iohr->next; \
            } \
            _len; })


/*
 * information used by device I/O injection function are recorded into dsm_ioir
 * structure, and an array of pointer to the dsm_ioir structure, dsm_ioir[] is
 * alocated statically to hold all combination of device type, I/O direction,
 * and instance index number.
 */
typedef struct dsm_io_injection_record {
    struct dsm_io_injection_record *next;        /* linked list of the record */
    struct dsm_io_injection_record *prev;        /* linked list of the record */
    bool valid;                                        /* true if pipe is valid */
    int index;                                        /* target instance index */
    int tgtfd;                                        /* target FD */
    int fds[2];                                        /* injection pipe FDs */
    void *vcon;
} dsm_ioir_t;
extern dsm_ioir_t *dsm_ioir[DIDX_LAST];

/*
 * four macros, IOIJ_SETUP(), IOIJ_WAIT(), IOIJ_READV(), and IOIJ_READ_FD() are
 * defined so that easy I/O injection handling can be archieved.  first three
 * macros will call actual processing function with dsm_didx_t index value
 * constructed from arguments of the macro.  while IOIJ_READ_FD() is an another
 * type of macro to return file desciptor of reader end of the injection pipe.
 */
extern dsm_ioir_t *dsm__ioij_lookup(dsm_didx_t, int index, int tgtfd);
extern void dsm__ioij_setup(dsm_didx_t, int index, int tgtfd, void *vcon);
extern bool dsm__ioij_wait(dsm_didx_t, int index, int devfd);
extern ssize_t dsm__ioij_readv(dsm_didx_t, int index,
                                struct iovec iov[], int num_iov);


#define IOIJ_SETUP(dev, dir, idx, tfd, vcon)                \
        dsm__ioij_setup(DIDX_IDX(__DEV(dev), __DIR(dir)), (idx), (tfd), (vcon))

#define IOIJ_WAIT(dev, dir, idx, dfd)                \
        dsm__ioij_wait(DIDX_IDX(__DEV(dev), __DIR(dir)), (idx), (dfd))

#define IOIJ_READV(dev, dir, idx, iov, niov)        \
        dsm__ioij_readv(DIDX_IDX(__DEV(dev), __DIR(dir)), (idx), (iov), (niov))

#define IOIJ_READ_FD(dev, dir, tfd)                \
        ({  dsm_didx_t _didx = DIDX_IDX(__DEV(dev), __DIR(dir)); \
            dsm_ioir_t *_ioir = dsm__ioij_lookup(_didx, -1, (tfd)); \
            _ioir ? _ioir->fds[0] : -1; })

#define IOIJ_CONSOLE_LOOKUP_VCON(dev, dir, idx, tfd)                \
        (dsm__ioij_lookup(DIDX_IDX(__DEV(dev), __DIR(dir)), (idx), (tfd)))->vcon

/*
 * for the internal of DSM memory observation functions,  we must know the
 * start address and size of User OS physical memory, and mapped base address
 * of it into 'dsysmon' process address space.  three global variables are
 * used to describe those memory parameters.
 *
 * dsm_user_phys_base:
 *        will hold a base physical address of User Guest OS.  initialized by
 *        the virtio backend module according to the value retrieved from VMM.
 *
 * dsm_user_phys_size:
 *        will hold a byte size of physical memory region used by User Guest OS.
 *        also initialized by the virtio backend module according to the value
 *        retrieved from VMM.
 *
 * dsm_user_map_base:
 *        will hold a base address in 'dsysmon' process on which physical memory
 *        of User Guest OS is mapped.  initialied by the virtio backend module.
 */
extern upaddr_t dsm_user_phys_base;
extern upaddr_t dsm_user_phys_size;
extern void *dsm_user_map_base;


/*
 * virtio backend module exported functions.
 *
 * you must call virtio_init() from main() with passed argc/argv arguments.
 * options can be recognized by virtio backend modlue are processed and
 * all required initiailization for devices are taken place in this function.
 *
 * after prforming any ather initialization tasks required for the application,
 * virtio_run() should be called to pass control to the virtio backend module.
 * note that virtio_run() never returned.
 *
 * virtio_usage() function print usage message for virtio backend module.
 */
extern void virtio_init(int argc, char *argv[]);
extern void virtio_run(void);
extern void virtio_usage(void);

#endif /* DSM_VIRTIO_H */
