#!/bin/bash

export CILANTRO_BASEDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export CILANTRO_INC_DIR=${CILANTRO_BASEDIR}/include
export CILANTRO_LIB_DIR=${CILANTRO_BASEDIR}/build
export CILANTRO_BIN_DIR=${CILANTRO_BASEDIR}/bin

# ADD PATHS
[[ ":$PATH:" != *":${CILANTRO_BIN_DIR}:"* ]] && export PATH="${CILANTRO_BIN_DIR}:${PATH}"
[[ ":$LD_LIBRARY_PATH:" != *":${CILANTRO_LIB_DIR}:"* ]] && export LD_LIBRARY_PATH="${CILANTRO_LIB_DIR}:${LD_LIBRARY_PATH}"


