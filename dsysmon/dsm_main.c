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
 * dsm_main.c:
 *        implementation of 'dsysmon' main function and application sub-module
 *        handling.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#include "dsm_api.h"
#include "dsm_virtio.h"
#include "dsm_main.h"


/*
 * definition of debug and verbose flags.  debug() and verbose() macros
 * defined in "dsm_api.h" can be used to show additional messages.
 */
bool verbose = false;
bool debug = false;


/*
 * 'dsm_start_time' is used to record start-up time of 'dsysmon' program.
 * application sub-module can refere it for any wall-time related usage.
 */
struct timeval dsm_start_time;


/*
 * definition of cooperation flag.  this flag is defaulted to false but set
 * to true if "--cooperation" option is specified.
 *
 * external channel for application sub-module will be implemented as named
 * pipe created in the certain directory.  path of this base directory will
 * be stored into 'coop_dir'.
 */
//bool cooperation = false;
bool cooperation = true;
static char coop_dir[] = "/tmp/dsysmonXXXXXXXXXX";


/*
 * registered application sub-modules are stored into app_rec[] array, type
 * of each element is app_rec structure.  currently maximum 8 application
 * sub-modules can be registered.
 *
 * there are some additional members for internal use.  'registered' flag
 * indicates that the record is certainly registered and valid.  'initialized'
 * flag shows whether the application sub-module has already beeen initialized
 * or not.
 *
 * 'next' is a member to construct a selected application list which will be
 * pointed by 'app_list_head' and 'app_list_tail' pointers.
 *
 * 'coop_ctl' and 'coop_msg' hold end-point FDs for external channel of the
 * application.  these FDs will be created and initialized if 'dsysmon' is
 * in cooperation mode.
 */
struct app_rec {
    struct dsm_application app;
    bool registered;
    bool initialized;
    struct app_rec *next;
    int coop_ctl;                        /* input FD of external channel */
    int coop_msg;                        /* output FD of external channel */
};
static struct app_rec app_rec[8];
static const int nr_app_rec = sizeof(app_rec) / sizeof(app_rec[0]);

static struct app_rec *app_list_head;
static struct app_rec *app_list_tail;


/*
 * we have some internal functions to handle cooperation external channel.
 *
 * 'coop_init()' will be called from our main() function to prepare parent
 * directory of all named pipes.
 *
 * 'create_coop_channel()' will be called from our application startup function
 * to create required named pipes and open them as end-points.
 *
 * 'coop_finish()' is a cleanup function to destroy all named pipes and parent
 * direcoty.
 */
static int first_coop_init = 0;
static void
coop_init(void)
{
    const pid_t pid = getpid();
    const mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;

    if (first_coop_init == 0) {
        first_coop_init = 1;
    } else {
        return;
    }

//    if (! cooperation)
//    return;

    snprintf(coop_dir, sizeof(coop_dir), "/tmp/dsysmon%d", pid);
    verbose("creating external channel directory, \"%s\"\n", coop_dir);
    if (mkdir(coop_dir, mode)) {
        perror("can't create cooperation base directory");
        exit(3);
    }
    return;
}

static void
create_coop_channel(struct app_rec *ar)
{
    const mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
    char pbuf[sizeof(coop_dir) + 1 + sizeof(ar->app.selector) + 1];
    struct stat st;

    if (! cooperation) {
        /* external channel should be disabled. */
        ar->coop_ctl  = -1;
        ar->coop_msg = -1;
        return;
    }

    /* make sure that the base directory has already been created. */
    while (stat(coop_dir, &st) < 0) {
        usleep(1000);
    }

    /*
     * construct input node path name and create a control FIFO.  note that
     * it should be opned with (O_RDWR | O_NONBLOCK) mode to prevent blocking
     * on open() systemcall.
     */
    snprintf(pbuf, sizeof(pbuf), "%s/%s-ctl", coop_dir, ar->app.selector);
    verbose("creating external channel fifo node, \"%s\"\n", pbuf);
    if (mkfifo(pbuf, mode) < 0) {
        perror("can't create cooperation FIFO node");
        return;
    }
    ar->coop_ctl = open(pbuf, O_RDWR | O_NONBLOCK);

    /*
     * construct output node path name and create message FIFO.  also note that
     * it should be opned with (O_RDWR | O_NONBLOCK) mode to prevent blocking
     * on open() systemcall.
     */
    snprintf(pbuf, sizeof(pbuf), "%s/%s-msg", coop_dir, ar->app.selector);
    verbose("creating external channel fifo node, \"%s\"\n", pbuf);
    if (mkfifo(pbuf, mode) < 0) {
        perror("can't create cooperation FIFO node");
        return;
    }
    ar->coop_msg = open(pbuf, O_RDWR | O_NONBLOCK);

    return;
}

static void
coop_finish(void)
{
    struct app_rec *ar;
    char pbuf[sizeof(coop_dir) + 1 + sizeof(ar->app.selector) + 1];

    if (! cooperation)
        return;

    for (ar = app_list_head; ar != NULL; ar = ar->next) {
        if (! ar->initialized)
            continue;

        /* close intpu/output end-point file descriptors. */
        if (ar->coop_ctl >= 0)
            close(ar->coop_ctl);
        if (ar->coop_msg >= 0)
            close(ar->coop_msg);

        ar->coop_ctl = -1;
        ar->coop_msg = -1;

        /* remove input and output named fifo nodes. */
        snprintf(pbuf, sizeof(pbuf), "%s/%s-ctl", coop_dir, ar->app.selector);
        verbose("removing external channel pipe node, \"%s\"\n", pbuf);
        (void)remove(pbuf);

        snprintf(pbuf, sizeof(pbuf), "%s/%s-msg", coop_dir, ar->app.selector);
        verbose("removing external channel pipe node, \"%s\"\n", pbuf);
        (void)remove(pbuf);
    }

    /* finally remove cooperation base directory. */
    verbose("removing external channel directory, \"%s\"\n", coop_dir);
    (void)rmdir(coop_dir);

    return;
}


/*
 * we defined some supplement functions to handle app_rec structure and
 * app_rec[] array.
 *
 * lookup_application() will find certain app_rec record which has specified
 * selector string.  if there is such application registered, corresponding
 * app_rec will be returned.  otherwise, the function returns NULL.
 *
 * select_application() will insert selected application specified by the
 * argc/argv pair into 'app_list' list number of selected application will be
 * returned.
 *
 * start_application() will kick the selected application by calling their
 * 'init()' handler function.  also it returnes number of applications
 * initialized.
 */
static struct app_rec *
lookup_application(const char *selector)
{
    int aridx;

    if (selector == NULL)
        return NULL;

    for (aridx = 0; aridx < nr_app_rec; aridx++) {
        struct app_rec * const ar = &(app_rec[aridx]);
        const int selector_maxlen = sizeof(ar->app.selector);

        if (! ar->registered)
            continue;

        if (strncmp(ar->app.selector, selector, selector_maxlen) == 0)
            return ar;
    }
    return NULL;
}

static int
select_application(int argc, char *argv[])
{
    int argidx;
    char *argstr;
    struct app_rec *ar;
    int nr_selected = 0;

    if (argc == 0 || argv == NULL)
        return 0;

    for (argidx = 0; argidx < argc; argidx++) {
        argstr = argv[argidx];

        /* appliation selector should start with "--" indicator. */
        if (argstr[0] != '-' || argstr[1] != '-')
            continue;

        /* lookup the option string, insert into selected list if any. */
        if ((ar = lookup_application(&argstr[2])) != NULL) {
            if (app_list_tail == NULL) {
                app_list_head = ar;
                app_list_tail = ar;
                ar->next = NULL;
            }
            else {
                app_list_tail->next = ar;
                app_list_tail = ar;
                ar->next = NULL;
            }
            nr_selected += 1;
        }
    }
    return nr_selected;
}

static int
start_application(int argc, char *argv[])
{
    struct app_rec *ar;
    int nr_initialized = 0;

    /* traverse selected list and invoke initialization functions. */
    for (ar = app_list_head; ar != NULL; ar = ar->next) {
        verbose("initializing %s application.\n", ar->app.name);

        /* create cooperation external channel first. */
        create_coop_channel(ar);

        /* then call application specific initialization function. */
        optind = 0;
        (ar->app.init)(argc, argv);

        ar->initialized = true;
        nr_initialized += 1;
    }
    return nr_initialized;
}

static int
terminate_application(void)
{
    struct app_rec *ar;
    int nr_terminated = 0;

    /* traverse selected list and invoke initialization functions. */
    for (ar = app_list_head; ar != NULL; ar = ar->next) {
        verbose("terminating %s application.\n", ar->app.name);

        /* call application specific termination function. */
        if (ar->app.terminate != NULL) {
            (ar->app.terminate)();
        }

        nr_terminated += 1;
    }
    return nr_terminated;
}

/*
 * dsm_register_application():
 *        all DSM application sub-modules (they are together linked into dsysmon
 *        program) should provide its own 'dsm_application' record to the main
 *        module.
 */
void
dsm_register_application(struct dsm_application *ap)
{
    int aridx;

    /* sanity check of passed dsm_application record. */
    if (ap == NULL ||
        ap->name == NULL ||
        ap->selector[0] == '\0' ||
        ap->init == NULL ||
        ap->usage == NULL) {
        fprintf(stderr, "invalid application record specified.\n");
        exit(1);
    }

    /* duplicate check. */
    if (lookup_application(ap->selector) != NULL) {
        fprintf(stderr, "application record has already been registered.\n");
        return;
    }

    /* register it. */
    for (aridx = 0; aridx < nr_app_rec; aridx++) {
        struct app_rec * const ar = &(app_rec[aridx]);

        if (! ar->registered) {
            /* copy dsm_application structure to empty element. */
            memcpy((void *)&(ar->app), ap, sizeof(ar->app));
            ar->registered = true;
            return;
        }
    }

    /* oops, out of range. */
    fprintf(stderr, "too many application have been registerd.\n");
    exit(1);
}

/*
 * dsm_usage():
 *        generic usage function of 'dsysmon' program.
 */
void
dsm_usage(void)
{
    int aridx;

    printf("Usage: dsysmon [options]\n");
    printf("generic options:\n"
            " --verbose [-v]\n"
            "      show verbose messages.\n"
            " --debug [-d]\n"
            "      show debug messages.\n"
            " --coop [-c]\n"
            "      enable cooperation mode.\n"
            " --start [-s]\n"
            "      automatically start User Guest OS.\n"
            " --help\n"
            "      show this help message.\n");
    printf("\n");
//    virtio_usage();

    /* show application specific option usage. */
    for (aridx = 0; aridx < nr_app_rec; aridx++) {
        struct app_rec * const ar = &(app_rec[aridx]);

        if (ar->registered && ar->app.usage != NULL) {
            printf("\n");
            (ar->app.usage)();
        }
    }
    exit(2);
}

/*
 * dsm_create_task():
 *        create an application specific task thread.  each thread has assigned
 *        64KB stack area allocated from heap space.  this function does not
 *        return a status.  if some error occure during task creation, 'dsysmon'
 *        process will be terminated.
 */
void
dsm_create_task(dsm_task_t *task, void *arg)
{
#if 0
    const size_t stack_size = 64 * 1024;                /* 64KB */
    void *stack_top;
    int thread;

    if ((stack_top = malloc(stack_size)) == NULL) {
        perror("can't allocate stack for task thread");
        exit(1);
    }
    thread = clone(task, stack_top + stack_size, CLONE_VM | SIGCHLD, arg);
    if (thread == (pid_t)-1) {
        perror("can't clone task thread");
        exit(1);
    }
#else
    pthread_attr_t attr;
    int rc = pthread_attr_init(&attr);
    if (rc != 0) {
        perror("pthread_attr_init error");
        return;
    }
    task->rc = pthread_create(&(task->thread), &attr, (void *)(task->func), arg);
    if (rc != 0) {
        perror("pthread_create error");
        return;
    }
#endif
    return;
}

void
dsm_cancel_task(dsm_task_t *task)
{
    if (task->rc == 0) {
        pthread_cancel(task->thread);
        task->rc = -1;
    }
}


/*
 * dsm_get_coop_channel():
 *        retreave end-point file descriptors of external channel if 'dsysmon'
 *        is in cooperation mode.
 */
int
dsm_get_coop_channel(const char *selector, int *fd_ctl, int *fd_msg)
{
    struct app_rec *ar;

    if (! cooperation) {
        *fd_ctl = -1;
        *fd_msg = -1;
        return -1;
    }

    for (ar = app_list_head; ar != NULL; ar = ar->next) {
        const int selector_maxlen = sizeof(ar->app.selector);
        if (strncmp(ar->app.selector, selector, selector_maxlen) == 0) {
            *fd_ctl = ar->coop_ctl;
            *fd_msg = ar->coop_msg;
            return 0;
        }
    }
    return -1;
}

/*
 * dsm_elapsed_ms():
 *        return elapsed time since 'dsysmon' program started in mili-seconds.
 */
unsigned long
dsm_elapsed_ms(void)
{
    struct timeval now, elps;
    unsigned long cur;

    (void)gettimeofday(&now, NULL);
    timersub(&now, &dsm_start_time, &elps);
    cur = (elps.tv_sec * 1000) + (elps.tv_usec / 1000);
    return cur;
}


#if 0
/*
 * send a command to the SysFS node of D-Visor86 VMM.  it may be used to
 * control behavior of User Guest OS.
 */
static void
dv86_user_command(const char *cmd)
{
    const char sysfs_user_cmd[] = "/sys/kernel/veidt/users/%d/cmd";
    const int cpuid = 1;                /* user guest is always 1. */
    char sysfs[128];
    FILE *fp;

    if (cmd == NULL) {
        printf("NULL command specified.\n");
        return;
    }

    snprintf(sysfs, sizeof(sysfs), sysfs_user_cmd, cpuid);

    if ((fp = fopen(sysfs, "w")) == NULL) {
        printf("can't open veidt command sysfs node.\n");
        return;
    }

    /* write out command, flush output, then close the node. */
    fprintf(fp, "%s\n", cmd);
    fflush(fp);
    fclose(fp);

    return;
}

/*
 * main():
 *        this is the main function of 'dsysmon' program.  we use 'getopt_long()'
 *        library function to parse option string.  note that 'getopt()' family
 *        will destruct argv[] vector at the end of parsing.  to pervent this,
 *        '+' character should be specified as the first charactor of "optstring"
 *        argument.  also "optind" global variable will be advanced during
 *        option parsing by 'getopt()' family, it should be reset to 0 before
 *        passing argc/argv pair to the sub-module initialization function.
 */
int
main(int argc, char *argv[])
{
    static struct option opts[] = {
        { "verbose",            no_argument,                NULL, 'v' },
        { "debug",              no_argument,                NULL, 'd' },
        { "coop",               no_argument,                NULL, 'c' },
        { "cooperation",        no_argument,                NULL, 'c' },
        { "start",              no_argument,                NULL, 's' },
        { "help",               no_argument,                NULL, 'h' },
        { NULL },
    };
    bool start_user_os = false;
    int c;

    /* suppress unknown option message by getopt(). */
    opterr = 0;

    /* first, 'dsysmon' global options should be processed. */
    optind = 0;
    while ((c = getopt_long(argc, argv, "+vdchs", opts, NULL)) != EOF) {
        switch (c) {
        case 'v':
            verbose = true;
            break;
        case 'd':
            debug = true;
            break;
        case 'c':
            cooperation = true;
            break;
        case 'h':
            dsm_usage();
            break;
        case 's':
            start_user_os = true;
            break;
        default:
            break;
        }
    }

    /* now we must initialize virtio backend sub-module. */
    optind = 0;
    virtio_init(argc, argv);

    /* then initialize cooperation external channel. */
    coop_init();

    /* remember start-up time in dsm_start_time global variable. */
    (void)gettimeofday(&dsm_start_time, NULL);

    /*
     * select application and call it's initialization function.  note that
     * even if no valid application selector option has been specified, we
     * must continue to run 'dsysmon' because it should act as virtio backend.
     */
    (void)select_application(argc, argv);
    (void)start_application(argc, argv);

    /* if specified so, attempt to start User Guest OS automatically. */
    if (start_user_os) {
        verbose("starting User Guest OS automatically.\n");
        dv86_user_command("restart");
    }

    /* finally kick virtio backend task, this does not return. */
    virtio_run();

    /* NOTREACHED */
    return 0;
}
#endif

static void __attribute__ ((destructor))
dsm_main_finish(void)
{
    coop_finish();
    return;
}

void
dsm_init(int argc, const char *argv[])
{
    coop_init();
    (void)gettimeofday(&dsm_start_time, NULL);
    (void)select_application(argc, (char **)argv);
    (void)start_application(argc, (char **)argv);
}

void
dsm_terminate(void)
{
    (void)terminate_application();
}
