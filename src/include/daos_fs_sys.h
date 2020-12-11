/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS File System "Sys" API
 *
 * The DFS Sys API provides a simplified layer directly on top
 * of the DFS API that is more similar to the equivalent
 * POSIX libraries.
 */

#include <dirent.h>

#include "daos.h"
#include "daos_fs.h"

#define DFS_SYS_NO_CACHE 1
#define DFS_SYS_NO_LOCK 2

/* TODO - Not necessary, but it would be useful if we keep "dfs"
 * public in this struct in case a user wants to call a dfs function
 * directly. The main use case for this is that the dfs functions
 * operate directly on an object instead of a path, so if the user
 * already has the object handle, it's technically more efficient
 * to call the dfs functions directly.
 * This is mostly a way of future-proofing, in case some new functions are
 * added to dfs.
 * An alternative is that we could just add direct wrappers for each of
 * the dfs functions, but we would get some name conflicts, since dfs_sys
 * already adds wrappers for most of these functions that take a "path".
 * As an example, see dfs_sys_punch and dfs_sys_opunch below.
 */
/** struct holding attributes for the dfs_sys calls */
typedef struct dfs_sys {
	dfs_t			*dfs;		/* mounted filesystem */
	struct d_hash_table	*dfs_hash;	/* optional lookup hash */
} dfs_sys_t;

/**
 * Mount a file system with dfs_mount and optionally
 * initialize a cache.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	coh	Container open handle.
 * \param[in]	mflags	Mount flags (O_RDONLY or O_RDWR).
 * \param[in]	sflags	Sys flags (DFS_SYS_NO_CACHE or DFS_SYS_NO_LOCK)
 * \param[out]	dfs_sys	Pointer to the file system object created.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_mount(daos_handle_t poh, daos_handle_t coh, int mflags, int sflags,
	      dfs_sys_t **dfs_sys);

/**
 * Unmount a file system with dfs_mount.
 *
 * \param[in]	dfs_sys	Pointer to the mounted file system.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_umount(dfs_sys_t *dfs_sys);

/**
 * Check access permissions on a path. Similar to Linux access(2).
 * By default, symlinks are dereferenced.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	mask	accessibility check(s) to be performed.
 *			It should be either the value F_OK, or a mask with
 *			bitwise OR of one or more of R_OK, W_OK, and X_OK.
 * \param[in]	flags	Access flags (O_NOFOLLOW).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_access(dfs_sys_t *dfs_sys, const char *path, int mask, int flags);

/**
 * Change permission access bits. Symlinks are dereferenced.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	mode	New permission access modes. For now, we don't support
 *			the sticky bit, setuid, and setgid.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_chmod(dfs_sys_t *dfs_sys, const char *path, mode_t mode);

/**
 * set stat attributes for a file and fetch new values.
 * By default, if the object is a symlink the link itself is modified.
 * See dfs_sys_stat() for which entries are filled.
 *
 * \param[in]	dfs_sys	Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in,out]
 *		stbuf	[in]: Stat struct with the members set.
 *			[out]: Stat struct with all valid members filled.
 * \param[in]	flags	Bitmask of flags to set.
 * \param[in]	sflags	(O_NOFOLLOW)
 *
 * \return		0 on Success. errno code on Failure.
 */
int
dfs_sys_setattr(dfs_sys_t *dfs_sys, const char *path, struct stat *stbuf,
		int flags, int sflags);

/**
 * Set atime and mtime of a path.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	times	[0]: atime to set
 *			[1]: mtime to set
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_utimens(dfs_sys_t *dfs_sys, const char *path,
		const struct timespec times[2], int flags);

/**
 * stat attributes of an entry. By default, if object is a symlink,
 * the link itself is interrogated. The following elements of the
 * stat struct are populated (the rest are set to 0):
 * mode_t	st_mode;
 * uid_t	st_uid;
 * gid_t	st_gid;
 * off_t	st_size;
 * blkcnt_t	st_blocks;
 * struct timespec st_atim;
 * struct timespec st_mtim;
 * struct timespec st_ctim;
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	stbuf	Stat struct with the members above filled.
 * \param[in]	flags	Stat flags (O_NOFOLLOW).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_stat(dfs_sys_t *dfs_sys, const char *path, struct stat *buf,
	     int flags);
/**
 * Create a file or directory.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of new object.
 * \param[in]	mode	mode_t (permissions + type).
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *			Valid on create only; ignored otherwise.
 * \param[in]	chunk_size
 *			Chunk size of the array object to be created.
 *			(pass 0 for default 1 MiB chunk size).
 *			Valid on file create only; ignored otherwise.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_mknod(dfs_sys_t *dfs_sys, const char *path, mode_t mode,
	      daos_oclass_id_t cid, daos_size_t chunk_size);

/**
 * list extended attributes of a path and place them all in a buffer
 * NULL terminated one after the other. By default, if path is a
 * symlink, the link itself is interrogated.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in,out]
 *		list	[in]: Allocated buffer for all xattr names.
 *			[out]: Names placed after each other (null terminated).
 * \param[in,out]
 *		size	[in]: Size of list. [out]: Actual size of list.
 *			On error, this is set to -1.
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 *			ERANGE	If size is too small.
 */
int
dfs_sys_listxattr(dfs_sys_t *dfs_sys, const char *path, char *list,
		  daos_size_t *size, int flags);

/**
 * Get extended attribute of a path. By default, if path is a symlink,
 * the link itself is interrogated.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	name	Name of xattr to get.
 * \param[out]	value	Buffer to place value of xattr.
 * \param[in,out]
 *		size	[in]: Size of buffer value. [out]: Actual size of xattr.
 *			On error, this is set to -1.
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 *			ERANGE	If size is too small.
 */
int
dfs_sys_getxattr(dfs_sys_t *dfs_sys, const char *path, const char *name,
		 void *value, daos_size_t *size, int flags);

/**
 * Set extended attribute on a path (file, dir, syml). By default, if path
 * is a symlink, the value is set on the symlink itself.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	name	Name of xattr to add.
 * \param[in]	value	Value of xattr.
 * \param[in]	size	Size in bytes of the value.
 * \param[in]	flags	Set flags. passing 0 does not check for xattr existence.
 *			XATTR_CREATE: create or fail if xattr exists.
 *			XATTR_REPLACE: replace or fail if xattr does not exist.
 * \param[in]	sflags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_setxattr(dfs_sys_t *dfs_sys, const char *path, const char *name,
		 const void *value, daos_size_t size, int flags, int sflags);

/**
 * Retrieve Symlink value of path if it's a symlink. If the buffer size passed
 * in is not large enough, we copy up to size of the buffer, and update the
 * size to actual value size. The size returned includes the null terminator.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in,out]
 *		buf	[in]: Allocated buffer for value.
 *			[out]: Symlink value.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in. [out]: Actual size of
 *			      value. On error, this is set to -1.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_readlink(dfs_sys_t *dfs_sys, const char *path, char *buf,
		 daos_size_t *size);

/**
 * Create a symlink.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	target	Symlink value.
 * \param[in]	path	Path to the new symlink.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_symlink(dfs_sys_t *dfs_sys, const char *target, const char *path);

/**
 * Create/Open a directory, file, or Symlink.
 * The object must be released with dfs_sys_close().
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of the object to create/open.
 * \param[in]	mode	mode_t (permissions + type).
 * \param[in]	flags	Access flags (handles: O_RDONLY, O_RDWR, O_EXCL,
 *			O_CREAT, O_TRUNC).
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *			Valid on create only; ignored otherwise.
 * \param[in]	chunk_size
 *			Chunk size of the array object to be created.
 *			(pass 0 for default 1 MiB chunk size).
 *			Valid on file create only; ignored otherwise.
 * \param[in]	value	Symlink value (NULL if not syml).
 * \param[in]	obj	Pointer to object opened.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_open(dfs_sys_t *dfs_sys, const char *path, mode_t mode, int flags,
	     daos_oclass_id_t cid, daos_size_t chunk_size,
	     const char *value, dfs_obj_t **obj);

/**
 * Close/release open object.
 *
 * \param[in]	obj	Object to release.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_close(dfs_obj_t *obj);

/**
 * Read data from the file object, and return actual data read.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in,out]
 *		buf	[in]: Allocated buffer for data.
 *			[out]: Actual data read.
 * \param[in]	off	Offset into the file to read from.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in. [out]: Actual size of
 *			      data read. On error, this is set to -1.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_read(dfs_sys_t *dfs_sys, dfs_obj_t *obj, void *buf, daos_off_t off,
	     daos_size_t *size, daos_event_t *ev);

/**
 * Write data to the file object.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	buf	Data to write.
 * \param[in]	off	Offset into the file to write to.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in. [out]: Actual size of
 *			      data written. On error, this is set to -1.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_write(dfs_sys_t *dfs_sys, dfs_obj_t *obj, const void *buf,
	      daos_off_t off, daos_size_t *size, daos_event_t *ev);

/**
 * Punch a hole in the file starting at offset to len. If len is set to
 * DFS_MAX_FSIZE, this will be a truncate operation to punch all bytes in the
 * file above offset. If the file size is smaller than offset, the file is
 * extended to offset and len is ignored.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	file	Link path of file.
 * \param[in]	offset	offset of file to punch at.
 * \param[in]	len	number of bytes to punch.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_punch(dfs_sys_t *dfs_sys, const char *file,
	      daos_off_t offset, daos_off_t len);

/**
 * Similar to dfs_sys_punch but on an open object.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	offset	offset of file to punch at.
 * \param[in]	len	number of bytes to punch.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_opunch(dfs_sys_t *dfs_sys, dfs_obj_t *obj,
	       daos_off_t offset, daos_off_t len);

/**
 * Remove an object identified by path. If object is a directory and is
 * non-empty; this will fail unless force option is true. If object is a
 * symlink, the symlink is removed.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	force	If true, remove dir even if non-empty.
 * \param[in]	oid	Optionally return the DAOS Object ID of the removed obj.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_remove(dfs_sys_t *dfs_sys, const char *path, bool force,
	       daos_obj_id_t *oid);

/**
 * Similar to dfs_sys_remove but optionally enforces a type check
 * on the entry.
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	force	If true, remove dir even if non-empty.
 * \param[in]	mode	mode_t (S_IFREG | S_IFDIR | S_IFLNK).
 *			Pass 0 skip the type check.
 * \param[out]	oid	Optionally return the DAOS Object ID of the removed obj.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_remove_type(dfs_sys_t *dfs_sys, const char *path, bool force,
		    mode_t mode, daos_obj_id_t *oid);

/**
 * Create a directory.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	dir	Link path of new dir.
 * \param[in]	mode	mkdir mode.
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_mkdir(dfs_sys_t *dfs_sys, const char *dir, mode_t mode,
	      daos_oclass_id_t cid);

/**
 * Open a directory.
 * The directory must be closed with dfs_sys_closedir().
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	dir	Link path of dir.
 * \param[in]	flags	(O_NOFOLLOW)
 * \param[out]	dirp	Pointer to the open directory.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_opendir(dfs_sys_t *dfs_sys, const char *dir, int flags, DIR **dirp);

/**
 * Close a directory opened with dfs_sys_opendir().
 *
 * \param[in]	dirp	Pointer to the open directory.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_closedir(DIR *dirp);

/**
 * Read a directory opened with dfs_sys_opendir().
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	dirp	Pointer to the open directory.
 * \param[in]	dirent	Pointer to the next directory.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_readdir(dfs_sys_t *dfs_sys, DIR *dirp, struct dirent **dirent);
