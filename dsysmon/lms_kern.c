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
 * lms_kern.c:
 *        implementation of Waseda Lightweight Monitoring Service (LMS).
 *
 *        "lms_kern.c" implements kernel data structure handling part for LMS
 *        application.  because we must prevent potential conflict about data
 *        type and structure definition between kernel code and user-land
 *        application code, "lms_kern.c" should not include any header files
 *        except that the header file is known to be safe with in-kernel
 *        definitions.  instead, limited number of interface functions defined
 *        in "lms.c" should be used to communicate with user-land part.
 */

#define __KERNEL__
/*
 * we must include kernel header files for the task and runqueue structures.
 * any header files for user-land should not be included here to prevent data
 * type conflict.  note that __KERNEL__ symbol must be defined here before
 * including kernel header files to import kernel only definitions.
 */
//#include <linux/autoconf.h>
#include <generated/autoconf.h>
#include <linux/stop_machine.h>
#include <linux/sched.h>

/*
 * we note that "struct rq", "struct cfs_rq", and few additional structures
 * are not defined in the separated header file, but in the C implementation
 * file "kernel/sched.c" of Linux kernel source tree.  we have extracted
 * related structure definitins from "kernel/sched.c" into "lms_rq.h" in order
 * to have same definition then the kernel.
 */
#include "lms_rq.h"

#undef __KERNEL__

/*
 * "lms_defs.h" should be include here to import interface definition between
 * user-land part and kernel part of LMS implementation.  in addition, we 
 * would like to include <setjump.h> here to enable global exit function in
 * the kernel part implementation.  we know <setjump.h> is safe enough to be
 * used with other kernel definitions.
 */
#include "lms_defs.h"
#include <setjmp.h>


/*
 * two additional macros, lms_verbose() and lms_debug() are defined here for
 * debugging purpose.  even if the verbose or debug switch is disabled at
 * runtime, these macros may involve one function call and one conditional
 * branch instruction.  this may slow down the examination process and cause
 * potential inconsitency due to the exception counter.
 *
 * to prevent this undesireble penalty, LMS_KERN_VERBOSE and LMS_KERN_DEBUG
 * pre-processor symbols are introduced as compilation time switch to enable
 * or disable these macros.  without those symbols specified, lms_is_verbose()
 * and lms_is_debug() are overwitten by constant 0.  this will remove all
 * undesired statements and instructions.
 */
#ifndef LMS_KERN_VERBOSE
#define lms_is_verbose()        (0)
#endif
#ifndef LMS_KERN_DEBUG
#define lms_is_debug()                (0)
#endif

#define lms_verbose(args...) \
        do { if (lms_is_verbose()) lms_err_printf(args); } while(0)

#define lms_debug(args...) \
        do { if (lms_is_debug()) lms_err_printf(args); } while(0)


/*
 * we would like to adopt global exit function to indicate that some data
 * structure inconsistency would have been detected.  'lms_global_exit_env'
 * is a jump buffer environment refered as target of global exit function.
 * 'lms_global_exit()' is a convenient function to do global exit with result
 * status.
 */
jmp_buf lms_global_exit_env;

static inline void
lms_global_exit(lms_result_t result)
{
    longjmp(lms_global_exit_env, (int)result);
}

/*
 * 'lms_convert_ukva()' is a convenient function to convert kernel virtual
 * address in the User Guest OS into mapped local address, with some data
 * structure inconsistency check.  specified 'ukva' must not be NULL and
 * must satisfy 'lms_check_ukva()', otherwise LMS_INCONSISTENT would be
 * thrown.
 */
static inline void *
lms_convert_ukva(lms_ukva_t ukva, const char *reason)
{
    if ((ukva == (lms_ukva_t)NULL) || (! lms_check_ukva(ukva))) {
        lms_debug("inconsistency detected on %s, "
                                "ukva = 0x%08x\n", reason, ukva);
        lms_global_exit(LMS_INCONSISTENT);
    }
    return lms_map_ukva(ukva);
}


/*
 * lms_check_task():
 *
 * check if the specified target task (process) exist in the process list
 * with respect to the process id (pid).  the function returns lms_result_t
 * type enumeration according to the examination result.  it returns
 * LMS_CHECK_OK if the target task was certainly found in the process list.
 * otherwise it returns LMS_CHECK_NG to indicate dishonesty.
 *
 * note that when any data structure inconsistency is detected, the function
 * attempt to throw an exception by global exit function.
 *
 * we know that all available tasks in the system are linked from 'init_task'
 * using "tasks" member, which is list_head structure of task_struct.  this
 * function also checks thread group list hanged by "thread_group" member of
 * each group leader task.
 */
static lms_result_t
lms_check_task(struct task_struct *target_task)
{
    struct task_struct *init_task;
    struct task_struct *tp;
    pid_t target_pid;
    lms_ukva_t ukva;

    /*
     * first of all, we remember PID of the target task into local variable.
     * this may prevent miss comparison when target task structure will be
     * re-used and assigned a new PID during the examination.
     */
    target_pid = target_task->pid;
    lms_debug("check_task: target task pid = %d\n", target_pid);

    /* examination loop is started from 'init_task'. */
    ukva = init_task_ukva;
    tp = init_task = lms_convert_ukva(ukva, "init_task_ukva");

    /* outer loop, traversing process list. */
    do {
        struct task_struct *inner_start = tp;

        /* inner loop, traversing thread group list. */
        do {
            if (tp->pid == target_pid) {
                /*
                 * task currently examined has same PID with the target task.
                 * that is, specified target task has been found in the process
                 * list and/or thread group.  we wll return LMS_CHECK_OK.
                 */
                lms_debug("check_task: target has found in process-list.\n");
                return LMS_CHECK_OK;
            }

            /* skip if the task is not a member of thread group. */
            if (tp->thread_group.next == NULL)
                break;

            /* updated to the next task in the thread group. */
            ukva = (lms_ukva_t)list_entry(tp->thread_group.next,
                                        struct task_struct, thread_group);
            tp = lms_convert_ukva(ukva, "tp->thread_group.next");
        } while (tp != inner_start);

        /* process list should be cyclic bi-directional list. */
        if (tp->tasks.next == NULL) {
            lms_debug("check_task: process list inconsistency detected.\n");
            lms_global_exit(LMS_INCONSISTENT);
        }

        /* update to the next task in the process list. */
        ukva = (lms_ukva_t)list_entry(tp->tasks.next,
                                        struct task_struct, tasks);
        tp = lms_convert_ukva(ukva, "tp->tasks.next");
    } while (tp != init_task);

    /*
     * process list has ended.  PID of the target task did not found in the
     * list at all.  we will return LMS_CHECK_NG.
     */
    lms_debug("check_task: target did not found in process-list.\n");
    return LMS_CHECK_NG;
}


/*
 * lms_get_sched_entity() is a conceptual replica of 'getEntity()' function
 * defined in "check_rq.c" of original LMS implementation.  the original 
 * comment of this function is "get sched_entity which has task_struct
 * information".  unfortunately, I can't understand what the comment and
 * function means.
 */
static struct sched_entity *
lms_get_sched_entity(struct sched_entity *se)
{
    struct cfs_rq *cfs;
    struct rb_node *left;
    lms_ukva_t ukva;

    if (se != NULL) {
        ukva = (lms_ukva_t)se->my_q;

        while (ukva != (lms_ukva_t)NULL) {
            cfs = lms_convert_ukva(ukva, "se->my_q");

            ukva = (lms_ukva_t)cfs->rb_leftmost;
            if (ukva == (lms_ukva_t)NULL)
                break;

            left = lms_convert_ukva(ukva, "cfs->rb_leftmost");
            se = container_of(left, struct sched_entity, run_node);

            ukva = (lms_ukva_t)se->my_q;
        }
    }
    return se;
}

/*
 * lms_check_rbtree() is a conceptual replica of "check_rbtree()" function
 * defined in "check_rq.c" of original LMS implementation.
 */
static lms_result_t
lms_check_rbtree(struct rb_node *parent, struct rb_node *node,
                                struct lms_task_info *hidden)
{
    struct sched_entity *se, *backup;
    struct rb_node *left_node, *right_node;
    lms_result_t result;
    lms_ukva_t ukva;


    /* 'hidden' must be supplied properly by the caller. */
    if (hidden == NULL) {
        lms_err_printf("LMS: internal error, %s %i\n", __FILE__, __LINE__);
        lms_global_exit(LMS_INCONSISTENT);
    }

    lms_debug("check_rbtree: parent = %08p, node = %08p\n", parent, node);

    if (node == NULL) {
        /* specified 'node' seems leaf node.  it's legal so return OK. */
        return LMS_CHECK_OK;
    }

    se = container_of(node, struct sched_entity, run_node);

    ukva = (lms_ukva_t)node->rb_right;
    lms_debug("check_rbtree: right node ukva = %08x\n", ukva);
    right_node = (ukva) ? lms_convert_ukva(ukva, "node->rb_right") : NULL;

    ukva = (lms_ukva_t)node->rb_left;
    lms_debug("check_rbtree:  left node ukva = %08x\n", ukva);
    left_node = (ukva) ? lms_convert_ukva(ukva, "node->rb_left") : NULL;

    backup = lms_get_sched_entity(se);
    if (backup != NULL) {
        struct task_struct *tp;
        tp = container_of(backup, struct task_struct, se);

        if ((hidden->pid = tp->pid) >= 0) {
            result = lms_check_task(tp);
            if (result != LMS_CHECK_OK) {
                strncpy(hidden->comm, tp->comm, sizeof(hidden->comm));
                lms_debug("check_rbtree: task check failed, %d\n", result);
                return result;
            }
        }
    }

    result = lms_check_rbtree(node, right_node, hidden);
    if (result == LMS_CHECK_OK && parent != left_node) {
        result = lms_check_rbtree(parent, left_node, hidden);
    }
    return result;
}

/*
 * lms_check_rq() is a conceptual replica of "check_rq()" function defined in
 * "check_rq.c" of original LMS implementation.
 */
lms_result_t
lms_check_rq(struct lms_task_info *hidden)
{
    struct rq *rq;
    struct cfs_rq *cfs;
    struct rb_node *node;
    struct task_struct *tp;
    struct sched_entity *se;
    struct list_head *first_head;
    int rq_nr_running, cfs_nr_running;
    lms_result_t result;
    lms_ukva_t ukva;

    lms_verbose("check_rq: starting, init_task = %08lx, per_cpu_rq = %08lx\n",
                                        init_task_ukva, per_cpu_rq_ukva);

    /* hidden task infomation should be supplied properly by the caller. */
    if (hidden == NULL) {
        lms_err_printf("LMS: internal error, %s %i\n", __FILE__, __LINE__);
        return LMS_INCONSISTENT;
    }

    /* setup global exit target of inconsistent case. */
    if ((result = (lms_result_t)setjmp(lms_global_exit_env)) != 0) {
        return result;
    }

    /* start at 'per_cpu__runqueues' pointer.  it must always be valid. */
int i;
#if 0 /* uni processor */
    ukva = per_cpu_rq_ukva;
#else /* SMP */
unsigned long runqueues = (unsigned long)per_cpu_rq_ukva;
unsigned long *__per_cpu_offset = lms_convert_ukva(per_cpu_offset_ukva, "__per_cpu_offset");
//for_each_cpu(i, 0xffff) {
for (i = 0; i < lms_nr_cpu_ids; i++) {
    //ukva = cpu_rq(i);
    ukva = runqueues + __per_cpu_offset[i];
#endif
    rq = lms_convert_ukva(ukva, "per_cpu_rq_ukva");
    rq_nr_running = rq->nr_running;
    cfs = &rq->cfs;
    lms_debug("check_rq: rq_nr_running = %d, cfs_nr_running = %d\n",
                                rq_nr_running, cfs->nr_running);

    /*
     * PHASE1: check current runinng task linked by 'rq->curr'.
     */
    ukva = (lms_ukva_t)rq->curr;
    if (ukva != (lms_ukva_t)NULL) {
        tp = lms_convert_ukva(ukva, "rq->curr");
        lms_verbose("check_rq: PHASE1: check rq->curr, ukva = %08x\n", ukva);

        if ((hidden->pid = tp->pid) >= 0) {
            result = lms_check_task(tp);
            if (result != LMS_CHECK_OK) {
                strncpy(hidden->comm, tp->comm, sizeof(hidden->comm));
                hidden->cpuid = i;
                lms_debug("check_rq: PHASE1: task check failed, %d\n", result);
                return result;
            }
        }
        if (rq->nr_running != rq_nr_running) {
            lms_debug("check_rq: PHASE1: inconsistent rq_nr_running "
                                "%d != %d\n", rq_nr_running, rq->nr_running);
            return LMS_INCONSISTENT;
        }
    }

    /*
     * PHASE2: traverse sched_entity tree linked from current CFS runqueue.
     */
    ukva = (lms_ukva_t)cfs->tasks_timeline.rb_node;
    if (ukva != (lms_ukva_t)NULL) {
        node = lms_convert_ukva(ukva, "cfs->tasks_timeline.rb_node");
        lms_verbose("check_rq: PHASE2: check cfs->tasks_timeline.rb_node, "
                                "ukva = %08x\n", ukva);

        result = lms_check_rbtree(NULL, node, hidden);
        if (result != LMS_CHECK_OK) {
            lms_debug("check_rq: PHASE2: timeline check failed, %d\n", result);
            return result;
        }
        if (rq->nr_running != rq_nr_running) {
            lms_debug("check_rq: PHASE2: inconsistent rq_nr_running "
                                "%d != %d\n", rq_nr_running, rq->nr_running);
            return LMS_INCONSISTENT;
        }
    }

    /*
     * PHASE3: traverse entier leaf CFS runqueue list.
     */
    first_head = &rq->leaf_cfs_rq_list;
    ukva = (lms_ukva_t)first_head->next;
    cfs = NULL;
    if (ukva != (lms_ukva_t)NULL) {
        ukva = (lms_ukva_t)list_entry((void *)ukva,
                                        struct cfs_rq, leaf_cfs_rq_list);
        cfs = lms_convert_ukva(ukva, "rq->leaf_cfs_rq_list");
    }

    if (lms_is_verbose() && cfs != NULL)
        lms_verbose("check_rq: PHASE3: check rq->leaf_cfs_rq_list.next, "
                                "ukva = %08x\n", ukva);

    while (cfs != NULL && &cfs->leaf_cfs_rq_list != first_head) {
        cfs_nr_running = cfs->nr_running;
        lms_debug("check_rq: PHASE3: cfs ukva = %08x, cfs_nr_running = %d\n",
                                ukva, cfs_nr_running);

        /* check current running task linked from the cfs. */
        ukva = (lms_ukva_t)cfs->curr;
        if (ukva != (lms_ukva_t)NULL) {
            se = lms_convert_ukva(ukva, "cfs->curr");
            lms_debug("check_rq: PHASE3-1: cfs->curr ukva = %08x\n", ukva);

            se = lms_get_sched_entity(se);
            if (se != NULL) {
                tp = container_of(se, struct task_struct, se);

                if ((hidden->pid = tp->pid) >= 0) {
                    result = lms_check_task(tp);
                    if (result != LMS_CHECK_OK) {
                        strncpy(hidden->comm, tp->comm, sizeof(hidden->comm));
                        lms_debug("check_rq: PHASE3-1: "
                                "current task check failed, %d\n", result);
                        return result;
                    }
                }
                if (rq->nr_running != rq_nr_running ||
                    cfs->nr_running != cfs_nr_running) {
                    if (lms_is_debug() && rq->nr_running != rq_nr_running) {
                        lms_debug("check_rq: PHASE3-1: inconsistent "
                                        "rq_nr_running %d != %d\n",
                                        rq_nr_running, rq->nr_running);
                    }
                    if (lms_is_debug() && cfs->nr_running != cfs_nr_running) {
                        lms_debug("check_rq: PHASE3-1: inconsistent "
                                        "cfs_nr_running %d != %d\n",
                                        cfs_nr_running, cfs->nr_running);
                    }
                    return LMS_INCONSISTENT;
                }
            }
        }

        /* traverse sched_entity tree linked from the cfs. */
        ukva = (lms_ukva_t)cfs->tasks_timeline.rb_node;
        if (ukva != (lms_ukva_t)NULL) {
            node = lms_convert_ukva(ukva, "cfs->tasks_timeline.rb_node");
            lms_debug("check_rq: PHASE3-2: cfs->tasks_timeline.rb_node "
                                "ukva = %08x\n", ukva);

            result = lms_check_rbtree(NULL, node, hidden);
            if (result != LMS_CHECK_OK) {
                lms_debug("check_rq: PHASE3-2: "
                                "rb-tree check failed, %d\n", result);
                return result;
            }
            if (rq->nr_running != rq_nr_running ||
                cfs->nr_running != cfs_nr_running) {
                if (lms_is_debug() && rq->nr_running != rq_nr_running) {
                    lms_debug("check_rq: PHASE3-2: inconsistent "
                                    "rq_nr_running %d != %d\n",
                                    rq_nr_running, rq->nr_running);
                }
                if (lms_is_debug() && cfs->nr_running != cfs_nr_running) {
                    lms_debug("check_rq: PHASE3-2: inconsistent "
                                    "cfs_nr_running %d != %d\n",
                                    cfs_nr_running, cfs->nr_running);
                }
                return LMS_INCONSISTENT;
            }
        }

        /* update to the next CFS in the leaf list. */
        ukva = (lms_ukva_t)cfs->leaf_cfs_rq_list.next;
        cfs = NULL;
        if (ukva != (lms_ukva_t)NULL) {
            ukva = (lms_ukva_t)list_entry((void *)ukva,
                                        struct cfs_rq, leaf_cfs_rq_list);
            cfs = lms_convert_ukva(ukva, "cfs->leaf_cfs_rq_list");
        }
    }
#if 1 /* SMP */
}
#endif

    /*
     * all done, there is no hidden process nor data structure inconsistency 
     * have been found.
     */
    lms_verbose("check_rq: passed all check.\n");
    return LMS_CHECK_OK;
}

/*
 * lms_show_proc_list() is a supplementary function to show current process
 * list of target guest OS from the observer.  this is very useful for
 * debugging to observe runing process status of target guest OS.
 */
lms_result_t
lms_show_proc_list(void)
{
    struct task_struct *init_task;
    struct task_struct *tp;
    lms_ukva_t ukva;
    lms_result_t result;

    /* setup global exit target of inconsistent case. */
    if ((result = (lms_result_t)setjmp(lms_global_exit_env)) != 0) {
        return result;
    }

    /* loop is started from 'init_task'. */
    ukva = init_task_ukva;
    tp = init_task = lms_convert_ukva(ukva, "init_task_ukva");

    /* show the header */
    lms_printf("%-5s %-5s %-4s %-8s %s\n",
                "PID", "TGID", "STAT", "FLAGS", "COMM");

    do {
        /* show pid, number of threads, state, and command line string. */
        lms_printf("%5d %5d %4d %08x %s\n",
                tp->pid, tp->tgid, tp->state, tp->flags, tp->comm);

        /* process list should be cyclic bi-directional list. */
        if (tp->tasks.next == NULL) {
            return LMS_INCONSISTENT;
        }

        /* update to the next task in the process list. */
        ukva = (lms_ukva_t)list_entry(tp->tasks.next,
                                        struct task_struct, tasks);
        tp = lms_convert_ukva(ukva, "tp->tasks.next");
    } while (tp != init_task);

    return LMS_CHECK_OK;
}
