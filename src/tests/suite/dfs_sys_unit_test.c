/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include "dfs_test.h"
#include "dfs_internal.h"
#include <pthread.h>

/** global DFS mount used for all tests */
static uuid_t		co_uuid;
static daos_handle_t	co_hdl;
static dfs_sys_t	*dfs_sys_mt;

/**
 * Common tree setup for many tests.
 */
static void
create_simple_tree(const char *dir1, const char *file1, const char *sym1,
		   const char *sym1_target)
{
	int     rc;

	rc = dfs_sys_mkdir(dfs_sys_mt, dir1, S_IWUSR | S_IRUSR, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mknod(dfs_sys_mt, file1, S_IFREG, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_symlink(dfs_sys_mt, sym1_target, sym1);
	assert_int_equal(rc, 0);
}

/**
 * Common tree removal for many tests.
 */
static void
delete_simple_tree(const char *dir1, const char *file1, const char *sym1)
{
	int	rc;

	rc = dfs_sys_remove(dfs_sys_mt, sym1, false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, file1, false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, dir1, false, NULL);
	assert_int_equal(rc, 0);
}

/**
 * Verify basic mount / umount.
 */
static void
dfs_sys_test_mount(void **state)
{
	test_arg_t		*arg = *state;
	uuid_t			cuuid;
	daos_cont_info_t	co_info;
	daos_handle_t		coh;
	dfs_sys_t		*dfs_sys;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** create a DFS container with POSIX layout */
	uuid_generate(cuuid);
	rc = dfs_cont_create(arg->pool.poh, cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	rc = daos_cont_open(arg->pool.poh, cuuid, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_rc_equal(rc, 0);

	rc = dfs_sys_mount(arg->pool.poh, coh, O_RDWR, 0, &dfs_sys);
	assert_int_equal(rc, 0);

	rc = dfs_sys_umount(dfs_sys);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 1, NULL);
	assert_rc_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
}

/**
 * Verify that we can access and use the underlying dfs_t.
 */
static void
dfs_sys_test_get_dfs(void **state)
{
	test_arg_t	*arg = *state;
	dfs_t		*dfs;
	dfs_attr_t	attr = {0};
	int		rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_sys_get_dfs_obj(dfs_sys_mt, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_query(dfs, &attr);
	assert_int_equal(rc, 0);
}

/**
 * Verify that we can create with:
 * mkdir, symlink, open, mknod.
 * And that we can destroy with:
 * remove, remove(force), remove_type, remove_type(force).
 */
static void
dfs_sys_test_create_remove(void **state)
{
	test_arg_t	*arg = *state;
	int		rc;
	const char	*dir1 = "/dir1";
	const char	*dir2 = "/dir1/dir2";
	const char	*dir3 = "/dir1/dir2/dir3";
	const char	*file1 = "/dir1/dir2/file1";
	const char	*file2 = "/dir1/dir2/dir3/file2";
	const char	*sym1 = "/dir1/dir2/sym1";
	const char	*sym1_target = "file1";
	dfs_obj_t	*obj;

	if (arg->myrank != 0)
		return;

	/** Create dirs with mkdir */
	rc = dfs_sys_mkdir(dfs_sys_mt, dir1, O_RDWR, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mkdir(dfs_sys_mt, dir2, O_RDWR, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mkdir(dfs_sys_mt, dir3, O_RDWR, 0);
	assert_int_equal(rc, 0);

	/** Create links with symlink */
	rc = dfs_sys_symlink(dfs_sys_mt, sym1_target, sym1);
	assert_int_equal(rc, 0);

	/** Remove dirs, links with remove */
	rc = dfs_sys_remove(dfs_sys_mt, sym1, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, dir3, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, dir2, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, dir1, 0, 0);
	assert_int_equal(rc, 0);

	/** Create dirs, files, links with open */
	rc = dfs_sys_open(dfs_sys_mt, dir1, S_IFDIR | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, dir2, S_IFDIR | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, dir3, S_IFDIR | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, file1, S_IFREG | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, file2, S_IFREG | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, sym1, S_IFLNK | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, sym1_target, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);

	/** Remove files with remove */
	rc = dfs_sys_remove(dfs_sys_mt, file2, 0, 0);
	assert_int_equal(rc, 0);

	/** Remove dirs, files, links with remove_type */
	rc = dfs_sys_remove_type(dfs_sys_mt, file1, false, S_IFREG, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove_type(dfs_sys_mt, sym1, false, S_IFLNK, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove_type(dfs_sys_mt, dir3, false, S_IFDIR, NULL);
	assert_int_equal(rc, 0);

	/** Remove dirs with remove_type(force) */
	rc = dfs_sys_remove_type(dfs_sys_mt, dir1, true, S_IFDIR, NULL);
	assert_int_equal(rc, 0);

	/** Create dirs, files with mknod */
	rc = dfs_sys_mknod(dfs_sys_mt, dir1, S_IFDIR | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mknod(dfs_sys_mt, dir2, S_IFDIR | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mknod(dfs_sys_mt, dir3, S_IFDIR | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mknod(dfs_sys_mt, file1, S_IFREG | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);

	/** Remove tree (dir) with remove(force) */
	rc = dfs_sys_remove(dfs_sys_mt, dir1, true, NULL);
	assert_int_equal(rc, 0);
}

/**
 * Verify that access works on entries with and without O_NOFOLLOW.
 * Verify that chmod works.
 */
static void
dfs_sys_test_access_chmod(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dir1 = "/dir1";
	const char	*file1 = "/dir1/file1";
	const char	*sym1 = "/dir1/sym1";
	const char	*sym1_target = "file1";
	int		rc;

	if (arg->myrank != 0)
		return;

	create_simple_tree(dir1, file1, sym1, sym1_target);

	/** dir1 has perms */
	rc = dfs_sys_access(dfs_sys_mt, dir1, R_OK | W_OK, 0);
	assert_int_equal(rc, 0);

	/** file1 does not have perms */
	rc = dfs_sys_access(dfs_sys_mt, file1, R_OK | W_OK, 0);
	assert_int_equal(rc, EPERM);

	/** link1 to file1 does not have perms */
	rc = dfs_sys_access(dfs_sys_mt, sym1, R_OK | W_OK, 0);
	assert_int_equal(rc, EPERM);

	/** link1 itself does have perms */
	rc = dfs_sys_access(dfs_sys_mt, sym1, R_OK | W_OK, O_NOFOLLOW);
	assert_int_equal(rc, 0);

	/** Give file1 perms */
	/* TODO - shouldn't need to pass S_IFREG - dfs bug */
	rc = dfs_sys_chmod(dfs_sys_mt, file1, S_IWUSR | S_IRUSR | S_IFREG);
	assert_int_equal(rc, 0);

	/** file1 should have perms now */
	rc = dfs_sys_access(dfs_sys_mt, file1, R_OK | W_OK, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_access(dfs_sys_mt, sym1, R_OK | W_OK, 0);
	assert_int_equal(rc, 0);

	delete_simple_tree(dir1, file1, sym1);
}

/**
 * Verify open and stat on the root.
 * Verify open and stat on existing entries, with and without O_NOFOLLOW.
 */
static void
dfs_sys_test_open_stat(void ** state)
{
	test_arg_t	*arg = *state;
	const char	*dir1 = "/dir1";
	const char	*file1 = "/dir1/file1";
	const char	*sym1 = "/dir1/sym1";
	const char	*sym1_target = "file1";
	dfs_obj_t	*obj;
	mode_t		mode = {0};
	struct stat	stbuf = {0};
	int		rc;

	if (arg->myrank != 0)
		return;

	/** Open/Stat root dir */
	rc = dfs_sys_open(dfs_sys_mt, "/", S_IFDIR, O_RDWR, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_get_mode(obj, &mode);
	assert_int_equal(rc, 0);
	assert_int_equal(mode & S_IFMT, S_IFDIR);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_stat(dfs_sys_mt, "/", &stbuf, 0);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_mode & S_IFMT, S_IFDIR);

	create_simple_tree(dir1, file1, sym1, sym1_target);

	/** Open/Stat dir1 */
	rc = dfs_sys_open(dfs_sys_mt, dir1, S_IFDIR, O_RDWR, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_get_mode(obj, &mode);
	assert_int_equal(rc, 0);
	assert_int_equal(mode & S_IFMT, S_IFDIR);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_stat(dfs_sys_mt, dir1, &stbuf, 0);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_mode & S_IFMT, S_IFDIR);

	/** Default should open file1 */
	rc = dfs_sys_open(dfs_sys_mt, file1, 0, O_RDWR, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_get_mode(obj, &mode);
	assert_int_equal(rc, 0);
	assert_int_equal(mode & S_IFMT, S_IFREG);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);

	/** Open/Stat file1 */
	rc = dfs_sys_open(dfs_sys_mt, file1, S_IFREG, O_RDWR, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_get_mode(obj, &mode);
	assert_int_equal(rc, 0);
	assert_int_equal(mode & S_IFMT, S_IFREG);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_stat(dfs_sys_mt, file1, &stbuf, 0);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_mode & S_IFMT, S_IFREG);

	/** Open/Stat sym1->file1 */
	rc = dfs_sys_open(dfs_sys_mt, sym1, S_IFREG, O_RDWR, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_get_mode(obj, &mode);
	assert_int_equal(rc, 0);
	assert_int_equal(mode & S_IFMT, S_IFREG);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_stat(dfs_sys_mt, sym1, &stbuf, 0);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_mode & S_IFMT, S_IFREG);

	/** Stat sym1 itself */
	rc = dfs_sys_stat(dfs_sys_mt, sym1, &stbuf, O_NOFOLLOW);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_mode & S_IFMT, S_IFLNK);

	delete_simple_tree(dir1, file1, sym1);
}

/**
 * Verify readlink on a non-symlink.
 * Verify readlink on a symlink.
 */
static void
dfs_sys_test_readlink(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dir1 = "/dir1";
	const char	*file1 = "/dir1/file1";
	const char	*sym1 = "/dir1/sym1";
	const char	*sym1_target = "file1";
	daos_size_t	sym1_target_size = 6;
	char		*buf = NULL;
	daos_size_t	buf_size = 0;
	int		rc;

	if (arg->myrank != 0)
		return;

	create_simple_tree(dir1, file1, sym1, sym1_target);

	/** readlink on non-symlink */
	rc = dfs_sys_readlink(dfs_sys_mt, file1, buf, &buf_size);
	assert_int_equal(rc, EINVAL);
	assert_int_equal(buf_size, -1);

	/** readlink with NULL buffer */
	rc = dfs_sys_readlink(dfs_sys_mt, sym1, buf, &buf_size);
	assert_int_equal(rc, 0);
	assert_int_equal(buf_size, sym1_target_size);
	D_ALLOC(buf, buf_size);
	assert_non_null(buf);

	/** readlink with allocated buffer */
	rc = dfs_sys_readlink(dfs_sys_mt, sym1, buf, &buf_size);
	assert_int_equal(rc, 0);
	assert_int_equal(buf_size, sym1_target_size);
	assert_string_equal(buf, sym1_target);
	D_FREE(buf);

	delete_simple_tree(dir1, file1, sym1);
}

/**
 * Verifies utimens on a path.
 * Verifies setatter on a path, arbitrarily using atime and mtime.
 */
static void
setattr_hlpr(const char *path, bool no_follow)
{
	struct stat	stbuf = {0};
	struct timespec	times[2] = {0};
	int		sflags = 0;
	int		rc;

	print_message("  setattr_hlpr(\"%s\", %d)\n", path, no_follow);

	if (no_follow)
		sflags |= O_NOFOLLOW;

	/** Get current times */
	rc = dfs_sys_stat(dfs_sys_mt, path, &stbuf, sflags);
	assert_int_equal(rc, 0);

	/** Increment times */
	times[0] = stbuf.st_atim;
	times[1] = stbuf.st_mtim;
	times[0].tv_sec += 1;
	times[1].tv_sec += 2;

	/** Set new times with utimens */
	rc = dfs_sys_utimens(dfs_sys_mt, path, times, sflags);
	assert_int_equal(rc, 0);

	/** Check new times are set */
	rc = dfs_sys_stat(dfs_sys_mt, path, &stbuf, sflags);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_atim.tv_sec, times[0].tv_sec);
	assert_int_equal(stbuf.st_mtim.tv_sec, times[1].tv_sec);

	/** Increment times again */
	times[0].tv_sec += 1;
	times[1].tv_sec += 2;

	/** Set new times with setattr */
	stbuf.st_atim = times[0];
	stbuf.st_mtim = times[1];
	rc = dfs_sys_setattr(dfs_sys_mt, path, &stbuf,
			     DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME,
			     sflags);
	assert_int_equal(rc, 0);

	/** Check new times are set */
	rc = dfs_sys_stat(dfs_sys_mt, path, &stbuf, sflags);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_atim.tv_sec, times[0].tv_sec);
	assert_int_equal(stbuf.st_mtim.tv_sec, times[1].tv_sec);
}

/**
 * Verify setattr with and without O_NOFOLLOW.
 * Verify shorthand utimens.
 */
static void
dfs_sys_test_setattr(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dir1 = "/dir1";
	const char	*file1 = "/dir1/file1";
	const char	*sym1 = "/dir1/sym1";
	const char	*sym1_target = "file1";

	if (arg->myrank != 0)
		return;

	create_simple_tree(dir1, file1, sym1, sym1_target);

	setattr_hlpr(dir1, false);
	setattr_hlpr(file1, false);
	setattr_hlpr(sym1, false);
	setattr_hlpr(sym1, true);

	delete_simple_tree(dir1, file1, sym1);
}

/*
 * Verify read, write, punch on a non-file.
 * Verify read, write, punch on a file.
 */
static void
dfs_sys_test_read_write(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dir1 = "/dir1";
	const char	*file1 = "/dir1/file1";
	const char	*sym1 = "/dir1/sym1";
	const char	*sym1_target = "file1";
	dfs_obj_t	*obj;
	daos_size_t	buf_size = 10;
	daos_size_t	got_size;
	void		*write_buf[buf_size];
	void		*read_buf[buf_size];
	int		rc;

	if (arg->myrank != 0)
		return;

	create_simple_tree(dir1, file1, sym1, sym1_target);

	/** Open a dir */
	rc = dfs_sys_open(dfs_sys_mt, dir1, S_IFDIR, O_RDWR, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);

	/** Try to write a dir */
	got_size = buf_size;
	rc = dfs_sys_write(dfs_sys_mt, obj, write_buf, 0, &got_size, NULL);
	assert_int_equal(rc, EINVAL);
	assert_int_equal(got_size, -1);

	/** Try to read a dir*/
	got_size = buf_size;
	rc = dfs_sys_read(dfs_sys_mt, obj, read_buf, 0, &got_size, NULL);
	assert_int_equal(rc, EINVAL);
	assert_int_equal(got_size, -1);

	/** Try to punch a dir */
	rc = dfs_sys_punch(dfs_sys_mt, dir1, 0, buf_size);
	assert_int_equal(rc, EINVAL);

	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);

	/** Open a file */
	rc = dfs_sys_open(dfs_sys_mt, file1, S_IFREG, O_RDWR, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);

	/** Write to file */
	got_size = buf_size;
	memset(write_buf, 1, buf_size);
	rc = dfs_sys_write(dfs_sys_mt, obj, write_buf, 0, &got_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(got_size, buf_size);

	/** Read from file */
	got_size = buf_size;
	memset(read_buf, 0, buf_size);
	rc = dfs_sys_read(dfs_sys_mt, obj, read_buf, 0, &got_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(got_size, buf_size);
	assert_memory_equal(read_buf, write_buf, buf_size);

	/** Punch file */
	rc = dfs_sys_punch(dfs_sys_mt, file1, 0, buf_size);
	assert_int_equal(rc, 0);

	/** Read empty file */
	got_size = buf_size;
	rc = dfs_sys_read(dfs_sys_mt, obj, read_buf, 0, &got_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(got_size, 0);

	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);

	delete_simple_tree(dir1, file1, sym1);
}

/**
 * Verify opendir + readdir.
 */
static void
dfs_sys_test_open_readdir(void **state)
{
	test_arg_t	*arg = *state;

	if (arg->myrank != 0)
		return;

	/** TODO opendir / readdir */
}

/**
 * Verify setxattr, listxattr, getxattr.
 */
static void
dfs_sys_test_xattr(void ** state)
{
	test_arg_t	*arg = *state;

	if (arg->myrank != 0)
		return;

	/** TODO setxattr */
	/** TODO listxattr */
	/** TODO getxattr */
}

static const struct CMUnitTest dfs_sys_unit_tests[] = {
	{ "DFS_SYS_UNIT_TEST1:  DFS Sys mount / umount",
	  dfs_sys_test_mount, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST2:  DFS Sys get_dfs_obj",
	  dfs_sys_test_get_dfs, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST3:  DFS Sys create / remove",
	  dfs_sys_test_create_remove, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST4:  DFS Sys access / chmod",
	  dfs_sys_test_access_chmod, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST5:  DFS Sys open / stat",
	  dfs_sys_test_open_stat, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST6:  DFS Sys readlink",
	  dfs_sys_test_readlink, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST7:  DFS Sys setattr",
	  dfs_sys_test_setattr, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST8:  DFS Sys read / write",
	  dfs_sys_test_read_write, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST9:  DFS Sys opendir / readdir",
	  dfs_sys_test_open_readdir, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST10: DFS Sys xattr",
	  dfs_sys_test_xattr, async_disable, test_case_teardown},
};

static int
dfs_sys_setup(void **state)
{
	test_arg_t		*arg;
	int			rc = 0;

	rc = test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			0, NULL);
	assert_int_equal(rc, 0);

	arg = *state;

	if (arg->myrank == 0) {
		uuid_generate(co_uuid);
		rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl,
				     NULL);
		assert_int_equal(rc, 0);
		printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));
		rc = dfs_sys_mount(arg->pool.poh, co_hdl, O_RDWR, 0,
				   &dfs_sys_mt);
		assert_int_equal(rc, 0);
	}

	handle_share(&co_hdl, HANDLE_CO, arg->myrank, arg->pool.poh, 0);
	dfs_sys_test_share(arg->pool.poh, co_hdl, arg->myrank, 0, &dfs_sys_mt);

	return rc;
}

static int
dfs_sys_teardown(void **state)
{
	test_arg_t	*arg = *state;
	int		rc;

	rc = dfs_sys_umount(dfs_sys_mt);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
		assert_rc_equal(rc, 0);
		print_message("Destroyed DFS Container "DF_UUIDF"\n",
			      DP_UUID(co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);

	return test_teardown(state);
}

int
run_dfs_sys_unit_test(int rank, int size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS_FileSystem_DFS_Sys_Unit",
					 dfs_sys_unit_tests, dfs_sys_setup,
					 dfs_sys_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}