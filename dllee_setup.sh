#!/bin/bash

export CILANTRO_BASEDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export CILANTRO_INCDIR=${CILANTRO_BASEDIR}/include
export CILANTRO_LIBDIR=${CILANTRO_BASEDIR}/build
export CILANTRO_BINDIR=${CILANTRO_BASEDIR}/bin

# ADD PATHS
[[ ":$PATH:" != *":${CILANTRO_BINDIR}:"* ]] && export PATH="${CILANTRO_BINDIR}:${PATH}"
[[ ":$LD_LIBRARY_PATH:" != *":${CILANTRO_LIBDIR}:"* ]] && export LD_LIBRARY_PATH="${CILANTRO_LIBDIR}:${LD_LIBRARY_PATH}"


