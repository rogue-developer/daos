#!/usr/bin/python
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from command_utils_base import CommandFailure
from daos_utils import DaosCommand
from test_utils_container import TestContainer
from pydaos.raw import str_to_c_uuid, DaosContainer, DaosObj, IORequest
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from data_mover_utils import DcpCommand, DsyncCommand, FsCopy, ContClone
from data_mover_utils import DserializeCommand, DdeserializeCommand
from data_mover_utils import format_daos_path, uuid_from_obj
from os.path import join
import uuid
import re
import ctypes
from general_utils import create_string_buffer


class DataMoverTestBase(IorTestBase, MdtestBase):
    # pylint: disable=too-many-ancestors
    """Base DataMover test class.

    Sample Use Case:
        # Create test file
        run_ior_with_params("DAOS", "/testFile, pool1, cont1,
                            flags="-w -K")

        # Set dcp as the tool to use
        self.set_tool("DCP")

        # Copy from DAOS to POSIX
        run_datamover(
            "some test description",
            "DAOS", "/testFile", pool1, cont1,
            "POSIX", "/some/posix/path/testFile")

        # Verify destination file
        run_ior_with_params("POSIX", "/some/posix/path/testFile",
                            flags="-r -R")
    :avocado: recursive

    """

    # The valid parameter types for setting params.
    PARAM_TYPES = ("POSIX", "DAOS_UUID", "DAOS_UNS")

    # The valid datamover tools that can be used
    TOOLS = (
        "DCP",       # mpifileutils dcp
        "DSYNC",     # mpifileutils dsync
        "DSERIAL",   # mpifileutils daos-serialize + daos-deserialize
        "FS_COPY",   # daos filesystem copy
        "CONT_CLONE" # daos container clone
    )

    def __init__(self, *args, **kwargs):
        """Initialize a DataMoverTestBase object."""
        super().__init__(*args, **kwargs)
        self.tool = None
        self.daos_cmd = None
        self.dcp_cmd = None
        self.dsync_cmd = None
        self.dserialize_cmd = None
        self.ddeserialize_cmd = None
        self.fs_copy_cmd = None
        self.cont_clone_cmd = None
        self.ior_processes = None
        self.mdtest_processes = None
        self.dcp_processes = None
        self.dsync_processes = None
        self.dserialize_processes = None
        self.ddeserialize_processes = None
        self.pool = []
        self.container = []
        self.uuids = []
        self.dfuse_hosts = None
        self.num_run_datamover = 0  # Number of times run_datamover was called

        # Temp directory for serialize/deserialize
        self.serial_tmp_dir = self.tmp

        # List of test paths to create and remove
        self.posix_test_paths = []

        # List of daos test paths to keep track of
        self.daos_test_paths = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.dfuse_hosts = self.agent_managers[0].hosts

        # initialize daos_cmd
        self.daos_cmd = DaosCommand(self.bin)

        # Get the processes for each explicitly
        # This is needed because both IorTestBase and MdtestBase
        # define self.processes
        self.ior_processes = self.params.get(
            "np", '/run/ior/client_processes/*')
        self.mdtest_processes = self.params.get(
            "np", '/run/mdtest/client_processes/*')
        self.dcp_processes = self.params.get(
            "np", "/run/dcp/client_processes/*", 1)
        self.dsync_processes = self.params.get(
            "np", "/run/dsync/client_processes/*", 1)
        self.dserialize_processes = self.params.get(
            "np", "/run/dserialize/client_processes/*", 1)
        self.ddeserialize_processes = self.params.get(
            "np", "/run/ddeserialize/client_processes/*", 1)

        tool = self.params.get("tool", "/run/datamover/*")
        if tool:
            self.set_tool(tool)

    def pre_tear_down(self):
        """Tear down steps to run before tearDown().

        Returns:
            list: a list of error strings to report at the end of tearDown().

        """
        error_list = []
        # Remove the created directories
        if self.posix_test_paths:
            command = "rm -rf {}".format(self._get_posix_test_path_string())
            try:
                self._execute_command(command)
            except CommandFailure as error:
                error_list.append(
                    "Error removing created directories: {}".format(error))
        return error_list

    def set_tool(self, tool):
        """Set the copy tool.

        Converts to upper-case and fails if the tool is not valid.

        Args:
            tool (str): the tool to use. Must be in self.TOOLS

        """
        _tool = str(tool).upper()
        if _tool in self.TOOLS:
            self.log.info("DataMover tool = %s", _tool)
            self.tool = _tool
        else:
            self.fail("Invalid tool: {}".format(_tool))

    def _get_posix_test_path_list(self):
        """Get a list of quoted posix test path strings.

        Returns:
            list: a list of quoted posix test path strings

        """
        return ["'{}'".format(item) for item in self.posix_test_paths]

    def _get_posix_test_path_string(self):
        """Get a string of all of the quoted posix test path strings.

        Returns:
            str: a string of all of the quoted posix test path strings

        """
        return " ".join(self._get_posix_test_path_list())

    def new_posix_test_path(self, create=True, parent=None):
        """Generate a new, unique posix path.

        Args:
            create (bool): Whether to create the directory.
                Defaults to True.
            parent (str, optional): The parent directory to create the
                path in. Defaults to self.tmp.

        Returns:
            str: the posix path.

        """
        dir_name = "posix_test{}".format(len(self.posix_test_paths))

        if parent:
            path = join(parent, dir_name)
        else:
            path = join(self.tmp, dir_name)

        # Add to the list of posix paths
        self.posix_test_paths.append(path)

        if create:
            # Create the directory
            cmd = "mkdir -p '{}'".format(path)
            self.execute_cmd(cmd)

        return path

    def new_daos_test_path(self, create=True, cont=None, parent="/"):
        """Create a new, unique daos container path.

        Args:
            create (bool, optional): Whether to create the directory.
                Defaults to True.
            cont (TestContainer, optional): The container to create the
                path within. This container should have a UNS path in DFUSE.
            parent (str, optional): The parent directory relative to the
                container root. Defaults to "/".

        Returns:
            str: the path relative to the root of the container.

        """
        dir_name = "daos_test{}".format(len(self.daos_test_paths))
        path = join(parent, dir_name)

        # Add to the list of daos paths
        self.daos_test_paths.append(path)

        if create:
            if not cont or not cont.path:
                self.fail("Container path required to create directory.")
            # Create the directory relative to the container path
            cmd = "mkdir -p '{}'".format(cont.path.value + path)
            self.execute_cmd(cmd)

        return path

    def _validate_param_type(self, param_type):
        """Validates the param_type.

        It converts param_types to upper-case and handles shorthand types.

        Args:
            param_type (str): The param_type to be validated.

        Returns:
            str: A valid param_type

        """
        _type = str(param_type).upper()
        if _type == "DAOS":
            return "DAOS_UUID"
        if _type in self.PARAM_TYPES:
            return _type
        self.fail("Invalid param_type: {}".format(_type))
        return None

    def create_pool(self):
        """Create a TestPool object and adds to self.pool.

        Returns:
            TestPool: the created pool

        """
        pool = self.get_pool(connect=False)

        # Save the pool and uuid
        self.pool.append(pool)
        self.uuids.append(str(pool.uuid))

        return pool

    def create_cont(self, pool, use_dfuse_uns=False,
                    dfuse_uns_pool=None, dfuse_uns_cont=None,
                    cont_type=None):
        # pylint: disable=arguments-differ
        """Create a TestContainer object.

        Args:
            pool (TestPool): pool to create the container in.
            use_dfuse_uns (bool, optional): whether to create a
                UNS path in the dfuse mount.
                Default is False.
            dfuse_uns_pool (TestPool, optional): pool in the
                dfuse mount for which to create a UNS path.
                Default assumes dfuse is running for a specific pool.
            dfuse_uns_cont (TestContainer, optional): container in the
                dfuse mount for which to create a UNS path.
                Default assumes dfuse is running for a specific container.
            cont_type (str, optional): the container type.

        Returns:
            TestContainer: the container object

        Note about uns path:
            These are only created within a dfuse mount.
            The full UNS path will be created as:
            <dfuse.mount_dir>/[pool_uuid]/[cont_uuid]/<dir_name>
            dfuse_uns_pool and dfuse_uns_cont should only be supplied
            when dfuse was not started for a specific pool/container.

        """
        container = self.get_container(pool, create=False)

        if use_dfuse_uns:
            path = str(self.dfuse.mount_dir.value)
            if dfuse_uns_pool:
                path = join(path, dfuse_uns_pool.uuid)
            if dfuse_uns_cont:
                path = join(path, dfuse_uns_cont.uuid)
            path = join(path, "uns{}".format(str(len(self.container))))
            container.path.update(path)

        if cont_type:
            container.type.update(cont_type)

        # Create container
        container.create()

        # Save container and uuid
        self.container.append(container)
        self.uuids.append(str(container.uuid))

        return container

    def get_cont(self, pool, cont_uuid):
        """Get an existing container.

        Args:
            pool (TestPool): pool to open the container in.
            cont_uuid (str): container uuid.

        Returns:
            TestContainer: the container object

        """
        # Open the container
        # Create a TestContainer instance
        container = TestContainer(pool, daos_command=self.get_daos_command())

        # Create the underlying DaosContainer instance
        container.container = DaosContainer(pool.context)
        container.container.uuid = str_to_c_uuid(cont_uuid)
        container.uuid = container.container.get_uuid_str()
        container.container.poh = pool.pool.handle

        # Save container and uuid
        self.container.append(container)
        self.uuids.append(str(container.uuid))

        return container

    def gen_uuid(self):
        """Generate a unique uuid.

        Returns:
            str: a unique uuid

        """
        new_uuid = str(uuid.uuid4())
        while new_uuid in self.uuids:
            new_uuid = str(uuid.uuid4())
        return new_uuid

    def parse_create_cont_uuid(self, output):
        """Parse a uuid from some output.

        Format:
            Successfully created container (.*-.*-.*-.*-.*)

        Args:
            output (str): The string to parse for the uuid.

        Returns:
            str: The parsed uuid.

        """
        uuid_search = re.search(
            r"Successfully created container (.*-.*-.*-.*-.*)",
            output)
        if not uuid_search:
            self.fail("Failed to parse container uuid")
        return uuid_search.group(1)

    def dataset_gen(self, cont, num_objs, num_dkeys, num_akeys_single,
                    num_akeys_array, akey_sizes, akey_extents):
        """Generate a dataset with some number of objects, dkeys, and akeys.

        Expects the container to be created with the API control method.

        Args:
            cont (TestContainer): the container.
            num_objs (int): number of objects to create in the container.
            num_dkeys (int): number of dkeys to create per object.
            num_akeys_single (int): number of DAOS_IOD_SINGLE akeys per dkey.
            num_akeys_array (int): number of DAOS_IOD_ARRAY akeys per dkey.
            akey_sizes (list): varying akey sizes to iterate.
            akey_extents (list): varying number of akey extents to iterate.

        Returns:
            list: a list of DaosObj created.

        """
        self.log.info("Creating dataset in %s/%s",
                      str(cont.pool.uuid), str(cont.uuid))

        cont.open()

        obj_list = []

        for obj_idx in range(num_objs):
            # Open the obj
            obj = DaosObj(cont.pool.context, cont.container)
            obj_list.append(obj)
            obj.create(rank=obj_idx, objcls=2)
            obj.open()

            ioreq = IORequest(cont.pool.context, cont.container, obj)
            for dkey_idx in range(num_dkeys):
                dkey = "dkey {}".format(dkey_idx)
                c_dkey = create_string_buffer(dkey)

                for akey_idx in range(num_akeys_single):
                    # Round-robin to get the size of data and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    data_val = str(akey_idx % 10)
                    data = data_size * data_val
                    akey = "akey single {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_data = create_string_buffer(data)
                    c_size = ctypes.c_size_t(ctypes.sizeof(c_data))
                    ioreq.single_insert(c_dkey, c_akey, c_data, c_size)

                for akey_idx in range(num_akeys_array):
                    # Round-robin to get the size of data and
                    # the number of extents, and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    akey_extent_idx = akey_idx % len(akey_extents)
                    num_extents = akey_extents[akey_extent_idx]
                    akey = "akey array {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_data = []
                    for data_idx in range(num_extents):
                        data_val = str(data_idx % 10)
                        data = data_size * data_val
                        c_data.append([
                            create_string_buffer(data), data_size])
                    ioreq.insert_array(c_dkey, c_akey, c_data)

            obj.close()
        cont.close()

        return obj_list

    # pylint: disable=too-many-locals
    def dataset_verify(self, obj_list, cont, num_objs, num_dkeys,
                       num_akeys_single, num_akeys_array, akey_sizes,
                       akey_extents):
        """Verify a dataset generated with dataset_gen.

        Args:
            obj_list (list): obj_list returned from dataset_gen.
            cont (TestContainer): the container.
            num_objs (int): number of objects created in the container.
            num_dkeys (int): number of dkeys created per object.
            num_akeys_single (int): number of DAOS_IOD_SINGLE akeys per dkey.
            num_akeys_array (int): number of DAOS_IOD_ARRAY akeys per dkey.
            akey_sizes (list): varying akey sizes to iterate.
            akey_extents (list): varying number of akey extents to iterate.

        """
        self.log.info("Verifying dataset in %s/%s",
                      str(cont.pool.uuid), str(cont.uuid))

        cont.open()

        for obj_idx in range(num_objs):
            # Open the obj
            c_oid = obj_list[obj_idx].c_oid
            obj = DaosObj(cont.pool.context, cont.container, c_oid=c_oid)
            obj.open()

            ioreq = IORequest(cont.pool.context, cont.container, obj)
            for dkey_idx in range(num_dkeys):
                dkey = "dkey {}".format(dkey_idx)
                c_dkey = create_string_buffer(dkey)

                for akey_idx in range(num_akeys_single):
                    # Round-robin to get the size of data and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    data_val = str(akey_idx % 10)
                    data = data_size * data_val
                    akey = "akey single {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_data = ioreq.single_fetch(c_dkey, c_akey,
                                                data_size + 1)
                    actual_data = str(c_data.value.decode())
                    if actual_data != data:
                        self.log.info("Expected:\n%s\nBut got:\n%s",
                            data[:100] + "...",
                            actual_data[:100] + "...")
                        self.log.info(
                            "For:\nobj: %s.%s\ndkey: %s\nakey: %s",
                            str(obj.c_oid.hi), str(obj.c_oid.lo),
                            dkey, akey)
                        self.fail("Single value verification failed.")

                for akey_idx in range(num_akeys_array):
                    # Round-robin to get the size of data and
                    # the number of extents, and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    akey_extent_idx = akey_idx % len(akey_extents)
                    num_extents = akey_extents[akey_extent_idx]
                    akey = "akey array {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_num_extents = ctypes.c_uint(num_extents)
                    c_data_size = ctypes.c_size_t(data_size)
                    actual_data = ioreq.fetch_array(c_dkey, c_akey,
                        c_num_extents, c_data_size)
                    for data_idx in range(num_extents):
                        data_val = str(data_idx % 10)
                        data = data_size * data_val
                        actual_idx = str(actual_data[data_idx].decode())
                        if data != actual_idx:
                            self.log.info("Expected:\n%s\nBut got:\n%s",
                                data[:100] + "...",
                                actual_idx + "...")
                            self.log.info(
                                "For:\nobj: %s.%s\ndkey: %s\nakey: %s",
                                    str(obj.c_oid.hi), str(obj.c_oid.lo),
                                    dkey, akey)
                            self.fail("Array verification failed.")

            obj.close()
        cont.close()

    def set_datamover_params(self,
                             src_type=None, src_path=None,
                             src_pool=None, src_cont=None,
                             dst_type=None, dst_path=None,
                             dst_pool=None, dst_cont=None):
        """Set the params for self.tool.
        Called by run_datamover if params are passed.

        Args:
            src_type (str): how to interpret the src params.
                Must be in PARAM_TYPES.
            src_path (str): source cont path or posix path.
            src_pool (TestPool, optional): the source pool or uuid.
            src_cont (TestContainer, optional): the source cont or uuid.
            dst_type (str): how to interpret the dst params.
                Must be in PARAM_TYPES.
            dst_path (str): destination cont path or posix path.
            dst_pool (TestPool, optional): the destination pool or uuid.
            dst_cont (TestContainer, optional): the destination cont or uuid.

        """
        if self.tool == "DCP":
            self._set_dcp_params(src_type, src_path, src_pool, src_cont,
                                 dst_type, dst_path, dst_pool, dst_cont)
        elif self.tool == "DSYNC":
            self._set_dsync_params(src_type, src_path, src_pool, src_cont,
                                   dst_type, dst_path, dst_pool, dst_cont)
        elif self.tool == "DSERIAL":
            assert src_type in (None, "DAOS", "DAOS_UUID") #nosec
            assert src_path is None #nosec
            assert dst_type in (None, "DAOS", "DAOS_UUID") #nosec
            assert dst_path is None #nosec
            assert dst_cont is None #nosec
            self._set_dserial_params(src_pool, src_cont, dst_pool)
        elif self.tool == "FS_COPY":
            self._set_fs_copy_params(src_type, src_path, src_pool, src_cont,
                                     dst_type, dst_path, dst_pool, dst_cont)
        elif self.tool == "CONT_CLONE":
            assert src_type in (None, "DAOS", "DAOS_UUID") # nosec
            assert src_path is None # nosec
            assert dst_type in (None, "DAOS", "DAOS_UUID") # nosec
            assert dst_path is None # nosec
            self._set_cont_clone_params(src_pool, src_cont,
                                        dst_pool, dst_cont)
        else:
            self.fail("Invalid tool: {}".format(str(self.tool)))

    def _set_dcp_params(self,
                        src_type=None, src_path=None,
                        src_pool=None, src_cont=None,
                        dst_type=None, dst_path=None,
                        dst_pool=None, dst_cont=None):
        """Set the params for dcp.
        This is a wrapper for DcpCommand.set_params.

        When both src_type and dst_type are DAOS_UNS, a prefix will
        only work for either the src or the dst, but not both.

        Args:
            src_type (str): how to interpret the src params.
                Must be in PARAM_TYPES.
            src_path (str): source cont path or posix path.
            src_pool (TestPool, optional): the source pool or uuid.
            src_cont (TestContainer, optional): the source cont or uuid.
            dst_type (str): how to interpret the dst params.
                Must be in PARAM_TYPES.
            dst_path (str): destination cont path or posix path.
            dst_pool (TestPool, optional): the destination pool or uuid.
            dst_cont (TestContainer, optional): the destination cont or uuid.

        """
        if src_type is not None:
            src_type = self._validate_param_type(src_type)
        if dst_type is not None:
            dst_type = self._validate_param_type(dst_type)

        if not src_type and (src_path or src_pool or src_cont):
            self.fail("src params require src_type")
        if not dst_type and (dst_path or dst_pool or dst_cont):
            self.fail("dst params require dst_type")

        # First, initialize a new dcp command
        self.dcp_cmd = DcpCommand(self.hostlist_clients, self.workdir)
        self.dcp_cmd.get_params(self)

        # Set the source params
        if src_type == "POSIX":
            self.dcp_cmd.set_params(
                src_path=str(src_path))
        elif src_type == "DAOS_UUID":
            self.dcp_cmd.set_params(
                src_path=format_daos_path(src_pool, src_cont, src_path))
        elif src_type == "DAOS_UNS":
            if src_cont:
                if src_path == "/":
                    self.dcp_cmd.set_params(
                        src_path=src_cont.path.value)
                else:
                    self.dcp_cmd.set_params(
                        daos_prefix=src_cont.path.value,
                        src_path=src_cont.path.value + src_path)

        # Set the destination params
        if dst_type == "POSIX":
            self.dcp_cmd.set_params(
                dst_path=str(dst_path))
        elif dst_type == "DAOS_UUID":
            self.dcp_cmd.set_params(
                dst_path=format_daos_path(dst_pool, dst_cont, dst_path))
        elif dst_type == "DAOS_UNS":
            if dst_cont:
                if dst_path == "/":
                    self.dcp_cmd.set_params(
                        dst_path=dst_cont.path.value)
                else:
                    self.dcp_cmd.set_params(
                        daos_prefix=dst_cont.path.value,
                        dst_path=dst_cont.path.value + dst_path)

    def _set_dsync_params(self,
                          src_type=None, src_path=None,
                          src_pool=None, src_cont=None,
                          dst_type=None, dst_path=None,
                          dst_pool=None, dst_cont=None):
        """Set the params for dsync.
        This is a wrapper for DsyncCommand.set_params.

        When both src_type and dst_type are DAOS_UNS, a prefix will
        only work for either the src or the dst, but not both.

        Args:
            src_type (str): how to interpret the src params.
                Must be in PARAM_TYPES.
            src_path (str): source cont path or posix path.
            src_pool (TestPool, optional): the source pool or uuid.
            src_cont (TestContainer, optional): the source cont or uuid.
            dst_type (str): how to interpret the dst params.
                Must be in PARAM_TYPES.
            dst_path (str): destination cont path or posix path.
            dst_pool (TestPool, optional): the destination pool or uuid.
            dst_cont (TestContainer, optional): the destination cont or uuid.

        """
        # First, initialize a new dsync command
        self.dsync_cmd = DsyncCommand(self.hostlist_clients, self.workdir)
        self.dsync_cmd.get_params(self)

        # Set the source params
        if src_type == "POSIX":
            self.dsync_cmd.set_params(
                src_path=str(src_path))
        elif src_type == "DAOS_UUID":
            self.dsync_cmd.set_params(
                src_path=format_daos_path(src_pool, src_cont, src_path))
        elif src_type == "DAOS_UNS":
            if src_cont:
                if src_path == "/":
                    self.dsync_cmd.set_params(
                        src_path=src_cont.path.value)
                else:
                    self.dsync_cmd.set_params(
                        daos_prefix=src_cont.path.value,
                        src_path=src_cont.path.value + src_path)

        # Set the destination params
        if dst_type == "POSIX":
            self.dsync_cmd.set_params(
                dst_path=str(dst_path))
        elif dst_type == "DAOS_UUID":
            self.dsync_cmd.set_params(
                dst_path=format_daos_path(dst_pool, dst_cont, dst_path))
        elif dst_type == "DAOS_UNS":
            if dst_cont:
                if dst_path == "/":
                    self.dsync_cmd.set_params(
                        dst_path=dst_cont.path.value)
                else:
                    self.dsync_cmd.set_params(
                        daos_prefix=dst_cont.path.value,
                        dst_path=dst_cont.path.value + dst_path)

    def _set_fs_copy_params(self,
                            src_type=None, src_path=None,
                            src_pool=None, src_cont=None,
                            dst_type=None, dst_path=None,
                            dst_pool=None, dst_cont=None):
        """Set the params for fs copy.

        daos fs copy does not support a "prefix" on UNS paths,
        so the param type for DAOS_UNS must have the path "/".

        Args:
            src_type (str): how to interpret the src params.
                Must be in PARAM_TYPES.
            src_path (str): source cont path or posix path.
            src_pool (TestPool, optional): the source pool or uuid.
            src_cont (TestContainer, optional): the source cont or uuid.
            dst_type (str): how to interpret the dst params.
                Must be in PARAM_TYPES.
            dst_path (str): destination cont path or posix path.
            dst_pool (TestPool, optional): the destination pool or uuid.
            dst_cont (TestContainer, optional): the destination cont or uuid.

        """
        if src_type is not None:
            src_type = self._validate_param_type(src_type)
        if dst_type is not None:
            dst_type = self._validate_param_type(dst_type)

        if not src_type and (src_path or src_pool or src_cont):
            self.fail("src params require src_type")
        if not dst_type and (dst_path or dst_pool or dst_cont):
            self.fail("dst params require dst_type")

        # First, initialize a new fs copy command
        self.fs_copy_cmd = FsCopy(self.daos_cmd, self.log)

        # Set the source params
        if src_type == "POSIX":
            self.fs_copy_cmd.set_fs_copy_params(
                src=str(src_path))
        elif src_type == "DAOS_UUID":
            self.fs_copy_cmd.set_fs_copy_params(
                src=format_daos_path(src_pool, src_cont, src_path))
        elif src_type == "DAOS_UNS":
            path = ""
            if src_cont:
                if src_path == "/":
                    path = str(src_cont.path)
                else:
                    self.fail("daos fs copy does not support a prefix")
            self.fs_copy_cmd.set_fs_copy_params(
                src=path)

        # Set the destination params
        if dst_type == "POSIX":
            self.fs_copy_cmd.set_fs_copy_params(
                dst=str(dst_path))
        elif dst_type == "DAOS_UUID":
            self.fs_copy_cmd.set_fs_copy_params(
                dst=format_daos_path(dst_pool, dst_cont, dst_path))
        elif dst_type == "DAOS_UNS":
            path = ""
            if dst_cont:
                if dst_path == "/":
                    path = str(dst_cont.path)
                else:
                    self.fail("daos fs copy does not support a prefix")
            self.fs_copy_cmd.set_fs_copy_params(
                dst=path)

    def _set_cont_clone_params(self,
                               src_pool=None, src_cont=None,
                               dst_pool=None, dst_cont=None):
        """Set the params for daos cont clone.

        This only supports DAOS -> DAOS copies.

        Args:
            src_pool (TestPool, optional): the source pool or uuid.
            src_cont (TestContainer, optional): the source cont or uuid.
            dst_pool (TestPool, optional): the destination pool or uuid.
            dst_cont (TestContainer, optional): the destination cont or uuid.

        """
        # First, initialize a new cont copy command
        self.cont_clone_cmd = ContClone(self.daos_cmd, self.log)

        # Set the source params
        if src_pool or src_cont:
            self.cont_clone_cmd.set_cont_clone_params(
                src=format_daos_path(src_pool, src_cont))

        # Set the destination params
        if dst_pool or dst_cont:
            self.cont_clone_cmd.set_cont_clone_params(
                dst=format_daos_path(dst_pool, dst_cont))

    def _set_dserial_params(self,
                            src_pool=None, src_cont=None,
                            dst_pool=None):
        """Set the params for daos-serialize and daos-deserialize.

        This uses a temporary POSIX path as the intermediate step
        between serializing and deserializing.

        Args:
            src_pool (TestPool, optional): the source pool or uuid.
            src_cont (TestContainer, optional): the source cont or uuid.
            dst_pool (TestPool, optional): the destination pool or uuid.

        """
        # First initialize new commands
        self.dserialize_cmd = DserializeCommand(self.hostlist_clients,
                                                self.workdir)
        self.ddeserialize_cmd = DdeserializeCommand(self.hostlist_clients,
                                                    self.workdir)

        # Get an intermediate path for HDF5 file(s)
        tmp_path = self.new_posix_test_path(create=False,
                                            parent=self.serial_tmp_dir)

        # Set the source params for dserialize
        if src_pool or src_cont:
            self.dserialize_cmd.set_params(
                src_path=format_daos_path(src_pool, src_cont),
                output_path=tmp_path)

        # Set the destination params for ddeserialize
        if dst_pool:
            self.ddeserialize_cmd.set_params(
                src_path=tmp_path,
                pool=uuid_from_obj(dst_pool))


    def set_ior_params(self, param_type, path, pool=None, cont=None,
                       path_suffix=None, flags=None, display=True):
        """Set the ior params.

        Args:
            param_type (str): how to interpret the params.
            path (str): cont path or posix path.
            pool (TestPool, optional): the pool object
            cont (TestContainer, optional): the cont or uuid.
            path_suffix (str, optional): suffix to append to the path.
                E.g. path="/some/path", path_suffix="testFile"
            flags (str, optional): ior_cmd flags to set
            display (bool, optional): print updated params. Defaults to True.

        """
        param_type = self._validate_param_type(param_type)

        # Reset params
        self.ior_cmd.api.update(None)
        self.ior_cmd.test_file.update(None)
        self.ior_cmd.dfs_pool.update(None)
        self.ior_cmd.dfs_cont.update(None)
        self.ior_cmd.dfs_group.update(None)

        if flags:
            self.ior_cmd.flags.update(flags, "flags" if display else None)

        display_api = "api" if display else None
        display_test_file = "test_file" if display else None

        # Allow cont to be either the container or the uuid
        cont_uuid = uuid_from_obj(cont)

        # Optionally append suffix
        if path_suffix:
            if path_suffix[0] == "/":
                path_suffix = path_suffix[1:]
            path = join(path, path_suffix)

        if param_type == "POSIX":
            self.ior_cmd.api.update("POSIX", display_api)
            self.ior_cmd.test_file.update(path, display_test_file)
        elif param_type in ("DAOS_UUID", "DAOS_UNS"):
            self.ior_cmd.api.update("DFS", display_api)
            self.ior_cmd.test_file.update(path, display_test_file)
            if pool and cont_uuid:
                self.ior_cmd.set_daos_params(self.server_group,
                                             pool, cont_uuid)
            elif pool:
                self.ior_cmd.set_daos_params(self.server_group,
                                             pool, None)

    def run_ior_with_params(self, param_type, path, pool=None, cont=None,
                            path_suffix=None, flags=None, display=True,
                            display_space=False):
        """Set the ior params and run ior.

        Args:
            param_type: see set_ior_params
            path: see set_ior_params
            pool: see set_ior_params
            cont: see set_ior_params
            path_suffix: see set_ior_params
            flags: see set_ior_params
            display (bool, optional): print updated params. Defaults to True.
            display_space (bool, optional): whether to display the pool space.
                Defaults to False.

        """
        self.set_ior_params(param_type, path, pool, cont,
                            path_suffix, flags, display)
        self.run_ior(self.get_ior_job_manager_command(), self.ior_processes,
                     display_space=(display_space and bool(pool)),
                     pool=pool)

    def set_mdtest_params(self, param_type, path, pool=None, cont=None,
                          flags=None, display=True):
        """Set the mdtest params.

        Args:
            param_type (str): how to interpret the params.
            path (str): cont path or posix path.
            pool (TestPool, optional): the pool object.
            cont (TestContainer, optional): the cont or uuid.
            flags (str, optional): mdtest_cmd flags to set
            display (bool, optional): print updated params. Defaults to True.

        """
        param_type = self._validate_param_type(param_type)

        # Reset params
        self.mdtest_cmd.api.update(None)
        self.mdtest_cmd.test_dir.update(None)
        self.mdtest_cmd.dfs_pool_uuid.update(None)
        self.mdtest_cmd.dfs_cont.update(None)
        self.mdtest_cmd.dfs_group.update(None)

        if flags:
            self.mdtest_cmd.flags.update(flags, "flags" if display else None)

        display_api = "api" if display else None
        display_test_dir = "test_dir" if display else None

        # Allow cont to be either the container or the uuid
        cont_uuid = uuid_from_obj(cont)

        if param_type == "POSIX":
            self.mdtest_cmd.api.update("POSIX", display_api)
            self.mdtest_cmd.test_dir.update(path, display_test_dir)
        elif param_type in ("DAOS_UUID", "DAOS_UNS"):
            self.mdtest_cmd.api.update("DFS", display_api)
            self.mdtest_cmd.test_dir.update(path, display_test_dir)
            if pool and cont_uuid:
                self.mdtest_cmd.set_daos_params(self.server_group,
                                                pool, cont_uuid)
            elif pool:
                self.mdtest_cmd.set_daos_params(self.server_group,
                                                pool, None)

    def run_mdtest_with_params(self, param_type, path, pool=None, cont=None,
                               flags=None, display=True):
        """Set the mdtest params and run mdtest.

        Args:
            param_type: see set_ior_params
            path: see set_mdtest_params
            pool: see set_mdtest_params
            cont: see set_mdtest_params
            flags see set_mdtest_params
            display (bool, optional): print updated params. Defaults to True.

        """
        self.set_mdtest_params(param_type, path, pool, cont, flags, display)
        self.run_mdtest(self.get_mdtest_job_manager_command(self.manager),
                        self.mdtest_processes,
                        display_space=(bool(pool)), pool=pool)

    def run_diff(self, src, dst, deref=False):
        """Run linux diff command.

        Args:
            src (str): the source path
            dst (str): the destination path
            deref (bool, optional): Whether to dereference symlinks.
                Defaults to False.

        """
        deref_str = ""
        if not deref:
            deref_str = "--no-dereference"

        cmd = "diff -r {} '{}' '{}'".format(
            deref_str, src, dst)
        self.execute_cmd(cmd)

    # pylint: disable=too-many-arguments
    def run_datamover(self, test_desc=None,
                      src_type=None, src_path=None,
                      src_pool=None, src_cont=None,
                      dst_type=None, dst_path=None,
                      dst_pool=None, dst_cont=None,
                      expected_rc=0, expected_output=None, expected_err=None,
                      processes=None):
        """Run the corresponding command specified by self.tool.
        Calls set_datamover_params if and only if any are passed in.

        Args:
            test_desc (str, optional): description to print before running
            src_type: see set_datamover_params
            src_path: see set_datamover_params
            src_pool: see set_datamover_params
            src_cont: see set_datamover_params
            dst_type: see set_datamover_params
            dst_path: see set_datamover_params
            dst_cont: see set_datamover_params
            expected_rc (int, optional): rc expected to be returned
            expected_output (list, optional): substrings expected in stdout
            expected_err (list, optional): substrings expected in stderr
            processes (int, optional): number of mpi processes.
                defaults to self.dcp_processes

        Returns:
            The result "run" object

        """
        self.num_run_datamover += 1
        self.log.info("run_datamover called %s times",
                      str(self.num_run_datamover))

        # Set the params if and only if any were passed in
        have_src_params = (src_type or src_path or src_pool or src_cont)
        have_dst_params = (dst_type or dst_path or dst_pool or dst_cont)
        if have_src_params or have_dst_params:
            self.set_datamover_params(
                src_type, src_path, src_pool, src_cont,
                dst_type, dst_path, dst_pool, dst_cont)

        # Default expected_output and expected_err to empty lists
        if not expected_output:
            expected_output = []
        if not expected_err:
            expected_err = []

        # Convert singular value to list
        if not isinstance(expected_output, list):
            expected_output = [expected_output]
        if not isinstance(expected_err, list):
            expected_err = [expected_err]

        if test_desc is not None:
            self.log.info("Running %s: %s", self.tool, test_desc)

        try:
            if self.tool == "DCP":
                if not processes:
                    processes = self.dcp_processes
                # If we expect an rc other than 0, don't fail
                self.dcp_cmd.exit_status_exception = (expected_rc == 0)
                result = self.dcp_cmd.run(processes)
            elif self.tool == "DSYNC":
                if not processes:
                    processes = self.dsync_processes
                # If we expect an rc other than 0, don't fail
                self.dsync_cmd.exit_status_exception = (expected_rc == 0)
                result = self.dsync_cmd.run(processes)
            elif self.tool== "DSERIAL":
                if processes:
                    processes1 = processes2 = processes
                else:
                    processes1 = self.dserialize_processes
                    processes2 = self.ddeserialize_processes
                result = self.dserialize_cmd.run(processes1)
                result = self.ddeserialize_cmd.run(processes2)
            elif self.tool == "FS_COPY":
                result = self.fs_copy_cmd.run()
            elif self.tool == "CONT_CLONE":
                result = self.cont_clone_cmd.run()
            else:
                self.fail("Invalid tool: {}".format(str(self.tool)))
        except CommandFailure as error:
            self.log.error("%s command failed: %s", str(self.tool), str(error))
            self.fail("Test was expected to pass but it failed: {}\n".format(
                test_desc))

        # Check the return code
        actual_rc = result.exit_status
        if actual_rc != expected_rc:
            self.fail("Expected (rc={}) but got (rc={}): {}\n".format(
                expected_rc, actual_rc, test_desc))

        # Check for expected output
        for s in expected_output:
            if s not in result.stdout_text:
                self.fail("stdout expected {}: {}".format(s, test_desc))
        for s in expected_err:
            if s not in result.stderr_text:
                self.fail("stderr expected {}: {}".format(s, test_desc))

        return result
