// `leakcheck`: creates and destroys each engine component in isolation and
// reports the handle-count growth per iteration, broken down by handle type.

#include "LeakCheck.h"

#include "capture/WgcCapture.h"
#include "output/MediaWriter.h"
#include "util/D3DDevice.h"
#include "video/FramePacer.h"
#include "video/VideoConverter.h"

#include <windows.h>

#include <winternl.h>

#include <mfapi.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

DWORD handleCount() {
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

// Counts this process's handles by kernel object type (Event, Thread, Section, ...).
std::map<std::wstring, int> handlesByType() {
    std::map<std::wstring, int> counts;
    alignas(8) unsigned char buffer[1024];
    for (ULONG_PTR value = 4; value < 0x10000; value += 4) {
        const auto h = reinterpret_cast<HANDLE>(value);
        DWORD flags = 0;
        if (!GetHandleInformation(h, &flags)) continue;
        ULONG length = 0;
        if (NtQueryObject(h, ObjectTypeInformation, buffer, sizeof(buffer), &length) < 0) continue;
        const auto* info = reinterpret_cast<const PUBLIC_OBJECT_TYPE_INFORMATION*>(buffer);
        ++counts[std::wstring(info->TypeName.Buffer, info->TypeName.Length / sizeof(wchar_t))];
    }
    return counts;
}

void measure(const char* name, int iterations, const std::function<void()>& body) {
    body();  // warm-up: DLL loads and one-time driver state
    body();
    const DWORD before = handleCount();
    const auto typesBefore = handlesByType();
    for (int i = 0; i < iterations; ++i) body();
    const DWORD after = handleCount();
    std::printf("%-32s handles %5lu -> %5lu  (%+.2f per iteration)", name, before, after,
                (static_cast<double>(after) - before) / iterations);
    for (const auto& [type, count] : handlesByType()) {
        const auto it = typesBefore.find(type);
        const int delta = count - (it == typesBefore.end() ? 0 : it->second);
        if (delta) std::printf("  %s %+d", winrt::to_string(type).c_str(), delta);
    }
    std::printf("\n");
}

}  // namespace

int runLeakCheck(const elv::CaptureSource& source, const std::filesystem::path& outDir, int iterations) {
    std::filesystem::create_directories(outDir);
    const HMONITOR monitor = elv::monitorForSource(source);

    measure("D3DDevice", iterations, [&] { elv::D3DDevice device(monitor); });

    measure("FramePacer start/stop", iterations, [&] {
        elv::FramePacer pacer(60);
        pacer.start([](std::uint64_t, bool) {});
        pacer.notifyNewFrame();
        std::this_thread::sleep_for(50ms);
        pacer.stop();
    });

    elv::D3DDevice device(monitor);
    measure("WgcCapture create", iterations, [&] { elv::WgcCapture capture(device, source); });

    measure("WgcCapture start/stop", iterations, [&] {
        elv::WgcCapture capture(device, source);
        capture.start([](const elv::CapturedFrame&) {}, [] {});
        std::this_thread::sleep_for(200ms);
        capture.stop();
    });

    measure("VideoConverter", iterations, [&] { elv::VideoConverter converter(device, 1280, 720); });

    const auto writerTest = [&](bool hardware) {
        return [&, hardware] {
            elv::VideoConverter converter(device, 1280, 720);
            elv::MediaWriterConfig config;
            config.path = std::filesystem::absolute(outDir / L"leak.mp4");
            config.width = 1280;
            config.height = 720;
            config.fpsNumerator = 30;
            config.preferHardware = hardware;
            {
                elv::MediaWriter writer(device, config);
                for (int f = 0; f < 15; ++f) writer.writeVideo(converter.output(), f * 333'333LL, 333'333LL);
                writer.finalize();
            }
            std::filesystem::remove(config.path);
        };
    };
    measure("MediaWriter (hardware)", iterations, writerTest(true));
    measure("MediaWriter (software)", iterations, writerTest(false));

    // Reproduces the known Intel Quick Sync issue with plain Media Foundation calls
    // (no engine code): one Event, Key and WaitCompletionPacket per SinkWriter.
    measure("bare MF SinkWriter (hardware)", iterations, [&] {
        MFStartup(MF_VERSION, MFSTARTUP_LITE);
        {
            winrt::com_ptr<IMFAttributes> attributes;
            MFCreateAttributes(attributes.put(), 1);
            attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            const auto path = std::filesystem::absolute(outDir / L"bare.mp4");
            winrt::com_ptr<IMFSinkWriter> writer;
            MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes.get(), writer.put());

            winrt::com_ptr<IMFMediaType> output;
            MFCreateMediaType(output.put());
            output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
            output->SetUINT32(MF_MT_AVG_BITRATE, 8'000'000);
            output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, 1280, 720);
            MFSetAttributeRatio(output.get(), MF_MT_FRAME_RATE, 30, 1);
            DWORD stream = 0;
            writer->AddStream(output.get(), &stream);

            winrt::com_ptr<IMFMediaType> input;
            MFCreateMediaType(input.put());
            output->CopyAllItems(input.get());
            input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            input->DeleteItem(MF_MT_AVG_BITRATE);
            writer->SetInputMediaType(stream, input.get(), nullptr);
            writer->BeginWriting();
            writer = nullptr;
            std::filesystem::remove(path);
        }
        MFShutdown();
    });
    return 0;
}
