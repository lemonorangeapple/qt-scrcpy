# Ships the official scrcpy release package (scrcpy, scrcpy-server, adb and the libraries
# they need) next to an executable, so the application works without scrcpy/adb being
# installed on the machine.
#
#   list(APPEND CMAKE_MODULE_PATH "<qt-scrcpy>/cmake")
#   include(QtScrcpyBundle)
#   qtscrcpy_bundle_runtime(myapp)
#
# The package is downloaded at configure time (once per download directory), checked
# against a pinned SHA-256 and unpacked. Every link of the target then copies the files
# next to its executable, where Adb and Scrcpy find them (after $ADB / $SCRCPY_EXECUTABLE,
# which win when set), and `cmake --install` installs them together with
# THIRD_PARTY_NOTICES.md and the licence texts (the packages ship none).
#
# Cache variables:
#   QTSCRCPY_BUNDLE_SCRCPY   AUTO | ON | OFF. ON bundles and fails if that is not possible.
#                            AUTO bundles where the scrcpy project publishes a package and
#                            otherwise warns and carries on without. OFF does nothing.
#                            (Default ON here; the qt-scrcpy project itself defaults to AUTO
#                            when it is the top-level project and to OFF when it is not.)
#   QTSCRCPY_SCRCPY_VERSION  scrcpy release to bundle; empty = the pinned one. Any other
#                            version needs QTSCRCPY_SCRCPY_SHA256.
#   QTSCRCPY_SCRCPY_SHA256   SHA-256 of the package.
#   QTSCRCPY_SCRCPY_PLATFORM linux-x86_64 | win64 | win32 | macos-aarch64 | macos-x86_64.
#                            Detected from the target system when empty.
#   QTSCRCPY_SCRCPY_MIRROR   Base URL (http, https or file) to download from instead of the
#                            GitHub release page; the package name is appended. The
#                            checksum still applies.
#   QTSCRCPY_SCRCPY_ARCHIVE  Path of an already downloaded package (offline builds);
#                            relative paths are taken relative to the source directory.
#   QTSCRCPY_DOWNLOAD_DIR    Where packages are downloaded and unpacked.
#
# Functions:
#   qtscrcpy_bundle_prepare(<var>)
#       Resolves the options, fetches the package if needed and sets <var> to TRUE when a
#       package is available (FALSE when bundling is OFF or was skipped in AUTO mode).
#   qtscrcpy_bundle_runtime(<target> [NO_INSTALL] [INSTALL_DESTINATION <dir>])
#       Copies the package next to <target>'s executable after every link and, unless
#       NO_INSTALL is given, installs it into <dir> (default: CMAKE_INSTALL_BINDIR - give
#       the directory the executable is installed to, the files must be adjacent).
#
# Needs CMake 3.16 or newer. State is kept in global properties and internal cache entries, not in the scope that
# included this file, so the functions work when called from any directory.
# The unpacked files and their scrcpy version are available in QTSCRCPY_BUNDLE_ROOT and
# QTSCRCPY_BUNDLE_VERSION (internal cache entries).

include_guard(GLOBAL)
include(CMakeParseArguments)

set_property(GLOBAL PROPERTY QTSCRCPY_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Release the checksums below belong to.
function(_qtscrcpy_pinned_version out)
    set(${out} "3.3.3" PARENT_SCOPE)
endfunction()

if(NOT DEFINED CACHE{QTSCRCPY_BUNDLE_SCRCPY})
    set(QTSCRCPY_BUNDLE_SCRCPY "ON" CACHE STRING
        "Bundle scrcpy and adb next to executables: AUTO (where scrcpy publishes a package), ON (required) or OFF")
endif()
set_property(CACHE QTSCRCPY_BUNDLE_SCRCPY PROPERTY STRINGS AUTO ON OFF)
set(QTSCRCPY_SCRCPY_VERSION "" CACHE STRING
    "scrcpy release to bundle (empty: the pinned one; any other version needs QTSCRCPY_SCRCPY_SHA256)")
set(QTSCRCPY_SCRCPY_SHA256 "" CACHE STRING "SHA-256 of the scrcpy package (required for a version without a pinned checksum)")
set(QTSCRCPY_SCRCPY_PLATFORM "" CACHE STRING
    "scrcpy package platform: linux-x86_64, win64, win32, macos-aarch64 or macos-x86_64 (empty: detect)")
set(QTSCRCPY_SCRCPY_MIRROR "" CACHE STRING "Base URL (http, https or file) to download the scrcpy package from instead of GitHub")
set(QTSCRCPY_SCRCPY_ARCHIVE "" CACHE FILEPATH "Use this already downloaded scrcpy package instead of downloading one")
set(QTSCRCPY_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/qtscrcpy-downloads" CACHE PATH
    "Directory for downloaded and unpacked scrcpy packages")

# Pinned SHA-256 of the official release packages of the pinned version. They are the values
# the scrcpy project publishes next to its download links (doc/linux.md, doc/windows.md,
# doc/macos.md at the v3.3.3 tag) and match what was downloaded.
function(_qtscrcpy_pinned_sha256 platform out)
    if(platform STREQUAL "linux-x86_64")
        set(hash "9b30e813e8191329ba8025dc80cb0f198fb0a318960a3b5c15395cf675c9c638")
    elseif(platform STREQUAL "win64")
        set(hash "4b458d33d0436688c69875cd267cae6fa8be08aa3c17772edf3a940a3dc4b17e")
    elseif(platform STREQUAL "win32")
        set(hash "e3d43e21c0bd6e070381c390c1e4cccd48a1e71ae73a8c217e6e6b8506598c79")
    elseif(platform STREQUAL "macos-aarch64")
        set(hash "b93299468f19ae89ac70f7c1453914c41f1f2bcd31f6ab530038da885c19581f")
    elseif(platform STREQUAL "macos-x86_64")
        set(hash "c767fc1d41e4ae26e40558656570962f474739924fd22ee023d8754889ee4366")
    else()
        set(hash "")
    endif()
    set(${out} "${hash}" PARENT_SCOPE)
endfunction()

# AUTO, ON or OFF.
function(_qtscrcpy_bundle_mode out)
    string(TOUPPER "${QTSCRCPY_BUNDLE_SCRCPY}" value)
    if(value STREQUAL "AUTO")
        set(mode "AUTO")
    elseif(value MATCHES "^(ON|TRUE|YES|Y|1)$")
        set(mode "ON")
    elseif(value STREQUAL "" OR value MATCHES "^(OFF|FALSE|NO|N|0|IGNORE)$" OR value MATCHES "-NOTFOUND$")
        set(mode "OFF")
    else()
        message(FATAL_ERROR "qt-scrcpy: QTSCRCPY_BUNDLE_SCRCPY must be AUTO, ON or OFF (is '${QTSCRCPY_BUNDLE_SCRCPY}').")
    endif()
    set(${out} "${mode}" PARENT_SCOPE)
endfunction()

# Maps the target system to the name scrcpy uses in its package file names; empty when the
# scrcpy project publishes no package for it. <processor_out> receives the processor the
# decision was based on (for messages).
function(_qtscrcpy_detect_platform out processor_out)
    set(processor "${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES architectures)
        if(architectures EQUAL 1)
            set(processor "${CMAKE_OSX_ARCHITECTURES}")
        else()
            message(WARNING
                "qt-scrcpy: CMAKE_OSX_ARCHITECTURES lists several architectures ('${CMAKE_OSX_ARCHITECTURES}'), "
                "but scrcpy's macOS packages are single-architecture. Bundling the one for ${CMAKE_SYSTEM_PROCESSOR}; "
                "set QTSCRCPY_SCRCPY_PLATFORM to choose.")
        endif()
    endif()
    string(TOLOWER "${processor}" lower_processor)
    if(lower_processor MATCHES "^(x86_64|amd64|x64)$")
        set(arch "x86_64")
    elseif(lower_processor MATCHES "^(i[3-6]86|x86)$")
        set(arch "x86")
    elseif(lower_processor MATCHES "^(arm64|aarch64)$")
        set(arch "aarch64")
    else()
        set(arch "${lower_processor}")
    endif()

    set(platform "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND arch STREQUAL "x86_64")
        set(platform "linux-x86_64")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # CMAKE_SYSTEM_PROCESSOR is the host's on a native build, so a 32-bit compiler on a
        # 64-bit machine is told apart by the size of a pointer.
        if(arch STREQUAL "x86" OR (arch STREQUAL "x86_64" AND CMAKE_SIZEOF_VOID_P EQUAL 4))
            set(platform "win32")
        elseif(arch STREQUAL "x86_64")
            set(platform "win64")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND arch STREQUAL "aarch64")
        set(platform "macos-aarch64")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND arch STREQUAL "x86_64")
        set(platform "macos-x86_64")
    endif()
    set(${out} "${platform}" PARENT_SCOPE)
    set(${processor_out} "${processor}" PARENT_SCOPE)
endfunction()

# ".exe" for the Windows packages (and for a custom package of a Windows target), else "".
function(_qtscrcpy_exe_suffix platform out)
    if(platform MATCHES "^win" OR (platform STREQUAL "custom" AND CMAKE_SYSTEM_NAME STREQUAL "Windows"))
        set(${out} ".exe" PARENT_SCOPE)
    else()
        set(${out} "" PARENT_SCOPE)
    endif()
endfunction()

# Strips credentials from a URL before it is printed.
function(_qtscrcpy_redact_url url out)
    string(REGEX REPLACE "^([A-Za-z][A-Za-z0-9+.-]*://)[^/@]*@" "\\1***@" redacted "${url}")
    set(${out} "${redacted}" PARENT_SCOPE)
endfunction()

# Looks for entries that could write outside the extraction directory: absolute or
# parent-relative names, and anything that is not a plain file or directory (symbolic and
# hard links let a later entry be written through them). Sets <problem_out> to a description,
# or to "" when the archive is fine.
function(_qtscrcpy_check_archive_entries archive problem_out)
    set(${problem_out} "" PARENT_SCOPE)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar tvf "${archive}"
                    OUTPUT_VARIABLE listing RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        set(${problem_out} "cannot read it as an archive: ${error}" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "\r" "" listing "${listing}")
    string(REPLACE "\n" ";" lines "${listing}")
    foreach(line IN LISTS lines)
        if(line STREQUAL "")
            continue()
        endif()
        # `tar tv` style: the first column is the entry type, the last one the name.
        if(NOT line MATCHES "^[-d]")
            set(${problem_out} "it contains an entry that is not a plain file or directory (${line})" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    execute_process(COMMAND ${CMAKE_COMMAND} -E tar tf "${archive}"
                    OUTPUT_VARIABLE names RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        set(${problem_out} "cannot read it as an archive: ${error}" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "\r" "" names "${names}")
    string(REPLACE "\n" ";" name_list "${names}")
    foreach(name IN LISTS name_list)
        if(name MATCHES "^[/\\\\]" OR name MATCHES "^[A-Za-z]:" OR name MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)")
            set(${problem_out} "it contains the absolute or parent-relative path '${name}'" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

# Unpacks `archive` (already verified to have SHA-256 `expected`) into <extract_dir>,
# keeping only the files an application needs in <extract_dir>/runtime. Works in a
# staging directory and renames it into place, so an interrupted run never leaves
# something that looks complete.
function(_qtscrcpy_unpack archive expected platform extract_dir)
    set(staging "${extract_dir}.partial")
    file(REMOVE_RECURSE "${staging}")
    file(MAKE_DIRECTORY "${staging}")

    # Hash, list and extract one private copy: the file at `archive` could be swapped in
    # between those steps.
    set(private_copy "${staging}/package.bin")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${archive}" "${private_copy}"
                    RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        file(REMOVE_RECURSE "${staging}")
        message(FATAL_ERROR "qt-scrcpy: cannot copy ${archive}: ${error}")
    endif()
    file(SHA256 "${private_copy}" copy_hash)
    if(NOT copy_hash STREQUAL expected)
        file(REMOVE_RECURSE "${staging}")
        message(FATAL_ERROR "qt-scrcpy: ${archive} changed while it was being processed (SHA-256 ${copy_hash}).")
    endif()

    _qtscrcpy_check_archive_entries("${private_copy}" problem)
    if(problem)
        file(REMOVE_RECURSE "${staging}")
        message(FATAL_ERROR "qt-scrcpy: refusing to extract ${archive}: ${problem}.")
    endif()

    set(unpacked "${staging}/unpacked")
    file(MAKE_DIRECTORY "${unpacked}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${private_copy}"
                    WORKING_DIRECTORY "${unpacked}" RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        file(REMOVE_RECURSE "${staging}")
        message(FATAL_ERROR "qt-scrcpy: extracting ${archive} failed: ${error}")
    endif()

    # Packages unpack into one directory named after themselves.
    file(GLOB entries LIST_DIRECTORIES true RELATIVE "${unpacked}" "${unpacked}/*")
    list(LENGTH entries entry_count)
    if(entry_count EQUAL 1 AND IS_DIRECTORY "${unpacked}/${entries}")
        set(package_root "${unpacked}/${entries}")
    else()
        set(package_root "${unpacked}")
    endif()

    _qtscrcpy_exe_suffix("${platform}" suffix)
    # If scrcpy ever changes its package layout, fail now and not at run time.
    set(wanted "scrcpy${suffix}" "adb${suffix}" "scrcpy-server")
    foreach(required IN LISTS wanted)
        if(NOT EXISTS "${package_root}/${required}" OR IS_DIRECTORY "${package_root}/${required}")
            file(REMOVE_RECURSE "${staging}")
            message(FATAL_ERROR "qt-scrcpy: '${required}' is missing from ${archive} (or is not a file).")
        endif()
    endforeach()
    if(EXISTS "${package_root}/icon.png")
        list(APPEND wanted "icon.png") # scrcpy's window icon
    endif()
    if(suffix STREQUAL ".exe")
        file(GLOB dlls RELATIVE "${package_root}" "${package_root}/*.dll")
        list(APPEND wanted ${dlls})
    endif()

    # Only what the programs need (no man page, no .bat/.vbs helpers); file(COPY) keeps the
    # permissions stored in the archive.
    set(runtime "${staging}/runtime")
    file(MAKE_DIRECTORY "${runtime}")
    foreach(item IN LISTS wanted)
        file(COPY "${package_root}/${item}" DESTINATION "${runtime}")
    endforeach()
    file(WRITE "${staging}/.qtscrcpy-extracted" "${expected}\n")

    file(REMOVE_RECURSE "${unpacked}" "${private_copy}")
    file(REMOVE_RECURSE "${extract_dir}")
    file(RENAME "${staging}" "${extract_dir}")
endfunction()

# Sets <out> to TRUE when <dir> (an extraction directory) holds a complete runtime of the
# package with SHA-256 `expected`: the marker must name that hash and the programs must still
# be there.
function(_qtscrcpy_runtime_is_complete dir expected platform out)
    set(ok FALSE)
    _qtscrcpy_exe_suffix("${platform}" suffix)
    set(marker "${dir}/.qtscrcpy-extracted")
    if(EXISTS "${marker}")
        file(READ "${marker}" recorded)
        string(STRIP "${recorded}" recorded)
        if(recorded STREQUAL expected
           AND EXISTS "${dir}/runtime/scrcpy${suffix}" AND NOT IS_DIRECTORY "${dir}/runtime/scrcpy${suffix}"
           AND EXISTS "${dir}/runtime/adb${suffix}" AND NOT IS_DIRECTORY "${dir}/runtime/adb${suffix}"
           AND EXISTS "${dir}/runtime/scrcpy-server" AND NOT IS_DIRECTORY "${dir}/runtime/scrcpy-server")
            set(ok TRUE)
        endif()
    endif()
    set(${out} ${ok} PARENT_SCOPE)
endfunction()

# Resolves the options and, when bundling is wanted, makes the package available.
# Sets the global property QTSCRCPY_BUNDLE_STATE to "ready" or "skipped".
function(_qtscrcpy_resolve)
    _qtscrcpy_bundle_mode(mode)
    if(mode STREQUAL "OFF")
        message(STATUS "qt-scrcpy: not bundling scrcpy and adb (QTSCRCPY_BUNDLE_SCRCPY=OFF)")
        set_property(GLOBAL PROPERTY QTSCRCPY_BUNDLE_STATE "skipped")
        return()
    endif()

    # ---- which package ----
    set(known_platforms linux-x86_64 win64 win32 macos-aarch64 macos-x86_64)
    set(custom_package FALSE)
    if(QTSCRCPY_SCRCPY_ARCHIVE AND QTSCRCPY_SCRCPY_SHA256)
        set(custom_package TRUE) # the user vouches for the file; the platform name is only a label
    endif()
    if(QTSCRCPY_SCRCPY_PLATFORM)
        set(platform "${QTSCRCPY_SCRCPY_PLATFORM}")
        if(NOT platform MATCHES "^[A-Za-z0-9_.-]+$")
            message(FATAL_ERROR "qt-scrcpy: QTSCRCPY_SCRCPY_PLATFORM '${platform}' is not a valid platform name.")
        endif()
        list(FIND known_platforms "${platform}" platform_index)
        if(platform_index EQUAL -1 AND NOT custom_package)
            message(FATAL_ERROR
                "qt-scrcpy: unknown QTSCRCPY_SCRCPY_PLATFORM '${platform}'. scrcpy publishes: ${known_platforms}.\n"
                "(Any name is accepted together with QTSCRCPY_SCRCPY_ARCHIVE and QTSCRCPY_SCRCPY_SHA256.)")
        endif()
    else()
        _qtscrcpy_detect_platform(platform processor)
        if(NOT platform AND custom_package)
            set(platform "custom")
        endif()
    endif()
    if(NOT platform)
        set(problem "qt-scrcpy: the scrcpy project publishes no package for ${CMAKE_SYSTEM_NAME}/${processor}.")
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            string(APPEND problem " (On Windows for ARM the x64 package runs under emulation: set QTSCRCPY_SCRCPY_PLATFORM=win64.)")
        endif()
        if(mode STREQUAL "AUTO")
            message(WARNING "${problem}\nContinuing without bundling scrcpy and adb; they must be installed on the machine "
                            "(or set QTSCRCPY_SCRCPY_PLATFORM, or QTSCRCPY_SCRCPY_ARCHIVE with QTSCRCPY_SCRCPY_SHA256, "
                            "to bundle a package that runs there).")
            set_property(GLOBAL PROPERTY QTSCRCPY_BUNDLE_STATE "skipped")
            return()
        endif()
        message(FATAL_ERROR "${problem}\nUse -DQTSCRCPY_BUNDLE_SCRCPY=OFF (or AUTO) to use the scrcpy and adb installed on the "
                            "machine, or set QTSCRCPY_SCRCPY_PLATFORM to a package that runs there.")
    endif()

    _qtscrcpy_pinned_version(pinned_version)
    if(QTSCRCPY_SCRCPY_VERSION)
        set(version "${QTSCRCPY_SCRCPY_VERSION}")
    else()
        set(version "${pinned_version}")
    endif()
    if(NOT version MATCHES "^[0-9A-Za-z._+-]+$")
        message(FATAL_ERROR "qt-scrcpy: QTSCRCPY_SCRCPY_VERSION '${version}' is not a valid version.")
    endif()

    if(platform MATCHES "^win")
        set(extension "zip")
    else()
        set(extension "tar.gz")
    endif()
    set(file_name "scrcpy-${platform}-v${version}.${extension}")

    # ---- expected checksum: an explicit one, else the pinned one for the pinned release ----
    string(STRIP "${QTSCRCPY_SCRCPY_SHA256}" expected)
    if(expected)
        string(TOLOWER "${expected}" expected)
        string(LENGTH "${expected}" expected_length)
        # (CMake regular expressions have no {n} repetition.)
        if(NOT expected MATCHES "^[0-9a-f]+$" OR NOT expected_length EQUAL 64)
            message(FATAL_ERROR
                "qt-scrcpy: QTSCRCPY_SCRCPY_SHA256 must be 64 hexadecimal digits (is '${QTSCRCPY_SCRCPY_SHA256}').")
        endif()
    elseif(version STREQUAL pinned_version)
        _qtscrcpy_pinned_sha256("${platform}" expected)
    endif()
    if(NOT expected)
        message(FATAL_ERROR
            "qt-scrcpy: no checksum is pinned for ${file_name}.\n"
            "Set QTSCRCPY_SCRCPY_SHA256 to the SHA-256 of the package, or use -DQTSCRCPY_BUNDLE_SCRCPY=OFF.")
    endif()

    # Several configures (a CI matrix, parallel build trees) may share one download directory:
    # let them take turns, so the second finds a finished cache instead of racing the first.
    file(MAKE_DIRECTORY "${QTSCRCPY_DOWNLOAD_DIR}")
    file(LOCK "${QTSCRCPY_DOWNLOAD_DIR}/.qtscrcpy.lock" GUARD FUNCTION TIMEOUT 900 RESULT_VARIABLE lock_result)
    if(NOT lock_result EQUAL 0)
        message(FATAL_ERROR "qt-scrcpy: could not lock ${QTSCRCPY_DOWNLOAD_DIR} (${lock_result}); is another configure stuck?")
    endif()

    # ---- the archive: a local one, the cached download, or a fresh download ----
    if(QTSCRCPY_SCRCPY_ARCHIVE)
        get_filename_component(archive "${QTSCRCPY_SCRCPY_ARCHIVE}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(NOT EXISTS "${archive}")
            message(FATAL_ERROR "qt-scrcpy: QTSCRCPY_SCRCPY_ARCHIVE '${archive}' does not exist.")
        endif()
        file(SHA256 "${archive}" actual)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR
                "qt-scrcpy: SHA-256 mismatch for ${archive}\n  expected ${expected}\n  actual   ${actual}")
        endif()
    else()
        set(archive "${QTSCRCPY_DOWNLOAD_DIR}/${file_name}")
        set(cached FALSE)
        if(EXISTS "${archive}")
            file(SHA256 "${archive}" actual)
            if(actual STREQUAL expected)
                set(cached TRUE)
            endif()
        endif()
        if(NOT cached)
            if(QTSCRCPY_SCRCPY_MIRROR)
                if(NOT QTSCRCPY_SCRCPY_MIRROR MATCHES "^(https?|file)://")
                    message(FATAL_ERROR "qt-scrcpy: QTSCRCPY_SCRCPY_MIRROR must be an http, https or file URL.")
                endif()
                string(REGEX REPLACE "/+$" "" base "${QTSCRCPY_SCRCPY_MIRROR}")
            else()
                set(base "https://github.com/Genymobile/scrcpy/releases/download/v${version}")
            endif()
            set(url "${base}/${file_name}")
            _qtscrcpy_redact_url("${url}" shown_url)

            # Download beside the target and only move a file that passed the check into
            # place, so the cache never holds a partial or wrong file.
            set(part "${archive}.part")
            file(REMOVE "${part}")
            message(STATUS "qt-scrcpy: fetching ${shown_url}")
            file(DOWNLOAD "${url}" "${part}" INACTIVITY_TIMEOUT 60 TLS_VERIFY ON STATUS download_status)
            list(GET download_status 0 download_code)
            list(GET download_status 1 download_message)
            if(NOT download_code EQUAL 0)
                file(REMOVE "${part}")
                message(FATAL_ERROR
                    "qt-scrcpy: could not download ${shown_url}\n  ${download_message} (code ${download_code})\n"
                    "Options: -DQTSCRCPY_SCRCPY_MIRROR=<base url>, -DQTSCRCPY_SCRCPY_ARCHIVE=<downloaded file>, "
                    "or -DQTSCRCPY_BUNDLE_SCRCPY=OFF to use the scrcpy and adb installed on the machine.")
            endif()
            file(SHA256 "${part}" actual)
            if(NOT actual STREQUAL expected)
                file(REMOVE "${part}")
                message(FATAL_ERROR
                    "qt-scrcpy: the file downloaded from ${shown_url} does not match the pinned checksum of ${file_name}\n"
                    "  expected ${expected}\n  actual   ${actual}\n"
                    "It was discarded. A mirror may be serving something else.")
            endif()
            file(RENAME "${part}" "${archive}")
        endif()
    endif()

    # ---- unpack once ----
    string(SUBSTRING "${expected}" 0 12 short_hash)
    set(extract_dir "${QTSCRCPY_DOWNLOAD_DIR}/scrcpy-${platform}-v${version}-${short_hash}")
    _qtscrcpy_runtime_is_complete("${extract_dir}" "${expected}" "${platform}" complete)
    if(NOT complete)
        _qtscrcpy_unpack("${archive}" "${expected}" "${platform}" "${extract_dir}")
    endif()
    set(root "${extract_dir}/runtime")

    message(STATUS "qt-scrcpy: bundling scrcpy ${version} (${platform}) from ${root}")
    set(QTSCRCPY_BUNDLE_ROOT "${root}" CACHE INTERNAL "Directory with the unpacked scrcpy runtime files")
    set(QTSCRCPY_BUNDLE_VERSION "${version}" CACHE INTERNAL "scrcpy version of the bundled package")
    if(NOT version STREQUAL pinned_version OR QTSCRCPY_SCRCPY_SHA256)
        message(WARNING
            "qt-scrcpy: THIRD_PARTY_NOTICES.md describes the scrcpy ${pinned_version} release packages. "
            "You are bundling '${file_name}' with a checksum you supplied; check its notices yourself.")
    endif()
    set_property(GLOBAL PROPERTY QTSCRCPY_BUNDLE_STATE "ready")
endfunction()

# qtscrcpy_bundle_prepare(<var>)
function(qtscrcpy_bundle_prepare out)
    get_property(state GLOBAL PROPERTY QTSCRCPY_BUNDLE_STATE)
    if(NOT state)
        _qtscrcpy_resolve()
        get_property(state GLOBAL PROPERTY QTSCRCPY_BUNDLE_STATE)
    endif()
    if(state STREQUAL "ready")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# qtscrcpy_bundle_runtime(<target> [NO_INSTALL] [INSTALL_DESTINATION <dir>])
function(qtscrcpy_bundle_runtime target)
    cmake_parse_arguments(ARG "NO_INSTALL" "INSTALL_DESTINATION" "" ${ARGN})
    if(NOT TARGET ${target})
        message(FATAL_ERROR "qtscrcpy_bundle_runtime(): '${target}' is not a target.")
    endif()
    qtscrcpy_bundle_prepare(ready)
    if(NOT ready)
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${QTSCRCPY_BUNDLE_ROOT}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Bundling scrcpy and adb next to ${target}"
        VERBATIM)

    if(ARG_NO_INSTALL)
        return()
    endif()
    include(GNUInstallDirs)
    if(ARG_INSTALL_DESTINATION)
        set(destination "${ARG_INSTALL_DESTINATION}")
    else()
        set(destination "${CMAKE_INSTALL_BINDIR}")
    endif()
    install(DIRECTORY "${QTSCRCPY_BUNDLE_ROOT}/"
            DESTINATION "${destination}"
            USE_SOURCE_PERMISSIONS)

    # The scrcpy packages carry no licence files, so the notices travel with them (once).
    get_property(notices_done GLOBAL PROPERTY QTSCRCPY_NOTICES_INSTALLED)
    if(NOT notices_done)
        get_property(module_dir GLOBAL PROPERTY QTSCRCPY_MODULE_DIR)
        install(FILES "${module_dir}/../THIRD_PARTY_NOTICES.md" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
        install(DIRECTORY "${module_dir}/../licenses" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
        set_property(GLOBAL PROPERTY QTSCRCPY_NOTICES_INSTALLED TRUE)
    endif()
endfunction()
