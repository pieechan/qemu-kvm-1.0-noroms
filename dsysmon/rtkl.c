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
 * rtkl.c:
 *	A implementation of RootkitLibra (RTKL)
 *
 *	The RTKL detects a rootkit in the NFS mounted directory by
 *	comparing the two views.
 *
 *	- trusted view.
 *	- untrusted view.
 *
 *	A trusted view is collected by sniffing NFS packets. A untrusted
 *	view is collected by hooking User OS's system call.  The data
 *	that collect is follows.
 *
 *	- file      ... inode number and file size
 *	- directory ... inode number and number of files in the directory.
 *
 *	For example, the RTKL warns when it is difference in a file size
 *	of trusted view and a file size of untrusted view.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <sched.h>
#include <sys/types.h>

#include "rtkl_defs.h"
#include "rtkl_rpc.h"


/* display whether or not rtkl outputs debug purpose messages too. A
 * default value is false. This means that rtkl do not display any debug
 * purpose messages. */
bool rtkl_debug = false;

/* display whether or not rtkl outputs verbose messages too. A default
 * value is false. This means that rtkl do not display any verbose
 * messages. */
bool rtkl_verbose = false;

/* This value specifies interval of the checking whether or not hided
 * file or file that modified file size exists. A default value is
 * 5000000.  This means that check every 5 sec. */
ulong rtkl_check_interval = 5 * 1000 * 1000;

/*
 * in the cooperation mode, we have an external channel to communicate with
 * the controller (typically GUI) program.
 */
FILE *rtkl_ext_ctl = NULL;
FILE *rtkl_ext_msg = NULL;


struct rtkl_pktq rtkl_pktq_head;
struct rtkl_strm rtkl_strm_head;
struct rtkl_tbl  rtkl_table;

struct rtkl_mlist rtkl_mlist_head;
struct rtkl_nfs_dirs rtkl_nfs_dirs;


// currently max number of rpc_parsing_task is 8.
static dsm_task_t rtkl_tcp_parsing_task;
static dsm_task_t rtkl_rpc_parsing_task[8];
static int rtkl_rpc_parsing_task_num = 0;
static dsm_task_t rtkl_sys_collect_task;
static dsm_task_t rtkl_tbl_check_task;


static int
rtkl_in_hook (dsm_dev_t type __UNUSED, dsm_io_t input, int index __UNUSED,
	      struct iovec* iov, size_t len)
{
	/* packets to USER OS. */
	return rtkl_pkt_filtering (input, iov, len);
}

static int
rtkl_out_hook (dsm_dev_t type __UNUSED, dsm_io_t output, int index __UNUSED,
	       struct iovec* iov, size_t len)
{
	/* packets from USER OS. */
	return rtkl_pkt_filtering (output, iov, len);
}

static bool
rtkl_hook_create (dsm_io_t io, struct rtkl_pktf* filt, void* hook)
{
	struct rtkl_pkt_strm* p;

	/* check whether or not duplicate stream is registered. */
	LIST_FOREACH (p, &rtkl_strm_head, next) {
		if (p->type == io) {
			return false;
		}
	}

	/* register new stream.
	 * Currently this is input stream or ouput stream only.
	 * ;; DSM_IO_INPUT or DSM_IO_OUTPUT */

	p = (struct rtkl_pkt_strm*) xmalloc (sizeof (struct rtkl_pkt_strm));
	p->type = io;

	TAILQ_INIT (&(p->list.tcp));
	TAILQ_INIT (&(p->tmps.tcp));

	p->list.sseq = 0;
	p->tmps.sseq = 0;

	p->list.lseq = 0;
	p->tmps.lseq = 0;

	p->list.nseq = 0;
	p->tmps.nseq = 0;

	if (sem_init (&(p->list.lock), 0, 1) < 0) {
		perror ("sem_init");
		goto failed;
	}

	if (sem_init (&(p->list.cond), 0, 0) < 0) {
		perror ("sem_init");
		goto failed;
	}

	p->list.waiting = 0;

	p->filt = *filt;

	LIST_INSERT_HEAD (&rtkl_strm_head, p, next);

	dsm_register_io_hook (DSM_DEV_NETWORK, io, 0, hook);

	return true;

failed:
	xfree (p);
	return false;
}

struct rtkl_pkt_strm*
rtkl_hook_strm_get (dsm_io_t type)
{
	struct rtkl_pkt_strm* p;

	LIST_FOREACH (p, &rtkl_strm_head, next) {
		if (p->type == type) {
			return p;
		}
	}

	return NULL;
}

static void
rtkl_setup_external_channel(void)
{
	int fd_ctl, fd_msg;
	FILE *fp_ctl, *fp_msg;

	if (! cooperation)
		return;

	if (dsm_get_coop_channel("rtkl", &fd_ctl, &fd_msg) < 0) {
		fprintf(stderr, "RTKL: couldn't get external channel.\n");
		return;
	}

	fp_ctl = fdopen(fd_ctl, "r");
	fp_msg = fdopen(fd_msg, "w");
	if (fp_ctl == NULL || fp_msg == NULL) {
		fprintf(stderr, "RTKL: couldn't reassign external channel.\n");
		return;
	}
	rtkl_ext_ctl = fp_ctl;
	rtkl_ext_msg = fp_msg;
	return;
}

void
rtkl_init (int argc, char** argv)
{
	static struct option opts[] = {
		{ "rtkl",		no_argument,		NULL, 'R' },
		{ "rtkl-interval",	required_argument,	NULL, 'c' },
		{ "rtkl-verbose",	no_argument,		NULL, 'V' },
		{ "rtkl-debug",		no_argument,		NULL, 'D' },
		{ NULL },
	};

	/* filter from NFS port number (2049). */
	struct rtkl_pktf filt[] = {
		[DSM_IO_INPUT]  = {0, 2049, 0, 0},
		[DSM_IO_OUTPUT] = {0, 0, 0, 2049},
	};

    /* suppress unknown option message by getopt(). */
    opterr = 0;

	int c;
	while ((c = getopt_long (argc, argv, "+", opts, NULL)) != EOF) {
		switch (c) {
		case 'c':
			/* Specified by mili seconds. */
			rtkl_check_interval = string_to_int (optarg,
							     10) * 1000;
			break;
		case 'D':
			rtkl_debug = true;
			break;
		case 'V':
			rtkl_verbose = true;
			break;
		default:
			break;
		}
	}
	rtkl_verbose ("%s: rtkl_check_interval = %lu\n", __FUNCTION__,
		      rtkl_check_interval);

	/* Setup external channel. */
	rtkl_setup_external_channel();

	/* If we are in the cooperation mode, disable automatic cheking. */
	if (cooperation) {
		rtkl_tbl_is_check = false;
	}


	TAILQ_INIT (&(rtkl_pktq_head.queue));

	/* semaphore for access of the queue. */
	if (sem_init (&(rtkl_pktq_head.lock), 0, 1) < 0) {
		perror ("sem_init");
		return;
	}

	/* semaphore for condition of the queue */
	if (sem_init (&(rtkl_pktq_head.cond), 0, 0) < 0) {
		perror ("sem_init");
		return;
	}


	LIST_INIT (&rtkl_strm_head);

	/* hook any packets to USER OS. */
	if (rtkl_hook_create (DSM_IO_INPUT, &filt[DSM_IO_INPUT],
			      rtkl_in_hook) == false) {
		fprintf (stderr,
			 "RTKL: failed to register a hook rtkl_in_hook\n.");
	}

	/* hook any packets from USER OS. */
	if (rtkl_hook_create (DSM_IO_OUTPUT, &filt[DSM_IO_OUTPUT],
			      rtkl_out_hook) == false) {
		fprintf (stderr,
			 "RTKL: failed to register a hook rtkl_out_hook\n.");
	}

	/* lining up saved packet in order to sequnece number. */
	rtkl_tcp_parsing_task.rc = -1;
	rtkl_tcp_parsing_task.func = rtkl_tcp_parsing;
	dsm_create_task (&rtkl_tcp_parsing_task, NULL);


	/* create RPC parsing task for each IO. */
	{
		struct rtkl_pkt_strm* p;

		LIST_INIT (&rtkl_mlist_head.call);
		if (sem_init (&(rtkl_mlist_head.sync), 0, 1) < 0) {
			perror ("sem_init");
			return;
		}

		if (sem_init (&(rtkl_mlist_head.reply_xid_sync), 0, 1) < 0) {
			perror ("sem_init");
			return;
		}

		rtkl_rpc_parsing_task_num = 0;
		LIST_FOREACH (p, &rtkl_strm_head, next) {
			/* Pull segments form TCP steream, and if that
			 * segment was RPC, parse that segments. */
			dsm_task_t *rpc_task = &(rtkl_rpc_parsing_task[rtkl_rpc_parsing_task_num]);
			rpc_task->rc = -1;
			rpc_task->func = rtkl_rpc_parsing;
			dsm_create_task (rpc_task, (void*)p->type);
			rtkl_rpc_parsing_task_num++;
		}
	}

	/* collecing RTKL untrusted root. */
	TAILQ_INIT (&rtkl_table.table);
	if (sem_init (&(rtkl_table.sync), 0, 1) < 0) {
		perror ("sem_init");
		return;
	}

	rtkl_sys_collect_task.rc = -1;
	rtkl_sys_collect_task.func = rtkl_sys_collect;
	dsm_create_task(&rtkl_sys_collect_task, NULL);

	/* check RTKL trust/untrust table */
	rtkl_tbl_check_task.rc = -1;
	rtkl_tbl_check_task.func = rtkl_tbl_check;
	dsm_create_task(&rtkl_tbl_check_task, (void *)rtkl_check_interval);

	return;
}

void
rtkl_usage (void)
{
	printf ("RTKL options:\n"
		"--rtkl\n"
		"      select RTKL application.\n"
		"--rtkl-interval=<ms>\n"
		"      specify check interval (ms) of file/directory table.\n"
		);
	return;
}

void
rtkl_terminate(void)
{
    int i;

    dsm_unregister_io_hook(DSM_DEV_NETWORK, DSM_IO_INPUT, 0);
    dsm_unregister_io_hook(DSM_DEV_NETWORK, DSM_IO_OUTPUT, 0);

    dsm_cancel_task(&rtkl_tcp_parsing_task);
    for (i = 0; i < rtkl_rpc_parsing_task_num; i++) {
        dsm_cancel_task(&(rtkl_rpc_parsing_task[i]));
    }
    dsm_cancel_task(&rtkl_sys_collect_task);
    dsm_cancel_task(&rtkl_tbl_check_task);
}

/* DSM application definition and registration. */
static struct dsm_application rtkl_application = {
	.name	    = "RootKitLibra",
	.selector  = "rtkl",
	.init	    = rtkl_init,
	.usage	    = rtkl_usage,
	.terminate = rtkl_terminate,
};

static void __attribute__ ((constructor))
rtkl_init_module (void)
{
	dsm_register_application (&rtkl_application);
}

/* rtkl.c ends here */
