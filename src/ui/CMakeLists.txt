add_library(localflix_ui STATIC
    CardFactory.cpp
    CardFactory.h
    CardRow.cpp
    CardRow.h
    CenterCard.cpp
    CenterCard.h
    CollectionsView.cpp
    CollectionsView.h
    ContinueCard.cpp
    ContinueCard.h
    Dialogs.cpp
    Dialogs.h
    EpisodeManagerView.cpp
    EpisodeManagerView.h
    FlowLayout.cpp
    FlowLayout.h
    HomeView.cpp
    HomeView.h
    KeepAwake.cpp
    KeepAwake.h
    LibraryActions.cpp
    LibraryActions.h
    LibraryView.cpp
    LibraryView.h
    MainWindow.cpp
    MainWindow.h
    PlayerView.cpp
    PlayerView.h
    PosterCard.cpp
    PosterCard.h
    SeekSlider.cpp
    SeekSlider.h
    SettingsView.cpp
    SettingsView.h
    ShowDetailsView.cpp
    ShowDetailsView.h
    Theme.cpp
    Theme.h
    Toast.cpp
    Toast.h
)

target_include_directories(localflix_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(localflix_ui PUBLIC localflix_mpv Qt6::Widgets Qt6::Svg dwmapi)
localflix_set_warnings(localflix_ui)
