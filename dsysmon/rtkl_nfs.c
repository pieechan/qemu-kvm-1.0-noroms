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

#include <rpcsvc/nfs_prot.h>
#include <linux/nfs3.h>
#include <byteswap.h>

#include "dsm_api.h"
#include "nfs3_prot.h"
#include "rtkl_defs.h"
#include "rtkl_rpc.h"

nfs3_reply_procp rtkl_nfs3_reply_search_func (u_int32_t, u_int32_t,
					      nfs3_reply_proc*);

bool rtkl_nfs3_reply_dummy (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_getattr (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_setattr (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_lookup (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_access (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_readlink (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_read (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_write (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_create (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_mkdir (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_symlink (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_readdir (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_readdirplus (char*, u_int32_t, struct rtkl_rpc_call*);
bool rtkl_nfs3_reply_commit (char*, u_int32_t, struct rtkl_rpc_call*);


struct nfs3_reply_proc g_nfs3_reply_procs[] =
{
	{NFS3PROC_NULL,		rtkl_nfs3_reply_dummy},
	{NFS3PROC_GETATTR,	rtkl_nfs3_reply_getattr},
	{NFS3PROC_SETATTR,	rtkl_nfs3_reply_setattr},
	{NFS3PROC_LOOKUP,	rtkl_nfs3_reply_lookup},
	{NFS3PROC_ACCESS,	rtkl_nfs3_reply_access},
	{NFS3PROC_READLINK,	rtkl_nfs3_reply_readlink},
	{NFS3PROC_READ,		rtkl_nfs3_reply_read},
	{NFS3PROC_WRITE,	rtkl_nfs3_reply_write},
	{NFS3PROC_CREATE,	rtkl_nfs3_reply_create},
	{NFS3PROC_MKDIR,	rtkl_nfs3_reply_mkdir},
	{NFS3PROC_SYMLINK,	rtkl_nfs3_reply_symlink},
	{NFS3PROC_MKNOD,	rtkl_nfs3_reply_dummy},
	{NFS3PROC_REMOVE,	rtkl_nfs3_reply_dummy},
	{NFS3PROC_RMDIR,	rtkl_nfs3_reply_dummy},
	{NFS3PROC_RENAME,	rtkl_nfs3_reply_dummy},
	{NFS3PROC_LINK,		rtkl_nfs3_reply_dummy},
	{NFS3PROC_READDIR,	rtkl_nfs3_reply_readdir},
	{NFS3PROC_READDIRPLUS,	rtkl_nfs3_reply_readdirplus},
	{NFS3PROC_FSSTAT,	rtkl_nfs3_reply_dummy},
	{NFS3PROC_FSINFO,	rtkl_nfs3_reply_dummy},
	{NFS3PROC_PATHCONF,	rtkl_nfs3_reply_dummy},
	{NFS3PROC_COMMIT,	rtkl_nfs3_reply_commit}
};

static struct {
	long		dino;
	u_int64_t	cookie;
} last_cookie;


static char*
rtkl_nfs3_status (char* head, u_int32_t len, u_int32_t* status)
{
	char* p = head;

	if (len < sizeof (u_int32_t)) {
		return NULL;
	}
	*status = ntohl ((u_int32_t) extract_32bits (p));
	p += sizeof (u_int32_t);

	return p;
}

static char*
rtkl_nfs3_fattr3 (char* head, u_int32_t len, fattr3* attr)
{
	char* p = head;

	if (len < sizeof (fattr3)) {
		return NULL;
	}
	memcpy (attr, p, sizeof (fattr3));
	p += sizeof (fattr3);

	return p;
}

static char*
rtkl_nfs3_pre_op_attr (char* head, u_int32_t len, pre_op_attr* attr)
{
	char* p = head;
	u_int32_t follow;

	if (len < sizeof (u_int32_t)) {
		return NULL;
	}
	follow = ntohl ((u_int32_t) extract_32bits (p));

	if (follow == 0) {
		return p + sizeof (u_int32_t);
	}

	if (len < sizeof (pre_op_attr)) {
		return NULL;
	}
	memcpy (attr, p, sizeof (pre_op_attr));
	p += sizeof (pre_op_attr);

	return p;
}

static char*
rtkl_nfs3_post_op_attr (char* head, u_int32_t len, post_op_attr* attr)
{
	char* p = head;
	u_int32_t follow;

	if (len < sizeof (u_int32_t)) {
		return NULL;
	}
	follow = ntohl (extract_32bits (p));

	if (follow == 0) {
		return p + sizeof (u_int32_t);
	}

	if (len < (int32_t) sizeof (post_op_attr)) {
		return NULL;
	}
	memcpy (attr, p, sizeof (post_op_attr));
	p += sizeof (post_op_attr);

	return p;
}

static char*
rtkl_nfs3_wcc_data (char* head, u_int32_t len, wcc_data* data)
{
	char* p = head;

	if ((p = rtkl_nfs3_pre_op_attr (p, len, &data->before)) == NULL) {
		return NULL;
	}

	if ((p = rtkl_nfs3_post_op_attr (p, len, &data->after)) == NULL) {
		return NULL;
	}

	return p;
}

static char*
rtkl_nfs3_nfs_fh3 (char* head, u_int32_t len, nfs_fh3* obj)
{
	char* p = head;
	u_int32_t obj_len;

	if (len < sizeof (u_int32_t)) {
		return NULL;
	}
	memcpy (obj, p, sizeof (struct nfs_fh3));
	obj_len = ntohl (obj->data.data_len);
	p += sizeof (u_int32_t);

	if ((len - (p - head)) < obj_len) {
		return NULL;
	}
	p += obj_len;

	return p;
}

static char*
rtkl_nfs3_post_op_fh3 (char* head, u_int32_t len, post_op_fh3* obj)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t follow;

	if (len < sizeof (u_int32_t)) {
		return NULL;
	}
	follow = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	if (follow == 0) {
		obj = NULL;
		return p;
	}

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_nfs_fh3 (p, cur_len,
				    &obj->post_op_fh3_u.handle)) == NULL) {
		return NULL;
	}

	return p;
}

static char*
rtkl_nfs3_uint32 (char* head, u_int32_t len, u_int32_t* val)
{
	char* p = head;
	u_int32_t cur_len;

	cur_len = len - (p - head);
	if (cur_len <  sizeof (u_int32_t)) {
		return false;
	}
	*val = ntohl (extract_32bits (p));
	p += sizeof (u_int32_t);

	return p;
}

static char*
rtkl_nfs3_uint64 (char* head, u_int32_t len, u_int64_t* val)
{
	char* p = head;
	u_int32_t cur_len;

	cur_len = len - (p - head);
	if (cur_len < sizeof (u_int64_t)) {
		return false;
	}
	memcpy (val, p, sizeof (u_int64_t));
	*val = bswap_64 (*val);
	p += sizeof (u_int64_t);

	return p;
}

static char*
rtkl_nfs3_filename (char* head, u_int32_t len, char* name, u_int32_t size)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t file_len = size + ((size % 4) > 0 ? 4 - (size % 4) : 0);

	cur_len = len - (p - head);
	if (cur_len < size) {
		return false;
	}
	memcpy (name, p, size);
	name[size] = '\0';
	p += file_len;

	return p;
}

static void
rtkl_nfs3_tbl_set_fattr3 (struct fattr3* attr, int filenum)
{
	rtkl_verbose ("  type   = %d\n"
		      "  mode   = %d\n"
		      "  nlink  = %d\n"
		      "  uid    = %d\n"
		      "  gid    = %d\n"
		      "  size   = %lu\n"
		      "  fsid   = %lu\n"
		      "  fileid = %lu\n",
		      ntohl (attr->type),
		      ntohl (attr->mode),
		      ntohl (attr->nlink),
		      ntohl (attr->uid),
		      ntohl (attr->gid),
		      bswap_64 (attr->size),
		      bswap_64 (attr->fsid),
		      bswap_64 (attr->fileid));

	if (ntohl (attr->type) == NFDIR) {
		rtkl_tbl_dir_set ((long) bswap_64 (attr->fileid),
				  (long) bswap_64 (attr->size), filenum, true);
	} else {
		rtkl_tbl_file_set ((long) bswap_64 (attr->fileid),
				   (long) bswap_64 (attr->size), true);
	}
}

static void
rtkl_nfs3_tbl_set_post_op_attr (struct post_op_attr* po_attr, int filenum)
{
	bool follow = ntohl (po_attr->attributes_follow);

	if (follow == true) {
		rtkl_nfs3_tbl_set_fattr3 (&po_attr->post_op_attr_u.attributes,
					  filenum);
	} else {
		rtkl_debug ("MSG: follow != 1\n");
	}
}

static long
rtkl_nfs3_inode (struct post_op_attr* po_attr)
{
	if (ntohl (po_attr->attributes_follow) != true) {
		return 0;
	}
	return (long) bswap_64 ((&po_attr->post_op_attr_u.attributes)->fileid);
}

static bool
rtkl_nfs3_dir_file_add (long dino, char* name)
{
	struct rtkl_nfs_dir*  pd;
	struct rtkl_nfs_file* pf;

	if (dino == 0) {
		return false;
	}

	LIST_FOREACH (pd, &rtkl_nfs_dirs, next) {
		if (pd->dino == dino) {
			goto add_file;
		}
	}
	pd = (struct rtkl_nfs_dir*) xmalloc (sizeof (struct rtkl_nfs_dir));
	pd->dino = dino;
	LIST_INIT (&(pd->files));
	LIST_INSERT_HEAD (&rtkl_nfs_dirs, pd, next);

add_file:
	LIST_FOREACH (pf, &pd->files, next) {
		if (STRING_EQL (pf->name, name)) {
			return false;
		}
	}
	pf = (struct rtkl_nfs_file*) xmalloc (sizeof (struct rtkl_nfs_file));
	pf->name = name;
	LIST_INSERT_HEAD (&pd->files, pf, next);

	return true;
}

static int
rtkl_nfs3_dir_filenum (long dino)
{
	int num = 0;
	struct rtkl_nfs_dir*  pd;
	struct rtkl_nfs_file* pf;

	if (dino == 0) {
		return false;
	}

	LIST_FOREACH (pd, &rtkl_nfs_dirs, next) {
		if (pd->dino == dino) {
			break;
		}
	}

	if (pd == NULL || LIST_EMPTY (&(pd->files))) {
		return 0;
	}

	LIST_FOREACH (pf, &pd->files, next) {
		num += 1;
	}

	return num;
}

static bool
rtkl_nfs3_dir_remove (long dino)
{
	struct rtkl_nfs_dir*  pd;
	struct rtkl_nfs_file* pf;
	struct rtkl_nfs_file* next_pf;

	if (dino == 0) {
		return false;
	}

	LIST_FOREACH (pd, &rtkl_nfs_dirs, next) {
		if (pd->dino == dino) {
			break;
		}
	}

	if (pd == NULL) {
		return false;
	}

	if (LIST_EMPTY (&(pd->files))) {
		goto clean_dir;
	}

        for (pf = (&(pd->files))->lh_first; pf != NULL; pf = next_pf) {
                next_pf = LIST_NEXT (pf, next);

		LIST_REMOVE (pf, next);
		xfree (pf->name);
		pf = NULL;
	}

clean_dir:
	LIST_REMOVE (pd, next);
	pd = NULL;

	return true;
}

bool
rtkl_nfs3_call_readdir (u_int32_t proc, char* head, u_int32_t len, 
			u_int64_t* cookie, u_int64_t* verf)
{
	char* p = head;
	u_int32_t cur_len;

	if (proc != NFS3PROC_READDIR && proc != NFS3PROC_READDIRPLUS) {
		return false;
	}

	struct nfs_fh3 data;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_nfs_fh3 (p, cur_len, &data)) == NULL) {
		return false;
	}

	if ((p = rtkl_nfs3_uint64 (p, len - (p - head), cookie)) == NULL) {
		return false;
	}

	if ((p = rtkl_nfs3_uint64 (p, len - (p - head), verf)) == NULL) {
		return false;
	}

	return true;
}

nfs3_reply_procp
rtkl_nfs3_reply_search_func (u_int32_t proc, u_int32_t size,
			     nfs3_reply_proc* procs)
{
	u_int32_t i = 0;

	for (i=0; i<size; ++i) {
		if (procs[i].proc == proc) {
			return procs[i].func;
		}
	}

	return NULL;
}

bool
rtkl_nfs3_reply_funcall (char* head, u_int32_t len,
			 struct rtkl_rpc_call* msg)
{
	int size = sizeof (g_nfs3_reply_procs) / sizeof (nfs3_reply_proc);
	nfs3_reply_procp func;

	func = rtkl_nfs3_reply_search_func (msg->proc, size,
					    g_nfs3_reply_procs);
	if (func) {
		return func (head, len, msg);
	}

	return false;
}

bool
rtkl_nfs3_reply_dummy (char*  head __UNUSED, u_int32_t len __UNUSED,
		       struct rtkl_rpc_call* msg __UNUSED)
{
	return true;
}

bool
rtkl_nfs3_reply_getattr (char* head, u_int32_t len,
			 struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: GETATTR\n");
	struct fattr3 attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_fattr3 (p, cur_len, &attr)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_fattr3 (&attr, -1);

	return true;
}

bool
rtkl_nfs3_reply_setattr (char* head, u_int32_t len,
			 struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK %d\n", status);
		return false;
	}

	rtkl_debug ("MSG: SETATTR\n");
	struct wcc_data data;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_wcc_data (p, cur_len, &data)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&data.after, -1);

	return true;
}

bool
rtkl_nfs3_reply_lookup (char* head, u_int32_t len,
			struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: LOOKUP\n");
	struct nfs_fh3 data;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_nfs_fh3 (p, cur_len, &data)) == NULL) {
		return false;
	}

	struct post_op_attr obj, dir;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &obj)) == NULL) {
		return false;
	}

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &dir)) == NULL) {
		return false;
	}

	rtkl_debug (" File:\n");
	rtkl_nfs3_tbl_set_post_op_attr (&obj, -1);
	rtkl_debug (" Directory:\n");
	rtkl_nfs3_tbl_set_post_op_attr (&dir, -1);

	return true;
}

bool
rtkl_nfs3_reply_access (char* head, u_int32_t len,
			struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: ACCESS\n");
	struct post_op_attr attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &attr)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&attr, -1);

	return true;
}

bool
rtkl_nfs3_reply_readlink (char* head, u_int32_t len,
			  struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: READLINK\n");
	struct post_op_attr attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &attr)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&attr, -1);

	return true;
}

bool
rtkl_nfs3_reply_read (char* head, u_int32_t len,
		      struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: READ\n");
	struct post_op_attr attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &attr)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&attr, -1);

	return true;
}

bool
rtkl_nfs3_reply_write (char* head, u_int32_t len,
		       struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: WRITE\n");
	struct wcc_data data;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_wcc_data (p, cur_len, &data)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&data.after, -1);

	return true;
}

bool
rtkl_nfs3_reply_create (char* head, u_int32_t len,
			struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}


	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: CREATE\n");
	struct post_op_fh3 fh;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_fh3 (p, cur_len, &fh)) == NULL) {
		return false;
	}

	struct post_op_attr attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &attr)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&attr, -1);

	return true;
}

bool
rtkl_nfs3_reply_mkdir (char* head, u_int32_t len,
		       struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}


	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: MKDIR\n");
	struct post_op_fh3 fh;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_fh3 (p, cur_len, &fh)) == NULL) {
		return false;
	}

	struct post_op_attr attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &attr)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&attr, -1);

	return true;
}

bool
rtkl_nfs3_reply_symlink (char* head, u_int32_t len,
			 struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}


	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: SYMLINK\n");
	struct post_op_fh3 fh;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_fh3 (p, cur_len, &fh)) == NULL) {
		return false;
	}

	struct post_op_attr attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &attr)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&attr, -1);

	return true;
}

bool
rtkl_nfs3_reply_readdir (char* head, u_int32_t len,
			 struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;
	int32_t file_num = 0;

	if (msg->cookie != last_cookie.cookie) {
		rtkl_nfs3_dir_remove (last_cookie.dino);
	}

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	rtkl_debug ("MSG: READDIR\n");
	struct post_op_attr attr;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_post_op_attr (p, cur_len, &attr)) == NULL) {
		return false;
	}
	long dino = rtkl_nfs3_inode (&attr);

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		goto failed_exit;
	}

	/* cookieverf3 */
	u_int64_t cookieverf3;
	if ((p = rtkl_nfs3_uint64 (p, len - (p - head), &cookieverf3)) == NULL) {
		goto failed_exit;
	}

	/* follow */
	u_int32_t follow;
	if ((p = rtkl_nfs3_uint32 (p, len - (p - head), &follow)) == NULL) {
		goto failed_exit;
	}
	if (follow == 0) {
		goto failed_exit;
	}

	while (1) {
		u_int64_t id;
		if ((p = rtkl_nfs3_uint64 (p, len - (p - head), &id)) == NULL) {
			goto failed_exit;
		}

		u_int32_t size;
		if ((p = rtkl_nfs3_uint32 (p, len - (p - head),
					   &size)) == NULL) {
			goto failed_exit;
		}

		char* name = (char*) xmalloc (size + 1);
		if ((p = rtkl_nfs3_filename (p, len - (p - head),
					     name, size)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		u_int64_t cookie;
		if ((p = rtkl_nfs3_uint64 (p, len - (p - head),
					   &cookie)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		u_int32_t next;
		if ((p = rtkl_nfs3_uint32 (p, len - (p -head),
					   &next)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		if (STRING_EQL (".", name) || STRING_EQL ("..", name) ||
		    (rtkl_nfs3_dir_file_add (dino, name) == false)) {
			xfree (name);
		}

		if (next == 0) {
			last_cookie.dino   = dino;
			last_cookie.cookie = cookie;
			break;
		}
	}

	u_int32_t eof;
	p = rtkl_nfs3_uint32 (p, len - (p -head), &eof);
	if (eof == 1) {
		file_num = rtkl_nfs3_dir_filenum (dino);
		rtkl_nfs3_tbl_set_post_op_attr (&attr, file_num);
		rtkl_nfs3_dir_remove (dino);
	}

	return true;

failed_exit:
	rtkl_nfs3_dir_remove (dino);
	return false;
}

bool
rtkl_nfs3_reply_readdirplus (char* head, u_int32_t len,
			     struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t status;
	int32_t file_num = 0;

	if (msg->cookie != last_cookie.cookie) {
		rtkl_nfs3_dir_remove (last_cookie.dino);
	}

	if ((p = rtkl_nfs3_status (p, len - (p - head), &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	rtkl_debug ("MSG: READDIRPLUS\n");
	struct post_op_attr attr;
	if ((p = rtkl_nfs3_post_op_attr (p, len - (p - head), &attr)) == NULL) {
		return false;
	}
	long dino = rtkl_nfs3_inode (&attr);

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		goto failed_exit;
	}

	/* cookieverf3 */
	u_int64_t cookieverf3;
	if ((p = rtkl_nfs3_uint64 (p, len - (p - head),
				   &cookieverf3)) == NULL) {
		goto failed_exit;
	}

	/* follow */
	u_int32_t follow;
	if ((p = rtkl_nfs3_uint32 (p, len - (p - head), &follow)) == NULL) {
		goto failed_exit;
	}
	if (follow == 0) {
		goto failed_exit;
	}

	while (1) {
		u_int64_t id;
		if ((p = rtkl_nfs3_uint64 (p, len - (p - head), &id)) == NULL) {
			goto failed_exit;
		}

		u_int32_t size;
		if ((p = rtkl_nfs3_uint32 (p, len - (p - head),
					   &size)) == NULL) {
			goto failed_exit;
		}

		char* name = (char*) xmalloc (size + 1);
		if ((p = rtkl_nfs3_filename (p, len - (p - head),
					     name, size)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		u_int64_t cookie;
		if ((p = rtkl_nfs3_uint64 (p, len - (p - head),
					   &cookie)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		struct post_op_attr fattr;
		if ((p = rtkl_nfs3_post_op_attr (p, len - (p - head),
						 &fattr)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		post_op_fh3 obj_fh;
		if ((p = rtkl_nfs3_post_op_fh3 (p, len - (p - head),
						&obj_fh)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		u_int32_t next;
		if ((p = rtkl_nfs3_uint32 (p, len - (p -head),
					   &next)) == NULL) {
			xfree (name);
			goto failed_exit;
		}

		if (! STRING_EQL (".", name) && ! STRING_EQL ("..", name)) {
			rtkl_nfs3_tbl_set_post_op_attr (&fattr, -1);
			if (rtkl_nfs3_dir_file_add (dino, name) == false) {
				xfree (name);
			}
		} else {
			xfree (name);
		}

		if (next == 0) {
			last_cookie.dino   = dino;
			last_cookie.cookie = cookie;
			break;
		}
	}

	u_int32_t eof;
	p = rtkl_nfs3_uint32 (p, len - (p -head), &eof);
	if (eof == 1) {
		file_num = rtkl_nfs3_dir_filenum (dino);
		rtkl_nfs3_tbl_set_post_op_attr (&attr, file_num);
		rtkl_nfs3_dir_remove (dino);
	}

	return true;

failed_exit:
	rtkl_nfs3_dir_remove (dino);
	return false;
}

bool
rtkl_nfs3_reply_commit (char* head, u_int32_t len,
			struct rtkl_rpc_call* msg __UNUSED)
{
	char* p = head;
	u_int32_t cur_len;
	u_int32_t status;

	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_status (p, cur_len, &status)) == NULL) {
		rtkl_debug ("E: failed to get NFS status.\n");
		return false;
	}

	if (status != NFS_OK) {
		rtkl_debug ("E: NFS *not* OK (%d)\n", status);
		return false;
	}

	rtkl_debug ("MSG: COMMIT\n");
	struct wcc_data data;
	cur_len = len - (p - head);
	if ((p = rtkl_nfs3_wcc_data (p, cur_len, &data)) == NULL) {
		return false;
	}
	rtkl_nfs3_tbl_set_post_op_attr (&data.after, -1);

	return true;
}

/* rtkl_nfs.c ends here */
