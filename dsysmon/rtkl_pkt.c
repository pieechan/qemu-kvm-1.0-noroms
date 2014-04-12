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
 * rtkl_pkt.c:
 *	A implementation of RootkitLibra (RTKL)
 */

#include <string.h>
#include <stdint.h>
#include <netinet/ip.h>
#include <net/ethernet.h>

#include "dsm_api.h"
#include "rtkl_defs.h"


static u_int8_t
rtkl_pkt_iov_byte_get (struct iovec* iov, size_t len, int offset)
{
	int i, c;

	for (i=0, c=0; c<(int)len; ++i) {
		if (iov[i].iov_len > (u_int32_t) offset) {
			return ((uint8_t *) iov[i].iov_base)[offset];
		}

		c      += iov[i].iov_len;
		offset -= iov[i].iov_len;
	}

	return 0;
}

static u_int16_t
rtkl_pkt_iov_short_get (struct iovec* iov, size_t len, int offset)
{
	int i = 0, j = 0, index = 0;
	u_int16_t tmp;
	char p[2];

	while (true) {
		if (i >= (int) len) {
			break;
		}

		if (i >= (int) iov[index].iov_len) {
			offset = iov[index].iov_len;
			++index;
		}

		if (i == offset) {
			p[j] = ((uint8_t *) iov[index].iov_base)[offset];
			++j;
			++offset;

			if (j == 2) {
				memcpy (&tmp, p, sizeof (u_int16_t));
				return tmp;
			}
		}

		++i;
	}

	return 0;
}

static bool
rtkl_pkt_is_tcp (struct iovec* iov, size_t len)
{
	int i = 0;

	/* check simply whether or not recieved packet is TCP. */

	if ((i + sizeof (struct ether_header)) > len) {
		return false;
	}

	/* check whether or not ethernet type is ETHERTYPE_IP.
	 * ethernet type is offset 12 of the ethernet header. */

	u_int16_t type = rtkl_pkt_iov_short_get (iov, len, 12);
	if (htons (type) != ETHERTYPE_IP) {
		rtkl_verbose ("RTKL: PKT: eth type == %d (NOT IP)\n", type);
		return false;
	}
	i += sizeof (struct ether_header);

	if ((i + sizeof (struct iphdr)) > len) {
		return false;
	}

	/* check whether or not IP protocol is TCP.
	 * IP protocol is offset 9 of the ip header. */

	u_int8_t protocol = rtkl_pkt_iov_byte_get (iov, len, i + 9);
	if (protocol != IPPROTO_TCP) {
		rtkl_verbose ("RTKL: PKT: protocol == %d (NOT TCP)\n",
			      protocol);
		return false;
	}

	return true;
}

static char*
rtkl_pkt_copy (struct iovec* iov, size_t len)
{
	/* Convert pakcet data structure to char* from iovec. This is
	 * becase of char* is easy to treat than iovec. */

	u_int32_t i, j, count;
	char* p = xmalloc (len);

	for (i=0, count=0; count<len; ++i) {
		for (j=0; count<len && j < iov[i].iov_len; ++j, ++count) {
			p[count] = ((uint8_t *) iov[i].iov_base)[j];
		}
	}

	return p;
}

int
rtkl_pkt_filtering (dsm_io_t io, struct iovec* iov, size_t len)
{
	struct rtkl_pkt*	pkt;

	/* A packet data length is lesser than 0. This is something
	 * funny. */
	if (len <= 0) {
		goto skip;
	}

	/* check whether or not recieved packet is TCP. If true, save
	 * this packet to the list. If false, skip this packet. */
	if (rtkl_pkt_is_tcp (iov, len) == true) {
		if (sem_wait (&(rtkl_pktq_head.lock)) < 0) {
			perror ("sem_wait 1");
			goto skip;
		}

		pkt = (struct rtkl_pkt*) xmalloc (sizeof (struct rtkl_pkt));
		pkt->type = io;
		pkt->data = rtkl_pkt_copy (iov, len);
		pkt->len  = len;
		TAILQ_INSERT_TAIL (&(rtkl_pktq_head.queue), pkt, next);

		if (sem_post (&(rtkl_pktq_head.cond)) < 0) {
			perror ("sem_post");
		}

		if (sem_post (&(rtkl_pktq_head.lock)) < 0) {
			perror ("sem_post");
			goto skip;
		}
	}

skip:
	return len;
}

/* rtkl_pkt.c ends here. */

