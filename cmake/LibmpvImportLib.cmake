# Generates an MSVC import library (.lib) for a libmpv DLL.
#
# The pre-built libmpv packages (e.g. shinchiro/mpv-winbuild-cmake "mpv-dev")
# ship only a MinGW import library (libmpv.dll.a), which MSVC cannot link.
# The DLL's export table is authoritative, so we read it with dumpbin, write a
# .def file and let lib.exe build the matching import library.
#
#   localflix_generate_mpv_import_lib(<dll> <output-dir> <out-var>)
#
# <out-var> receives the path of the generated .lib.

function(localflix_generate_mpv_import_lib dll_path out_dir out_var)
    if(NOT EXISTS "${dll_path}")
        message(FATAL_ERROR "libmpv DLL not found: ${dll_path}\n"
                "Run scripts\\fetch-deps.cmd or see docs/DEPENDENCIES.md.")
    endif()

    get_filename_component(dll_file "${dll_path}" NAME)
    get_filename_component(stem "${dll_path}" NAME_WE)
    set(def_file "${out_dir}/${stem}.def")
    set(lib_file "${out_dir}/${stem}.lib")

    if(NOT EXISTS "${lib_file}" OR "${dll_path}" IS_NEWER_THAN "${lib_file}")
        get_filename_component(msvc_bin_dir "${CMAKE_LINKER}" DIRECTORY)
        find_program(LOCALFLIX_DUMPBIN dumpbin HINTS "${msvc_bin_dir}" REQUIRED)
        find_program(LOCALFLIX_LIB_EXE lib HINTS "${msvc_bin_dir}" REQUIRED)

        file(MAKE_DIRECTORY "${out_dir}")
        execute_process(
            COMMAND "${LOCALFLIX_DUMPBIN}" /nologo /exports "${dll_path}"
            OUTPUT_VARIABLE exports_text
            RESULT_VARIABLE dumpbin_rc)
        if(NOT dumpbin_rc EQUAL 0)
            message(FATAL_ERROR "dumpbin failed on ${dll_path} (exit ${dumpbin_rc})")
        endif()

        # Export table rows look like:  "     33   20 01019E80 mpv_abort_async_command"
        string(REGEX MATCHALL "\n +[0-9]+ +[0-9A-Fa-f]+ +[0-9A-Fa-f]+ +mpv_[A-Za-z0-9_]+"
               export_rows "${exports_text}")
        if(NOT export_rows)
            message(FATAL_ERROR "No mpv_* exports found in ${dll_path}; is this really libmpv?")
        endif()

        set(def_text "LIBRARY ${dll_file}\nEXPORTS\n")
        foreach(row IN LISTS export_rows)
            string(REGEX REPLACE ".* (mpv_[A-Za-z0-9_]+)$" "\\1" fn "${row}")
            string(APPEND def_text "    ${fn}\n")
        endforeach()
        file(WRITE "${def_file}" "${def_text}")

        execute_process(
            COMMAND "${LOCALFLIX_LIB_EXE}" /nologo "/def:${def_file}" /machine:x64 "/out:${lib_file}"
            RESULT_VARIABLE lib_rc
            OUTPUT_VARIABLE lib_out
            ERROR_VARIABLE lib_err)
        if(NOT lib_rc EQUAL 0)
            message(FATAL_ERROR "lib.exe failed (exit ${lib_rc}):\n${lib_out}\n${lib_err}")
        endif()
        list(LENGTH export_rows export_count)
        message(STATUS "Generated MSVC import library for ${dll_file} (${export_count} exports)")
    endif()

    set(${out_var} "${lib_file}" PARENT_SCOPE)
endfunction()
