// WASAPI capture sources and audio endpoint enumeration (enumerateAudioDevices).

#include "audio/WasapiSource.h"

#include "core/IRecorder.h"

#include <Audioclient.h>
#include <propsys.h>  // DEFINE_PROPERTYKEY for the header below
#include <functiondiscoverykeys_devpkey.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

#include <winrt/base.h>

#include <spdlog/spdlog.h>

#include <format>

namespace elv {

namespace {

constexpr REFERENCE_TIME kBufferDuration = 1'000'000;  // 100 ms
constexpr DWORD kPollMs = 10;
constexpr DWORD kRetryMs = 1'000;
constexpr int kDefaultCheckPolls = 100;  // ~1 s

EDataFlow flowOf(AudioDeviceKind kind) { return kind == AudioDeviceKind::Output ? eRender : eCapture; }

winrt::com_ptr<IMMDeviceEnumerator> deviceEnumerator() {
    return winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
}

std::wstring deviceId(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id))) return {};
    std::wstring result(id);
    CoTaskMemFree(id);
    return result;
}

std::wstring friendlyName(IMMDevice* device) {
    winrt::com_ptr<IPropertyStore> props;
    if (FAILED(device->OpenPropertyStore(STGM_READ, props.put()))) return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) name = value.pwszVal;
    PropVariantClear(&value);
    return name;
}

std::string narrow(const std::wstring& s) { return winrt::to_string(s); }

// COM for threads the engine does not own (Qt, thread pools).
class ComScope {
public:
    ComScope() : initialized_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope() {
        if (initialized_) CoUninitialize();
    }

private:
    bool initialized_;
};

// Thrown to reopen the device without reporting an error (the default device changed).
struct ReopenDevice {};

}  // namespace

std::wstring defaultAudioDeviceId(AudioDeviceKind kind) {
    winrt::com_ptr<IMMDevice> device;
    if (FAILED(deviceEnumerator()->GetDefaultAudioEndpoint(flowOf(kind), eConsole, device.put()))) return {};
    return deviceId(device.get());
}

std::vector<AudioDevice> enumerateAudioDevices(AudioDeviceKind kind) {
    ComScope com;
    std::vector<AudioDevice> devices;
    try {
        const auto enumerator = deviceEnumerator();
        const std::wstring defaultId = defaultAudioDeviceId(kind);
        winrt::com_ptr<IMMDeviceCollection> collection;
        winrt::check_hresult(enumerator->EnumAudioEndpoints(flowOf(kind), DEVICE_STATE_ACTIVE, collection.put()));
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            winrt::com_ptr<IMMDevice> device;
            if (FAILED(collection->Item(i, device.put()))) continue;
            AudioDevice d;
            d.id = deviceId(device.get());
            d.name = friendlyName(device.get());
            d.isDefault = d.id == defaultId;
            if (d.isDefault)
                devices.insert(devices.begin(), std::move(d));
            else
                devices.push_back(std::move(d));
        }
    } catch (const winrt::hresult_error& e) {
        spdlog::warn("cannot list audio devices: {}", winrt::to_string(e.message()));
    }
    return devices;
}

WasapiSource::WasapiSource(AudioDeviceKind kind, std::wstring deviceId)
    : kind_(kind), requestedId_(std::move(deviceId)), stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

WasapiSource::WasapiSource(std::uint32_t processId)
    : kind_(AudioDeviceKind::Output), processId_(processId), stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

WasapiSource::~WasapiSource() {
    stop();
    CloseHandle(stopEvent_);
}

void WasapiSource::start(PacketHandler onPacket, WarningHandler onWarning) {
    if (thread_.joinable()) return;
    onPacket_ = std::move(onPacket);
    onWarning_ = std::move(onWarning);
    ResetEvent(stopEvent_);
    thread_ = std::thread([this] { run(); });
}

void WasapiSource::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
}

std::wstring WasapiSource::deviceName() const {
    std::lock_guard lock(nameMutex_);
    return deviceName_;
}

void WasapiSource::run() {
    SetThreadDescription(GetCurrentThread(), kind_ == AudioDeviceKind::Output ? L"ELVideo system audio"
                                                                               : L"ELVideo microphone");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    ComScope com;

    const char* const what = processId_ ? "app audio" : kind_ == AudioDeviceKind::Output ? "system audio" : "microphone";
    for (;;) {
        DWORD retryMs = kRetryMs;
        try {
            captureFrom(processId_ || !requestedId_.empty() ? requestedId_ : defaultAudioDeviceId(kind_));
        } catch (const ReopenDevice&) {
            retryMs = 0;
        } catch (const winrt::hresult_error& e) {
            const std::string message = e.code() == AUDCLNT_E_DEVICE_INVALIDATED
                                            ? std::format("{} device was disconnected", what)
                                            : std::format("cannot record {}: {} (0x{:08X})", what,
                                                          winrt::to_string(e.message()),
                                                          static_cast<unsigned>(e.code().value));
            spdlog::warn("{}", message);
            if (!warned_ && onWarning_) onWarning_(message);  // once per outage, not per retry
            warned_ = true;
        }
        {
            std::lock_guard lock(nameMutex_);
            deviceName_.clear();
        }
        if (WaitForSingleObject(stopEvent_, retryMs) != WAIT_TIMEOUT) break;
    }
}

namespace {

// Completion handler for ActivateAudioInterfaceAsync (agile, as the API requires).
struct ActivationHandler : winrt::implements<ActivationHandler, IActivateAudioInterfaceCompletionHandler> {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_PENDING;
    winrt::com_ptr<IAudioClient> client;

    ~ActivationHandler() { CloseHandle(done); }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activateResult = E_FAIL;
        winrt::com_ptr<IUnknown> unknown;
        result = operation->GetActivateResult(&activateResult, unknown.put());
        if (SUCCEEDED(result)) result = activateResult;
        if (SUCCEEDED(result)) result = unknown->QueryInterface(IID_PPV_ARGS(client.put()));
        SetEvent(done);
        return S_OK;
    }
};

// Audio client that captures only `processId` and its child processes.
winrt::com_ptr<IAudioClient> activateProcessLoopback(std::uint32_t processId) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = processId;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation{};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    const auto handler = winrt::make_self<ActivationHandler>();
    winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> operation;
    winrt::check_hresult(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                                     &activation, handler.get(), operation.put()));
    if (WaitForSingleObject(handler->done, 5'000) != WAIT_OBJECT_0) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    winrt::check_hresult(handler->result);
    return handler->client;
}

std::int64_t qpcNowHns() {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart / frequency * 10'000'000 + t.QuadPart % frequency * 10'000'000 / frequency;
}

}  // namespace

void WasapiSource::captureFrom(const std::wstring& id) {
    winrt::com_ptr<IAudioClient> client;
    std::wstring name;
    if (processId_) {
        client = activateProcessLoopback(processId_);
        name = std::format(L"app audio (process {})", processId_);
    } else {
        if (id.empty()) winrt::throw_hresult(E_NOTFOUND);  // no device of this kind right now
        winrt::com_ptr<IMMDevice> device;
        winrt::check_hresult(deviceEnumerator()->GetDevice(id.c_str(), device.put()));
        winrt::check_hresult(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void()));
        name = friendlyName(device.get());
    }

    // Ask for 48 kHz stereo float whatever the device mix format is; the shared-mode
    // engine resamples and remixes (AUTOCONVERTPCM).
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = kChannels;
    format.Format.nSamplesPerSec = kSampleRate;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = kChannels * 4;
    format.Format.nAvgBytesPerSec = kSampleRate * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (kind_ == AudioDeviceKind::Output) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    // The process-loopback client only works event-driven; the event is also polled.
    const HANDLE audioEvent = processId_ ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
    struct CloseEvent {
        HANDLE h;
        ~CloseEvent() {
            if (h) CloseHandle(h);
        }
    } eventGuard{audioEvent};
    if (audioEvent) flags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    winrt::check_hresult(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0,
                                            reinterpret_cast<WAVEFORMATEX*>(&format), nullptr));
    if (audioEvent) winrt::check_hresult(client->SetEventHandle(audioEvent));
    winrt::com_ptr<IAudioCaptureClient> capture;
    winrt::check_hresult(client->GetService(__uuidof(IAudioCaptureClient), capture.put_void()));
    winrt::check_hresult(client->Start());

    {
        std::lock_guard lock(nameMutex_);
        deviceName_ = name;
    }
    warned_ = false;
    spdlog::info("{} capture opened: {}",
                 processId_ ? "app loopback" : kind_ == AudioDeviceKind::Output ? "loopback" : "microphone", narrow(name));

    struct StopClient {
        IAudioClient* client;
        ~StopClient() { client->Stop(); }
    } stopGuard{client.get()};

    int polls = 0;
    while (WaitForSingleObject(stopEvent_, kPollMs) == WAIT_TIMEOUT) {
        UINT32 packet = 0;
        winrt::check_hresult(capture->GetNextPacketSize(&packet));
        while (packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD bufferFlags = 0;
            UINT64 qpcPosition = 0;
            winrt::check_hresult(capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, &qpcPosition));
            if (frames > 0 && onPacket_) {
                const bool silent = bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT;
                // Process loopback may not stamp packets; then the packet ended about now.
                const auto timestamp = qpcPosition ? static_cast<std::int64_t>(qpcPosition)
                                                   : qpcNowHns() - static_cast<std::int64_t>(frames) * 10'000'000 / kSampleRate;
                onPacket_(silent ? nullptr : reinterpret_cast<const float*>(data), frames, timestamp);
            }
            winrt::check_hresult(capture->ReleaseBuffer(frames));
            winrt::check_hresult(capture->GetNextPacketSize(&packet));
        }

        // Following the default device: switch when the user changes it in Windows.
        if (!processId_ && requestedId_.empty() && ++polls >= kDefaultCheckPolls) {
            polls = 0;
            if (defaultAudioDeviceId(kind_) != id) {
                spdlog::info("default {} device changed; reopening", kind_ == AudioDeviceKind::Output ? "output" : "input");
                throw ReopenDevice{};
            }
        }
    }
}

}  // namespace elv
