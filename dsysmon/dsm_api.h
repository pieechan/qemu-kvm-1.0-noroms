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
 * dsm_api.h:
 *        definition of D-System Monitor (DSM) application interface (API) and
 *        'dsysmon' application sub-module interface.
 */
#ifndef _DSM_API_H
#define _DSM_API_H

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>


/*
 * 'dsysmon' global verbose and debug flags and macros.
 *
 * you can use these flags and macros to debug your application. verbose flag
 * will be set if "--verbose" or "-v" option is specified, and debug flag will
 * be set if "--debug" or "-d" option is specified.
 */
extern bool verbose;
#define verbose(args...) \
        do { if (verbose) fprintf(stderr, args); } while(0)

extern bool debug;
#define debug(args...) \
        do { if (debug) fprintf(stderr, args); } while(0)


/*
 * 'dsysmon' application sub-module initialization and registration.
 *
 * all application sub-modules should provide its name, selector string,
 * initialization function, and usage function to the main module of 'dsysmon'
 * program.  it would be done by allocating 'dsm_application' record locally,
 * filling it with your desired values, then call 'dsm_register_application()'
 * function.  'dsm_application' structure has following members.
 *
 * const char *name;
 *        descriptive name of the application, such as "RootkitLibra" or
 *        "FoxyKBD".
 *
 * const char *selector;
 *        option string to select this application.  prepending two dashes "--"
 *        to the selector string will construct a selector option.  for example,
 *        if a string "lms" is specified as a selector, "--lms" will be the
 *        selector option.
 *
 * void (*init)(int argc, char *argv[]);
 *        application sub-module initialization function.  this function will be
 *        called from 'main()' function of 'dsysmon' program, when corresponding
 *        application selector option is specified.  argc/argv argument pair,
 *        which has same semantics with 'main()' function,  is passed to the
 *        'init()' function allowing application specific option parsing and
 *        processing.  we note that 'init()' function must return after finishing
 *        its initialization task.  if you would like to have an independent
 *        running context for the application, creating application task thread
 *        (by dsm_create_task() API function) might be an easy way to do so.
 *
 * void (*usage)(void);
 *        application sub-module usage function.  this function will be called
 *        from generic usage function 'dsm_usage()' to show the description of
 *        any application specific options.
 */
struct dsm_application {
    const char *name;                           /* application name */
    const char selector[32];                    /* selector option string */
    void (*init)(int argc, char *argv[]);       /* initialization function */
    void (*usage)(void);                        /* usage function */
    void (*terminate)(void);                    /* termination function */
};

/*
 * 'dsm_register_application() function should be used to register application
 * sub-module, specified by passed 'dsm_application' record.  because 'init()'
 * function of the application will called from 'main()' function of 'dsysmon'
 * program, application sub-module registration should be done BEFORE 'main()'
 * function start to run.  this can be achieved by specifying 'constructr'
 * attribute to the sub-module start function, such like,
 *
 *        static void __attribute__ ((constructor)) my_init_module(void)
 *        {
 *                dsm_register_application(&my_application);
 *        }
 *
 * 'my_init_module()' function shown above has "contsructor" attribute so it
 * will be called BEFORE 'main()' function start to run.
 */
extern void dsm_register_application(struct dsm_application *);

/*
 * 'dsm_usage()' function is a generic usage function to show description about
 * acceptable options for all supported sub-modules, including 'dsysmon' main
 * module and virtio sub-module.
 */
extern void dsm_usage(void);


/*
 * 'dsysmon' application sub-module private task creation.
 *
 * DSM application sub-module offen requires its own running context in the
 * 'dsysmon' program.  this can be achieved by creating application specific
 * thread and drive its own main loop task.  DSM-API will support this kind
 * of work by providing convenient function to create application thread on
 * demand.
 *
 * application task function is represented by dsm_task_func_t type, which takes an
 * argument of void pointer.  those task function prepared in the application
 * sub-module can be spawn by calling dsm_create_task() API function with
 * extra argument which will be passed to the task function.
 */
typedef int (*dsm_task_func_t)(void *arg);
typedef struct {
    dsm_task_func_t func;
    pthread_t thread;
    int rc;
} dsm_task_t;
extern void dsm_create_task(dsm_task_t *task, void *arg);
extern void dsm_cancel_task(dsm_task_t *task);


/*
 * 'dsysmon' cooperation mode vs. standalone mode.
 *
 * 'dsysmon' has an ability to cooperate with external contoller program such
 * as GUI panel.  in this case, all notification message generated by the
 * application sub-modules must be sent to the external channel instead of
 * standard output of 'dsysmon' program.
 *
 * a boolean global variable 'cooperation' will be used to indicate whether
 * the 'dsysmon' is currently in the cooperation mode or the standalone mode.
 * initial value of 'cooperation' variable might be determined by the command
 * line option "--cooperation".
 */
extern bool cooperation;

/*
 * in the case of cooperation mode, a bi-directional external channel for every
 * activated application sub-modules will be created automatically.  a pair of
 * file descriptor (one for control input and another for message output),
 * which represent an end-point of bi-directional stream pipe, will be given
 * to the application sub-module by calling 'dsm_get_coop_channel()' API
 * function.
 */
extern int dsm_get_coop_channel(const char *selector, int *ctl, int *msg);


/*
 * 'dsysmon' start-up time and elaplsed time functions.
 *
 * 'dsysmon' main module will record its start-up time into global variable
 * 'dsm_start_time' of type struct timeval.  application sub-modules can
 * examine it for any wall-time related usage.
 *
 * also the 'dsm_elapsed_ms()' API function can be used to get elapsed
 * time since 'dsysmon' started in mili-seconds.
 */
extern struct timeval dsm_start_time;
extern unsigned long dsm_elapsed_ms(void);


/*
 * device type of DSM is described by an enumeration dsm_dev_t.  currently,
 * console, network, and block devices are supported.
 */
typedef enum dsm_dev_t {
    DSM_DEV_CONSOLE     = 0,                /* console device */
    DSM_DEV_NETWORK     = 1,                /* network device */
    DSM_DEV_BLOCK       = 2,                /* block (disk) device */
} dsm_dev_t;

/*
 * device I/O direction is described by an enumeration dsm_io_t.  INPUT means
 * transfering data from virtio backend to User Guest OS.  in the opposite,
 * OUTPUT means transfering data from User Guest OS to virtio backend.
 */
typedef enum dsm_io_t {
    DSM_IO_INPUT        = 0,                /* input: read in by user OS */
    DSM_IO_OUTPUT       = 1,                /* output: write out from user OS */
} dsm_io_t;


/*
 * DSM device I/O observation hook APIs.
 *
 * we can register/unregister device I/O hook (call-back) functions to sneak
 * I/O data transfered on certain devices.  observed device can be specified
 * by combination of device type (dsm_dev_t) and I/O direction (dsm_io_t) as
 * well as the instance index number within same device type.
 *
 * for example, if 'dsysmon' is configured to provide two network channels to
 * the Guest OS, first channel will be named instance 0 and second channel
 * will be named instance 1, according to the order of the options specified.
 * if you would like to observe only first network channel, you can do it by
 * specifying instance index '0' at the registration of hook function.
 *
 * on the other hand, instance index '-1' will be considered as wildcard.  if
 * you specify instance index '-1' when registering your hook function, any
 * instance of same device type and I/O direction will match and the hook
 * function will be called with actual instance index value in 'index'
 * argument.
 *
 * I/O hook function should be defined in accordance with the definition of
 * type dsm_io_hook_t.  first and second arguments passed to the hook function
 * are device type and I/O direction, third argument is the instance number
 * within the same device type, followed by some additonal arguments vary
 * across the device type.
 *
 * CONSOLE device:
 *  int hook(dsm_dev_t, dsm_io_t, int index, struct iovec iov[], size_t len)
 *
 * NETWORK device:
 *  int hook(dsm_dev_t, dsm_io_t, int index, struct iovec iov[], size_t len)
 *
 * BLOCK device:
 *  void hook(dsm_dev_t, dsm_io_t, int index, 
 *                                struct iovec iov[], size_t len, off64_t off)
 *
 * these functions are invoked with observed data buffer in 'iovec' style.
 * an array of 'iovec' structure and total transfer length in bytes are passed.
 * except block type device, hook function must return resulted total transfer
 * length in bytes to the caller.
 *
 * if you would like to do so, any data in the iovec buffer can be modified.
 * also if you decided to truncate or enlarge total length of transfered data,
 * you can do it by adjusting buffer size of iovec elements and return resulted
 * total byte length from the hook function.  however, original buffer size of
 * each iovec element is not known, enlarging data may corrupt the system.  be
 * careful.
 *
 * for the block type device, buffer length must be preserved and no truncation
 * nor enlargement are allowed.  returned value from block type hook function
 * will completely be ignored.
 *
 * any number of hook functions can be registered to a device channel.  in this
 * case, order of each registered functions called is not known.
 */
typedef int (*dsm_io_hook_t)(dsm_dev_t, dsm_io_t, int index, ...);

/* register/unregster device I/O ovservation hook. */
int dsm_register_io_hook(dsm_dev_t, dsm_io_t, int index, void *hook);
void dsm_unregister_io_hook(dsm_dev_t, dsm_io_t, int index);


/*
 * DSM device I/O injection APIs.
 *
 * we can inject some data chunks into device I/O stream by DSM-API function
 * dsm_io_injection().  on which device type, I/O direction, and instance you
 * would like to inject your data should be specified by dsm_dev_t, dsm_io_t,
 * and instance index number arguments.  however, data injection of output
 * direction (from User Guest OS to virtio backend) seems have no meaning so
 * only input direction is currently acceptable.  also we have no idea to
 * inject data into block device I/O stream because data transfer of block
 * device is completely synchronous and always initiated by the User Guest OS.
 *
 * as a result, only console and network device type with input direction will
 * be supported.  if unsupported dsm_dev_t and dsm_io_t combination will be
 * specified, dsm_io_injection() will indicate an error.
 *
 * data chunks you would like to inject should be passed as 'iovec' style with
 * total number of bytes to be transfer.
 */
int dsm_io_injection(dsm_dev_t, dsm_io_t, int index,
                                struct iovec iov[], size_t len);


/*
 * DSM User Guest OS memory contents observation APIs.
 *
 * often observer may want to access kernel virtual address space of User Guest
 * OS.  a set of data types and functions are defined in DSM to satisfy those
 * requirements.
 *
 * first, we have defined two data types.  'ukvaddr_t' can be used to represent
 * a kernel virtual address in the User Guest OS, and 'upaddr_t' can be used to
 * specify a physical memory address in the User Guest OS context.
 */
//typedef u_int32_t ukvaddr_t;
//typedef u_int32_t upaddr_t;
typedef u_int64_t ukvaddr_t;
typedef u_int64_t upaddr_t;

/*
 * we provide basic address translation (mapping) functions, dsm_map_ukva()
 * and dsm_map_upa().  dsm_map_ukva() will convert kernel virtual address of
 * User Guest OS to the local process address on which the corresponding
 * physical meory is mapped.  dsm_map_upa() will convert physical address of
 * User Guest OS context to the local process address on which the
 * corresponding physical memory is mapped.
 *
 * additional functions, dsm_check_ukva() and dsm_check_upa() are provided for
 * the purpose that detect out of range condition of User Guest OS addresses
 * range.
 */
extern void *dsm_map_ukva(ukvaddr_t ukva);
extern void *dsm_map_upa(upaddr_t upa);
extern bool dsm_check_ukva(ukvaddr_t ukva);
extern bool dsm_check_upa(upaddr_t upa);


/*
 * DSM User Guest OS kernel activity observation APIs.
 *
 * offen application need to know the activity of User Guest OS kernel with
 * respect to the kernel mode transition.  dsm_kernel_activity structure and
 * dsm_get_kernel_activity() function can be used to know current state of
 * User Guest OS kernel.
 *
 * currently, number of exceptions occured in the Guest OS is defined by
 * dsm_kernel_activity structure.
 *
 * the API function dsm_get_kernel_activity() returns -1 if it is failed to
 * get kernel activity of the User Guest OS.  otherwise, it returns 0.
 */
struct dsm_kernel_activity {
    u_int64_t exception_count;                /* total exception count */
};
extern int dsm_get_kernel_activity(struct dsm_kernel_activity *);


/*
 * DSM direct cross-feed (DXFEED) data channel APIs.
 *
 * we have direct data channel between special user-land process run on the
 * User Guest OS and observer process run on the Watcher Guest OS, bypassing
 * potentially untrusted User Guset OS kernel interference.  we call this
 * special channel "dxfeed", stands for direct cross feed channel.
 *
 * dxfeed channel is designed so that a data chunk wirtten is guaranteed to
 * be read atomically by the partner, like a packet interface.  however,
 * maximum byte size of a data chunk is limieted.  any data chunk larger than
 * this limit can not be handled by the dxfeed APIs.  you can inspect this
 * size limit by calling 'dsm_dxfeed_maxsize()' API function.
 *
 * simple writer side and reader side API functions are defined by DSM-API,
 * 'dsm_dxfeed_write()' and 'dsm_dxfeed_read()' respectively.
 *
 * 'dsm_dxfeed_write()' function can be used to write 'len' bytes of data chunk
 * pointed by 'buf' to the dxfeed channel.  the function may block if it is
 * considered that the channel is full.
 *
 * 'dsm_dxfeed_read()' function can be used to read at most 'len' bytes of data
 * chunk from the dxfeed channel, into local buffer pointed by 'buf'.  if there
 * is no data chunk queued in the channel, the function may block until at
 * least one data chunk will be written by the partner.
 *
 * since we have implemented atomic read semantics, if specified buffer size
 * is small than the size of current data chunk, an error will be indicated
 * without removing any data from the channel.  caller should then specify
 * sufficient amount of data buffer to receive entier data chunk.
 *
 * we strongly note that 'dsm_dxfeed_write()', the writer side API function,
 * is dedicated for the special user-land process running on the User Guest
 * OS, not for the application sub-module in the 'dsysmon' process, which run
 * on the Watcher Guset OS.
 */
extern size_t dsm_dxfeed_maxsize(void);
extern int dsm_dxfeed_write(char *buf, size_t len);
extern int dsm_dxfeed_read(char *buf, size_t len);

#endif /* _DSM_API_H */
