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
 * rtkl_base.c:
 *	A implementation of RootkitLibra (RTKL)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include "rtkl_defs.h"

/* malloc safely.
 * arg1: size   size that allocte.
 * return pointer of allocated region. */
void*
xmalloc (size_t size)
{
	void* p;

	if ((p = malloc (size)) == NULL && size != 0) {
		perror ("malloc");
		exit (EXIT_FAILURE);
	}

	return p;
}

/* realloc safely. */
void*
xrealloc (void* ptr, size_t size)
{
	if (ptr == NULL) {
		return xmalloc (size);
	}

	if ((ptr = realloc (ptr, size)) == NULL) {
		perror ("realloc");
		exit (EXIT_FAILURE);
	}
	return ptr;
}

/* free safely.
 * arg1: p      pointer that to release. */
void
xfree (void* p)
{
	if (p != NULL) {
		free (p);
	}
}

/* Convert number string to integer */
int32_t
string_to_int (char* str, int32_t base)
{
	int   num;
	char* ep;
	long  val;

	errno = 0;
	val = strtol (str, &ep, base);
	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
	    (errno != 0 && val == 0)) {
		perror ("strtol");
		return -1;
	}

	if (ep == optarg) {
		fprintf (stderr, "No digits were found.\n");
		return -1;
	}

	return (num = val);
}

int8_t
extract_8bits (char* p)
{
	int8_t tmp;
	memcpy (&tmp, p, sizeof (int8_t));
	return tmp;
}

int16_t
extract_16bits (char* p)
{
	int16_t tmp;
	memcpy (&tmp, p, sizeof (int16_t));
	return tmp;
}

int32_t
extract_32bits (char* p)
{
	int32_t tmp;
	memcpy (&tmp, p, sizeof (int32_t));
	return tmp;
}

/* rtkl_base.c ends here. */

