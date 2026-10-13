add_library(localflix_core STATIC
    ApplicationController.cpp
    ApplicationController.h
    Clock.h
    DatabaseManager.cpp
    DatabaseManager.h
    DbError.h
    EpisodeQueueManager.cpp
    EpisodeQueueManager.h
    FilenameParser.cpp
    FilenameParser.h
    Format.cpp
    Format.h
    IPlaybackBackend.h
    LibraryManager.cpp
    LibraryManager.h
    MediaImportService.cpp
    MediaImportService.h
    MediaRepository.cpp
    MediaRepository.h
    Migrations.cpp
    Migrations.h
    PlaybackController.cpp
    PlaybackController.h
    PlaybackProgressManager.cpp
    PlaybackProgressManager.h
    ResumeLogic.cpp
    ResumeLogic.h
    SettingsManager.cpp
    SettingsManager.h
    SqlUtil.h
    Types.h
)

target_include_directories(localflix_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(localflix_core PUBLIC Qt6::Core Qt6::Sql)
localflix_set_warnings(localflix_core)
