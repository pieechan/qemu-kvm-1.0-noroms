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
 * rtkl_rpc.c:
 *	A implementation of RootkitLibra (RTKL)
 */

#ifndef RTKL_RPC_H
#define RTKL_RPC_H

#include <rpcsvc/nfs_prot.h>
#include <linux/nfs3.h>
#include <semaphore.h>

struct rtkl_rpc_data {
	char*		data;
	u_int32_t	dlen;
	u_int32_t	rpc_len;
};

struct rtkl_rpc_msg {
	u_int32_t length;
	u_int32_t xid;
	u_int32_t type;
};

struct rtkl_rpc_call_body
{
	u_int32_t rpc_ver;
	u_int32_t prog;
	u_int32_t ver;
	u_int32_t proc;
};

struct rtkl_rpc_call
{
	u_int32_t	xid;
	u_int32_t	prog;
	u_int32_t	ver;
	u_int32_t	proc;
	u_int64_t	cookie;
	u_int64_t	verf;
	int		count;
	LIST_ENTRY (rtkl_rpc_call) next;
};

LIST_HEAD (rtkl_rpc_call_list, rtkl_rpc_call);

struct rtkl_mlist
{
	struct rtkl_rpc_call_list	call;
	sem_t				sync;

	/* for syncing xid. In case, when I recieve rpc reply data
	 * previously, wait for that call comes. */

	/* xid that wait to come. */
	u_int32_t xid;

	/* semaphore */
	sem_t reply_xid_sync;
};

struct rtkl_rpc_reply_body
{
	u_int32_t reply_stat;
	u_int32_t accept_stat;
};

struct rtkl_nfs_file {
	char* name;
	LIST_ENTRY (rtkl_nfs_file) next;
};

LIST_HEAD (rtkl_nfs_files, rtkl_nfs_file);

struct rtkl_nfs_dir {
	long dino;
	struct rtkl_nfs_files files;
	LIST_ENTRY (rtkl_nfs_dir) next;
};

LIST_HEAD (rtkl_nfs_dirs, rtkl_nfs_dir);


enum rtkl_rpc_msg_type {
	RTKL_RPC_CALL  = 0,
	RTKL_RPC_REPLY = 1
};

enum rtkl_rpc_reply_stat {
	RTKL_RPC_REP_MSG_ACCEPTED = 0,
	RTKL_RPC_REP_MSG_DENIED   = 1,
};

enum rtkl_rpc_reply_accept_stat {
	RTKL_RPC_REP_SUCCESS       = 0,
	RTKL_RPC_REP_PROG_UNAVAIL  = 1,
	RTKL_RPC_REP_PROG_MISMATCH = 2,
	RTKL_RPC_REP_PROC_UNAVAIL  = 3,
	RTKL_RPC_REP_GARBAGE_ARGS  = 4,
	RTKL_RPC_REP_SYSTEM_ERR    = 5
};


typedef struct nfs3_reply_proc
{
	u_int32_t proc;
	bool (*func)(char*, u_int32_t, struct rtkl_rpc_call*);
} nfs3_reply_proc;
typedef bool (*nfs3_reply_procp)(char*, u_int32_t, struct rtkl_rpc_call*);

bool rtkl_nfs3_reply_funcall (char*, u_int32_t, struct rtkl_rpc_call*);

bool rtkl_nfs3_call_readdir (u_int32_t, char*, u_int32_t, u_int64_t*,
                             u_int64_t*);

extern struct rtkl_mlist rtkl_mlist_head;

extern struct rtkl_nfs_dirs rtkl_nfs_dirs;

#endif

/* rtkl_rpc.h ends here. */
