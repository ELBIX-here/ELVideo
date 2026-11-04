find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Widgets OpenGL Network)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

# Google sign-in for Drive folders uses the "installed app" OAuth client in
# google_oauth.json at the repository root (not in git; README "Google Drive").
# Without the file the build has no Drive sign-in and says so in the app.
set(ELV_GOOGLE_CLIENT_ID "")
set(ELV_GOOGLE_CLIENT_SECRET "")
set(ELV_GOOGLE_OAUTH_FILE ${CMAKE_SOURCE_DIR}/google_oauth.json)
if(EXISTS ${ELV_GOOGLE_OAUTH_FILE})
    file(READ ${ELV_GOOGLE_OAUTH_FILE} ELV_GOOGLE_OAUTH_JSON)
    string(JSON ELV_GOOGLE_CLIENT_ID ERROR_VARIABLE ELV_GOOGLE_OAUTH_ERROR GET ${ELV_GOOGLE_OAUTH_JSON} installed client_id)
    string(JSON ELV_GOOGLE_CLIENT_SECRET ERROR_VARIABLE ELV_GOOGLE_OAUTH_ERROR GET ${ELV_GOOGLE_OAUTH_JSON} installed client_secret)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ELV_GOOGLE_OAUTH_FILE})
endif()
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/elv_google_oauth.h
     CONTENT "#pragma once
#define ELV_GOOGLE_CLIENT_ID \"@ELV_GOOGLE_CLIENT_ID@\"
#define ELV_GOOGLE_CLIENT_SECRET \"@ELV_GOOGLE_CLIENT_SECRET@\"
"
     @ONLY)

qt_add_executable(elv_app WIN32
    main.cpp
    src/AppSettings.h
    src/AppSettings.cpp
    src/CrashHandler.h
    src/CrashHandler.cpp
    src/Desktop.h
    src/Desktop.cpp
    src/FolderListing.h
    src/FolderListing.cpp
    src/GoogleDrive.h
    src/GoogleDrive.cpp
    src/Hotkeys.h
    src/Hotkeys.cpp
    src/LibraryModel.h
    src/LibraryModel.cpp
    src/OverlayGeometry.h
    src/OverlayGeometry.cpp
    src/RecorderModel.h
    src/RecorderModel.cpp
    src/ScreenCapture.h
    src/ScreenCapture.cpp
    src/SourceModel.h
    src/SourceModel.cpp
    src/TrayIcon.h
    src/TrayIcon.cpp
    src/WatchProgress.h
    src/WatchProgress.cpp
    resources/elvideo.rc)
set_target_properties(elv_app PROPERTIES OUTPUT_NAME ELVideo)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The resource compiler's include of the generated version header is not tracked.
set_source_files_properties(resources/elvideo.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/generated/elv_version.h)

# The player (libmpv) is optional; without it `--player` explains it is missing.
set(ELV_PLAYER_QML)
if(ELV_HAVE_MPV)
    target_sources(elv_app PRIVATE
        src/player/MpvItem.h
        src/player/MpvItem.cpp
        src/player/PlayerMain.h
        src/player/PlayerMain.cpp
        src/player/SeekThumbnailer.h
        src/player/SeekThumbnailer.cpp)
    # src: "player/MpvItem.h"; src/player: the generated QML type registration includes <MpvItem.h>
    target_include_directories(elv_app PRIVATE src src/player)
    target_compile_definitions(elv_app PRIVATE ELV_HAVE_MPV)
    target_link_libraries(elv_app PRIVATE libmpv Qt6::OpenGL)
    add_custom_command(TARGET elv_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libmpv> $<TARGET_FILE_DIR:elv_app>
        COMMENT "Copying libmpv-2.dll")
    set(ELV_PLAYER_QML qml/Player.qml qml/SeekBar.qml)
endif()

qt_add_qml_module(elv_app
    URI ELVideo
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Splash.qml
        qml/LogoMark.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/IconButton.qml
        qml/SourcePicker.qml
        qml/SourceCard.qml
        qml/ControlBar.qml
        qml/RecordButton.qml
        qml/SettingsPage.qml
        qml/SettingRow.qml
        qml/HotkeyField.qml
        qml/FloatingBar.qml
        qml/Toast.qml
        qml/ToggleSwitch.qml
        qml/LevelMeter.qml
        qml/RegionSelector.qml
        qml/RegionFrame.qml
        qml/PreviewPanel.qml
        qml/ScreenshotOverlay.qml
        qml/LibraryPage.qml
        qml/AppButton.qml
        qml/AppComboBox.qml
        qml/AppSlider.qml
        qml/AppSpinBox.qml
        qml/AppMenu.qml
        qml/AppMenuItem.qml
        qml/SegmentedControl.qml
        qml/RoundedClip.qml
        qml/HudButton.qml
        qml/AppMenuSeparator.qml
        qml/ProductShadow.qml
        qml/Frost.qml
        qml/SearchField.qml
        qml/SidebarItem.qml
        qml/FolderPage.qml
        qml/DriveBrowserPage.qml
        qml/ContinuePage.qml
        ${ELV_PLAYER_QML}
)

qt_add_resources(elv_app "icons"
    PREFIX "/"
    BASE resources
    FILES resources/elvideo.ico
          resources/fonts/Pretendard-Light.otf
          resources/fonts/Pretendard-Regular.otf
          resources/fonts/Pretendard-SemiBold.otf)

# qmlcachegen's ahead-of-time compiled QML (.rcc/qmlcache/*.cpp) trips C4702
# (unreachable code) inside Qt headers under /W4.
target_compile_options(elv_app PRIVATE /wd4702)

target_link_libraries(elv_app PRIVATE
    elv::core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Network
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    dwmapi
    dbghelp
    crypt32)
