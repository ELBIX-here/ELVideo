find_package(spdlog CONFIG REQUIRED)

add_library(elv_core STATIC
    include/core/IRecorder.h
    include/core/Types.h
    include/core/Events.h
    src/audio/AudioMixer.h
    src/audio/AudioMixer.cpp
    src/audio/WasapiSource.h
    src/audio/WasapiSource.cpp
    src/capture/SourceEnumerator.cpp
    src/capture/Thumbnail.cpp
    src/capture/WebcamSource.h
    src/capture/WebcamSource.cpp
    src/capture/WgcCapture.h
    src/capture/WgcCapture.cpp
    src/output/EncoderProbe.cpp
    src/output/MediaWriter.h
    src/output/MediaWriter.cpp
    src/output/ReplayStream.h
    src/output/ReplayStream.cpp
    src/video/FramePacer.h
    src/video/FramePacer.cpp
    src/video/PreviewRenderer.h
    src/video/PreviewRenderer.cpp
    src/video/VideoConverter.h
    src/video/VideoConverter.cpp
    src/RecordingSession.h
    src/RecordingSession.cpp
    src/RecorderController.h
    src/RecorderController.cpp
    src/util/BoundedQueue.h
    src/util/D3DDevice.h
    src/util/D3DDevice.cpp
    src/util/ImageIO.h
    src/util/ImageIO.cpp
    src/util/MasterClock.h
    src/util/MasterClock.cpp
    src/Platform.cpp
)
add_library(elv::core ALIAS elv_core)

target_include_directories(elv_core
    PUBLIC  include
    PRIVATE src)

# C++/WinRT comes from the Windows SDK headers (no NuGet package).
target_link_libraries(elv_core
    PUBLIC  elv_options
    PRIVATE spdlog::spdlog windowsapp CoreMessaging d3d11 dxgi dwmapi windowscodecs mfplat mfreadwrite mfuuid mf mmdevapi)

# Internal engine classes for tools/cli and tests. The app must use elv::core only.
add_library(elv_core_internal INTERFACE)
add_library(elv::core_internal ALIAS elv_core_internal)
target_include_directories(elv_core_internal INTERFACE src)
target_link_libraries(elv_core_internal INTERFACE elv_core spdlog::spdlog)
