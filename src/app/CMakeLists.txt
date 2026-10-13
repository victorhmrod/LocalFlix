qt_add_executable(localflix WIN32
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/localflix.rc
    # Resources must live in the executable: a static library's resources get
    # discarded by the linker because nothing references them.
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)
target_link_libraries(localflix PRIVATE localflix_ui)
target_compile_definitions(localflix PRIVATE LOCALFLIX_VERSION="${PROJECT_VERSION}")
localflix_set_warnings(localflix)

# The player needs libmpv-2.dll next to the executable.
add_custom_command(TARGET localflix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LOCALFLIX_MPV_DLL}" "$<TARGET_FILE_DIR:localflix>"
    VERBATIM)
