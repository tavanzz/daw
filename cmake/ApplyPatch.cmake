# Applies PATCH_FILE to the sources in the current directory (`patch -p1`), exactly (no
# fuzz) or not at all. Sources the patch is already in are left alone, so FetchContent may
# run the step again; sources it no longer fits (after a dependency update, say) stop the
# configure untouched, with a message saying what to do.
#   cmake -DPATCH_FILE=<file> -P ApplyPatch.cmake

find_program(DAW_PATCH_PROGRAM patch)
if(NOT DAW_PATCH_PROGRAM)
    message(FATAL_ERROR "The program `patch` is needed to apply ${PATCH_FILE}.")
endif()

execute_process(
    COMMAND "${DAW_PATCH_PROGRAM}" -p1 --reverse --dry-run --force --silent --fuzz=0
            --input "${PATCH_FILE}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET)

if(already_applied EQUAL 0)
    message(STATUS "Already applied: ${PATCH_FILE}")
    return()
endif()

execute_process(
    COMMAND "${DAW_PATCH_PROGRAM}" -p1 --forward --dry-run --force --fuzz=0
            --input "${PATCH_FILE}"
    RESULT_VARIABLE fits
    OUTPUT_VARIABLE report
    ERROR_VARIABLE report)

if(NOT fits EQUAL 0)
    message(FATAL_ERROR
        "${PATCH_FILE} no longer applies to the sources in ${CMAKE_CURRENT_SOURCE_DIR}, which"
        " were left unchanged. If the dependency was updated, follow"
        " cmake/patches/README.md: delete the patch if the new version fixes what it is for,"
        " otherwise make it again against the new sources.\n"
        "What patch says:\n${report}")
endif()

execute_process(
    COMMAND "${DAW_PATCH_PROGRAM}" -p1 --forward --force --silent --fuzz=0
            --input "${PATCH_FILE}"
    RESULT_VARIABLE result)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not apply ${PATCH_FILE} in ${CMAKE_CURRENT_SOURCE_DIR}.")
endif()
message(STATUS "Applied: ${PATCH_FILE}")
