#pragma once

#include <mfidl.h>

#include <winrt/base.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace elv {

// In-memory IMFByteStream for the replay buffer. The SinkWriter writes fragmented
// MP4 into it; the stream splits that into the init segment (ftyp + moov) and
// fragments (moof + mdat), and keeps only the last `windowSeconds` (plus slack).
//
// save() writes ftyp + moov + the fragments from the last video keyframe at or
// before "now - seconds". Media Foundation's fragments carry no tfdt (times follow
// from the durations before them) and absolute base_data_offsets, so each saved
// fragment gets a tfdt with its time relative to the first saved one, and the
// offsets are moved to the new file positions.
class ReplayStream : public winrt::implements<ReplayStream, IMFByteStream> {
public:
    explicit ReplayStream(double windowSeconds);

    // Returns the saved length, or nullopt when there is nothing to save yet.
    std::optional<std::chrono::milliseconds> save(const std::filesystem::path& path, double seconds);
    // Seconds of video currently held.
    double bufferedSeconds() const;
    std::size_t bufferedBytes() const;

    // IMFByteStream
    STDMETHOD(GetCapabilities)(DWORD* capabilities) override;
    STDMETHOD(GetLength)(QWORD* length) override;
    STDMETHOD(SetLength)(QWORD length) override;
    STDMETHOD(GetCurrentPosition)(QWORD* position) override;
    STDMETHOD(SetCurrentPosition)(QWORD position) override;
    STDMETHOD(IsEndOfStream)(BOOL* endOfStream) override;
    STDMETHOD(Read)(BYTE* buffer, ULONG count, ULONG* read) override;
    STDMETHOD(BeginRead)(BYTE* buffer, ULONG count, IMFAsyncCallback* callback, IUnknown* state) override;
    STDMETHOD(EndRead)(IMFAsyncResult* result, ULONG* read) override;
    STDMETHOD(Write)(const BYTE* buffer, ULONG count, ULONG* written) override;
    STDMETHOD(BeginWrite)(const BYTE* buffer, ULONG count, IMFAsyncCallback* callback, IUnknown* state) override;
    STDMETHOD(EndWrite)(IMFAsyncResult* result, ULONG* written) override;
    STDMETHOD(Seek)(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD flags, QWORD* position) override;
    STDMETHOD(Flush)() override;
    STDMETHOD(Close)() override;

private:
    struct Sample {
        std::uint32_t duration = 0;
        std::uint32_t size = 0;
        std::uint32_t flags = 0;
        std::uint32_t compositionOffset = 0;  // raw trun value (signed when trunVersion is 1)
    };
    struct TrackFragment {
        std::uint32_t track = 0;
        std::uint64_t baseTime = 0;     // decode time of the first sample, track timescale
        std::uint64_t duration = 0;     // sum of sample durations
        std::size_t tfdtOffset = 0;     // in moof bytes, of the tfdt value; 0 if there is no tfdt
        std::size_t trafStart = 0;      // in moof bytes
        std::size_t tfhdEnd = 0;        // in moof bytes, where a tfdt can be inserted
        bool tfdtIs64 = false;
        bool sync = true;               // first sample is a sync sample
        std::size_t baseDataOffsetPos = 0;  // tfhd base_data_offset position, 0 if absent
        // Per-sample table (one trun with durations, sizes and flags) for cutting the
        // first fragment at a keyframe; empty if the fragment does not have one.
        std::vector<Sample> samples;
        std::uint64_t dataStart = 0;  // absolute stream offset of the first sample's data
        std::uint8_t trunVersion = 0;
    };
    struct Chunk {
        std::vector<std::uint8_t> bytes;  // moof + mdat
        std::uint64_t filePosition = 0;   // where moof was in the written stream
        std::size_t mfhdSequencePos = 0;
        std::vector<TrackFragment> tracks;
    };
    struct TrackInfo {
        std::uint32_t timescale = 1;
        bool video = false;
        std::uint32_t defaultDuration = 0;  // trex
        std::uint32_t defaultFlags = 0;
    };

    void append(const std::uint8_t* data, std::size_t size);  // at the end of the stream
    void parsePending();
    void addInit(const std::uint8_t* box, std::size_t size);
    void parseMoov(const std::uint8_t* box, std::size_t size);
    Chunk parseMoof(std::vector<std::uint8_t> bytes, std::uint64_t position) const;
    void trim();
    std::optional<double> videoTime(const Chunk& chunk) const;  // seconds; nullopt if no video
    // Fragment and video sample to start a save at: the last keyframe at or before
    // "end - seconds" (or the first keyframe held).
    std::optional<std::pair<std::size_t, std::size_t>> startPoint(double seconds) const;
    // Rebuilds `chunk` to start at video sample `first`, dropping audio that ends before it.
    Chunk cutChunk(const Chunk& chunk, std::size_t first) const;

    const double windowSeconds_;
    mutable std::mutex mutex_;
    std::uint64_t position_ = 0;
    std::uint64_t length_ = 0;
    std::vector<std::uint8_t> init_;     // ftyp + moov (and anything before the first moof)
    bool initDone_ = false;
    std::vector<std::uint8_t> pending_;  // bytes of incomplete top-level boxes
    std::uint64_t pendingStart_ = 0;
    std::vector<std::uint8_t> moof_;     // moof waiting for its mdat
    std::uint64_t moofPosition_ = 0;
    std::vector<Chunk> chunks_;
    std::map<std::uint32_t, TrackInfo> tracks_;
    std::map<std::uint32_t, std::uint64_t> decodeTime_;  // per track, for fragments without tfdt
};

}  // namespace elv
