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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "dsm_api.h"
#include "rtkl_defs.h"


struct linux_dirent {
	long	       d_ino;
	//off_t	       d_off;
	unsigned long d_off;
	unsigned short d_reclen;
	char	       d_name[];
};


static void
rtkl_sys_open (char* data, int len __UNUSED)
{
	char*	p = data;
	int	str_len, mod, ret;
	char*	filename;

	str_len = extract_32bits (&p[4]);
	filename = (char*) xmalloc (str_len + 1);
	memcpy (filename, &p[8], str_len);
	filename[str_len] = '\0';
	mod = extract_32bits (&p[8  + str_len]);
	ret = extract_32bits (&p[12 + str_len]);

	rtkl_debug ("open: filename = %s, mode = %d return = %d\n",
		    filename, mod, ret);

	xfree (filename);
	return ;
}

static void
rtkl_sys_close (char* data, int len __UNUSED)
{
	char*	p = data;
	int	fd, ret;

	fd = extract_32bits (&p[4]);
	ret = extract_32bits (&p[8]);

	rtkl_debug ("close: fd = %d, return = %d\n", fd, ret);

	return ;
}
static void
rtkl_sys_fstat (char* data, int len __UNUSED)
{
	char*		p = data;
	int		fd, ret;
	struct stat	st;

	fd  = extract_32bits (&p[4]);
	memcpy (&st, &p[8], sizeof (struct stat));
	ret = extract_32bits (&p[8 + sizeof (struct stat)]);

	rtkl_debug ("fstat: fd = %d, inode = %ld size = %ld return = %d\n",
		    fd, st.st_ino, st.st_size, ret);

	if (ret < 0) {
		return ;
	}

	if (S_ISDIR (st.st_mode)) {
		rtkl_tbl_dir_set (st.st_ino, st.st_size, -1, false);
	} else {
		rtkl_tbl_file_set (st.st_ino, st.st_size, false);
	}

	return ;
}

static void
rtkl_sys_lstat (char* data, int len __UNUSED)
{
	char*		p = data;
	int		str_len, ret;
	char*		filename;
	struct stat	st;

	str_len = extract_32bits (&p[4]);
	filename = (char*) xmalloc (str_len + 1);
	memcpy (filename, &p[8], str_len);
	filename[str_len] = '\0';
	memcpy (&st, &p[8 + str_len], sizeof (struct stat));
	ret = extract_32bits (&p[8 + str_len + sizeof (struct stat)]);

	rtkl_debug ("lstat: name = %s, inode = %ld size = %ld return = %d\n",
		    filename, st.st_ino, st.st_size, ret);

	if (ret < 0) {
		goto file_free;
	}

	if (S_ISDIR (st.st_mode)) {
		rtkl_tbl_dir_set (st.st_ino, st.st_size, -1, false);
	} else {
		rtkl_tbl_file_set (st.st_ino, st.st_size, false);
	}

file_free:
	xfree (filename);
	return ;
}

static void
rtkl_sys_getdents (char* data, int len __UNUSED)
{
	char*			 p = data;
	int			 fd, str_len, count, ret, pos;
	char*			 dirp;
	struct linux_dirent	*entry;

	/* Save parent directory inode number and current number of files. */
	static long dir_ino = 0;
	static int file_num = 0;

	fd	= extract_32bits (&p[4]);
	str_len	= extract_32bits (&p[8]);
	dirp	= (char*) xmalloc (str_len);
	memcpy (dirp, &p[12], str_len);
	count	= extract_32bits (&p[12 + str_len]);
	ret	= extract_32bits (&p[16 + str_len]);

	rtkl_debug ("getdents: fd = %d, len of dirp = %d count = %d ret = %d "
		    "(current file_num = %d)\n",
		    fd, str_len, count, ret, file_num);
	if (ret == 0 && dir_ino > 0) {
		rtkl_debug ("getdents (parsed): dir inode = %ld, "
			    "file num = %d\n",
			    dir_ino, file_num);
		rtkl_tbl_dir_set (dir_ino, -1, file_num, false);
		goto init_getdents;
	}

	if (ret <= 0) {
		goto init_getdents;
	}

	for (pos = 0; pos < ret;) {
		entry = (struct linux_dirent*) (dirp + pos);
		pos += entry->d_reclen;

		if (STRING_EQL (".",  entry->d_name)) {
			dir_ino = entry->d_ino;
			continue;
		}

		if (STRING_EQL ("..", entry->d_name)) {
			continue;
		}

		file_num++;
	}

	xfree (dirp);
	return ;

init_getdents:
	dir_ino	 = 0;
	file_num = 0;
	xfree (dirp);
	return ;
}

int
rtkl_sys_collect (void* arg __UNUSED)
{
	const int	max = dsm_dxfeed_maxsize ();
	int		len;
	char*		buf;

	buf = (char*) xmalloc (max);
	memset (buf, 0, max);

	while (true) {
		memset (buf, 0, max);
		if ((len = dsm_dxfeed_read (buf, max)) < 0) {
			fprintf (stderr,
				 "E: dsm_dxfeed_read returns %d\n", len);
			abort ();
		}

		switch (extract_32bits (buf)) {
		case SYS_open:
			if (rtkl_debug) {
				rtkl_sys_open (buf, len);
			}
			break;
		case SYS_close:
			if (rtkl_debug) {
				rtkl_sys_close (buf, len);
			}
			break;
		case SYS_fstat:
			rtkl_sys_fstat (buf, len);
			break;
		case SYS_lstat:
			rtkl_sys_lstat (buf, len);
			break;
		case SYS_getdents:
			rtkl_sys_getdents (buf, len);
			break;
		default:
			break;
		}
	}

	/* NOT REACHED */
	return 0;
}

/* rtkl_sys.c ends here. */
