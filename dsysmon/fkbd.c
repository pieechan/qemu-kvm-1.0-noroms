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
 * fkbd.c:
 *        Implementation of FoxyKBD (FKBD).
 *
 *         This file "fkbd.c" implements application behavior of FokxyKBD.
 *        Original implementation of FKBD hooks "keyUp" and "KeyDown" events,
 *        but we have no ways to hook those 'events' because our implementation
 *        is intended to work with Linux console device, i.e. only character
 *        based input/output can be allowed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/time.h>
#include "dsm_api.h"


#if !defined(__UNUSED)
#define __UNUSED __attribute__((unused))
#endif

#ifndef MIN
#define MIN(a, b)                ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)                ((a) > (b) ? (a) : (b))
#endif


/*
 * we would like to inject character string 'inj_string[]' periodically into
 * the console input.
 */
static const char inj_string[] = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM<>\n";

/*
 * a structure 'fkbd_inj' is defined to collect all related data of console
 * injection task.  an instance of 'fkbd_inj' is statically allocated and
 * used.
 *
 * we have two mode of injection, one-shot and multi-shot.  one-shot injection
 * is controlled by the command line option by specifying start/stop timing
 * of the injection.  multi-shot injection is controlled by the commands sent
 * through the external channel (fkbd_ext_ctl).
 *
 * in the case of both mode, some length of pre-defined string will be injected
 * into console periodically.  the interval of each injection is specified by
 * 'interval' member of 'fkbd_inj' structure.
 */
struct fkbd_inj {
    const char *string;                        /* pointer to injection string */
    unsigned long interval;                /* injection interval in mili-seconds */
    bool active;                        /* injection is activated or not */
    unsigned long last;                        /* last injection time */

    unsigned long start;                /* one-shot injection start timing */
    unsigned long stop;                        /* one-shot injection stop timing */
};
static struct fkbd_inj fkbd_inj = {
    .string = inj_string,
    .active = false,
    .interval = 300,                        /* default to 300ms interval */
};

/*
 * a structure 'fkbd_log' is defined for device I/O transfer log output.
 * upmost 4 instances each for network and disk device will be statically
 * allocated and used.
 *
 * we adopt coalescing feature for the log output.  transfer bytes will be
 * accumulated into 'bytes' member of the structure by I/O hook function and
 * lator written into log file (via. 'fp_file' member) and/or sent to external
 * message channel (vid. 'fp_fifo' member) by the log task thread.
 *
 * coalescing timeout is described by 'cl_timeout' member.  first transfer on
 * the device will kick the log task thread to enter coalescing timeout, then
 * after expiration of the timeout the thread will attempt to output the log
 * message into specified destination(s).
 */
struct fkbd_log {
    sem_t fl_lock;                        /* fkbd_log structure access lock */

    dsm_dev_t type;                        /* device type, network or block */
    unsigned int index;                        /* device index, base 0 */
    char ident[32];                        /* device identification string */

    unsigned long bytes;                /* accumulator of trasnfer bytes */
    unsigned long timestamp;                /* timestamp of the transfer */

    FILE *fp_file;                        /* log file to be written into */
    FILE *fp_fifo;                        /* external channel to be sent to */

    sem_t cl_wchan;                        /* wait channel of coalecsing */
    unsigned long cl_timeout;                /* coalescing timer timeout in ms */
    enum {
        CL_IDLE                = 0,                /* coalescing timer is not started */
        CL_PROGRESS        = 1,                /* coalescing timer is in progress */
    } cl_state;
};
static struct fkbd_log fkbd_netlog[4];
static struct fkbd_log fkbd_hddlog[4];

extern void fkbd_init(int argc, char** argv);
extern void fkbd_usage (void);
extern void fkbd_terminate (void);

/*
 * we have control input channel and message output channel if cooperation
 * mode is enabled.  message output channel, stored in 'fkbd_ext_msg' may be
 * used as log output of both network and block devices.
 *
 * 'fkbd_ext_lock' will be used to control atomic access on the external
 * channel.
 */
static FILE *fkbd_ext_ctl = NULL;
static FILE *fkbd_ext_msg = NULL;
sem_t fkbd_ext_lock;


/*
 * fkbd_simple_auto_injection() injects specified character string onto the
 * console device.
 */
static void
fkbd_simple_auto_injection(void)
{
    /*
     * note that calling strlen() for the constant string might be computed at
     * the compilation time and there might be no runtime penalty.
     */
    const int len = strlen(fkbd_inj.string);
    struct iovec iov[1];

    iov[0].iov_base = (void *)fkbd_inj.string;
    iov[0].iov_len = len;
    dsm_io_injection(DSM_DEV_CONSOLE, DSM_IO_INPUT, 0, iov, len);
    return;
}

static void
fkbd_process_ext_ctl(struct fkbd_inj * const fi)
{
    char linebuf[256];
    unsigned long val;

    if (fgets(linebuf, sizeof(linebuf), fkbd_ext_ctl) == NULL) {
        /* there is no message to be read from control channel. */
        return;
    }

    /*
     * currently we have three controll messages.
     *
     * "start"                immediately start injection.
     * "stop"                immediately stop injection.
     * "interval %d"        set interval of injection to specified value.
     *
     * note that calling strlen() for the constant literal string might be
     * computed at the compilation time and there might be no runtime penalty.
     */
    if (strncmp(linebuf, "start", strlen("start")) == 0) {
        verbose("FKBD: start injection.\n");
        fi->active = true;
        fi->last = 0;
    }
    else
    if (strncmp(linebuf, "stop", strlen("stop")) == 0) {
        verbose("FKBD: stop injection.\n");
        fi->active = false;
        fi->last = 0;
    }
    else
    if (sscanf(linebuf, "interval %li", &val) == 1) {
        verbose("FKBD: interval changed to %lums.\n", val);
        fi->interval = val;
    }
    return;
}

static int
fkbd_wait_for_something(struct fkbd_inj * const fi)
{
    unsigned long cur;
    struct timeval tmout, *tvp;
    fd_set fdset;
    int nfds;

    nfds = 0;
    FD_ZERO(&fdset);

    /*
     * if the external control channel is enabled, file descriptor of the
     * channel should be added to the select() systemcall.
     */
    if (fkbd_ext_ctl != NULL) {
        FD_SET(fileno(fkbd_ext_ctl), &fdset);
        nfds = fileno(fkbd_ext_ctl) + 1;
    }

    /*
     * we would like to determine minimum timeout to the next event.
     *
     * if injection is currently acitivated, at least next injection should be
     * scheduled on the time (fi->last + fi->interval).
     *
     * if injection is currently deactivated but injection start time is not
     * yet reached, start timing described by fi->start may be the next valid
     * event.
     *
     * otherwise, we must wait for the command from external channel and no
     * timeout is affected.
     */
    cur = dsm_elapsed_ms();
    tvp = NULL;
    if (fi->active) {
        const unsigned long next = fi->last + fi->interval;
        if (cur > next) {
            /* umm, past the next injection timeing, return immediately. */
            return 0;
        }
        tmout.tv_sec  = (next - cur) / 1000;
        tmout.tv_usec = ((next - cur) % 1000) * 1000;
        tvp = &tmout;
    }
    else
    if (cur < fi->start) {
        tmout.tv_sec  = fi->start / 1000;
        tmout.tv_usec = (fi->start % 1000) * 1000;
        tvp = &tmout;
    }

    /* call select() to wait for something happen, and return the result. */
    return select(nfds, &fdset, NULL, NULL, tvp);
}

/* The main loop function of the FKBD. This lives as thread in the
 * dsysmon process. */
static int
fkbd_main_loop(void* arg)
{
    struct fkbd_inj * const fi = (struct fkbd_inj *)arg;
    unsigned long cur;

    verbose("FKBD: main loop, start %lums, stop %lums, interval %lums\n",
                fi->start, fi->stop, fi->interval);

    while (true) {
        if (fkbd_wait_for_something(fi) > 0) {
            /* controll channel can be readable, process it first. */
            fkbd_process_ext_ctl(fi);
        }
        cur = dsm_elapsed_ms();

        /*
         * determine single-shot injection active window.  if we are in the
         * cooperation mode, injection should completely be controlled by the
         * external program.  otherwise, time between 'inj_start' and
         * 'inj_stop' is an injection active window.
         */
        if (! cooperation) {
            bool prev_active = fi->active;
            fi->active = (fi->start <= cur && cur < fi->stop) ? true : false;

            if (prev_active != fi->active) {
                fi->last = 0;
                verbose("FKBD: time to %s injection.\n",
                                fi->active ? "start" : "stop");
            }
        }

        if (fi->active && (fi->last + fi->interval) <= cur) {
            fkbd_simple_auto_injection();
            fi->last = cur;
        }
    }

    /* NOTREACHED */
    return 0;
}

static int
fkbd_dev_hook(dsm_dev_t type, dsm_io_t io __UNUSED, int index __UNUSED,
                                struct iovec *iov, size_t len)
{
    struct fkbd_log *fl;

    switch (type) {
    case DSM_DEV_NETWORK:
        fl = &fkbd_netlog[index];
        break;

    case DSM_DEV_BLOCK:
        fl = &fkbd_hddlog[index];
        break;

    default:
        return len;
    }

    (void)sem_wait(&fl->fl_lock);
    fl->bytes += len;

    if (fl->cl_state == CL_IDLE) {
        fl->timestamp = dsm_elapsed_ms();
        fl->cl_state = CL_PROGRESS;
        (void)sem_post(&fl->cl_wchan);
    }
    (void)sem_post(&fl->fl_lock);

    return len;
}

static int
fkbd_log_task_func(void *arg)
{
    struct fkbd_log * const fl = (struct fkbd_log *)arg;

    verbose("FKBD: %s task, coalescing timeout %lums\n",
                fl->ident, fl->cl_timeout);

    while (true) {
        /* we must wait until someone kick the coalescing timeout. */
        (void)sem_wait(&fl->cl_wchan);

        /* then we must sleep until coalescing timeout will be expired. */
        usleep((useconds_t)(fl->cl_timeout * 1000));

        /* now, we go to the log output. */
        (void)sem_wait(&fl->fl_lock);

        if (fl->fp_file != NULL) {
            fprintf(fl->fp_file, "%lu %lu\n", fl->timestamp, fl->bytes);
            fflush(fl->fp_file);
        }
        if (fl->fp_fifo != NULL) {
            (void)sem_wait(&fkbd_ext_lock);
            fprintf(fl->fp_fifo, "%s %lu %lu\n",
                                fl->ident, fl->timestamp, fl->bytes);
            fflush(fl->fp_fifo);
            (void)sem_post(&fkbd_ext_lock);
        }
        fl->timestamp = 0;
        fl->bytes = 0;
        fl->cl_state = CL_IDLE;

        (void)sem_post(&fl->fl_lock);
    }

    /* NOTREACHED */
    return 0;
}

static dsm_task_t fkbd_log_task;
static dsm_task_t fkbd_main_task;

static void
fkbd_setup_log(dsm_dev_t type, int index,
                        const char *log_file, unsigned long cl_timeout)
{
    const int nr_fkbd_netlog = sizeof(fkbd_netlog) / sizeof(fkbd_netlog[0]);
    const int nr_fkbd_hddlog = sizeof(fkbd_hddlog) / sizeof(fkbd_hddlog[0]);
    const unsigned long min_cl_timeout = 10;        /* at least 10ms */
    struct fkbd_log *fl;
    int instance;
    const char *fmt;

    /*
     * 'index' may be -1 if old style option is used.  in this case actual
     * instance number is fixed to zero and identifier string should not
     * include the index number.
     */
    instance = (index < 0) ? 0 : index;

    switch (type) {
    case DSM_DEV_NETWORK:
        if (nr_fkbd_netlog <= instance) {
            fprintf(stderr, "network device instance index out of range.\n");
            return;
        }
        fl = &fkbd_netlog[instance];
        fmt = (index < 0) ? "netlog" : "netlog%d";
        break;

    case DSM_DEV_BLOCK:
        if (nr_fkbd_hddlog <= instance) {
            fprintf(stderr, "block device instance index out of range.\n");
            return;
        }
        fl = &fkbd_hddlog[instance];
        fmt = (index < 0) ? "hddlog" : "hddlog%d";
        break;

    default:
        fprintf(stderr, "FKBD: unknown device type, ignoring.\n");
        return;
    }

    memset(fl, 0, sizeof(struct fkbd_log));
    (void)sem_init(&fl->fl_lock, 0, 1);

    fl->type = type;
    fl->index = instance;
    snprintf(fl->ident, sizeof(fl->ident), fmt, fl->index);

    fl->bytes = 0;
    fl->timestamp = 0;

    /*
     * determine log output destination and get FILE pointer of them.  then
     * register device I/O hook on the specified device type and instance.
     */
    fl->fp_file = (log_file != NULL) ? fopen(log_file, "w") : NULL;
    fl->fp_fifo = (fkbd_ext_msg != NULL) ? fkbd_ext_msg : NULL;
    if (fl->fp_file != NULL || fl->fp_fifo != NULL) {
        dsm_register_io_hook(fl->type, DSM_IO_OUTPUT, fl->index, fkbd_dev_hook);
    }

    (void)sem_init(&fl->cl_wchan, 0, 0);
    fl->cl_timeout = MAX(min_cl_timeout, cl_timeout);
    fl->cl_state = CL_IDLE;

    /*
     * OK, we have initialized fkbd_log structure for specified device and
     * instance index.  create log output task with additional argument as
     * just initialized fkbd_log structure.
     */
    fkbd_log_task.rc = -1;
    fkbd_log_task.func = fkbd_log_task_func;
    dsm_create_task(&fkbd_log_task, (void *)fl);

    return;
}

static void
fkbd_setup_external_channel(void)
{
    int fd_ctl, fd_msg;
    FILE *fp_ctl, *fp_msg;

    if (! cooperation)
            return;

    if (dsm_get_coop_channel("fkbd", &fd_ctl, &fd_msg) < 0) {
        fprintf(stderr, "FKBD: couldn't get external channel.\n");
        return;
    }

    fp_ctl = fdopen(fd_ctl, "r");
    fp_msg = fdopen(fd_msg, "w");
    if (fp_ctl == NULL || fp_msg == NULL) {
        fprintf(stderr, "FKBD: couldn't reassign external channel.\n");
        return;
    }
    fkbd_ext_ctl = fp_ctl;
    fkbd_ext_msg = fp_msg;

    (void)sem_init(&fkbd_ext_lock, 0, 1);

    return;
}

void
fkbd_usage (void)
{
    printf("FKBD options:\n"
        "--fkbd\n"
        "      select FoxyKBD application.\n"
        "--injection=<interval>[:<delay>:<runtime>]\n"
        "      enable key injection timing on virtio console.\n"
        "--netlog<index>[=<filename>]\n"
        "      enable logging on virtio network device instance <index>.\n"
        "--hddlog<index>[=<filename>]\n"
        "      enable logging on virtio block device instance <index>.\n"
        "--coalesce=<timeout>\n"
        "      specify log coalescing timeout in mili-seconds.\n"
    );
    return;
}

/*
 * fkbd_init() is an initialization function of FKBD application. it will be
 * registered by fkbd_init_module() function and called according with the
 * command line selector option "--fkbd".
 */
void
fkbd_init(int argc, char** argv)
{
    static struct option opts[] = {
        { "fkbd",                no_argument,                NULL, 'F' },
        { "injection",                required_argument,        NULL, 'i' },
        { "coalesce",                required_argument,        NULL, 'c' },
        { NULL },
    };
    unsigned long interval, delay, runtime;
    unsigned long cl_timeout = 100;                /* default 100ms */
    int c, i;

    /* setup external channel. */
    fkbd_setup_external_channel();

    /* suppress unknown option message by getopt(). */
    opterr = 0;

    /*
     * injection and coalescing options must be determined first.
     */
    optind = 0;
    while ((c = getopt_long(argc, argv, "+", opts, NULL)) != EOF) {
        switch (c) {
        case 'i':
            if (sscanf(optarg, "%lu:%lu:%lu",
                                &interval, &delay, &runtime) == 3) {
                fkbd_inj.interval = interval;
                fkbd_inj.start = delay;
                fkbd_inj.stop = delay + runtime;
                fkbd_inj.active = true;
            }
            else
            if (sscanf(optarg, "%lu", &interval) == 1) {
                fkbd_inj.interval = interval;
                fkbd_inj.start = 0;
                fkbd_inj.stop = 0;
            }
            else {
                fprintf(stderr, "FKBD: invalid interval option specified.\n");
                fkbd_usage();
                exit(3);
            }
            break;

        case 'c':
            if (sscanf(optarg, "%lu", &cl_timeout) == 1) {
                /* OK, valid option argument. */
            }
            else {
                fprintf(stderr, "FKBD: invalid coalesce option specified.\n");
                fkbd_usage();
                exit(3);
            }

        default:
                break;
        }
    }

    /*
     * then we are going to parse log options.  because the format of the log
     * option is much differ than the one 'getopt()' library function accepts,
     * they are processed locally.
     *
     * we must accept two types of the option format, old (compatible) style
     * and new style with instance index specified.
     *
     * old (compatible) style option is such that,
     *
     *    --net-log[=<log-file>] | --netlog[=<log-file]
     *    --hdd-log[=<log-file>] | --hddlog[=<log-file>]
     *
     * in this case, instance index is not included in the option itself and
     * fixed to zero.
     *
     * new style option is like,
     *
     *    --netlog<index>[=<log-file>]
     *    --hddlog<index>[=<log-file>]
     *
     *in this case, instance index is specified by <index> part of the option,
     * which should be one digit of range 0-9.
     */
    for (i = 1; i < argc; i++) {
        const char * const argp = argv[i];
        const char *ap = argp;
        dsm_dev_t type;
        int index;
        const char *fname;

        if (! (ap[0] == '-' && ap[1] == '-'))
            continue;
        ap += 2;

        type = -1;
        if (strncmp(ap, "netlog", strlen("netlog")) == 0) {
            type = DSM_DEV_NETWORK;
            ap += strlen("netlog");
        }
        else
        if (strncmp(ap, "hddlog", strlen("hddlog")) == 0) {
            type = DSM_DEV_BLOCK;
            ap += strlen("hddlog");
        }
        else
        if (strncmp(ap, "net-log", strlen("net-log")) == 0) {
            type = DSM_DEV_NETWORK;
            ap += strlen("net-log");
        }
        else
        if (strncmp(ap, "hdd-log", strlen("hdd-log")) == 0) {
            type = DSM_DEV_BLOCK;
            ap += strlen("hdd-log");
        }
        else {
            /* does not match "netlog" nor "hddlog". */
            continue;
        }

        if (ap[0] == '-') {
            /* skip trailing dash on "--net-log-0" case. */
            ap += 1;
        }

        index = -1;
        if (isdigit(ap[0])) {
            /* new style, instance index number. */
            index = ap[0] - '0';
            ap += 1;
        }

        fname = NULL;
        if (ap[0] == '=') {
            /* log file name is specified. */
            ap += 1;
            fname = ap;
            ap += strlen(fname);
        }

        /*
         * now, 'ap' would point the last of the option string, i.e. '*ap'
         * should be '\0'.
         */
        if (ap[0] != '\0') {
            fprintf(stderr, "FKBD: invalid option \"%s\" specified.\n", argp);
            fkbd_usage();
            exit(3);
        }

        /* setup log facility according to the option specified. */
        fkbd_setup_log(type, index, fname, cl_timeout);
    }

    /* Kick FKBD main loop task. */
    fkbd_main_task.rc = -1;
    fkbd_main_task.func = fkbd_main_loop;
    dsm_create_task(&fkbd_main_task, (void *)&fkbd_inj);

    return;
}

void
fkbd_terminate(void)
{
    dsm_unregister_io_hook(DSM_DEV_NETWORK, DSM_IO_OUTPUT, 0);

    dsm_cancel_task(&fkbd_main_task);
    dsm_cancel_task(&fkbd_log_task);
}

/* DSM application definition and registration. */
static struct dsm_application fkbd_application = {
    .name        = "FoxyKBD",
    .selector    = "fkbd",
    .init        = fkbd_init,
    .usage       = fkbd_usage,
    .terminate   = fkbd_terminate,
};

static void __attribute__ ((constructor))
fkbd_init_module(void)
{
    dsm_register_application(&fkbd_application);
}

static void __attribute__ ((destructor))
fkbd_fini_module(void)
{
    const int nr_fkbd_netlog = sizeof(fkbd_netlog) / sizeof(fkbd_netlog[0]);
    const int nr_fkbd_hddlog = sizeof(fkbd_hddlog) / sizeof(fkbd_hddlog[0]);
    int idx;

    /* close all log files in case of automatic mode. */
    if (! cooperation) {
        for (idx = 0; idx < nr_fkbd_netlog; idx++) {
            struct fkbd_log *fl = &fkbd_netlog[idx];
            if (fl->fp_file != NULL)
                fclose(fl->fp_file);
        }
        for (idx = 0; idx < nr_fkbd_hddlog; idx++) {
            struct fkbd_log *fl = &fkbd_hddlog[idx];
            if (fl->fp_file != NULL)
                fclose(fl->fp_file);
        }
    }
    return;
}
