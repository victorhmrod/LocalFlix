add_library(localflix_mpv STATIC
    MpvBackend.cpp
    MpvBackend.h
    MpvWidget.cpp
    MpvWidget.h
)

target_include_directories(localflix_mpv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(localflix_mpv PUBLIC localflix_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL mpv::libmpv)

# libmpv-2.dll is loaded lazily so the application can explain a missing DLL
# instead of the Windows loader failing before main().
target_link_libraries(localflix_mpv INTERFACE delayimp)
target_link_options(localflix_mpv INTERFACE "/DELAYLOAD:libmpv-2.dll")

localflix_set_warnings(localflix_mpv)
