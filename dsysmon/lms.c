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
 * lms.c:
 *        implementation of Waseda Lightweight Monitoring Service (LMS).
 *
 *        "lms.c" implements application behavior of LMS.  because we must
 *        prevent data type and structure definition conflict between kernel
 *        code and user-land application code, all kernel data type handling
 *        functions are separated into "lms_kern.c".  few selected interface
 *        functions are used to communicate between user-land part and kernel
 *        data handling part.
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sched.h>
#include "dsm_api.h"

#include "lms_defs.h"


//#define INIT_TASK_DEFAULT  0xc0723b20UL
//#define PER_CPU_RQ_DEFAULT 0xc072d2e0UL
#define INIT_TASK_DEFAULT  0xffffffff81c0d020UL
#define PER_CPU_RQ_DEFAULT 0x00000000000137c0UL

// for SMP
#define PER_CPU_OFFSET     0xffffffff81cde480UL
#define NR_CPU_IDS         0xffffffff81ce0364UL

/*
 * kernel virtual address of symbols "init_task" and "per_cpu__rqunqueues"
 * must be determined with respect to the User Linux build, and stored into
 * global variables 'init_task_ukva' and 'per_cpu_rq_ukva' respectively.
 * default value of those symbols must be specified at compilation time by
 * INIT_TASK_DEFAULT and PER_CPU_RQ_DEFAULT, perhaps specified by Makefile.
 *
 * we note that if you would like to change those address at runtime, you can
 * do it by "--init-task=<addr>" and "--per-cpu-rq=<addr>" options.
 */
#if !defined(INIT_TASK_DEFAULT) || !defined(PER_CPU_RQ_DEFAULT)
#error "INIT_TASK_DEFAULT / PER_CPU_RQ_DEFAULT must be specified."
#endif

lms_ukva_t init_task_ukva  = INIT_TASK_DEFAULT;
lms_ukva_t per_cpu_rq_ukva = PER_CPU_RQ_DEFAULT;

// for SMP
lms_ukva_t per_cpu_offset_ukva = PER_CPU_OFFSET;
lms_ukva_t nr_cpu_ids_ukva     = NR_CPU_IDS;
int lms_nr_cpu_ids = 1;

/*
 * we have some LMS user-land part specific global variables. 'lms_interval'
 * indicates an interval time in mili-seconds of automatic examination of
 * monitoring service function.  'lms_loose' is a loose factor of exception
 * count.  if the difference of exception count is less than this factor,
 * the result of the examination is considered as valid.
 */
static unsigned long lms_interval = 1000;                /* 1000ms */
static unsigned long lms_loose = 5;                        /* 5 count */

/*
 * result of the examination will be printed on the termial as text message.
 * however, if we are in the cooperation mode, all message should be sent to
 * the external output channel.  also if we are in the cooperation mode, some
 * control message may come from external input channel.  global variables
 * 'lms_ext_ctl' and 'lms_ext_msg' of type pointer to FILE are used to hold
 */
static FILE *lms_ext_ctl = NULL;
static FILE *lms_ext_msg = NULL;

/*
 * one more global variable 'lms_acitve' is used to indicate whether the
 * examination is currently activated or not.  it is defaulted to true but
 * it must be initialized to false if we are in the cooperation mode to
 * prevent automatic examination.
 */
static bool lms_active = true;


/*
 * few wrapper functions are defined as the interface between LMS kernel part
 * and DSM-API functions.  note that LMS kernel part can not use 'bool' type
 * so lms_is_verbose() and lms_is_debug() are defined as integer function.
 */
int
lms_is_verbose(void)
{
    return verbose;
}

int
lms_is_debug(void)
{
    return debug;
}

void
lms_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vprintf(fmt, ap);
    va_end(ap);
    return;
}

void
lms_err_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
    return;
}

void *
lms_map_ukva(unsigned long ukva)
{
    return dsm_map_ukva((ukvaddr_t)ukva);
}

int
lms_check_ukva(unsigned long ukva)
{
    return dsm_check_ukva((ukvaddr_t)ukva);
}

static int
lms_wait_for_something(void)
{
    struct timeval tmout, *tvp;
    fd_set fdset;
    int nfds;

    nfds = 0;
    FD_ZERO(&fdset);

    /*
     * if the external control channel is enabled, file descriptor of the
     * channel should be added to the select() systemcall.
     */
    if (lms_ext_ctl != NULL) {
        FD_SET(fileno(lms_ext_ctl), &fdset);
        nfds = fileno(lms_ext_ctl) + 1;
    }

    /*
     * if examination is currently stopped, timeout of the select() systemcall
     * should be disabled.  otherwise, timeout should be set to currentl value
     * of 'lms_interval'.
     */
    tvp = NULL;
    if (lms_active) {
        tmout.tv_sec  = lms_interval / 1000;
        tmout.tv_usec = (lms_interval % 1000) * 1000;
        tvp = &tmout;
    }

    /* call select() to wait for something happen, and return the result. */
    return select(nfds, &fdset, NULL, NULL, tvp);
}

static void
lms_process_ext_ctl(void)
{
    char linebuf[256];
    unsigned long val;

    if (fgets(linebuf, sizeof(linebuf), lms_ext_ctl) == NULL) {
        /* there is no message to be read from control channel. */
        return;
    }

    /*
     * currently three control messages are defined.
     *
     * "start"                start automaic examination.
     * "stop"                stop automatic examination.
     * "interval %d"        set interval to specified value in mili-seconds.
     * "loose %d"        set loose factor to specified value.
     */
    if (strncmp(linebuf, "start", strlen("start")) == 0) {
        verbose("LMS: automatic examination enabled.\n");
        lms_active = true;
    }
    else
    if (strncmp(linebuf, "stop", strlen("stop")) == 0) {
        verbose("LMS: automatic examination disabled.\n");
        lms_active = false;
    }
    else
    if (sscanf(linebuf, "interval %li", &val) == 1) {
        verbose("LMS: interval changed to %lums\n", val);
        lms_interval = val;
    }
    else
    if (sscanf(linebuf, "loose %li", &val) == 1) {
        verbose("LMS: loose factor changed to %lu\n", val);
        lms_loose = val;
    }
    return;
}

/*
 * lms_main_loop() is the main loop of LMS application, lived as a thread in
 * the 'dsysmon' process.
 */
static int
lms_main_loop(void *arg)
{
    unsigned int loop = 0;

    verbose("LMS: main loop, automatic examination %s, %lums interval.\n",
                lms_active ? "enabled" : "disabled", lms_interval);

    /* we would like to do task check per every interval. */
    while (true) {
        struct dsm_kernel_activity act_bf, act_af;
        typeof (act_bf.exception_count) diff;
        unsigned long elps, elpsint, elpsfrc;
        struct lms_task_info hidden;
        lms_result_t result;

        if (lms_wait_for_something() > 0) {
            lms_process_ext_ctl();
            continue;
        }

        /* update loop counter with modulo 1000. */
        loop = (loop + 1) % 1000;

        if (dsm_get_kernel_activity(&act_bf) < 0) {
            verbose("LMS[%03d]: failed to get activity (before).\n", loop);
            continue;
        }
        debug("LMS[%03d]: # of exception before %lu\n",
                                        loop, act_bf.exception_count);

        /* now, we would like to check the task state. */
        memset(&hidden, 0, sizeof(hidden));
        result = lms_check_rq(&hidden);

        if (dsm_get_kernel_activity(&act_af) < 0) {
            verbose("LMS[%03d]: failed to get activity (after).\n", loop);
            continue;
        }
        debug("LMS[%03d]: # of exception after  %lu\n",
                                        loop, act_af.exception_count);

        /* check if there is no activity of target kernel. */
        diff = act_af.exception_count - act_bf.exception_count;

        /* get elapsed time in ms. and show the result. */
        elps = dsm_elapsed_ms();
        elpsint = elps / 1000;
        elpsfrc = elps % 1000;

        switch (result) {
        case LMS_CHECK_OK:
            verbose("[%5lu.%03lu] LMS: passed all check. (loose %lu)\n",
                        elpsint, elpsfrc, diff);
            break;

        case LMS_CHECK_NG:
            if (hidden.pid != 0xffff && diff <= lms_loose) {
                fprintf(lms_ext_msg, "[%5lu.%03lu] LMS: "
                        "found hidden process, PID %d [%s] CPU:%d\n",
                        elpsint, elpsfrc, hidden.pid, hidden.comm, hidden.cpuid);
                fflush(lms_ext_msg);
            }
            else {
                verbose("[%5lu.%03lu] LMS: found hidden process, "
                        "PID %d [%s] (loose %lu)\n",
                        elpsint, elpsfrc, hidden.pid, hidden.comm, diff);
            }
            break;

        case LMS_INCONSISTENT:
            verbose("[%5lu.%03lu] LMS: data structure inconsistency detected. "
                        "(loose %lu)\n", elpsint, elpsfrc, diff);
        }
    }

    /* NOTREACHED */
    return 0;
}

/*
 * lms_ps_loop() is an alternative application loop to get process list of the
 * target OS.
 */
static int
lms_ps_loop(void *arg)
{
    lms_result_t result;
    unsigned int loop = 0;

    printf("LMS: start PS loop, %lums interval.\n", lms_interval);

    /* we would like to do task check per every interval. */
    while (true) {

        usleep(lms_interval * 1000);

        /* update loop counter with modulo 1000. */
        loop = (loop + 1) % 1000;

        printf("LMS[%03d]: current process list of the target.\n", loop);

        result = lms_show_proc_list();

        switch (result) {
        case LMS_CHECK_OK:
            printf("LMS[%03d]: end of process list.\n", loop);
            break;

        case LMS_INCONSISTENT:
            printf("LMS[%03d]: data structure inconsistency detected.\n", loop);
            break;

        default:
            break;
        }
    }

    /* NOTREACHED */
    return 0;
}

static dsm_task_t lms_task;

/*
 * lms_init() is an initialization function of LMS application.  it will be
 * recorded by lms_init_module() funciton, and may be called from 'dsysmon'
 * main module if "--lms" application selector will be specified.
 */
void
lms_init(int argc, char *argv[])
{
    static struct option opts[] = {
        { "lms",                no_argument,                NULL, 'L' },
        { "lms-interval",        required_argument,        NULL, 'i' },
        { "lms-loose",                required_argument,        NULL, 'l' },
        { "init-task",                required_argument,        NULL, 't' },
        { "per-cpu-rq",                required_argument,        NULL, 'q' },
        { "ps-loop",                no_argument,                NULL, 'p' },
        { NULL },
    };
    int (*task_func)(void*) = lms_main_loop;
    unsigned long val;
    int c;

    /*
     * sanity check for LMS local type definitions.  data type 'lms_ukva_t'
     * should be same size as the DSM-API defined 'ukvaddr_t'.  also data
     * type 'lms_pid_t' should be same size as the Linux defined 'pid_t'.
     */
    if (sizeof(lms_ukva_t) != sizeof(ukvaddr_t)) {
        fprintf(stderr, "LMS: lms_ukva_t has different size "
                        "than ukvaddr_t, aborting.\n");
        exit(3);
    }
    if (sizeof(lms_pid_t) != sizeof(pid_t)) {
        fprintf(stderr, "LMS: lms_pid_t has different size "
                        "than pid_t, aborting.\n");
        exit(3);
    }

    /* suppress unknown option message by getopt(). */
    opterr = 0;

    while ((c = getopt_long(argc, argv, "+", opts, NULL)) != EOF) {
        switch (c) {
        case 'i':
            if (sscanf(optarg, "%lu", &val) != 1) {
                fprintf(stderr, "LMS: parameter parsing error on "
                                "\"lms-interval\" option.\n");
                dsm_usage();
            }
            lms_interval = val;
            break;

        case 'l':
            if (sscanf(optarg, "%lu", &val) != 1) {
                fprintf(stderr, "LMS: parameter parsing error on "
                                "\"lms-loose\" option.\n");
                dsm_usage();
            }
            lms_loose = val;
            break;

        case 't':
            if (sscanf(optarg, "%li", &val) != 1) {
                fprintf(stderr, "LMS: parameter parsing error on "
                                "\"init-task\" option.\n");
                dsm_usage();
            }
            init_task_ukva = (lms_ukva_t)val;
            break;

        case 'q':
            if (sscanf(optarg, "%li", &val) != 1) {
                fprintf(stderr, "LMS: parameter parsing error on "
                                "\"per-cpu-rq\" option.\n");
                dsm_usage();
            }
            per_cpu_rq_ukva = (lms_ukva_t)val;
            break;

        case 'p':
            task_func = lms_ps_loop;
            break;

        default:
            break;
        }
    }

    /*
     * standalone mode vs. cooperation mode.
     *
     * in the standalone mode, periodic examination should be automatically
     * enabled so 'lms_active' is set to true.  global variable 'lms_interval'
     * is used to determine examination interval.
     *
     * in the cooperation mode, automatic periodic examination should initially
     * be disabled and lator may be enabled through control message.  also we
     * must initialize external message output and control input channel as
     * well.
     */
    lms_ext_ctl = NULL;
    lms_ext_msg = stdout;
    lms_active = true;

    if (cooperation) {
        int fd_ctl, fd_msg;
        if (dsm_get_coop_channel("lms", &fd_ctl, &fd_msg) < 0) {
            fprintf(stderr, "LMS: couldn't get external channel.\n");
        }
        else {
            FILE *fp_ctl = fdopen(fd_ctl, "r");
            FILE *fp_msg = fdopen(fd_msg, "w");
            if (fp_ctl == NULL || fp_msg == NULL) {
                fprintf(stderr, "LMS: couldn't reassign external channel.\n");
            }
            else {
                /*
                 * OK, we are certainly in the cooperation mode and external
                 * channels are usable.
                 */
                lms_ext_ctl = fp_ctl;
                lms_ext_msg = fp_msg;
                lms_active = false;
            }
        }
    }

    // for SMP
    int *p_nr_cpu_ids = lms_map_ukva(nr_cpu_ids_ukva);
    lms_nr_cpu_ids = *p_nr_cpu_ids;

    /* now kick off the LMS main loop task. */
    lms_task.rc = -1;
    lms_task.func = task_func;
    dsm_create_task(&lms_task, NULL);

    return;
}

void
lms_usage(void)
{
    printf("Lightweight Monitoring Service (LMS) options:\n"
        " --lms\n"
        "      select LMS application.\n"
        " --lms-interval=<ms>\n"
        "      specify observation interval in mili-seconds.\n"
        " --lms-loose=<count>\n"
        "      specify loose count of hidden process detection.\n"
        " --init-task=<addr>\n"
        "      specify kernel virtual address of init_task.\n"
        " --per-cpu-rq=<addr>\n"
        "      specify kernel virtual address of per_cpu__runqueues.\n"
        " --ps-loop\n"
        "      show the process list of target OS for system debugging.\n"
    );
    return;
}

void
lms_terminate(void)
{
    dsm_cancel_task(&lms_task);
}


/* DSM application definition and registration. */
static struct dsm_application lms_application = {
    .name                = "Lightweight Monitoring Service",
    .selector            = "lms",
    .init                = lms_init,
    .usage               = lms_usage,
    .terminate           = lms_terminate,
};

static void __attribute__ ((constructor))
lms_init_module(void)
{
    dsm_register_application(&lms_application);
}

