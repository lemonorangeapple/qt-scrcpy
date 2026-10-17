# Copies the files of the unpacked scrcpy runtime into a target's output directory:
#   cmake -DSRC=<runtime dir> -DDST=<output dir> -P copy_runtime.cmake
#
# Files that are already identical are left alone; the others are written under a temporary
# name and renamed into place. Several targets may share one output directory and be built in
# parallel, and a program may be running from it: nobody ever sees or produces a half-written
# file (a plain `cmake -E copy_directory` fails when two copies write the same file at once).

if(NOT SRC OR NOT DST)
    message(FATAL_ERROR "copy_runtime.cmake: SRC and DST are required")
endif()

# SRC may come from the user's directory names: keep [ ] ? * from acting as wildcards.
string(REGEX REPLACE "[[?*]" "[\\0]" src_glob "${SRC}")
file(GLOB items RELATIVE "${SRC}" "${src_glob}/*")
file(MAKE_DIRECTORY "${DST}")

foreach(name IN LISTS items)
    set(from "${SRC}/${name}")
    set(to "${DST}/${name}")
    if(IS_DIRECTORY "${from}")
        continue()
    endif()

    set(identical FALSE)
    if(EXISTS "${to}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${from}" "${to}"
                        RESULT_VARIABLE compared OUTPUT_QUIET ERROR_QUIET)
        if(compared EQUAL 0)
            set(identical TRUE)
        endif()
    endif()
    if(identical)
        continue()
    endif()

    # A name that two simultaneous copies will not pick together: time plus a random part.
    string(TIMESTAMP now "%s%f")
    string(MD5 seed "${to}${now}")
    string(SUBSTRING "${seed}" 0 8 unique)
    set(temporary "${DST}/.qtscrcpy-copy-${unique}")
    file(MAKE_DIRECTORY "${temporary}")
    file(COPY "${from}" DESTINATION "${temporary}") # keeps the permissions of the original
    file(RENAME "${temporary}/${name}" "${to}")
    file(REMOVE_RECURSE "${temporary}")
endforeach()
