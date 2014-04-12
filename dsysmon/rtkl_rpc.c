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

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>


#include "rtkl_defs.h"
#include "rtkl_rpc.h"

static bool
rtkl_rpc_mlist_push (struct rtkl_rpc_call* call)
{
	int	val, num = 0;
	bool	status = true;
	bool	wakeup = false;
	struct rtkl_rpc_call*	p;

	if (sem_wait (&rtkl_mlist_head.sync) < 0) {
		perror ("sem_wait 21");
		return false;
	}

	/* check duplicates */
	LIST_FOREACH (p, &rtkl_mlist_head.call, next) {
		if (p->xid == rtkl_mlist_head.xid) {
			wakeup = true;
		}

		if (p->xid == call->xid) {
			rtkl_verbose ("RTKL: RPC: duplicate xid %x\n",
				      call->xid);
			p->count += 1;
			status = true;
			goto exit;
		}
		num += 1;
	}
	rtkl_verbose ("RTKL: RPC: list size = %d\n", num);
	LIST_INSERT_HEAD (&rtkl_mlist_head.call, call, next);

	if (sem_getvalue (&rtkl_mlist_head.reply_xid_sync, &val) < 0) {
		perror ("sem_getvalue");
		goto exit;
	}

	if ((val <= 0) && (wakeup == true)) {
		if (sem_post (&rtkl_mlist_head.reply_xid_sync) < 0) {
			perror ("sem_post");
		}
		rtkl_mlist_head.xid = 0;
	}

exit:
	if (sem_post (&rtkl_mlist_head.sync) < 0) {
		perror ("sem_post");
		return false;
	}

	return status;
}

static bool
rtkl_rpc_mlist_reply_sync (u_int32_t xid, u_int32_t sec)
{
	struct timespec ts;

	if (sem_wait (&rtkl_mlist_head.sync) < 0) {
		perror ("sem_wait 22");
		return false;
	}

	rtkl_mlist_head.xid = xid;

	if (sem_post (&(rtkl_mlist_head.sync)) < 0) {
		perror ("sem_post");
	}

	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		perror ("clock_gettime");
		return true;
	}

	/* setup wait SEC seconds. */
	ts.tv_sec += sec;

	if (sem_timedwait (&rtkl_mlist_head.reply_xid_sync, &ts) < 0) {
		if (errno == ETIMEDOUT) {
			return false;
		}
		perror ("sem_wait 23\n");
	}

	return true;
}


static struct rtkl_rpc_call*
rtkl_rpc_mlist_search (u_int32_t xid)
{
	struct rtkl_rpc_call* p;
	struct rtkl_rpc_call* rtn = NULL;

	if (sem_wait (&rtkl_mlist_head.sync) < 0) {
		perror ("sem_wait 24");
		return false;
	}

	LIST_FOREACH (p, &rtkl_mlist_head.call, next) {
		if (p->xid == xid) {
			rtn = p;
			goto exit;
		}
	}

exit:
	if (sem_post (&rtkl_mlist_head.sync) < 0) {
		perror ("sem_post");
		return false;
	}

	return rtn;
}

static bool
rtkl_rpc_mlist_delete (u_int32_t xid)
{
	bool			status = false;
	struct rtkl_rpc_call*	p;
	struct rtkl_rpc_call*	next_p;

	if (sem_wait (&rtkl_mlist_head.sync) < 0) {
		perror ("sem_wait 25");
		return false;
	}

	for (p = rtkl_mlist_head.call.lh_first; p != NULL; p = next_p) {
		next_p = LIST_NEXT (p, next);

		if (p->xid == xid) {
			if (p->count > 0) {
				p->count -= 1;
				goto exit;
			}

			LIST_REMOVE (p, next);
			xfree (p);
			p = NULL;
			status = true;
			goto exit;
		}
	}

exit:
	if (sem_post (&rtkl_mlist_head.sync) < 0) {
		perror ("sem_post");
		return false;
	}

	return status;
}

static char*
rtkl_rpc_msg_head_parsing (char* head, u_int32_t len, struct rtkl_rpc_msg* msg)
{
	char* p = head;

	/* fragment is already parsed. */
	msg->length = len;

	/* xid : 4 bytes */
	if (len < (p - head) + sizeof (u_int32_t)) {
		return NULL;
	}
	msg->xid = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* type: 4bytes */
	if (len < (p - head) + sizeof (u_int32_t)) {
		return NULL;
	}
	msg->type   = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	rtkl_debug ("RTKL: RPC: xid: %x type: %x\n", msg->xid, msg->type);

	return p;
}

static char*
rtkl_rpc_call_body_pasing (char* head, u_int32_t len,
			   struct rtkl_rpc_call_body* body)
{
	char* p = head;

	/* rpc: rpc version (4bytes) */
	if (len < sizeof (u_int32_t)) {
		return NULL;
	}
	body->rpc_ver = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* rpc: program (4bytes) */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	body->prog = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* rpc: version (4bytes) */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	body->ver = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* rpc: procedure (4bytes) */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	body->proc = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* rpc: cred flavor (4bytes) */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	p += sizeof (u_int32_t);

	/* rpc: cred length (4bytes) */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	u_int32_t cred_len = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* rpc: cred body (auth_len bytes) */
	if (cred_len > 0) {
		if (len < ((p - head) + cred_len)) {
			return NULL;
		}
		p += cred_len;
	}

	/* rpc: verf flavor */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	p += sizeof (u_int32_t);

	/* rpc: verf length */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	u_int32_t verf_len = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* rpc: verf body */
	if (verf_len > 0) {
		if (len < ((p - head) + verf_len)) {
			return NULL;
		}
		p += verf_len;
	}

	return p;
}

static char*
rtkl_rpc_call_parsing (char* head, u_int32_t len, struct rtkl_rpc_call* call)
{
	char* p;
	struct rtkl_rpc_call_body body;

	if ((p = rtkl_rpc_call_body_pasing (head, len, &body)) == NULL) {
		rtkl_verbose ("RTKL: E: packedt is not RPC call.\n");
		return NULL;
	}

	if (body.rpc_ver != 2) {
		rtkl_verbose ("RTKL: E: rpc vesion != 2.\n");
		return NULL;
	}

	if ((body.prog != NFS_PROGRAM) || (body.ver != NFS3_VERSION)) {
		rtkl_verbose ("RTKL: E: packet != NFS version 3 "
			      "(xid = %x, rpc_ver = %x, prog %x, ver %x, "
			      "proc = %x)\n",
			      call->xid, body.rpc_ver, body.prog, body.ver,
			      call->proc);
	}

	call->prog = body.prog;
	call->ver  = body.ver;
	call->proc = body.proc;

	if (rtkl_nfs3_call_readdir (call->proc, p, len - (p - head), 
				    &(call->cookie), &(call->verf)) == false) {
		call->cookie = 0;
		call->verf   = 0;
	}

	call->count = 0;

	return p;
}

static char*
rtkl_rpc_reply_body_pasing (char* head, u_int32_t len,
			    struct rtkl_rpc_reply_body* body)
{
	char* p = head;

	/* get rpc reply status (4bytes). */
	if (len < sizeof (u_int32_t)) {
		return NULL;
	}
	body->reply_stat = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* get auth flavor (4bytes). */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	p += sizeof (u_int32_t);

	/* get opaque length (4bytes). */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	u_int32_t opq_len = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	/* skip opaque body (opq_len bytes). */
	opq_len = opq_len == 0 ? 0 : opq_len;
	if (len < ((p - head) + opq_len)) {
		return NULL;
	}
	p += opq_len;

	/* get accept stat (4bytes). */
	if (len < ((p - head) + sizeof (u_int32_t))) {
		return NULL;
	}
	body->accept_stat = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	return p;
}

static char*
rtkl_rpc_reply_parsing (char* head, u_int32_t len)
{
	char* p = head;
	struct rtkl_rpc_reply_body body;

	if ((p = rtkl_rpc_reply_body_pasing (head, len, &body)) == NULL) {
		rtkl_verbose ("RTKL: E: packet is not RPC replay.\n");
		return NULL;
	}

	/* check reply status. */
	if (body.reply_stat != RTKL_RPC_REP_MSG_ACCEPTED) {
		rtkl_verbose ("RTKL: E: reply stat denied = %d\n",
			      body.reply_stat);
		return NULL;
	}

	/* check accept status. */
	if (body.accept_stat != RTKL_RPC_REP_SUCCESS) {
		rtkl_verbose ("RTKL: E: accept status not success (%d).\n",
			      body.accept_stat);
		return NULL;
	}

	return p;
}

static bool
rtkl_rpc_parsing1 (char* rpc_head, u_int32_t rpc_len)
{
	bool retry = false;
	struct rtkl_rpc_msg	msg;
	struct rtkl_rpc_call*	call = NULL;

	int   len = rpc_len;
	char* p   = rpc_head;

	/* Get the common item between call and reply in the header.
         * -> fragment, xid, type */
	if ((p = rtkl_rpc_msg_head_parsing (rpc_head, rpc_len, &msg)) == NULL) {
		rtkl_verbose ("E: Packet is not RPC packet.\n");
		return NULL;
	}

	switch (msg.type) {
	case RTKL_RPC_CALL:
		call = (struct rtkl_rpc_call*)
			xmalloc (sizeof (struct rtkl_rpc_call));
		len = rpc_len - (p - rpc_head);
		call->xid = msg.xid;

		if ((p = rtkl_rpc_call_parsing (p, len, call)) == NULL) {
			xfree (call);
			return false;
		}

		rtkl_verbose ("RTKL: MSG: CALL: xid = %x prog = %x ver = %x "
			      "proc = %x\n",
			      call->xid, call->prog, call->ver, call->proc);

		if (rtkl_rpc_mlist_push (call) == false) {
			fprintf (stderr,
				 "RTKL: E: rtkl_rpc_mlist_push failed (%x).\n",
				call->xid);
			xfree (call);
			return false;
		}
		break;
	case RTKL_RPC_REPLY:
		/* Get a adjustment call information by xid. */
		while ((call = rtkl_rpc_mlist_search (msg.xid)) == NULL) {
			/* sync `reply and call` with timeout.
			 * When reply data came first, wait 5sec to come
			 * call data. And if timeout occur (5sec
			 * passed), retry searcing adjustment xid. If
			 * adjustment xid was not found, skip this retry
			 * data. */

			if (retry == true) {
				rtkl_verbose ("RTKL: E: timeout occured. "
					      "no adjustment xid %x\n",
					      msg.xid);
				return false;
			}

			if (rtkl_rpc_mlist_reply_sync (msg.xid, 5) == false) {
				retry = true;
			}
		}

		if ((call->prog != NFS_PROGRAM) || (call->ver != NFS3_VERSION)) {
			rtkl_verbose ("RTKL: E: XID: %x not NFSv3 "
				      "(prog %x, ver %x).\n",
				      call->xid, call->prog, call->ver);
			goto reply_exit;
		}

		/* Parse the rpc reply body. */
		len = rpc_len - (p - rpc_head);
		if ((p = rtkl_rpc_reply_parsing (p, len)) == NULL) {
			if (rtkl_rpc_mlist_delete (call->xid) == false) {
				fprintf (stderr,
					 "RTKL: E: No such xid in the list.\n");
			}
			break;
		}

		rtkl_verbose ("RTKL: MSG: REPLY: xid = %x\n", call->xid);
		len = rpc_len - (p - rpc_head);
		if (rtkl_nfs3_reply_funcall (p, len, call) == false) {
			rtkl_verbose ("RTKL: E: nfs3_reply_funcall\n");
		}

	reply_exit:
		/* Delete a entry that used in this time from xid-list. */
		if (rtkl_rpc_mlist_delete (call->xid) == false) {
			fprintf (stderr, "RTKL: E: No such xid in the list.\n");
		}
		break;
	}

	return true;
}

static bool
rtkl_rpc_has_whole_data (dsm_io_t type)
{
	char*		data	= NULL;
	bool		is_last = false;
	int32_t		offset	= 0;
	u_int32_t	fraglen;

	do {
		rtkl_debug ("RTKL: RPC: %s:%d: offset %u\n",
			    __FUNCTION__, __LINE__, offset);
		/* Get RPC fragment (to know RPC data size.) */
		if ((data = rtkl_tcp_strm_get (type, offset,
					       (offset + 4))) == NULL) {
			return false;
		}
		is_last = extract_8bits (data) ? true : false;
		fraglen = ntohl ((u_int32_t) extract_32bits (data));
		fraglen = (fraglen) & 0x7FFFFFFF;
		xfree (data);

		if (is_last == false) {
			rtkl_debug ("RTKL: RPC: This is not last. "
				    "fraglen %u offset %u\n",
				    fraglen, offset);
		}

		rtkl_debug ("RTKL: RPC: %s:%d: last %d fraglen %u offset %u\n",
			    __FUNCTION__, __LINE__, is_last, fraglen, offset);
		/* Get whole RPC data. */
		if ((data = rtkl_tcp_strm_get (type, (offset + 4),
					       fraglen)) == NULL) {
			return false;
		}
		offset = fraglen + 4;
		xfree (data);
	} while (is_last == false);

	return true;
}

static struct rtkl_rpc_data*
rtkl_rpc_whole_data_get (dsm_io_t type)
{
	char*			tmpdata	= NULL;
	bool			is_last	= false;
	int32_t			offset	= 0;
	u_int32_t		size	= 0;
	struct rtkl_rpc_data*	rpc	= NULL;
	u_int32_t		fraglen;

	rpc = (struct rtkl_rpc_data*) xmalloc (sizeof (struct rtkl_rpc_data));
	rpc->data    = NULL;
	rpc->dlen    = 0;
	rpc->rpc_len = 0;

	do {
		rtkl_debug ("RTKL: RPC: %s:%d: offset %u\n",
			    __FUNCTION__, __LINE__, offset);
		/* Get RPC fragment (to know RPC data size.) */
		if ((tmpdata = rtkl_tcp_strm_get (type, offset,
						  (offset + 4))) == NULL) {
			goto failed;
		}
		is_last = extract_8bits (tmpdata) ? true : false;
		fraglen = ntohl ((u_int32_t) extract_32bits (tmpdata));
		fraglen = (fraglen) & 0x7FFFFFFF;
		xfree (tmpdata);

		rtkl_debug ("RTKL: RPC: %s:%d: last %d fraglen %u offset %u\n",
			    __FUNCTION__, __LINE__, is_last, fraglen, offset);
		/* Get whole RPC data. */
		if ((tmpdata = rtkl_tcp_strm_get (type, (offset + 4),
						  fraglen)) == NULL) {
			goto failed;
		}
		rpc->data = xrealloc (rpc->data, size + fraglen);
		memcpy (&rpc->data[size], tmpdata, fraglen);
		size	     += fraglen;
		rpc->dlen    += fraglen;
		rpc->rpc_len += fraglen + 4;

		offset = fraglen + 4;
		xfree (tmpdata);
	} while (is_last == false);

	rtkl_debug ("RTKL: RPC: %s:%d: size %u fraglen %u len %u %u\n",
		    __FUNCTION__, __LINE__, size, fraglen, rpc->rpc_len,
		    rpc->dlen);
	return rpc;

failed:
	xfree (rpc->data);
	xfree (rpc);
	return NULL;
}

int
rtkl_rpc_parsing (void* arg)
{
	dsm_io_t		type = (dsm_io_t) arg;
	struct rtkl_pkt_strm*	strm = NULL;
	struct rtkl_rpc_data*	rpc  = NULL;

	if ((strm = rtkl_hook_strm_get (type)) == NULL) {
		fprintf (stderr, "RTKL: TCP: %s: unknown type %d\n",
			 __FUNCTION__, type);
		return 0;
	}

	if (type == DSM_IO_INPUT) {
		LIST_INIT (&rtkl_nfs_dirs);
	}

	while (true) {
		if (rtkl_rpc_has_whole_data (type) == false) {
			strm->list.waiting += 1;
			if (sem_wait (&(strm->list.cond)) < 0) {
				perror ("sem_wait 26");
			}
			strm->list.waiting -= 1;
			continue;
		}

		if ((rpc = rtkl_rpc_whole_data_get (type)) == NULL) {
			fprintf (stderr,
				 "RTKL: RPC: "
				 "rtkl_rpc_while_data_get failed.\n");
			continue;
		}

		if (rtkl_rpc_parsing1 (rpc->data, rpc->dlen) == false) {
			rtkl_verbose ("RTKL: E: RPC: parse failed.\n");
		}

		if (rtkl_tcp_strm_remove (type, rpc->rpc_len) == false) {
			fprintf (stderr, "RTKL: RPC: remove failed %u\n",
				 rpc->rpc_len);
		}

		xfree (rpc->data);
		xfree (rpc);
		rpc = NULL;
		usleep (1000);
	}

	/* NOT REACH. */

	return 1;
}

/* rtkl_rpc.c ends here. */
