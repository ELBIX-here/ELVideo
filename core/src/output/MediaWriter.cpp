#include "output/MediaWriter.h"

#include "core/IRecorder.h"

#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>

#include <initguid.h>  // defines the CODECAPI_* GUIDs below
#include <codecapi.h>
#include <strmif.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace elv {

namespace {

void setColorAttributes(IMFMediaType* type) {
    winrt::check_hresult(type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709));
    winrt::check_hresult(type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709));
    winrt::check_hresult(type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709));
    winrt::check_hresult(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
}

winrt::com_ptr<IMFMediaType> createVideoType(const GUID& subtype, const MediaWriterConfig& config) {
    winrt::com_ptr<IMFMediaType> type;
    winrt::check_hresult(MFCreateMediaType(type.put()));
    winrt::check_hresult(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    winrt::check_hresult(type->SetGUID(MF_MT_SUBTYPE, subtype));
    winrt::check_hresult(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    winrt::check_hresult(MFSetAttributeSize(type.get(), MF_MT_FRAME_SIZE, config.width, config.height));
    winrt::check_hresult(
        MFSetAttributeRatio(type.get(), MF_MT_FRAME_RATE, config.fpsNumerator, config.fpsDenominator));
    winrt::check_hresult(MFSetAttributeRatio(type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    setColorAttributes(type.get());
    return type;
}

GUID subtypeOf(VideoCodec codec) {
    switch (codec) {
    case VideoCodec::HEVC: return MFVideoFormat_HEVC;
    case VideoCodec::AV1: return MFVideoFormat_AV1;
    case VideoCodec::H264: break;
    }
    return MFVideoFormat_H264;
}

// Hardware MFTs usually lack a friendly name on the instance; look it up in the
// registration that has the same hardware URL.
std::wstring hardwareEncoderName(const std::wstring& hardwareUrl, VideoCodec codec) {
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    const MFT_REGISTER_TYPE_INFO outputInfo{MFMediaType_Video, subtypeOf(codec)};
    if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE, nullptr, &outputInfo, &activates,
                         &count)))
        return {};

    std::wstring name;
    for (UINT32 i = 0; i < count; ++i) {
        wchar_t url[512]{};
        wchar_t friendly[256]{};
        if (name.empty() &&
            SUCCEEDED(activates[i]->GetString(MFT_ENUM_HARDWARE_URL_Attribute, url, ARRAYSIZE(url), nullptr)) &&
            hardwareUrl == url &&
            SUCCEEDED(activates[i]->GetString(MFT_FRIENDLY_NAME_Attribute, friendly, ARRAYSIZE(friendly), nullptr)))
            name = friendly;
        activates[i]->Release();
    }
    CoTaskMemFree(activates);
    return name;
}

void setCodecValue(ICodecAPI* codec, const GUID& property, UINT32 value) {
    VARIANT v{};
    v.vt = VT_UI4;
    v.ulVal = value;
    if (FAILED(codec->SetValue(&property, &v))) spdlog::debug("encoder ignored a codec property");
}

}  // namespace

MediaWriter::MfPlatform::MfPlatform() { winrt::check_hresult(MFStartup(MF_VERSION, MFSTARTUP_LITE)); }
MediaWriter::MfPlatform::~MfPlatform() { MFShutdown(); }

MediaWriter::MediaWriter(const D3DDevice& device, const MediaWriterConfig& config)
    : device_(device), config_(config) {
    if (config.container == Container::Mkv)
        throw std::runtime_error("MKV output needs the FFmpeg writer (not implemented yet)");
    if (config.width % 2 || config.height % 2) throw std::runtime_error("frame size must be even");

    winrt::check_hresult(MFCreateDXGIDeviceManager(&resetToken_, deviceManager_.put()));
    winrt::check_hresult(deviceManager_->ResetDevice(device.device(), resetToken_));

    if (config.preferHardware) {
        try {
            createWriter(true);
            // MF silently picks the software encoder when no hardware one fits;
            // that encoder is much faster fed from system memory.
            if (!hardware_) releaseWriter();
        } catch (const winrt::hresult_error& e) {
            spdlog::warn("hardware encoder setup failed ({}); using software encoder",
                         winrt::to_string(e.message()));
            releaseWriter();
        }
    }
    if (!writer_) createWriter(false);

    const auto inputType = createVideoType(MFVideoFormat_NV12, config_);
    if (hardware_) {
        winrt::check_hresult(MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(allocator_.put())));
        winrt::check_hresult(allocator_->SetDirectXManager(deviceManager_.get()));
        winrt::check_hresult(allocator_->InitializeSampleAllocatorEx(4, 12, nullptr, inputType.get()));
    } else {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = config_.width;
        desc.Height = config_.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_NV12;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        winrt::check_hresult(device_.device()->CreateTexture2D(&desc, nullptr, staging_.put()));
    }

    spdlog::info("encoder: {} ({}), {}x{} @ {}/{} fps, {} kbps", winrt::to_string(encoderName_),
                 hardware_ ? "hardware" : "software", config_.width, config_.height, config_.fpsNumerator,
                 config_.fpsDenominator, config_.bitrateKbps);
}

MediaWriter::~MediaWriter() {
    try {
        finalize();
    } catch (const winrt::hresult_error& e) {
        spdlog::error("finalize failed: {}", winrt::to_string(e.message()));
    }
}

void MediaWriter::createWriter(bool hardware) {
    winrt::com_ptr<IMFAttributes> attributes;
    winrt::check_hresult(MFCreateAttributes(attributes.put(), 4));
    if (hardware) {
        winrt::check_hresult(attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, deviceManager_.get()));
        winrt::check_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
    }
    winrt::check_hresult(attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE,
                                             config_.container == Container::FragmentedMp4
                                                 ? MFTranscodeContainerType_FMPEG4
                                                 : MFTranscodeContainerType_MPEG4));

    writer_ = nullptr;
    if (config_.byteStream) {
        // A second attempt (hardware -> software) starts the stream over.
        winrt::check_hresult(config_.byteStream->SetLength(0));
        winrt::check_hresult(config_.byteStream->SetCurrentPosition(0));
        winrt::check_hresult(MFCreateSinkWriterFromURL(nullptr, config_.byteStream, attributes.get(), writer_.put()));
    } else {
        winrt::check_hresult(MFCreateSinkWriterFromURL(config_.path.c_str(), nullptr, attributes.get(), writer_.put()));
    }

    const auto outputType = createVideoType(subtypeOf(config_.codec), config_);
    winrt::check_hresult(outputType->SetUINT32(MF_MT_AVG_BITRATE, config_.bitrateKbps * 1000));
    const UINT32 profile = config_.codec == VideoCodec::HEVC  ? static_cast<UINT32>(eAVEncH265VProfile_Main_420_8)
                           : config_.codec == VideoCodec::AV1 ? static_cast<UINT32>(eAVEncAV1VProfile_Main_420_8)
                                                              : static_cast<UINT32>(eAVEncH264VProfile_High);
    // HEVC encoders default to Main; some hardware HEVC encoders (NVIDIA, DX12) reject
    // the output type when the profile is set explicitly.
    if (config_.codec != VideoCodec::HEVC) winrt::check_hresult(outputType->SetUINT32(MF_MT_MPEG2_PROFILE, profile));
    winrt::check_hresult(writer_->AddStream(outputType.get(), &videoStream_));

    const auto inputType = createVideoType(MFVideoFormat_NV12, config_);
    winrt::check_hresult(writer_->SetInputMediaType(videoStream_, inputType.get(), nullptr));

    winrt::com_ptr<ICodecAPI> codec;
    if (SUCCEEDED(writer_->GetServiceForStream(videoStream_, GUID_NULL, IID_PPV_ARGS(codec.put())))) {
        // A keyframe every 2 s keeps seeking and fragment sizes reasonable.
        setCodecValue(codec.get(), CODECAPI_AVEncMPVGOPSize,
                      2 * config_.fpsNumerator / std::max(1u, config_.fpsDenominator));
        setCodecValue(codec.get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_PeakConstrainedVBR);
        setCodecValue(codec.get(), CODECAPI_AVEncCommonMaxBitRate, config_.bitrateKbps * 1500);
    }

    if (config_.audio) addAudioStream();

    winrt::check_hresult(writer_->BeginWriting());
    queryEncoder();
}

void MediaWriter::queryEncoder() {
    hardware_ = false;
    encoderName_ = L"unknown";

    winrt::com_ptr<IMFTransform> transform;
    if (FAILED(writer_->GetServiceForStream(videoStream_, GUID_NULL, IID_PPV_ARGS(transform.put())))) return;
    winrt::com_ptr<IMFAttributes> attributes;
    const std::wstring generic = winrt::to_hstring(codecName(config_.codec)).c_str();
    if (FAILED(transform->GetAttributes(attributes.put())) || !attributes) {
        encoderName_ = L"Microsoft " + generic + L" encoder";
        return;
    }

    wchar_t url[512]{};
    hardware_ = SUCCEEDED(attributes->GetString(MFT_ENUM_HARDWARE_URL_Attribute, url, ARRAYSIZE(url), nullptr));
    wchar_t name[256]{};
    if (SUCCEEDED(attributes->GetString(MFT_FRIENDLY_NAME_Attribute, name, ARRAYSIZE(name), nullptr)))
        encoderName_ = name;
    else if (hardware_)
        encoderName_ = hardwareEncoderName(url, config_.codec);
    if (encoderName_.empty() || encoderName_ == L"unknown")
        encoderName_ = (hardware_ ? L"hardware " : L"Microsoft ") + generic + L" encoder";
}

bool MediaWriter::writeVideo(ID3D11Texture2D* nv12, std::int64_t timeHns, std::int64_t durationHns) {
    if (!writer_) return false;
    if (!hardware_) {
        writeSystemMemory(nv12, timeHns, durationHns);
        return true;
    }

    winrt::com_ptr<IMFSample> sample;
    const HRESULT hr = allocator_->AllocateSample(sample.put());
    if (hr == MF_E_SAMPLEALLOCATOR_EMPTY) return false;
    winrt::check_hresult(hr);

    winrt::com_ptr<IMFMediaBuffer> buffer;
    winrt::check_hresult(sample->GetBufferByIndex(0, buffer.put()));
    const auto dxgiBuffer = buffer.as<IMFDXGIBuffer>();
    winrt::com_ptr<ID3D11Texture2D> surface;
    winrt::check_hresult(dxgiBuffer->GetResource(IID_PPV_ARGS(surface.put())));
    UINT subresource = 0;
    winrt::check_hresult(dxgiBuffer->GetSubresourceIndex(&subresource));

    device_.context()->CopySubresourceRegion(surface.get(), subresource, 0, 0, 0, nv12, 0, nullptr);

    DWORD maxLength = 0;
    winrt::check_hresult(buffer->GetMaxLength(&maxLength));
    winrt::check_hresult(buffer->SetCurrentLength(maxLength));
    winrt::check_hresult(sample->SetSampleTime(timeHns));
    winrt::check_hresult(sample->SetSampleDuration(durationHns));
    {
        std::lock_guard lock(writeMutex_);
        winrt::check_hresult(writer_->WriteSample(videoStream_, sample.get()));
    }
    ++samplesWritten_;
    return true;
}

void MediaWriter::writeSystemMemory(ID3D11Texture2D* nv12, std::int64_t timeHns, std::int64_t durationHns) {
    // Synchronous readback: Map waits for the copy. Only the software fallback pays this.
    auto* context = device_.context();
    context->CopyResource(staging_.get(), nv12);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(context->Map(staging_.get(), 0, D3D11_MAP_READ, 0, &mapped));

    // Tightly packed NV12: Y rows, then interleaved UV rows at half height.
    const DWORD size = config_.width * config_.height * 3 / 2;
    winrt::com_ptr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(size, buffer.put());
    BYTE* dest = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&dest, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        const auto* src = static_cast<const BYTE*>(mapped.pData);
        const UINT rows = config_.height * 3 / 2;  // the UV plane follows Y at the same pitch
        hr = MFCopyImage(dest, config_.width, src, mapped.RowPitch, config_.width, rows);
        buffer->Unlock();
    }
    context->Unmap(staging_.get(), 0);
    winrt::check_hresult(hr);
    winrt::check_hresult(buffer->SetCurrentLength(size));

    winrt::com_ptr<IMFSample> sample;
    winrt::check_hresult(MFCreateSample(sample.put()));
    winrt::check_hresult(sample->AddBuffer(buffer.get()));
    winrt::check_hresult(sample->SetSampleTime(timeHns));
    winrt::check_hresult(sample->SetSampleDuration(durationHns));
    {
        std::lock_guard lock(writeMutex_);
        winrt::check_hresult(writer_->WriteSample(videoStream_, sample.get()));
    }
    ++samplesWritten_;
}

void MediaWriter::addAudioStream() {
    // AAC-LC, 48 kHz stereo. The Microsoft AAC encoder takes 16-bit PCM only and
    // accepts 96/128/160/192 kbps.
    const std::uint32_t kbps = std::clamp(config_.audioBitrateKbps / 32 * 32, 96u, 192u);

    winrt::com_ptr<IMFMediaType> output;
    winrt::check_hresult(MFCreateMediaType(output.put()));
    winrt::check_hresult(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    winrt::check_hresult(output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
    winrt::check_hresult(output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    winrt::check_hresult(output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioSampleRate));
    winrt::check_hresult(output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudioChannels));
    winrt::check_hresult(output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kbps * 1000 / 8));
    winrt::check_hresult(writer_->AddStream(output.get(), &audioStream_));

    winrt::com_ptr<IMFMediaType> input;
    winrt::check_hresult(MFCreateMediaType(input.put()));
    winrt::check_hresult(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    winrt::check_hresult(input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
    winrt::check_hresult(input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    winrt::check_hresult(input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioSampleRate));
    winrt::check_hresult(input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudioChannels));
    winrt::check_hresult(input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kAudioChannels * 2));
    winrt::check_hresult(input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioSampleRate * kAudioChannels * 2));
    winrt::check_hresult(writer_->SetInputMediaType(audioStream_, input.get(), nullptr));
}

void MediaWriter::writeAudio(const std::int16_t* pcm, std::uint32_t frames, std::int64_t timeHns) {
    if (!writer_ || !config_.audio || frames == 0) return;
    const DWORD bytes = frames * kAudioChannels * sizeof(std::int16_t);
    winrt::com_ptr<IMFMediaBuffer> buffer;
    winrt::check_hresult(MFCreateMemoryBuffer(bytes, buffer.put()));
    BYTE* dest = nullptr;
    winrt::check_hresult(buffer->Lock(&dest, nullptr, nullptr));
    std::memcpy(dest, pcm, bytes);
    buffer->Unlock();
    winrt::check_hresult(buffer->SetCurrentLength(bytes));

    winrt::com_ptr<IMFSample> sample;
    winrt::check_hresult(MFCreateSample(sample.put()));
    winrt::check_hresult(sample->AddBuffer(buffer.get()));
    winrt::check_hresult(sample->SetSampleTime(timeHns));
    winrt::check_hresult(sample->SetSampleDuration(static_cast<LONGLONG>(frames) * 10'000'000 / kAudioSampleRate));
    std::lock_guard lock(writeMutex_);
    winrt::check_hresult(writer_->WriteSample(audioStream_, sample.get()));
}

// Note: with the Intel Quick Sync encoder, every hardware SinkWriter leaves one
// Event, one registry Key and one WaitCompletionPacket handle behind even when
// fully released (reproduced with a bare MF SinkWriter; not with NVIDIA).
// It is inside Media Foundation / the Intel MFT; see README "Known issues".
void MediaWriter::releaseWriter() { writer_ = nullptr; }

void MediaWriter::finalize() {
    if (!writer_) return;
    if (samplesWritten_ == 0) {
        // Finalize fails with MF_E_SINK_NO_SAMPLES_PROCESSED; the caller removes the empty file.
        releaseWriter();
        spdlog::info("no frames written; output discarded");
        return;
    }
    std::lock_guard lock(writeMutex_);
    const HRESULT hr = writer_->Finalize();
    releaseWriter();
    winrt::check_hresult(hr);
    spdlog::info("output finalized: {}", winrt::to_string(config_.path.wstring()));
}

const char* codecName(VideoCodec codec) {
    switch (codec) {
    case VideoCodec::HEVC: return "HEVC";
    case VideoCodec::AV1: return "AV1";
    case VideoCodec::H264: break;
    }
    return "H.264";
}

std::vector<VideoEncoderInfo> enumerateVideoEncoders() {
    std::vector<VideoEncoderInfo> encoders;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return encoders;
    for (const auto codec : {VideoCodec::H264, VideoCodec::HEVC, VideoCodec::AV1}) {
        const MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, subtypeOf(codec)};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        const UINT32 flags = MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                             MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, &output, &activates, &count))) continue;
        for (UINT32 i = 0; i < count; ++i) {
            VideoEncoderInfo info;
            info.codec = codec;
            wchar_t name[256]{};
            if (SUCCEEDED(activates[i]->GetString(MFT_FRIENDLY_NAME_Attribute, name, ARRAYSIZE(name), nullptr)))
                info.name = name;
            info.hardware = MFGetAttributeUINT32(activates[i], MF_TRANSFORM_FLAGS_Attribute, 0) & MFT_ENUM_FLAG_HARDWARE;
            encoders.push_back(std::move(info));
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
    }
    MFShutdown();
    return encoders;
}

}  // namespace elv
