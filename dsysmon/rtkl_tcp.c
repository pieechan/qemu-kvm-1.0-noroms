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
 * rtkl_tcp.c:
 *	A implementation of RootkitLibra (RTKL)
 */

#include <stdlib.h>
#include <string.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <net/ethernet.h>

#include "rtkl_defs.h"

static void
rtkl_tcp_seg_push_tail (struct rtkl_tcp_strm* list, struct tcphdr* head,
			char* data, u_int32_t dlen)
{
	u_int32_t seq = ntohl (head->seq);
	struct rtkl_tcp* p;

	p = (struct rtkl_tcp*) xmalloc (sizeof (struct rtkl_tcp));
	p->head = *head;
	p->data = data;
	p->dlen = dlen;
	TAILQ_INSERT_TAIL (&(list->tcp), p, next);

	/* Update curernt (already read) sequence number and next
	 * sequence number. If current segment was first segment (syn
	 * flag is on), next sequnece number is curernt sequence number
	 * plus 1. */

	list->lseq = seq;
	list->nseq = (head->syn == 1) ? seq + 1 : seq + dlen;
}

static void
rtkl_tcp_seg_push_after (struct rtkl_tcp_strm* list, struct rtkl_tcp* target,
			 struct tcphdr* head, char* data, u_int32_t dlen)
{
	u_int32_t seq = ntohl (head->seq);
	struct rtkl_tcp* p;

	p = (struct rtkl_tcp*) xmalloc (sizeof (struct rtkl_tcp));
	p->head  = *head;
	p->data = data;
	p->dlen = dlen;
	TAILQ_INSERT_AFTER (&(list->tcp), target, p, next);

	/* If target was last element, update curernt (already read)
	 * sequence number and next sequence number. If current segment
	 * was first segment (syn flag is on), next sequnece number is
	 * curernt sequence number plus 1. */

	if (TAILQ_LAST (&(list->tcp), rtkl_tcp_pkts) == target) {
		list->lseq = seq;
		list->nseq = (head->syn == 1) ? seq + 1 : seq + dlen;
	}
}

static void
rtkl_tcp_seg_push_before (struct rtkl_tcp* target, struct tcphdr* head,
			  char* data, u_int32_t dlen)
{
	struct rtkl_tcp* p;

	p = (struct rtkl_tcp*) xmalloc (sizeof (struct rtkl_tcp));
	p->head  = *head;
	p->data = data;
	p->dlen = dlen;
	TAILQ_INSERT_BEFORE (target, p, next);
}

static bool
rtkl_tcp_strm_push1 (struct rtkl_pkt_strm* strm, struct tcphdr* head,
		    char* data, u_int32_t dlen)
{
	u_int32_t		seq = ntohl (head->seq);
	struct rtkl_tcp*	p;
	struct rtkl_tcp*	next_p;

	rtkl_debug ("RTKL: TCP: seq %u len %u lseq %u nseq %u\n",
		    seq, dlen, strm->list.lseq, strm->list.nseq);
	rtkl_debug ("RTKL: TCP: fin %d syn %d rst %d psh %d ack %d urg %d\n",
		    head->fin, head->syn, head->rst, head->psh, head->ack,
		    head->urg);

	if (head->syn == 1) {
		/* TCP syn flag is true. (In this case, length of the
		 * TCP data part is 0.) This segment is first segment of
		 * this (captured) TCP connection. */

		rtkl_debug ("RTKL: TCP: init.\n");

		/* XXX: (re-)initialize list */
		/* XXX: set MSS. */

		strm->list.sseq = 0;
		strm->list.lseq = seq;
		strm->list.nseq = seq + 1;
	} else if (seq == strm->list.nseq) {
		/* A sequentiality segment has come.
		 * Push main list (strm->list). And also check temporary
		 * list whether I have already not recieved the future
		 * segment. */

		rtkl_debug ("RTKL: TCP: next segment.\n");

		/* There is possibility that segment that length of data
		 * part is 0 comes. This is not an error (not a
		 * re-send). Probably any TCP flag is set. But in here,
		 * ignore that packet. */

		if (dlen <= 0) {
			return false;
		}

		/* push this segment to main list. */
		rtkl_tcp_seg_push_tail (&(strm->list), head, data, dlen);
		if (strm->list.sseq == 0) {
			strm->list.sseq =  seq;
		}

		/* check temporary list whether I have alread not recieved the
		 * future segment. */
		for (p = strm->tmps.tcp.tqh_first; p != NULL; p = next_p) {
			next_p = TAILQ_NEXT (p, next);

			if (ntohl (p->head.seq) == strm->list.nseq) {
				rtkl_tcp_seg_push_tail (&(strm->list), &p->head,
							p->data, p->dlen);
				TAILQ_REMOVE (&(strm->tmps.tcp), p, next);
			} else {
				break;
			}
		}
	} else if (seq > strm->list.nseq) {
		/* The future segment has come.
		 * If save this segment to main list (strm->list),
		 * It lose sequentiality of the sequnce number.
		 * Hence, once I save this segment to the temporary list. */

		rtkl_debug ("RTKL: TCP: future segment.\n");

		/* If data length == 0, ignore this segment. */
		if (dlen == 0) {
			return false;
		}

		TAILQ_FOREACH (p, &(strm->tmps.tcp), next) {
			if (ntohl (p->head.seq) == seq) {
				/* alread registered segment (dup segment)
				 * -> skip. */

				rtkl_debug ("RTKL: TCP: %s:%d: "
					    "already recieved segment.\n",
					    __FUNCTION__, __LINE__);

				return false;
			} else if (ntohl (p->head.seq) > seq) {
				/* p->head is not a next sequnce number.
				 * p->head is futher more next sequnce
				 * number. */

				rtkl_tcp_seg_push_before (p, head, data, dlen);
				return true;
			} else if ((ntohl (p->head.seq) + p->dlen) == seq) {
				/* The current sequnce number is next of
				 * p->head. */

				rtkl_tcp_seg_push_after (&(strm->tmps), p,
							 head, data, dlen);
				return true;
			}
		}

		/* A seq is futher more next sequnce number or first
		 * elments of strm->tmps list. */
		rtkl_tcp_seg_push_tail (&(strm->tmps), head, data, dlen);
	} else {
		/* already recieved tcp segment */
		rtkl_debug ("RTKL: TCP: %s:%d: already recieved segment. "
			    "(cur_seq %u nseq %u)\n",
			    __FUNCTION__, __LINE__, seq, strm->list.nseq);
		return false;
	}

	return true;
}

static bool
rtkl_tcp_strm_push (dsm_io_t type, struct tcphdr* head, char* data,
		    u_int32_t dlen)
{
	bool status;
	struct rtkl_pkt_strm* strm;

	/* get stream of the current segment (input or
	 * output). */
	if ((strm = rtkl_hook_strm_get (type)) == NULL) {
		fprintf (stderr, "RTKL: TCP: %s: unknown type %d\n",
			 __FUNCTION__, type);
		return false;
	}

	if (sem_wait (&(strm->list.lock)) < 0) {
		perror ("sem_wait 41");
		return false;
	}

	status = rtkl_tcp_strm_push1 (strm, head, data, dlen);

	if (status == true && strm->list.waiting > 0) {
		if (sem_post (&(strm->list.cond)) < 0) {
			perror ("sem_post");
		}
	}

	if (sem_post (&(strm->list.lock)) < 0) {
		perror ("sem_post");
	}

	return status;
}

static char*
rtkl_tcp_dhead (dsm_io_t type, char* data, u_int32_t len, struct tcphdr* tcp)
{
	u_int32_t		index = 0;
	struct rtkl_pkt_strm*	strm;

	if ((strm = rtkl_hook_strm_get (type)) == NULL) {
		fprintf (stderr, "RTKL: TCP: %s: unknown type %d\n",
			 __FUNCTION__, type);
		return false;
	}

	/* Re-check whether or not recieved packet is TCP.  If recieved
	 * packet is TCP, return pointer of the TCP data part and save
	 * tcp header to argument tcp. */

	struct ether_header eth;
	if ((index + sizeof (struct ether_header)) > len) {
		return NULL;
	}
	memcpy (&eth, &data[index], sizeof (struct ether_header));
	index += sizeof (struct ether_header);

	if (htons (eth.ether_type) != ETHERTYPE_IP) {
		return NULL;
	}

	struct iphdr ip;
	if ((index + sizeof (struct iphdr)) > len) {
		return NULL;
	}
	memcpy (&ip, &data[index], sizeof (struct iphdr));
	index += (ip.ihl * 4);

	if (ip.protocol != IPPROTO_TCP) {
		return NULL;
	}

	/* filter from IP address.
	 * If filter was enable (filt.XX_ip > 0), skip the packet that
	 * has ip address that differ from ip address that registered in
	 * filter. This is also same as port too. */
	if ((strm->filt.src_ip > 0 && strm->filt.src_ip != ntohl (ip.saddr)) ||
	    (strm->filt.dst_ip > 0 && strm->filt.dst_ip != ntohl (ip.daddr))) {
		return NULL;
	}

	if ((index + sizeof (struct tcphdr)) > len) {
		return NULL;
	}
	memcpy (tcp, &data[index], sizeof (struct tcphdr));
	index += (tcp->doff * 4);

	if (index > len) {
		return NULL;
	}

	/* filter form PORT number. */
	if ((strm->filt.src_port > 0 &&
	     strm->filt.src_port != ntohs (tcp->source)) ||
	    (strm->filt.dst_port > 0 &&
	     strm->filt.dst_port != ntohs (tcp->dest))) {
		return NULL;
	}

	return &data[index];
}

static bool
rtkl_tcp_strm_remove1 (dsm_io_t type, u_int32_t byte)
{
	u_int32_t		undeleted_bytes, deleted_bytes, delete_start;
	u_int32_t		pkt_seq, pkt_len;
	struct rtkl_tcp*	p;
	struct rtkl_tcp*	next_p;
	struct rtkl_pkt_strm*	strm;

	if ((strm = rtkl_hook_strm_get (type)) == NULL) {
		fprintf (stderr, "RTKL: TCP: %s: unknown type %d\n",
			 __FUNCTION__, type);
		return false;
	}

	pkt_seq    = ntohl (TAILQ_FIRST (&(strm->list.tcp))->head.seq);
	pkt_len    = TAILQ_FIRST (&(strm->list.tcp))->dlen;

	undeleted_bytes = byte;
	deleted_bytes   = 0;
	delete_start    = strm->list.sseq - pkt_seq;

	rtkl_debug ("RTKL: TCP: %s:%d type %d byte %u\n",
		    __FUNCTION__, __LINE__, type, byte);
	for (p = strm->list.tcp.tqh_first; p != NULL; p = next_p) {
		next_p  = TAILQ_NEXT (p, next);
		pkt_seq = ntohl (p->head.seq);
		pkt_len = p->dlen;

		if (pkt_len <= delete_start) {
			rtkl_debug ("RTKL: TCP: %s:%d "
				    "delete_start %u pkt_len %u -> %u\n",
				    __FUNCTION__, __LINE__,
				    delete_start, pkt_len,
				    (delete_start - pkt_len));
			delete_start -= pkt_len;
			continue;
		}

		rtkl_debug ("RTKL: TCP: %s:%d "
			    "seq %u (%u) / delete_start %u / "
			    "deleted %u/%u (remain %u)\n",
			    __FUNCTION__, __LINE__,
			    pkt_seq, pkt_len, delete_start,
			    deleted_bytes, byte, undeleted_bytes);
		if (pkt_len > (delete_start + undeleted_bytes)) {
			rtkl_debug ("RTKL: TCP: %s:%d "
				    "pkt_len %u > %u (%u %u)/ "
				    "seq %u -> %u (%u) / "
				    "delete %u/%u (remain %u)\n",
				    __FUNCTION__, __LINE__,
				    pkt_len, delete_start + undeleted_bytes,
				    delete_start, undeleted_bytes,
				    strm->list.sseq,
				    strm->list.sseq + undeleted_bytes,
				    pkt_seq,
				    deleted_bytes, byte, 0);
			strm->list.sseq += undeleted_bytes;
			deleted_bytes   += undeleted_bytes;
			undeleted_bytes  = 0;
		} else if (pkt_len <= (delete_start + undeleted_bytes)) {
			rtkl_debug ("RTKL: TCP: %s:%d: "
				    "seq %u->%u (%u), "
				    "delete %u/%u (remain %u)\n",
				    __FUNCTION__, __LINE__,
				    strm->list.sseq,
				    strm->list.sseq + pkt_len - delete_start,
				    pkt_seq - delete_start,
				    deleted_bytes + pkt_len - delete_start,
				    byte,
				    undeleted_bytes - (pkt_len -delete_start));
			TAILQ_REMOVE (&(strm->list.tcp), p, next);
			strm->list.sseq += (pkt_len - delete_start);
			deleted_bytes   += (pkt_len - delete_start);
			undeleted_bytes -= (pkt_len - delete_start);
			delete_start     = 0;
		}

		rtkl_debug ("RTKL: TCP: %s:%d "
			    "seq %u (%u) / delete_start %u / "
			    "deleted %u/%u (remain %u)\n",
			    __FUNCTION__, __LINE__,
			    pkt_seq, pkt_len, delete_start,
			    deleted_bytes, byte, undeleted_bytes);
		if (undeleted_bytes == 0) {
			break;
		}
	}

	rtkl_debug ("RTKL: TCP: %s:%d delete %u byte %u\n",
		    __FUNCTION__, __LINE__, deleted_bytes, byte);
	if (deleted_bytes != byte) {
		return false;
	}

	return true;
}

bool
rtkl_tcp_strm_remove (dsm_io_t type, u_int32_t byte)
{
	bool rtn;
	struct rtkl_pkt_strm*	strm;

	if ((strm = rtkl_hook_strm_get (type)) == NULL) {
		fprintf (stderr, "RTKL: TCP: %s: unknown type %d\n",
			 __FUNCTION__, type);
		return false;
	}

	if (sem_wait (&(strm->list.lock)) < 0) {
		perror ("sem_wait 42");
		return false;
	}

	rtn = rtkl_tcp_strm_remove1 (type, byte);

	if (sem_post (&(strm->list.lock)) < 0) {
		perror ("sem_post");
	}

	return rtn;
}

static char*
rtkl_tcp_strm_get1 (dsm_io_t type, u_int32_t offset, u_int32_t byte)
{
	u_int32_t uncopied_dlen, copied_dlen, copy_start;
	u_int32_t pkt_seq, pkt_len, seq_offset;
	char* data;
	struct rtkl_tcp*	p;
	struct rtkl_pkt_strm*	strm;

	if ((strm = rtkl_hook_strm_get (type)) == NULL) {
		fprintf (stderr, "RTKL: TCP: %s: unknown type %d\n",
			 __FUNCTION__, type);
		return NULL;
	}

	if (TAILQ_EMPTY (&(strm->list.tcp))) {
		return NULL;
	}

	uncopied_dlen = byte;
	copied_dlen   = 0;

	pkt_seq    = ntohl (TAILQ_FIRST (&(strm->list.tcp))->head.seq);
	pkt_len    = TAILQ_FIRST (&(strm->list.tcp))->dlen;
	seq_offset = strm->list.sseq - pkt_seq;
	if (seq_offset > pkt_len) {
		fprintf (stderr, "RTKL: TCP: %d strange offset %u-%u (%u/%u)\n",
			 type, strm->list.sseq, pkt_seq, seq_offset, pkt_len);
		return NULL;
	}

	rtkl_debug ("RTKL: TCP: %s:%d: type %d offset %u byte %u\n",
		    __FUNCTION__, __LINE__, type, offset, byte);
	copy_start = seq_offset + offset;
	data = (char*) xmalloc (byte);
	TAILQ_FOREACH (p, &(strm->list.tcp), next) {
		pkt_seq = ntohl (p->head.seq);
		pkt_len = p->dlen;

		if (pkt_len <= copy_start) {
			/* skip next segment (and decrease pkt_len value
			 * of the copy_start). Normally, this value is
			 * 0. */
			rtkl_debug ("RTKL: TCP: %s:%d: "
				    "copy_start %u pkt_len %u -> %u\n",
				    __FUNCTION__, __LINE__,
				    copy_start, pkt_len,
				    (copy_start - pkt_len));
			copy_start -= pkt_len;
			continue;
		}

		rtkl_debug ("RTKL: TCP: %s:%d: "
			    "seq %u (%u) / copy_start %u / "
			    "copied %u/%u (remain %u)\n",
			    __FUNCTION__, __LINE__,
			    pkt_seq, pkt_len, copy_start,
			    copied_dlen, byte, uncopied_dlen);
		if (pkt_len >= (copy_start + uncopied_dlen)) {
			/* The segment length was larger than length that
			 * want to copy. copy all data. */
			rtkl_debug ("RTKL: TCP: %s:%d "
				    "copied %u from %u-%u (%u bytes) / "
				    "copy_start -> %u (%u)\n",
				    __FUNCTION__, __LINE__,
				    copied_dlen, copy_start,
				    copy_start + uncopied_dlen, uncopied_dlen,
				    copy_start + uncopied_dlen, pkt_len);
			memcpy (&data[copied_dlen], &p->data[copy_start],
				uncopied_dlen);
			copied_dlen    += uncopied_dlen;
			uncopied_dlen   = 0;
			copy_start = copy_start + uncopied_dlen;
			if (pkt_len == (copy_start + uncopied_dlen)) {
				copy_start = 0;
			}
		} else if (pkt_len < (copy_start + uncopied_dlen)) {
			/* The segment length was shorter than length
			 * that want to copy. copy only length segment
			 * length. */
			rtkl_debug ("RTKL: TCP: %s:%d "
				    "copied %u from %u-%u (%u bytes) / "
				    "copy_start -> 0 / "
				    "uncopied %u copied %u\n",
				    __FUNCTION__, __LINE__,
				    copied_dlen, copy_start, pkt_len,
				    pkt_len - copy_start,
				    uncopied_dlen - (pkt_len - copy_start),
				    copied_dlen + (pkt_len - copy_start));
			memcpy (&data[copied_dlen], &p->data[copy_start],
				(pkt_len - copy_start));
			uncopied_dlen -= pkt_len - copy_start;
			copied_dlen   += pkt_len - copy_start;
			copy_start     = 0;
		}

		rtkl_debug ("RTKL: TCP: %s:%d: "
			    "seq %u (%u) / copy_start %u / "
			    "copied %u/%u (remain %u)\n",
			    __FUNCTION__, __LINE__,
			    pkt_seq, pkt_len, copy_start,
			    copied_dlen, byte, uncopied_dlen);
		if (uncopied_dlen == 0) {
			break;
		}
	}

	rtkl_debug ("RTKL: TCP: %s:%d: copied %u byte %u\n",
		    __FUNCTION__, __LINE__, copied_dlen, byte);
	if (copied_dlen != byte) {
		xfree (data);
		return NULL;
	}

	return data;
}

char*
rtkl_tcp_strm_get (dsm_io_t type, u_int32_t offset, u_int32_t byte)
{
	char* data;
	struct rtkl_pkt_strm*	strm;

	if ((strm = rtkl_hook_strm_get (type)) == NULL) {
		fprintf (stderr, "RTKL: TCP: %s: unknown type %d\n",
			 __FUNCTION__, type);
		return NULL;
	}

	if (sem_wait (&(strm->list.lock)) < 0) {
		perror ("sem_wait 43");
		return NULL;
	}

	data = rtkl_tcp_strm_get1 (type, offset, byte);

	if (sem_post (&(strm->list.lock)) < 0) {
		perror ("sem_post");
	}

	return data;
}

int
rtkl_tcp_parsing (void* arg __UNUSED)
{
	struct rtkl_pkt*	pkt;
	struct tcphdr		tcp_hdr;
	char*			data = NULL;
	u_int32_t		len  = 0;

	while (true) {
		if (sem_wait (&(rtkl_pktq_head.cond)) < 0) {
			perror ("sem_wait 44");
			continue;
		}

		if (sem_wait (&(rtkl_pktq_head.lock)) < 0) {
			perror ("sem_wait 45");
		}

		if (TAILQ_EMPTY (&(rtkl_pktq_head.queue))) {
			goto next_loop;
		}
		pkt = TAILQ_FIRST (&(rtkl_pktq_head.queue));

		/* lining up the packet in the order of sequence
		 * number and save another list. */
		if ((data = (rtkl_tcp_dhead (pkt->type, pkt->data, pkt->len,
					     &tcp_hdr))) != NULL) {
			len = pkt->len - (data - pkt->data);

			rtkl_debug ("RTKL: TCP: type = %d len = %u seq = %u\n",
				    pkt->type, len, ntohl (tcp_hdr.seq));

			if (rtkl_tcp_strm_push (pkt->type, &tcp_hdr, data,
						len) == false) {
				rtkl_debug ("RTKL: TCP: segment was skiped.\n");
			}
		}

		TAILQ_REMOVE (&(rtkl_pktq_head.queue), pkt, next);
		xfree (pkt);
		pkt = NULL;

	next_loop:
		if (sem_post (&(rtkl_pktq_head.lock)) < 0) {
			perror ("sem_post");
		}
	}

	/* NOT REACH. */

	return 0;
}

/* rtkl_tcp.c ends here. */
