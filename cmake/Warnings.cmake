# Strict warnings for project targets. JUCE and Catch2 are compiled with their own
# flags and their headers are included as system headers, so only our code is
# held to these rules.

option(DAW_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in project targets" ON)

function(daw_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /external:W0
            $<$<BOOL:${DAW_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
            -Woverloaded-virtual -Wformat=2 -Wimplicit-fallthrough
            $<$<CXX_COMPILER_ID:GNU>:-Wmisleading-indentation -Wduplicated-cond -Wlogical-op>
            $<$<BOOL:${DAW_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()
