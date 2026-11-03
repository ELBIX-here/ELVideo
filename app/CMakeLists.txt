find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Widgets)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(elv_app WIN32
    main.cpp
    src/AppSettings.h
    src/AppSettings.cpp
    src/CrashHandler.h
    src/CrashHandler.cpp
    src/Desktop.h
    src/Desktop.cpp
    src/Hotkeys.h
    src/Hotkeys.cpp
    src/RecorderModel.h
    src/RecorderModel.cpp
    src/SourceModel.h
    src/SourceModel.cpp
    src/TrayIcon.h
    src/TrayIcon.cpp
    resources/elvideo.rc)
set_target_properties(elv_app PROPERTIES OUTPUT_NAME ELVideo)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The resource compiler's include of the generated version header is not tracked.
set_source_files_properties(resources/elvideo.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/generated/elv_version.h)

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
)

qt_add_resources(elv_app "icons"
    PREFIX "/"
    BASE resources
    FILES resources/elvideo.ico)

# qmlcachegen's ahead-of-time compiled QML (.rcc/qmlcache/*.cpp) trips C4702
# (unreachable code) inside Qt headers under /W4.
target_compile_options(elv_app PRIVATE /wd4702)

target_link_libraries(elv_app PRIVATE
    elv::core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    dwmapi
    dbghelp)
