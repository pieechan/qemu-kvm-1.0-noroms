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
 * lms_defs.h:
 *        definition of LMS application internal symbols and functions.
 */
#ifndef _LMS_DEFS_H
#define _LMS_DEFS_H


/*
 * enumeration lms_result_t is used to indicate a result of task checking.
 *
 * LMS_CHECK_OK:
 *        examination has been completed and any dishonesty nor inconsistency
 *        has been detected.
 *
 * LMS_CHECK_NG:
 *        some dishonesty has been detected during the examination.  intruder
 *        may be cheating kernel data structures.
 *
 * LMS_INCONSISTENT:
 *        some data structure inconsistency has been detected during the
 *        examination.  the attempt should be ignored.
 */
typedef enum {
    LMS_CHECK_OK        = 0,
    LMS_CHECK_NG        = 1,
    LMS_INCONSISTENT        = 2,
} lms_result_t;

/*
 * we have defined lms_pid_t for the process id number returned from LMS kernel
 * part to user-land part as the result of the monitoring service.  also it
 * must be exactly same as type pid_t used by the user-land implementation.
 */
typedef int lms_pid_t;

/*
 * lms_task_info structure is also used to pass information for hidden process 
 * from LMS kernel part to user-land part.  currently process ID and command
 * string are useful.
 */
struct lms_task_info {
    lms_pid_t pid;
    char comm[32];
    int cpuid;
};


/*
 * kernel virtual address of User Guest OS will be represented by lms_ukva_t
 * in LMS kernel part implementation, because LMS kernel part should be written
 * without direct reference to the DSM-API header files.  it must be exactly
 * same as the type ukvaddr_t defined by DSM-API.
 */
typedef unsigned long lms_ukva_t;


/*
 * two dedicated kernel virtual address of User Guest OS, 'init_task' and
 * 'per_cpu__runqueues' should be determined by LMS user land part and stored
 * into follwing global variables.
 */
extern lms_ukva_t init_task_ukva;
extern lms_ukva_t per_cpu_rq_ukva;

// for SMP
extern lms_ukva_t per_cpu_offset_ukva;
extern lms_ukva_t nr_cpu_ids_ukva;
extern int lms_nr_cpu_ids;

/*
 * these functions are the wrapper of existing DSM-APIs, implemented in the
 * LMS user-land part and exported to the LMS kernel part.  this is required
 * because LMS kernel part can not use any user-land APIs directly.
 */
extern int lms_is_verbose(void);
extern int lms_is_debug(void);
extern void lms_printf(const char *fmt, ...);
extern void lms_err_printf(const char *fmt, ...);
extern void *lms_map_ukva(lms_ukva_t);
extern int lms_check_ukva(lms_ukva_t);


/*
 * monitoring service function implemented in the LMS kernel part.  this
 * function may be called periodically from LMS user-land part.  it will
 * examine run-queue and process-list of User Guest OS kernel and attempt to
 * detect any dishonesty, typically intentional hidden process.
 */
extern lms_result_t lms_check_rq(struct lms_task_info *hidden);


/*
 * we have alternative service function lms_show_proc_list() intended for
 * debugging.  this function may list all processes lived in the target OS
 * like 'ps' command does.
 */
extern lms_result_t lms_show_proc_list(void);


extern void lms_init(int, char**);
extern void lms_usage(void);
extern void lms_terminate(void);

#endif /* _LMS_DEFS_H */
