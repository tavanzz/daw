# Applies PATCH_FILE to the current directory with `patch -p1`, once: sources the patch is
# already in are left alone, so that FetchContent may run the step again.
#   cmake -DPATCH_FILE=<file> -P ApplyPatch.cmake

execute_process(
    COMMAND patch -p1 --reverse --dry-run --force --silent --input "${PATCH_FILE}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET)

if(already_applied EQUAL 0)
    message(STATUS "Already applied: ${PATCH_FILE}")
    return()
endif()

execute_process(
    COMMAND patch -p1 --forward --input "${PATCH_FILE}"
    RESULT_VARIABLE result)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not apply ${PATCH_FILE}")
endif()
