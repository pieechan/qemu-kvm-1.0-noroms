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
 * rtkl_nfs.c:
 *	A implementation of RootkitLibra (RTKL)
 */

#include <string.h>
#include <sys/queue.h>
#include <unistd.h>

#include "dsm_api.h"
#include "rtkl_defs.h"

/* RTKL trusted and untrusted file/directory table */
//extern struct rtkl_tbl	rtkl_table;

/* If rtkl_tbl_is_check is set to true, RTKL table check task will  check
 * file system dishonesty periodically. */
bool rtkl_tbl_is_check = true;


/* contol for parameter flag of struct rtkl_tbl_entry */
#define RTKL_TBL_FILE		0x0
#define RTKL_TBL_DIR		0x1
#define RTKL_TBL_TRUST_SIZE	0x2
#define RTKL_TBL_UNTRUST_SIZE	0x4
#define RTKL_TBL_TRUST_DITEM	0x8
#define RTKL_TBL_UNTRUST_DITEM	0x10

static inline void
rtkl_tbl_trust_filesize_set (struct rtkl_tbl_ent* np, long size)
{
	np->trust.size	= size;
	np->flag       |= RTKL_TBL_FILE + RTKL_TBL_TRUST_SIZE;
}

static inline void
rtkl_tbl_untrust_filesize_set (struct rtkl_tbl_ent* np, long size)
{
	np->untrust.size  = size;
	np->flag	 |= RTKL_TBL_FILE + RTKL_TBL_UNTRUST_SIZE;
}

static inline void
rtkl_tbl_trust_dirsize_set (struct rtkl_tbl_ent* np, long size)
{
	np->trust.size	= size;
	np->flag       |= RTKL_TBL_DIR + RTKL_TBL_TRUST_SIZE;
}

static inline void
rtkl_tbl_untrust_dirsize_set (struct rtkl_tbl_ent* np, long size)
{
	np->untrust.size  = size;
	np->flag	 |= RTKL_TBL_DIR + RTKL_TBL_UNTRUST_SIZE;
}

static inline void
rtkl_tbl_trust_diritem_set (struct rtkl_tbl_ent* np, long num)
{
	np->trust.dir_item = num;
	np->flag |= RTKL_TBL_DIR + RTKL_TBL_TRUST_DITEM;
}

static inline void
rtkl_tbl_untrust_diritem_set (struct rtkl_tbl_ent* np, long num)
{
	np->untrust.dir_item = num;
	np->flag |= RTKL_TBL_DIR + RTKL_TBL_UNTRUST_DITEM;
}

static inline void
rtkl_tbl_filesize_set (struct rtkl_tbl_ent* np, long size, bool is_trust)
{
	if (size > 0) {
		if (is_trust) {
			rtkl_tbl_trust_filesize_set (np, size);
		} else {
			rtkl_tbl_untrust_filesize_set (np, size);
		}
	}
}

static inline void
rtkl_tbl_dirsize_set (struct rtkl_tbl_ent* np, long size, bool is_trust)
{
	if (size > 0) {
		if (is_trust) {
			rtkl_tbl_trust_dirsize_set (np, size);
		} else {
			rtkl_tbl_untrust_dirsize_set (np, size);
		}
	}
}

static inline void
rtkl_tbl_diritem_set (struct rtkl_tbl_ent* np, long num, bool is_trust)
{
	if (num > 0) {
		if (is_trust) {
			rtkl_tbl_trust_diritem_set (np, num);
		} else {
			rtkl_tbl_untrust_diritem_set (np, num);
		}
	}
}

static inline bool
rtkl_tbl_is_file_entry_filled (int flag)
{
	int size = RTKL_TBL_TRUST_SIZE + RTKL_TBL_UNTRUST_SIZE;

	if ((flag & 0x1) != RTKL_TBL_FILE) {
		return false;
	}

	if ((flag & size) == size) {
		return true;
	}

	return false;
}

static inline bool
rtkl_tbl_is_dir_entry_filled (int flag)
{
	int size = RTKL_TBL_TRUST_DITEM + RTKL_TBL_UNTRUST_DITEM;

	if ((flag & 0x1) != RTKL_TBL_DIR) {
		return false;
	}

	if ((flag & size) == size) {
		return true;
	}

	return false;
}

void
rtkl_tbl_file_set (long ino, long size, bool is_trust)
{
	struct rtkl_tbl_ent* np;

	if (sem_wait (&rtkl_table.sync) < 0) {
		perror ("sem_wait 31");
		return ;
	}

	if (TAILQ_EMPTY(&rtkl_table.table)) {
		goto entry_add;
	}

	TAILQ_FOREACH (np, &rtkl_table.table, next) {
		if (np->ino == ino) {
			rtkl_tbl_filesize_set (np, size, is_trust);
			goto exit;
		}
	}

entry_add:
	np = (struct rtkl_tbl_ent*) xmalloc (sizeof (struct rtkl_tbl_ent));
	np->ino = ino;
	np->flag = RTKL_TBL_FILE;
	rtkl_tbl_filesize_set (np, size, is_trust);
	TAILQ_INSERT_TAIL (&rtkl_table.table, np, next);

exit:
	if (sem_post (&rtkl_table.sync) < 0) {
		perror ("sem_post");
		return ;
	}

	return ;
}

void
rtkl_tbl_dir_set (long ino, long size, long dir_item, bool is_trust)
{
	struct rtkl_tbl_ent* np;

	if (sem_wait (&rtkl_table.sync) < 0) {
		perror ("sem_wait 32");
		return ;
	}

	if (TAILQ_EMPTY(&rtkl_table.table)) {
		goto entry_add;
	}

	TAILQ_FOREACH (np, &rtkl_table.table, next) {
		if (np->ino == ino) {
			rtkl_tbl_dirsize_set (np, size, is_trust);
			rtkl_tbl_diritem_set (np, dir_item, is_trust);
			goto exit;
		}
	}

entry_add:
	np = (struct rtkl_tbl_ent*) xmalloc (sizeof (struct rtkl_tbl_ent));
	np->ino = ino;
	np->flag = RTKL_TBL_DIR;
	rtkl_tbl_dirsize_set (np, size, is_trust);
	rtkl_tbl_diritem_set (np, dir_item, is_trust);
	TAILQ_INSERT_TAIL (&rtkl_table.table, np, next);

exit:
	if (sem_post (&rtkl_table.sync) < 0) {
		perror ("sem_post");
		return ;
	}

	return ;
}

void
rtkl_tbl_clear (void)
{
	struct rtkl_tbl_ent* np;
	struct rtkl_tbl_ent* np_next;

	if (sem_wait (&rtkl_table.sync) < 0) {
		perror ("sem_wait 33");
		return ;
	}

	for (np = rtkl_table.table.tqh_first; np != NULL; np = np_next) {
		np_next = TAILQ_NEXT (np, next);
		TAILQ_REMOVE (&rtkl_table.table, np, next);
		xfree (np);
		np = NULL;
	}

	if (sem_post (&rtkl_table.sync) < 0) {
		perror ("sem_post");
		return ;
	}
}

static void
rtkl_tbl_show (long ino)
{
	char type;
	long num1, num2;
	struct rtkl_tbl_ent* np;

	if (sem_wait (&rtkl_table.sync) < 0) {
		perror ("sem_wait 34");
		return ;
	}

	TAILQ_FOREACH (np, &rtkl_table.table, next) {
		if (np->ino == ino) {
			if ((np->flag & 0x1) == RTKL_TBL_DIR) {
				num1 = np->trust.dir_item;
				num2 = np->untrust.dir_item;
				type = 'D';
			} else {
				num1 = np->trust.size;
				num2 = np->untrust.size;
				type = 'F';
			}

			printf ("RTKL: TBL: [%c] %lu (%lu %lu) %d\n",
				type, np->ino, num1, num2, np->flag);
			break;
		}
	}

	if (sem_post (&rtkl_table.sync) < 0) {
		perror ("sem_post");
	}
}

static void
rtkl_tbl_show_all (void)
{
	char type;
	long num1, num2;
	struct rtkl_tbl_ent* np;

	if (sem_wait (&rtkl_table.sync) < 0) {
		perror ("sem_wait 35");
		return ;
	}

	TAILQ_FOREACH (np, &rtkl_table.table, next) {
		if ((np->flag & 0x1) == RTKL_TBL_DIR) {
			num1 = np->trust.dir_item;
			num2 = np->untrust.dir_item;
			type = 'D';
		} else {
			num1 = np->trust.size;
			num2 = np->untrust.size;
			type = 'F';
		}

		printf ("RTKL: TBL: [%c] %lu (%lu %lu) %d\n",
			type, np->ino, num1, num2, np->flag);
	}

	if (sem_post (&rtkl_table.sync) < 0) {
		perror ("sem_post");
	}
}

static void
rtkl_tbl_warn_file (unsigned long elps, struct rtkl_tbl_ent* np)
{
	const unsigned long elpsint = elps / 1000;
	const unsigned long elpsfrc = elps % 1000;
	FILE *fp = stdout;

	if (rtkl_ext_msg != NULL)
		fp = rtkl_ext_msg;

	fprintf (fp, "[%5lu.%03lu] RTKL: file size mismatch, "
		     "i-node %ld (nfs:%ld != sys:%ld)\n",
		     elpsint, elpsfrc,
		     np->ino, np->trust.size, np->untrust.size);
	fflush (fp);
	return;
}

static void
rtkl_tbl_warn_dir (unsigned long elps, struct rtkl_tbl_ent* np)
{
	const unsigned long elpsint = elps / 1000;
	const unsigned long elpsfrc = elps % 1000;
	FILE *fp = stdout;

	if (rtkl_ext_msg != NULL)
		fp = rtkl_ext_msg;

	fprintf (fp, "[%5lu.%03lu] RTKL: number of directory entry mismatch, "
		     "i-node %ld (nfs:%ld != sys:%ld)\n",
		     elpsint, elpsfrc,
		     np->ino, np->trust.dir_item, np->untrust.dir_item);
	fflush (fp);
	return;
}

static int
rtkl_wait_for_something(unsigned long sleep_time)
{
	struct timeval tmout, *tvp;
	fd_set fdset;
	int nfds;

	nfds = 0;
	FD_ZERO(&fdset);

	/*
	 * if the external control channel is enabled, file descriptor of
	 * the channel should be added to the select() systemcall.
	 */
	if (rtkl_ext_ctl != NULL) {
		FD_SET(fileno(rtkl_ext_ctl), &fdset);
		nfds = fileno(rtkl_ext_ctl) + 1;
	}

	/*
	 * if table checking is currently stopped, timeout of the select()
	 * should be disabled and wait until some command to be reached on the
	 * control channel.  otherwise, timeout of select() should be set to
	 * the specified 'sleep_time' value.  note that 'sleep_time' is in
	 * micro-seconds.
	 */
	tvp = NULL;
	if (rtkl_tbl_is_check) {
		tmout.tv_sec  = sleep_time / (1000 * 1000);
		tmout.tv_usec = sleep_time % (1000 * 1000);
		tvp = &tmout;
	}

	/* call select() to wait for something happen, and return the result. */
	return select(nfds, &fdset, NULL, NULL, tvp);
}

static void
rtkl_process_ext_ctl(void)
{
	long ino;
	char linebuf[256];

	if (fgets(linebuf, sizeof(linebuf), rtkl_ext_ctl) == NULL) {
		/* there is no message to be read from control channel. */
		return;
	}

	/*
	 * following three commands are supported.
	 *
	 * "start"		start RTKL table checking.
	 * "stop"		stop RTKL table checking.
	 * "clear"		clear RTKL table contents.
	 */
	if (strncmp(linebuf, "start", strlen("start")) == 0) {
		verbose("RTKL: start table checking.\n");
		rtkl_tbl_is_check = true;
	}
	else
	if (strncmp(linebuf, "stop", strlen("stop")) == 0) {
		verbose("RTKL: stop table checking.\n");
		rtkl_tbl_is_check = false;
	}
	else
	if (strncmp(linebuf, "clear", strlen("clear")) == 0) {
		verbose("RTKL: clearing table contents.\n");
		bool tmp = rtkl_tbl_is_check;
		rtkl_tbl_is_check = false;
		rtkl_tbl_clear ();
		rtkl_tbl_is_check = tmp;
	}
	else if (sscanf (linebuf, "info %ld\n", &ino) == 1) {
		rtkl_tbl_show (ino);
	}
	else if (strncmp(linebuf, "showall", strlen("showall")) == 0) {
		rtkl_tbl_show_all ();
	}

	return;
}



/* Check file/directory Table.
 * If differnce was found, display a warning message. */
int
rtkl_tbl_check (void* arg)
{
	unsigned long		sleep_time = (unsigned long) arg;
	unsigned long		elps;
	struct rtkl_tbl_ent*	np;

	verbose("RTKL: table check %s, sleep time = %lu\n",
		rtkl_tbl_is_check ? "enabled" : "disabled", sleep_time);

	while (true) {
		if (rtkl_wait_for_something(sleep_time) > 0) {
			rtkl_process_ext_ctl();
			continue;
		}

		if (rtkl_tbl_is_check == false) {
			continue;
		}

		if (sem_wait (&rtkl_table.sync) < 0) {
			perror ("sem_wait 36");
			continue;
		}

		if (TAILQ_EMPTY (&rtkl_table.table)) {
			goto next_loop;
		}

		elps = dsm_elapsed_ms();

		TAILQ_FOREACH (np, &rtkl_table.table, next) {
			rtkl_debug ("check ino = %ld flag = %d\n",
				    np->ino, np->flag);

			if (rtkl_tbl_is_file_entry_filled (np->flag) &&
			    (np->trust.size != np->untrust.size)) {

				rtkl_tbl_warn_file (elps, np);
			} else if (rtkl_tbl_is_dir_entry_filled (np->flag) &&
				   (np->trust.dir_item !=
				    np->untrust.dir_item)) {

				rtkl_tbl_warn_dir (elps, np);
			}
		}

	next_loop:
		if (sem_post (&rtkl_table.sync) < 0) {
			perror ("sem_post");
		}
	}

	return 0;
}

/* rtkl_tbl.c ends here */
