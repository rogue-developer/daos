/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <errno.h>
#include <getopt.h>
#include <dlfcn.h>
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#define D_LOGFAC DD_FAC(dfuse)

#include "dfuse.h"

#include "daos_fs.h"
#include "daos_api.h"
#include "daos_uns.h"

#include <gurt/common.h>

/* Signal handler for SIGCHLD, it doesn't need to do anything, but it's
 * presence makes pselect() return EINTR in the dfuse_bg() function which
 * is used to detect abnormal exit.
 */
static void
noop_handler(int arg) {
}

static int bg_fd;

/* Send a message to the foreground thread */
static int
dfuse_send_to_fg(int rc)
{
	int nfd;
	int ret;

	if (bg_fd == 0)
		return -DER_SUCCESS;

	DFUSE_LOG_INFO("Sending %d to fg", rc);

	ret = write(bg_fd, &rc, sizeof(rc));

	close(bg_fd);
	bg_fd = 0;

	if (ret != sizeof(rc))
		return -DER_MISC;

	/* If the return code is non-zero then that means there's an issue so
	 * do not perform the rest of the operations in this function.
	 */
	if (rc != 0)
		return -DER_SUCCESS;

	ret = chdir("/");

	nfd = open("/dev/null", O_RDWR);
	if (nfd == -1)
		return -DER_MISC;

	dup2(nfd, STDIN_FILENO);
	dup2(nfd, STDOUT_FILENO);
	dup2(nfd, STDERR_FILENO);
	close(nfd);

	if (ret != 0)
		return -DER_MISC;

	DFUSE_LOG_INFO("Success");

	return -DER_SUCCESS;
}

/* Optionally go into the background
 *
 * It's not possible to simply call daemon() here as if we do that after
 * daos_init() then libfabric doesn't like it, and if we do it before
 * then there are no reporting of errors.  Instead, roll our own where
 * we create a socket pair, call fork(), and then communicate on the
 * socket pair to allow the foreground process to stay around until
 * the background process has completed.  Add in a check for SIGCHLD
 * from the background in case of abnormal exit to avoid deadlocking
 * the parent in this case.
 */
static int
dfuse_bg(struct dfuse_info *dfuse_info)
{
	sigset_t pset;
	fd_set read_set = {};
	int err;
	struct sigaction sa = {};
	pid_t child_pid;
	sigset_t sset;
	int rc;
	int di_spipe[2];

	rc = pipe(&di_spipe[0]);
	if (rc)
		return 1;

	sigemptyset(&sset);
	sigaddset(&sset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sset, NULL);

	child_pid = fork();
	if (child_pid == -1)
		return 1;

	if (child_pid == 0) {
		bg_fd = di_spipe[1];
		return 0;
	}

	sa.sa_handler = noop_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	sigemptyset(&pset);

	FD_ZERO(&read_set);
	FD_SET(di_spipe[0], &read_set);

	errno = 0;
	rc = pselect(di_spipe[0] + 1, &read_set, NULL, NULL, NULL, &pset);
	err = errno;

	if (err == EINTR) {
		printf("Child process died without reporting failure\n");
		exit(2);
	}

	if (FD_ISSET(di_spipe[0], &read_set)) {
		ssize_t b;
		int child_ret;

		b = read(di_spipe[0], &child_ret, sizeof(child_ret));
		if (b != sizeof(child_ret)) {
			printf("Read incorrect data %zd\n", b);
			exit(2);
		}
		if (child_ret) {
			printf("Exiting %d %s\n", child_ret,
			       d_errstr(child_ret));
			exit(-(child_ret + DER_ERR_GURT_BASE));
		} else {
			exit(0);
		}
	}

	printf("Socket is not set\n");
	exit(2);
}

static int
ll_loop_fn(struct dfuse_info *dfuse_info)
{
	int			ret;

	/* Blocking */
	if (dfuse_info->di_threaded)
		ret = dfuse_loop(dfuse_info);
	else
		ret = fuse_session_loop(dfuse_info->di_session);
	if (ret != 0)
		DFUSE_TRA_ERROR(dfuse_info,
				"Fuse loop exited with return code: %d", ret);

	return ret;
}

/*
 * Creates a fuse filesystem for any plugin that needs one.
 *
 * Should be called from the post_start plugin callback and creates
 * a filesystem.
 * Returns true on success, false on failure.
 */
bool
dfuse_launch_fuse(struct dfuse_projection_info *fs_handle,
		  struct fuse_lowlevel_ops *flo,
		  struct fuse_args *args)
{
	struct dfuse_info	*dfuse_info;
	int			rc;

	dfuse_info = fs_handle->dpi_info;

	dfuse_info->di_session = fuse_session_new(args,
						   flo,
						   sizeof(*flo),
						   fs_handle);
	if (!dfuse_info->di_session)
		goto cleanup;

	rc = fuse_session_mount(dfuse_info->di_session,
				dfuse_info->di_mountpoint);
	if (rc != 0)
		goto cleanup;

	fuse_opt_free_args(args);

	if (dfuse_send_to_fg(0) != -DER_SUCCESS)
		goto cleanup;

	rc = ll_loop_fn(dfuse_info);
	fuse_session_unmount(dfuse_info->di_session);
	if (rc)
		goto cleanup;

	return true;
cleanup:
	return false;
}

static void
show_version(char *name)
{
	fprintf(stdout, "%s version %s, libdaos %d.%d.%d\n",
		name, DAOS_VERSION, DAOS_API_VERSION_MAJOR,
		DAOS_API_VERSION_MINOR, DAOS_API_VERSION_FIX);
	fprintf(stdout, "Using fuse %s\n", fuse_pkgversion());
};

static void
show_help(char *name)
{
	printf("usage: %s -m mountpoint\n"
		"Options:\n"
		"\n"
		"	-m --mountpoint=<path>	Mount point to use\n"
		"\n"
		"	   --pool=name		pool UUID/label\n"
		"	   --container=name	container UUID/label\n"
		"	   --path=<path>	Path to load UNS pool/container data\n"
		"	   --sys-name=STR	DAOS system name context for servers\n"
		"\n"
		"	-S --singlethread	Single threaded\n"
		"	-t --thread-count=count	Number of fuse threads to use\n"
		"	-f --foreground		Run in foreground\n"
		"	   --disable-caching	Disable all caching\n"
		"	   --disable-wb-cache	Use write-through rather than write-back cache\n"
		"\n"
		"	-h --help		Show this help\n"
		"	-v --version		Show version\n"
		"\n"
		"Specifying pool and container are optional. If not set then dfuse can connect to\n"
		"many using the uuids as leading components of the path.\n"
		"Pools and containers can be specified using either uuids or labels.\n"
		"\n"
		"The path option can be use to set a filesystem path from which Namespace attributes\n"
		"will be loaded, or if path is not set then the mount directory will also be\n"
		"checked.  Only one way of setting pool and container data should be used.\n"
		"\n"
		"The default thread count is one per available core to allow maximum throughput,\n"
		"this can be modified by running dfuse in a cpuset via numactl or similar tools.\n"
		"One thread will be started for asynchronous I/O handling so at least two threads\n"
		"must be specified in all cases.\n"
		"Singlethreaded mode will use the libfuse loop to handle requests rather than the\n"
		"threading logic in dfuse."
		"\n"
		"If dfuse is running in background mode (the default unless launched via mpirun)\n"
		"then it will stay in the foreground until the mount is registered with the\n"
		"kernel to allow appropriate error reporting.\n"
		"\n"
		"Caching is on by default with short metadata timeouts and write-back data cache,\n"
		"this can be disabled entirely for the mount by the use of command line options.\n"
		"Further settings can be set on a per-container basis via the use of container\n"
		"attributes.  If the --disable-caching option is given then no caching will be\n"
		"performed and the container attributes are not used, if --disable-wb-cache is\n"
		"given the data caching for the whole mount is performed in write-back mode and\n"
		"the container attributes are still used\n"
		"\n"
		"version: %s\n",
		name, DAOS_VERSION);
}

int
main(int argc, char **argv)
{
	struct dfuse_projection_info	*fs_handle;
	struct dfuse_info	*dfuse_info = NULL;
	struct dfuse_pool	*dfp = NULL;
	struct dfuse_cont	*dfs = NULL;
	struct duns_attr_t	path_attr = {};
	struct duns_attr_t	duns_attr = {};
	uuid_t			cont_uuid = {};
	uuid_t			pool_uuid = {};
	char			*pool_name = NULL;
	char			*cont_name = NULL;
	char			c;
	int			rc;
	char			*path = NULL;
	bool			have_thread_count = false;

	struct option long_options[] = {
		{"mountpoint",		required_argument, 0, 'm'},
		{"path",		required_argument, 0, 'P'},
		{"pool",		required_argument, 0, 'p'},
		{"container",		required_argument, 0, 'c'},
		{"sys-name",		required_argument, 0, 'G'},
		{"singlethread",	no_argument,	   0, 'S'},
		{"thread-count",	required_argument, 0, 't'},
		{"foreground",		no_argument,	   0, 'f'},
		{"disable-caching",	no_argument,	   0, 'A'},
		{"disable-wb-cache",	no_argument,	   0, 'B'},
		{"version",		no_argument,	   0, 'v'},
		{"help",		no_argument,	   0, 'h'},
		{0, 0, 0, 0}
	};

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ALLOC_PTR(dfuse_info);
	if (dfuse_info == NULL)
		D_GOTO(out_debug, rc = -DER_NOMEM);

	dfuse_info->di_threaded = true;
	dfuse_info->di_caching = true;
	dfuse_info->di_wb_cache = true;

	while (1) {
		c = getopt_long(argc, argv, "m:St:fhv",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'p':
			pool_name = optarg;
			break;
		case 'c':
			cont_name = optarg;
			break;
		case 'G':
			dfuse_info->di_group = optarg;
			break;
		case 'A':
			dfuse_info->di_caching = false;
			dfuse_info->di_wb_cache = false;
			break;
		case 'B':
			dfuse_info->di_wb_cache = false;
			break;
		case 'm':
			dfuse_info->di_mountpoint = optarg;
			break;
		case 'P':
			path = optarg;
			break;
		case 'S':
			/* Set it to be single threaded, but allow an extra one
			 * for the event queue processing
			 */
			dfuse_info->di_threaded = false;
			dfuse_info->di_thread_count = 2;
			break;
		case 't':
			dfuse_info->di_thread_count = atoi(optarg);
			have_thread_count = true;
			break;
		case 'f':
			dfuse_info->di_foreground = true;
			break;
		case 'h':
			show_help(argv[0]);
			D_GOTO(out_debug, rc = -DER_SUCCESS);
			break;
		case 'v':
			show_version(argv[0]);

			D_GOTO(out_debug, rc = -DER_SUCCESS);
			break;
		case '?':
			show_help(argv[0]);
			D_GOTO(out_debug, rc = -DER_INVAL);
			break;
		}
	}

	if (!dfuse_info->di_foreground && getenv("PMIX_RANK")) {
		DFUSE_TRA_WARNING(dfuse_info,
				  "Not running in background under orterun");
		dfuse_info->di_foreground = true;
	}

	if (!dfuse_info->di_mountpoint) {
		printf("Mountpoint is required\n");
		show_help(argv[0]);
		D_GOTO(out_debug, rc = -DER_INVAL);
	}

	if (dfuse_info->di_threaded && !have_thread_count) {
		cpu_set_t cpuset;

		rc = sched_getaffinity(0, sizeof(cpuset), &cpuset);
		if (rc != 0) {
			printf("Failed to get cpuset information\n");
			D_GOTO(out_debug, rc = -DER_INVAL);
		}

		dfuse_info->di_thread_count = CPU_COUNT(&cpuset);
	}

	if (dfuse_info->di_thread_count < 2) {
		printf("Dfuse needs at least two threads.\n");
		D_GOTO(out_debug, rc = -DER_INVAL);
	}

	/* Reserve one CPU thread for the daos event queue */
	dfuse_info->di_thread_count -= 1;

	if (!dfuse_info->di_foreground) {
		rc = dfuse_bg(dfuse_info);
		if (rc != 0) {
			printf("Failed to background\n");
			exit(2);
		}
	}

	if (cont_name && !pool_name) {
		printf("Container name specified without pool\n");
		D_GOTO(out_debug, rc = -DER_INVAL);
	}

	rc = daos_init();
	if (rc != -DER_SUCCESS)
		D_GOTO(out_debug, rc);

	DFUSE_TRA_ROOT(dfuse_info, "dfuse_info");

	rc = dfuse_fs_init(dfuse_info, &fs_handle);
	if (rc != 0)
		D_GOTO(out_debug, rc);

	/* Firsly check for attributes on the path.  If this option is set then
	 * it is expected to work.
	 */
	if (path) {
		if (pool_name) {
			printf("Pool specified multiple ways\n");
			D_GOTO(out_daos, rc = -DER_INVAL);
		}

		path_attr.da_no_reverse_lookup = true;
		rc = duns_resolve_path(path, &path_attr);
		DFUSE_TRA_INFO(dfuse_info,
			       "duns_resolve_path() on path returned %d %s",
			       rc, strerror(rc));
		if (rc == ENOENT) {
			printf("Attr path does not exist\n");
			D_GOTO(out_daos, rc = daos_errno2der(rc));
		} else if (rc != 0) {
			/* Abort on all errors here, even ENODATA or ENOTSUP
			 * because the path is supposed to provide
			 * pool/container details and it's an error if it can't.
			 */
			printf("Error reading attr from path %d %s\n",
				rc, strerror(rc));
			D_GOTO(out_daos, rc = daos_errno2der(rc));
		}
		uuid_copy(pool_uuid, path_attr.da_puuid);
		uuid_copy(cont_uuid, path_attr.da_cuuid);
	}

	/* Check for attributes on the mount point itself to use.
	 * Abort if path exists and mountpoint has attrs as both should not be
	 * set, but if nothing exists on the mountpoint then this is not an
	 * error so keep going.
	 */
	duns_attr.da_no_reverse_lookup = true;
	rc = duns_resolve_path(dfuse_info->di_mountpoint, &duns_attr);
	DFUSE_TRA_INFO(dfuse_info,
		       "duns_resolve_path() on mountpoint returned %d %s",
		       rc, strerror(rc));
	if (rc == 0) {
		if (pool_name) {
			printf("Pool specified multiple ways\n");
			D_GOTO(out_daos, rc = -DER_INVAL);
		}
		/* If path was set, and is different to mountpoint then abort.
		 */
		if (path && (strcmp(path, dfuse_info->di_mountpoint) == 0)) {
			printf("Attributes set on both path and mountpoint\n");
			D_GOTO(out_daos, rc = -DER_INVAL);
		}
		uuid_copy(pool_uuid, duns_attr.da_puuid);
		uuid_copy(cont_uuid, duns_attr.da_cuuid);
	} else if (rc == ENOENT) {
		printf("Mount point does not exist\n");
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	} else if (rc != ENODATA && rc != ENOTSUP) {
		/* Other errors from DUNS, it should have logged them already */
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	}

	/* Connect to a pool.
	 * At this point if a pool is chosen by another means then pool_uuid
	 * is already set, so try and parse pool_name, if that's not a uuid
	 * then try it as a label, else try it as a uuid.
	 */
	if (pool_name && (uuid_parse(pool_name, pool_uuid) < 0))
		rc = dfuse_pool_connect_by_label(fs_handle,
						 pool_name,
						 &dfp);
	else
		rc = dfuse_pool_connect(fs_handle, &pool_uuid, &dfp);
	if (rc != 0) {
		printf("Failed to connect to pool (%d) %s\n",
			rc, strerror(rc));
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	}

	if (cont_name && (uuid_parse(cont_name, cont_uuid) < 0))
		rc = dfuse_cont_open_by_label(fs_handle,
					      dfp,
					      cont_name,
					      &dfs);
	else
		rc = dfuse_cont_open(fs_handle, dfp, &cont_uuid, &dfs);
	if (rc != 0) {
		printf("Failed to connect to container (%d) %s\n",
			rc, strerror(rc));
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	}

	/* The container created by dfuse_cont_open() will have taken a ref
	 * on the pool, so drop the initial one.
	 */
	d_hash_rec_decref(&fs_handle->dpi_pool_table, &dfp->dfp_entry);

	if (uuid_is_null(dfp->dfp_pool))
		dfs->dfs_ops = &dfuse_pool_ops;

	rc = dfuse_start(fs_handle, dfs);
	if (rc != -DER_SUCCESS)
		D_GOTO(out_daos, rc);

	/* Remove all inodes from the hash tables */
	rc = dfuse_fs_fini(fs_handle);

	fuse_session_destroy(dfuse_info->di_session);

out_daos:
	DFUSE_TRA_DOWN(dfuse_info);
	daos_fini();
out_debug:
	D_FREE(dfuse_info);
	DFUSE_LOG_INFO("Exiting with status %d", rc);
	daos_debug_fini();
out:
	dfuse_send_to_fg(rc);

	/* Convert CaRT error numbers to something that can be returned to the
	 * user.  This needs to be less than 256 so only works for CaRT, not
	 * DAOS error numbers.
	 */

	if (rc)
		return -(rc + DER_ERR_GURT_BASE);
	else
		return 0;
}
