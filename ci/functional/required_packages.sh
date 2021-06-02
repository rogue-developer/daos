#!/bin/bash

set -eux

distro="$1"
client_ver="$2"

pkgs="openmpi3 hwloc ndctl fio     \
      patchutils ior               \
      romio-tests                  \
      testmpio                     \
      mpi4py-tests                 \
      hdf5-mpich-tests             \
      hdf5-openmpi3-tests          \
      hdf5-vol-daos-mpich-tests    \
      hdf5-vol-daos-openmpi3-tests \
      MACSio-mpich                 \
      MACSio-openmpi3              \
      mpifileutils-mpich"

if [[ $distro = el8* ]] || [[ $distro = centos8* ]]; then
    pkgs="openmpi hwloc ndctl fio     \
          patchutils ior              \
          romio-tests                 \
          testmpio                    \
          mpi4py-tests                \
          hdf5-mpich-tests            \
          hdf5-openmpi-tests          \
          hdf5-vol-daos-mpich-tests   \
          hdf5-vol-daos-openmpi-tests \
          MACSio-mpich                \
          MACSio-openmpi             \
          mpifileutils-mpich"
fi

if [[ $distro = el7* ]] || [[ $distro = centos7* ]]; then
    pkgs="--exclude openmpi $pkgs"
fi

if [[ $distro = ubuntu20* ]]; then
    pkgs="openmpi-bin ndctl fio"
fi

echo "$pkgs"
exit 0
