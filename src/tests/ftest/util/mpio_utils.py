#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import os
from env_modules import load_mpi
from command_utils_base import EnvironmentVariables
from general_utils import run_command, DaosTestError


class MpioFailed(Exception):
    """Raise if MPIO failed."""


class MpioUtils():
    """MpioUtils Class."""

    def __init__(self):
        """Initialize a MpioUtils object."""
        self.mpichinstall = None

    def mpich_installed(self, hostlist):
        """Check if mpich is installed.

        Args:
            hostlist (list): list of hosts

        Returns:
            bool: whether mpich is installed on the first host in the list

        """
        if not load_mpi('mpich'):
            print("Failed to load mpich")
            return False

        # checking mpich install
        cmd = "set -e; "                                           \
              "export MODULEPATH=/usr/share/modulefiles:"          \
                                 "/usr/share/modules:"             \
                                 "/etc/modulefiles; "              \
              "for mod in mpi/mpich-x86_64 gnu-mpich; do "         \
                  "if module is-avail $mod >/dev/null 2>&1; then " \
                      "module load $mod >/dev/null 2>&1; "         \
                      "break; "                                    \
                  "fi; "                                           \
              "done; "                                             \
              "command -v mpichversion"
        cmd = '/usr/bin/ssh {} {}'.format(hostlist[0], cmd)
        try:
            result = run_command(cmd)
            self.mpichinstall = \
                result.stdout_text.rstrip()[:-len('bin/mpichversion')]
            return True

        except DaosTestError as excep:
            print("Mpich not installed \n {}".format(excep))
            return False
        return False

    # pylint: disable=R0913
    def run_mpiio_tests(self, hostfile, pool_uuid, test_repo,
                        test_name, client_processes, cont_uuid):
        """Run the LLNL, MPI4PY, and HDF5 testsuites.

        Args:
            hostfile (str): client hostfile
            pool_uuid (str): pool UUID
            test_repo (str): test repo location
            test_name (str): name of test to be tested
            client_processes (int): number of client processes
            cont_uuid (str): container UUID

        Raises:
            MpioFailed: for an invalid test name or test execution failure

        Return:
            CmdResult: an avocado.utils.process CmdResult object containing the
                result of the command execution.

        """
        print("self.mpichinstall: {}".format(self.mpichinstall))

        # environment variables only to be set on client node
        env = EnvironmentVariables()
        env["DAOS_POOL"] = "{}".format(pool_uuid)
        env["DAOS_CONT"] = "{}".format(cont_uuid)
        env["DAOS_BYPASS_DUNS"] = "1"
        mpirun = os.path.join(self.mpichinstall, "bin", "mpirun")

        executables = {
            "romio": [os.path.join(test_repo, "runtests")],
            "llnl": [os.path.join(test_repo, "testmpio_daos")],
            "mpi4py": [os.path.join(test_repo, "test_io_daos.py")],
            "hdf5": [
                os.path.join(test_repo, "testphdf5"),
                os.path.join(test_repo, "t_shapesame")
            ]
        }

        # Verify the test name is valid
        if test_name not in executables:
            raise MpioFailed(
                "Invalid test name: {} not supported".format(test_name))

        # Verify the executables exist for the valid test name
        if not all([os.path.join(exe) for exe in executables[test_name]]):
            raise MpioFailed(
                "Missing test name: {} missing executables {}".format(
                    test_name, ", ".join(executables[test_name])))

        # Setup the commands to run for this test name
        commands = []
        if test_name == "romio":
            commands.append(
                "{} -fname=daos:test1 -subset".format(
                    executables[test_name][0]))
        elif test_name == "llnl":
            env["MPIO_USER_PATH"] = "daos:"
            for exe in executables[test_name]:
                commands.append(
                    "{} -np {} --hostfile {} {} 1".format(
                        mpirun, client_processes, hostfile, exe))
        elif test_name == "mpi4py":
            for exe in executables[test_name]:
                commands.append(
                    "{} -np {} --hostfile {} python {}".format(
                        mpirun, client_processes, hostfile, exe))
        elif test_name == "hdf5":
            env["HDF5_PARAPREFIX"] = "daos:"
            for exe in executables[test_name]:
                commands.append(
                    "{} -np {} --hostfile {} {}".format(
                        mpirun, client_processes, hostfile, exe))

        for command in commands:
            print("run command: {}".format(command))
            try:
                result = run_command(
                    command, timeout=None, verbose=True, env=env)

            except DaosTestError as excep:
                raise MpioFailed(
                    "<Test FAILED> \nException occurred: {}".format(
                        str(excep))) from excep

        return result
