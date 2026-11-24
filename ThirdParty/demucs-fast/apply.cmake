# Applies Stem Splitter's speed-ups to a demucs.cpp checkout. Idempotent: safe to run on every configure.
#   cmake -DDEMUCS_DIR=<checkout> -DPATCH_DIR=<this folder> -P apply.cmake
#
#  - conv.hpp      : faster drop-in replacement (same templates, same results)
#  - layers.patch  : faster group/layer norm and attention softmax (same results)

file(READ "${DEMUCS_DIR}/src/layers.cpp" layers)
string(FIND "${layers}" "Stem Splitter patch" already_patched)

if(already_patched EQUAL -1)
    execute_process(
        COMMAND git apply --whitespace=nowarn "${PATCH_DIR}/layers.patch"
        WORKING_DIRECTORY "${DEMUCS_DIR}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)

    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Couldn't apply layers.patch to demucs.cpp: ${error}")
    endif()

    message(STATUS "demucs.cpp: applied layers.patch")
endif()

configure_file("${PATCH_DIR}/conv.hpp" "${DEMUCS_DIR}/src/conv.hpp" COPYONLY)
