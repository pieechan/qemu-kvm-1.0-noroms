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
 * lms_rq.h:
 *        structure definitions of Linux kernel runqueues, extracted from Linux
 *        2.6.32.28 "kernel/sched.c".  in order to get exactly same definition
 *        with the target linux kernel, <linux/autoconf.h> in the target Linux
 *        kernel build directory should be included before this file.
 */
#ifndef _LMS_RQ_H
#define _LMS_RQ_H

/*
 * This is the priority-queue data structure of the RT scheduling class:
 */
struct rt_prio_array {
        DECLARE_BITMAP(bitmap, MAX_RT_PRIO+1); /* include 1 bit for delimiter */
        struct list_head queue[MAX_RT_PRIO];
};

struct rt_bandwidth {
        /* nests inside the rq lock: */
        raw_spinlock_t                rt_runtime_lock;
        ktime_t                        rt_period;
        u64                        rt_runtime;
        struct hrtimer                rt_period_timer;
};

#include <linux/cgroup.h>

struct cfs_rq;

struct cfs_bandwidth {
#ifdef CONFIG_CFS_BANDWIDTH
        raw_spinlock_t lock;
        ktime_t period;
        u64 quota, runtime;
        s64 hierarchal_quota;
        u64 runtime_expires;

        int idle, timer_active;
        struct hrtimer period_timer, slack_timer;
        struct list_head throttled_cfs_rq;

        /* statistics */
        int nr_periods, nr_throttled;
        u64 throttled_time;
#endif
};

/* task group related information */
struct task_group {
        struct cgroup_subsys_state css;

#ifdef CONFIG_FAIR_GROUP_SCHED
        /* schedulable entities of this group on each cpu */
        struct sched_entity **se;
        /* runqueue "owned" by this group on each cpu */
        struct cfs_rq **cfs_rq;
        unsigned long shares;

        atomic_t load_weight;
#endif

#ifdef CONFIG_RT_GROUP_SCHED
        struct sched_rt_entity **rt_se;
        struct rt_rq **rt_rq;

        struct rt_bandwidth rt_bandwidth;
#endif

        struct rcu_head rcu;
        struct list_head list;

        struct task_group *parent;
        struct list_head siblings;
        struct list_head children;

#ifdef CONFIG_SCHED_AUTOGROUP
        struct autogroup *autogroup;
#endif

        struct cfs_bandwidth cfs_bandwidth;
};

/* CFS-related fields in a runqueue */
struct cfs_rq {
        struct load_weight load;
        unsigned long nr_running, h_nr_running;

        u64 exec_clock;
        u64 min_vruntime;
#ifndef CONFIG_64BIT
        u64 min_vruntime_copy;
#endif

        struct rb_root tasks_timeline;
        struct rb_node *rb_leftmost;

        struct list_head tasks;
        struct list_head *balance_iterator;

        /*
         * 'curr' points to currently running entity on this cfs_rq.
         * It is set to NULL otherwise (i.e when none are currently running).
         */
        struct sched_entity *curr, *next, *last, *skip;

#ifdef        CONFIG_SCHED_DEBUG
        unsigned int nr_spread_over;
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
        struct rq *rq;        /* cpu runqueue to which this cfs_rq is attached */

        /*
         * leaf cfs_rqs are those that hold tasks (lowest schedulable entity in
         * a hierarchy). Non-leaf lrqs hold other higher schedulable entities
         * (like users, containers etc.)
         *
         * leaf_cfs_rq_list ties together list of leaf cfs_rq's in a cpu. This
         * list is used during load balance.
         */
        int on_list;
        struct list_head leaf_cfs_rq_list;
        struct task_group *tg;        /* group that "owns" this runqueue */

#ifdef CONFIG_SMP
        /*
         * the part of load.weight contributed by tasks
         */
        unsigned long task_weight;

        /*
         *   h_load = weight * f(tg)
         *
         * Where f(tg) is the recursive weight fraction assigned to
         * this group.
         */
        unsigned long h_load;

        /*
         * Maintaining per-cpu shares distribution for group scheduling
         *
         * load_stamp is the last time we updated the load average
         * load_last is the last time we updated the load average and saw load
         * load_unacc_exec_time is currently unaccounted execution time
         */
        u64 load_avg;
        u64 load_period;
        u64 load_stamp, load_last, load_unacc_exec_time;

        unsigned long load_contribution;
#endif
#ifdef CONFIG_CFS_BANDWIDTH
        int runtime_enabled;
        u64 runtime_expires;
        s64 runtime_remaining;

        u64 throttled_timestamp;
        int throttled, throttle_count;
        struct list_head throttled_list;
#endif
#endif
};

/* Real-Time classes' related field in a runqueue: */
struct rt_rq {
        struct rt_prio_array active;
        unsigned long rt_nr_running;
#if defined CONFIG_SMP || defined CONFIG_RT_GROUP_SCHED
        struct {
                int curr; /* highest queued rt task prio */
#ifdef CONFIG_SMP
                int next; /* next highest */
#endif
        } highest_prio;
#endif
#ifdef CONFIG_SMP
        unsigned long rt_nr_migratory;
        unsigned long rt_nr_total;
        int overloaded;
        struct plist_head pushable_tasks;
#endif
        int rt_throttled;
        u64 rt_time;
        u64 rt_runtime;
        /* Nests inside the rq lock: */
        raw_spinlock_t rt_runtime_lock;

#ifdef CONFIG_RT_GROUP_SCHED
        unsigned long rt_nr_boosted;

        struct rq *rq;
        struct list_head leaf_rt_rq_list;
        struct task_group *tg;
#endif
};

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the thread migration code), lock
 * acquire operations must be ordered by ascending &runqueue.
 */
struct rq {
        /* runqueue lock: */
        raw_spinlock_t lock;

        /*
         * nr_running and cpu_load should be in the same cacheline because
         * remote CPUs use both these fields when doing load calculation.
         */
        unsigned long nr_running;
        #define CPU_LOAD_IDX_MAX 5
        unsigned long cpu_load[CPU_LOAD_IDX_MAX];
        unsigned long last_load_update_tick;
#ifdef CONFIG_NO_HZ
        u64 nohz_stamp;
        unsigned char nohz_balance_kick;
#endif
        int skip_clock_update;

        /* capture load from *all* tasks on this cpu: */
        struct load_weight load;
        unsigned long nr_load_updates;
        u64 nr_switches;

        struct cfs_rq cfs;
        struct rt_rq rt;

#ifdef CONFIG_FAIR_GROUP_SCHED
        /* list of leaf cfs_rq on this cpu: */
        struct list_head leaf_cfs_rq_list;
#endif
#ifdef CONFIG_RT_GROUP_SCHED
        struct list_head leaf_rt_rq_list;
#endif

        /*
         * This is part of a global counter where only the total sum
         * over all CPUs matters. A task can increase this counter on
         * one CPU and if it got migrated afterwards it may decrease
         * it on another CPU. Always updated under the runqueue lock:
         */
        unsigned long nr_uninterruptible;

        struct task_struct *curr, *idle, *stop;
        unsigned long next_balance;
        struct mm_struct *prev_mm;

        u64 clock;
        u64 clock_task;

        atomic_t nr_iowait;

#ifdef CONFIG_SMP
        struct root_domain *rd;
        struct sched_domain *sd;

        unsigned long cpu_power;

        unsigned char idle_balance;
        /* For active balancing */
        int post_schedule;
        int active_balance;
        int push_cpu;
        struct cpu_stop_work active_balance_work;
        /* cpu of this runqueue: */
        int cpu;
        int online;

        u64 rt_avg;
        u64 age_stamp;
        u64 idle_stamp;
        u64 avg_idle;
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
        u64 prev_irq_time;
#endif
#ifdef CONFIG_PARAVIRT
        u64 prev_steal_time;
#endif
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
        u64 prev_steal_time_rq;
#endif

        /* calc_load related fields */
        unsigned long calc_load_update;
        long calc_load_active;

#ifdef CONFIG_SCHED_HRTICK
#ifdef CONFIG_SMP
        int hrtick_csd_pending;
        struct call_single_data hrtick_csd;
#endif
        struct hrtimer hrtick_timer;
#endif

#ifdef CONFIG_SCHEDSTATS
        /* latency stats */
        struct sched_info rq_sched_info;
        unsigned long long rq_cpu_time;
        /* could above be rq->cfs_rq.exec_clock + rq->rt_rq.rt_runtime ? */

        /* sys_sched_yield() stats */
        unsigned int yld_count;

        /* schedule() stats */
        unsigned int sched_switch;
        unsigned int sched_count;
        unsigned int sched_goidle;

        /* try_to_wake_up() stats */
        unsigned int ttwu_count;
        unsigned int ttwu_local;
#endif

#ifdef CONFIG_SMP
        struct llist_head wake_list;
#endif
};

#endif /* _LMS_RQ_H */
