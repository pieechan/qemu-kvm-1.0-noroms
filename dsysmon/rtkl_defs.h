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
 */


#ifndef RTKL_DEFS_H
#define RTKL_DEFS_H

#include <netinet/tcp.h>
#include <sys/queue.h>
#include <semaphore.h>

#include "dsm_api.h"

/* This structre save 1 raw packet entry. */
struct rtkl_pkt {
	dsm_io_t	type;
	char*		data;
	u_int32_t	len;
	TAILQ_ENTRY (rtkl_pkt) next;
};

TAILQ_HEAD (rtkl_pkts, rtkl_pkt);

struct rtkl_pktq {
	sem_t			lock;
	sem_t			cond;
	struct rtkl_pkts	queue;
};

/* packet filter definition for RTKL. */
struct rtkl_pktf {
	u_int32_t src_ip;
	u_int32_t src_port;
	u_int32_t dst_ip;
	u_int32_t dst_port;
};

/* This structure save 1 tcp segment entry. A parameter head is
 * information of the tcp header. A parameter data stores tcp data
 * part. A parameter dlen is length of the tcp data part. */
struct rtkl_tcp {
	struct tcphdr	head;
	char*		data;
	u_int32_t	dlen;
	TAILQ_ENTRY (rtkl_tcp) next;
};

TAILQ_HEAD (rtkl_tcp_pkts, rtkl_tcp);

struct rtkl_tcp_strm {
	struct rtkl_tcp_pkts	tcp;
	/* start sequence number */
	u_int32_t		sseq;
	/* last sequence number */
	u_int32_t		lseq;
	/* next sequence number */
	u_int32_t		nseq;
	/* semaphore */
	sem_t			lock;
	sem_t			cond;
	int			waiting;
};

struct rtkl_pkt_strm {
	dsm_io_t		type;
	struct rtkl_tcp_strm	list;
	struct rtkl_tcp_strm	tmps;
	struct rtkl_pktf	filt;
	LIST_ENTRY (rtkl_pkt_strm) next;
};

LIST_HEAD (rtkl_strm, rtkl_pkt_strm);

/* file or directory trust/untrust information of the RTKL. */
struct rtkl_tbl_ent_info {
	/* file or directory size. */
	long size;
	/* number of files if directory. 0 if file. */
	long dir_item;
};

/* 1 entry of the RTKL trust/untrst information table. */
struct rtkl_tbl_ent {
	/* inode number */
	long ino;

	/* file or directory information by trust root. */
	struct rtkl_tbl_ent_info trust;

	/* file or directory information by untrust root. */
	struct rtkl_tbl_ent_info untrust;

	/* A flag parameter shows status of the trust member and untrust
	 * member. The meaning of this parameter follows.
	 * ---
	 * | |  ... file or directory (0: file, 1: directory)
	 * |-|
	 * | |  ... trusted file size (0: not fill, 1: filled)
	 * |-|
	 * | |  ... untrsuted file size (0: not fill, 1: filled)
	 * |-|
	 * | |  ... trusted  dir_item (0: not fill, 1: filled)
	 * |-|
	 * | |  ... untrusted dir_item (0: not fill, 1: filled)
	 * ---
	 *  */
	int flag;

	TAILQ_ENTRY(rtkl_tbl_ent) next;
};

TAILQ_HEAD(rtkl_tbl_entq, rtkl_tbl_ent);

/* RTKL trust/untrust information table. */
struct rtkl_tbl {
	struct rtkl_tbl_entq	table;
	sem_t			sync;
};


/* Debug purpose variables */
extern bool rtkl_debug;
extern bool rtkl_verbose;

/* External channel */
extern FILE *rtkl_ext_ctl;
extern FILE *rtkl_ext_msg;

/* The interval to check for rootkits. (Unit: mili second)*/
extern ulong rtkl_check_interval;

/* this variable whether to check integrity of the RTKL trust/unstrust
 * table. */
extern bool rtkl_tbl_is_check;

/* The list for saving recieved packets directly. */
extern struct rtkl_pktq rtkl_pktq_head;

/* The list for saving tcp stream in order to sequnce number.
 * This list has both of input and output stream. */
extern struct rtkl_strm rtkl_strm_head;

/* RTKL trust/untrust information table.

 * This table stores trust and untrust information (file or directory
 * size, number of files) of the file or directory.  A trust information
 * comes from parsing NFS packets. A untrust information comes from
 * parsing system calls. */
extern struct rtkl_tbl  rtkl_table;


/* malloc safely. If memory was not able to allocate, exit this
 * program. */
void* xmalloc (size_t);

/* realloc safely. If 1st argument is NULL, do xmalloc. If memory was
 * not able to allocate, exit this program by EXIT_FAILURE. */
void* xrealloc (void*, size_t);

/* free safely. Check NULL and free. */
void xfree (void*);

/* convert string to integer. A first argument is character string to
 * convert. A second argument is base. For example, the base was 10,
 * this function treates as a number in 10 base. */
int32_t string_to_int (char*, int32_t);

/* extract 8bit integer from character data. */
int8_t extract_8bits (char*);

/* extract 16bit integer from character data. */
int16_t extract_16bits (char*);

/* extract 32bit integer from character data. */
int32_t extract_32bits (char*);

/* get specified (input or output) TCP stream. */
struct rtkl_pkt_strm* rtkl_hook_strm_get (dsm_io_t);

/* Read specified bytes from TCP stream that was preserved to the list
 * in advance. A first agument is type (input or output). This is
 * specified either DSM_IO_INPUT or DSM_IO_OUTPUT by default. A second
 * argument is offset to read. A third argument is byte to read. this
 * function returns malloced char string. So, you have to free when that
 * space became unnecessary. */
char* rtkl_tcp_strm_get (dsm_io_t, u_int32_t, u_int32_t);

/* Delete (free) specified bytes from TCP stream that was preserved to
 * the list in advance. A first argument is type (input or output). A
 * sencod argument is byte number to delete (free). If deleting was
 * succeeded, this functin returns true. If failed, returns false. */
bool rtkl_tcp_strm_remove (dsm_io_t, u_int32_t);

/* filter recived packet to only TCP segment. The result of this
 * function is save to the list rtkl_pktq_head. */
int rtkl_pkt_filtering (dsm_io_t, struct iovec*, size_t);

/* check the list rtkl_pktq_head, and lining up the packets in the order
 * of sequence number. The result of this function is saved to the list
 * rtkl_strm_head. */
int rtkl_tcp_parsing (void*);

/* pull segments from TCP stream list (rtkl_strm_head), and If pulled
 * segment was RPC data, reassemble TCP segment and parse RPC data. */
int rtkl_rpc_parsing (void*);

/* collecting RTKL untrusted root from User OS's system call hooking. */
int rtkl_sys_collect (void*);

/* check fifo status. */
int rtkl_tbl_ctrl_loop (void*);

/* check RTKL trust/untrust table information. */
int rtkl_tbl_check (void*);

/* set RTKL file data (inode number, file size) to rtkl_tbl. */
void rtkl_tbl_file_set (long, long, bool);

/* set RTKL directory data (inode number, directory size, number of
 * files in directory) to rtkl_tbl. */
void rtkl_tbl_dir_set (long, long, long, bool);

void rtkl_tbl_clear (void);


/* Debug purpose MACRO functions. */
#define rtkl_debug(args...) \
	do { if (rtkl_debug) fprintf(stderr, args); } while(0)

#define rtkl_verbose(args...) \
	do { if (rtkl_verbose) fprintf(stderr, args); } while(0)


#if !defined(__UNUSED)
#define __UNUSED __attribute__((unused))
#endif

#endif

#define STRING_EQL(s1, s2)						\
	((strncmp ((s1), (s2), strlen (s1)) == 0) &&			\
	 (strlen (s1) == strlen (s2)))


extern void rtkl_init (int, char**);
extern void rtkl_usage (void);
extern void rtkl_terminate (void);

/* rtkl_defs.h ends here. */
