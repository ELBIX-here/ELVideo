#include "output/ReplayStream.h"

#include <mfapi.h>
#include <mferror.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <string_view>

namespace elv {

namespace {

// Seconds kept beyond the window so a keyframe at or before the window start survives.
constexpr double kSlackSeconds = 6.0;
constexpr std::uint32_t kNonSyncFlag = 0x00010000;  // sample_is_non_sync_sample
// Attribute key carrying the byte count from BeginWrite to EndWrite.
constexpr GUID kWrittenKey = {0x3c1b2f40, 0x8e0d, 0x4f6a, {0x9a, 0x51, 0x2e, 0x7d, 0x10, 0x44, 0x6b, 0x21}};

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}
std::uint64_t be64(const std::uint8_t* p) { return (std::uint64_t{be32(p)} << 32) | be32(p + 4); }
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}
void put64(std::uint8_t* p, std::uint64_t v) {
    put32(p, static_cast<std::uint32_t>(v >> 32));
    put32(p + 4, static_cast<std::uint32_t>(v));
}

// Calls fn(type, boxStart, boxSize, headerSize) for each box in [p, p + size).
void forEachBox(const std::uint8_t* p, std::size_t size,
                const std::function<void(std::string_view, const std::uint8_t*, std::size_t, std::size_t)>& fn) {
    std::size_t at = 0;
    while (at + 8 <= size) {
        std::uint64_t boxSize = be32(p + at);
        std::size_t header = 8;
        if (boxSize == 1) {
            if (at + 16 > size) return;
            boxSize = be64(p + at + 8);
            header = 16;
        } else if (boxSize == 0) {
            boxSize = size - at;
        }
        if (boxSize < header || at + boxSize > size) return;
        fn(std::string_view(reinterpret_cast<const char*>(p + at + 4), 4), p + at, static_cast<std::size_t>(boxSize),
           header);
        at += static_cast<std::size_t>(boxSize);
    }
}

}  // namespace

ReplayStream::ReplayStream(double windowSeconds) : windowSeconds_(windowSeconds) {}

// --- box parsing -------------------------------------------------------------

void ReplayStream::parseMoov(const std::uint8_t* box, std::size_t size) {
    forEachBox(box + 8, size - 8, [this](std::string_view type, const std::uint8_t* p, std::size_t n, std::size_t h) {
        if (type == "trak") {
            std::uint32_t id = 0, timescale = 1;
            bool video = false;
            forEachBox(p + h, n - h, [&](std::string_view t, const std::uint8_t* q, std::size_t m, std::size_t hh) {
                if (t == "tkhd") {
                    const std::uint8_t version = q[hh];
                    id = be32(q + hh + 4 + (version == 1 ? 16 : 8));
                } else if (t == "mdia") {
                    forEachBox(q + hh, m - hh, [&](std::string_view u, const std::uint8_t* r, std::size_t, std::size_t rh) {
                        if (u == "mdhd") {
                            const std::uint8_t version = r[rh];
                            timescale = be32(r + rh + 4 + (version == 1 ? 16 : 8));
                        } else if (u == "hdlr") {
                            video = std::string_view(reinterpret_cast<const char*>(r + rh + 8), 4) == "vide";
                        }
                    });
                }
            });
            auto& info = tracks_[id];
            info.timescale = std::max(1u, timescale);
            info.video = video;
        } else if (type == "mvex") {
            forEachBox(p + h, n - h, [&](std::string_view t, const std::uint8_t* q, std::size_t, std::size_t hh) {
                if (t != "trex") return;
                auto& info = tracks_[be32(q + hh + 4)];
                info.defaultDuration = be32(q + hh + 12);
                info.defaultFlags = be32(q + hh + 20);
            });
        }
    });
}

ReplayStream::Chunk ReplayStream::parseMoof(std::vector<std::uint8_t> bytes, std::uint64_t position) const {
    Chunk chunk;
    chunk.filePosition = position;
    const std::uint8_t* moof = bytes.data();
    const std::size_t moofSize = be32(moof);
    forEachBox(moof + 8, moofSize - 8, [&](std::string_view type, const std::uint8_t* p, std::size_t n, std::size_t h) {
        if (type == "mfhd") {
            chunk.mfhdSequencePos = static_cast<std::size_t>(p - moof) + h + 4;
        } else if (type == "traf") {
            TrackFragment tf;
            tf.trafStart = static_cast<std::size_t>(p - moof);
            std::uint32_t defaultDuration = 0, defaultFlags = 0;
            bool haveDefaultDuration = false, haveDefaultFlags = false;
            std::optional<std::uint32_t> firstFlags;
            std::uint64_t duration = 0;
            std::uint32_t sampleCount = 0;
            bool perSampleDuration = false;
            forEachBox(p + h, n - h, [&](std::string_view t, const std::uint8_t* q, std::size_t, std::size_t hh) {
                const std::uint32_t flags = be32(q + hh) & 0xFFFFFF;
                const std::uint8_t version = q[hh];
                if (t == "tfhd") {
                    tf.track = be32(q + hh + 4);
                    tf.tfhdEnd = static_cast<std::size_t>(q - moof) + be32(q);
                    std::size_t at = hh + 8;
                    if (flags & 0x1) {
                        tf.baseDataOffsetPos = static_cast<std::size_t>(q - moof) + at;
                        at += 8;
                    }
                    if (flags & 0x2) at += 4;
                    if (flags & 0x8) {
                        defaultDuration = be32(q + at);
                        haveDefaultDuration = true;
                        at += 4;
                    }
                    if (flags & 0x10) at += 4;
                    if (flags & 0x20) {
                        defaultFlags = be32(q + at);
                        haveDefaultFlags = true;
                    }
                } else if (t == "tfdt") {
                    tf.tfdtIs64 = version == 1;
                    tf.tfdtOffset = static_cast<std::size_t>(q - moof) + hh + 4;
                    tf.baseTime = tf.tfdtIs64 ? be64(q + hh + 4) : be32(q + hh + 4);
                } else if (t == "trun") {
                    const std::uint32_t count = be32(q + hh + 4);
                    std::size_t at = hh + 8;
                    const bool full = (flags & 0x700) == 0x700 && sampleCount == 0;  // duration, size, flags
                    tf.trunVersion = version;
                    if (flags & 0x1) {
                        tf.dataStart = be32(q + at);  // data_offset; made absolute below
                        at += 4;
                    }
                    if (flags & 0x4) {
                        firstFlags = be32(q + at);
                        at += 4;
                    }
                    const std::size_t fieldSize = ((flags & 0x100) ? 4 : 0) + ((flags & 0x200) ? 4 : 0) +
                                                  ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
                    for (std::uint32_t i = 0; i < count; ++i) {
                        std::size_t field = at + i * fieldSize;
                        Sample sample;
                        if (flags & 0x100) {
                            sample.duration = be32(q + field);
                            duration += sample.duration;
                            perSampleDuration = true;
                            field += 4;
                        }
                        if (flags & 0x200) {
                            sample.size = be32(q + field);
                            field += 4;
                        }
                        if (flags & 0x400) {
                            sample.flags = be32(q + field);
                            if (i == 0 && !firstFlags) firstFlags = sample.flags;
                            field += 4;
                        }
                        if (flags & 0x800) sample.compositionOffset = be32(q + field);
                        if (full) tf.samples.push_back(sample);
                    }
                    if (!full) tf.samples.clear();  // second trun or missing fields: no cutting
                    sampleCount += count;
                }
            });
            const auto info = tracks_.find(tf.track);
            const bool video = info != tracks_.end() && info->second.video;
            if (!perSampleDuration) {
                const std::uint32_t d = haveDefaultDuration ? defaultDuration
                                        : info != tracks_.end() ? info->second.defaultDuration : 0;
                duration = static_cast<std::uint64_t>(d) * sampleCount;
            }
            tf.duration = duration;
            // Absolute data position: base_data_offset (or the moof itself) + data_offset.
            const std::uint64_t base = tf.baseDataOffsetPos ? be64(moof + tf.baseDataOffsetPos) : position;
            tf.dataStart += base;
            const std::uint32_t flags0 = firstFlags  ? *firstFlags
                                         : haveDefaultFlags ? defaultFlags
                                         : info != tracks_.end() ? info->second.defaultFlags : 0;
            tf.sync = !video || !(flags0 & kNonSyncFlag);
            chunk.tracks.push_back(tf);
        }
    });
    chunk.bytes = std::move(bytes);
    return chunk;
}

std::optional<double> ReplayStream::videoTime(const Chunk& chunk) const {
    for (const auto& tf : chunk.tracks) {
        const auto info = tracks_.find(tf.track);
        if (info != tracks_.end() && info->second.video) return static_cast<double>(tf.baseTime) / info->second.timescale;
    }
    return std::nullopt;
}

void ReplayStream::addInit(const std::uint8_t* box, std::size_t size) {
    init_.insert(init_.end(), box, box + size);
    if (std::string_view(reinterpret_cast<const char*>(box + 4), 4) == "moov") parseMoov(box, size);
}

void ReplayStream::parsePending() {
    for (;;) {
        if (pending_.size() < 8) return;
        std::uint64_t size = be32(pending_.data());
        if (size == 1) {
            if (pending_.size() < 16) return;
            size = be64(pending_.data() + 8);
        }
        if (size < 8 || pending_.size() < size) return;
        const std::string_view type(reinterpret_cast<const char*>(pending_.data() + 4), 4);
        const auto boxSize = static_cast<std::size_t>(size);

        if (type == "moof") {
            initDone_ = true;
            moof_.assign(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(boxSize));
            moofPosition_ = pendingStart_;
        } else if (type == "mdat" && !moof_.empty()) {
            std::vector<std::uint8_t> bytes = std::move(moof_);
            moof_.clear();
            bytes.insert(bytes.end(), pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(boxSize));
            Chunk chunk = parseMoof(std::move(bytes), moofPosition_);
            for (auto& tf : chunk.tracks) {
                if (!tf.tfdtOffset) tf.baseTime = decodeTime_[tf.track];  // implicit: after the previous fragment
                decodeTime_[tf.track] = tf.baseTime + tf.duration;
            }
            chunks_.push_back(std::move(chunk));
            trim();
        } else if (!initDone_) {
            addInit(pending_.data(), boxSize);
        }  // else: mfra/sidx etc. after the fragments; not needed
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(boxSize));
        pendingStart_ += size;
    }
}

void ReplayStream::trim() {
    std::optional<double> latest;
    for (auto it = chunks_.rbegin(); it != chunks_.rend() && !latest; ++it) latest = videoTime(*it);
    if (!latest) return;
    const double keepFrom = *latest - windowSeconds_ - kSlackSeconds;
    std::size_t drop = 0;
    while (drop + 1 < chunks_.size()) {
        const auto t = videoTime(chunks_[drop]);
        if (t && *t >= keepFrom) break;
        if (!t) {  // audio-only chunk: drop it with the video before it
            const auto& tf = chunks_[drop].tracks;
            const auto info = tf.empty() ? tracks_.end() : tracks_.find(tf.front().track);
            if (info != tracks_.end() && static_cast<double>(tf.front().baseTime) / info->second.timescale >= keepFrom) break;
        }
        ++drop;
    }
    if (drop) chunks_.erase(chunks_.begin(), chunks_.begin() + static_cast<std::ptrdiff_t>(drop));
}

void ReplayStream::append(const std::uint8_t* data, std::size_t size) {
    pending_.insert(pending_.end(), data, data + size);
    parsePending();
}

std::optional<std::pair<std::size_t, std::size_t>> ReplayStream::startPoint(double seconds) const {
    std::optional<double> latest;
    for (auto it = chunks_.rbegin(); it != chunks_.rend() && !latest; ++it) {
        if (const auto t = videoTime(*it)) {
            for (const auto& tf : it->tracks)
                if (tracks_.at(tf.track).video) latest = *t + static_cast<double>(tf.duration) / tracks_.at(tf.track).timescale;
        }
    }
    if (!latest) return std::nullopt;
    const double from = *latest - seconds;
    std::optional<std::pair<std::size_t, std::size_t>> best, first;
    for (std::size_t i = 0; i < chunks_.size(); ++i) {
        for (const auto& tf : chunks_[i].tracks) {
            const auto& info = tracks_.at(tf.track);
            if (!info.video) continue;
            if (tf.samples.empty()) {  // no sample table: only whole fragments that open with a keyframe
                if (!tf.sync) continue;
                if (!first) first = std::make_pair(i, std::size_t{0});
                if (static_cast<double>(tf.baseTime) / info.timescale <= from + 1e-6) best = std::make_pair(i, std::size_t{0});
                continue;
            }
            std::uint64_t time = tf.baseTime;
            for (std::size_t s = 0; s < tf.samples.size(); ++s) {
                if (!(tf.samples[s].flags & kNonSyncFlag)) {
                    if (!first) first = std::make_pair(i, s);
                    if (static_cast<double>(time) / info.timescale <= from + 1e-6) best = std::make_pair(i, s);
                }
                time += tf.samples[s].duration;
            }
        }
    }
    return best ? best : first;
}

ReplayStream::Chunk ReplayStream::cutChunk(const Chunk& chunk, std::size_t first) const {
    // Keyframe time: samples before it are dropped from the video, and audio that
    // ends before it from the audio.
    double cutSeconds = 0;
    for (const auto& tf : chunk.tracks) {
        const auto& info = tracks_.at(tf.track);
        if (!info.video) continue;
        std::uint64_t t = tf.baseTime;
        for (std::size_t s = 0; s < first; ++s) t += tf.samples[s].duration;
        cutSeconds = static_cast<double>(t) / info.timescale;
    }

    struct Part {
        const TrackFragment* tf;
        std::size_t from;
        std::uint64_t base;
        std::uint64_t dataOffset;  // in chunk.bytes
        std::uint64_t dataSize;
    };
    std::vector<Part> parts;
    for (const auto& tf : chunk.tracks) {
        const auto& info = tracks_.at(tf.track);
        std::size_t from = 0;
        std::uint64_t base = tf.baseTime;
        std::uint64_t offset = tf.dataStart - chunk.filePosition;
        if (info.video) {
            from = first;
        } else {
            while (from + 1 < tf.samples.size() &&
                   static_cast<double>(base + tf.samples[from].duration) / info.timescale <= cutSeconds) {
                base += tf.samples[from].duration;
                ++from;
            }
            base = tf.baseTime;
            for (std::size_t s = 0; s < from; ++s) base += tf.samples[s].duration;
        }
        if (info.video) {
            for (std::size_t s = 0; s < from; ++s) base += tf.samples[s].duration;
        }
        for (std::size_t s = 0; s < from; ++s) offset += tf.samples[s].size;
        std::uint64_t size = 0;
        for (std::size_t s = from; s < tf.samples.size(); ++s) size += tf.samples[s].size;
        parts.push_back({&tf, from, base, offset, size});
    }

    // moof: mfhd + per track (tfhd with base_data_offset, tfdt v1, trun with every field).
    std::vector<std::uint8_t> moof;
    const auto put = [&](std::size_t at, std::uint32_t v) { put32(moof.data() + at, v); };
    const auto add32 = [&](std::uint32_t v) {
        moof.resize(moof.size() + 4);
        put(moof.size() - 4, v);
    };
    Chunk out;
    add32(0);
    moof.insert(moof.end(), {'m', 'o', 'o', 'f'});
    add32(16);
    moof.insert(moof.end(), {'m', 'f', 'h', 'd'});
    add32(0);
    out.mfhdSequencePos = moof.size();
    add32(0);
    std::vector<std::size_t> basePositions;
    for (const auto& part : parts) {
        const std::size_t count = part.tf->samples.size() - part.from;
        const std::size_t trafStart = moof.size();
        add32(0);
        moof.insert(moof.end(), {'t', 'r', 'a', 'f'});
        add32(24);
        moof.insert(moof.end(), {'t', 'f', 'h', 'd'});
        add32(0x000001);  // base_data_offset present
        add32(part.tf->track);
        basePositions.push_back(moof.size());
        add32(0);
        add32(0);
        TrackFragment tf = *part.tf;
        tf.trafStart = trafStart;
        add32(20);
        moof.insert(moof.end(), {'t', 'f', 'd', 't'});
        add32(0x01000000);  // version 1
        tf.tfdtOffset = moof.size();
        tf.tfdtIs64 = true;
        tf.baseTime = part.base;
        add32(0);
        add32(0);
        add32(static_cast<std::uint32_t>(20 + 16 * count));
        moof.insert(moof.end(), {'t', 'r', 'u', 'n'});
        add32((static_cast<std::uint32_t>(part.tf->trunVersion) << 24) | 0x000F01);
        add32(static_cast<std::uint32_t>(count));
        add32(0);  // data_offset: samples start exactly at base_data_offset
        tf.duration = 0;
        for (std::size_t s = part.from; s < part.tf->samples.size(); ++s) {
            const auto& sample = part.tf->samples[s];
            add32(sample.duration);
            add32(sample.size);
            add32(sample.flags);
            add32(sample.compositionOffset);
            tf.duration += sample.duration;
        }
        tf.sync = true;
        tf.samples.assign(part.tf->samples.begin() + static_cast<std::ptrdiff_t>(part.from), part.tf->samples.end());
        put(trafStart, static_cast<std::uint32_t>(moof.size() - trafStart));
        out.tracks.push_back(tf);
    }
    put(0, static_cast<std::uint32_t>(moof.size()));

    // mdat with each track's remaining sample data; base_data_offsets are relative to
    // this chunk (filePosition 0), and save() moves them to the file position.
    std::uint64_t mdatSize = 8;
    for (const auto& part : parts) mdatSize += part.dataSize;
    std::uint64_t at = moof.size() + 8;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        put64(moof.data() + basePositions[i], at);
        out.tracks[i].baseDataOffsetPos = basePositions[i];
        at += parts[i].dataSize;
    }
    out.bytes = std::move(moof);
    const std::size_t mdatAt = out.bytes.size();
    out.bytes.resize(out.bytes.size() + 8);
    put32(out.bytes.data() + mdatAt, static_cast<std::uint32_t>(mdatSize));
    std::memcpy(out.bytes.data() + mdatAt + 4, "mdat", 4);
    for (const auto& part : parts)
        out.bytes.insert(out.bytes.end(), chunk.bytes.begin() + static_cast<std::ptrdiff_t>(part.dataOffset),
                         chunk.bytes.begin() + static_cast<std::ptrdiff_t>(part.dataOffset + part.dataSize));
    out.filePosition = 0;
    return out;
}

double ReplayStream::bufferedSeconds() const {
    std::lock_guard lock(mutex_);
    std::optional<double> first, last;
    for (const auto& c : chunks_)
        if (const auto t = videoTime(c)) {
            if (!first) first = t;
            last = t;
        }
    return first ? std::min(*last - *first, windowSeconds_) : 0.0;
}

std::size_t ReplayStream::bufferedBytes() const {
    std::lock_guard lock(mutex_);
    std::size_t total = init_.size();
    for (const auto& c : chunks_) total += c.bytes.size();
    return total;
}

std::optional<std::chrono::milliseconds> ReplayStream::save(const std::filesystem::path& path, double seconds) {
    std::vector<std::uint8_t> init;
    std::vector<Chunk> chunks;
    std::map<std::uint32_t, TrackInfo> tracks;
    {
        std::lock_guard lock(mutex_);
        const auto start = startPoint(std::min(seconds, windowSeconds_));
        if (!start || init_.empty()) return std::nullopt;
        init = init_;
        const auto& firstChunk = chunks_[start->first];
        const bool cuttable = std::all_of(firstChunk.tracks.begin(), firstChunk.tracks.end(),
                                          [](const TrackFragment& tf) { return !tf.samples.empty(); });
        chunks.push_back(cuttable ? cutChunk(firstChunk, start->second) : firstChunk);
        chunks.insert(chunks.end(), chunks_.begin() + static_cast<std::ptrdiff_t>(start->first) + 1, chunks_.end());
        tracks = tracks_;
    }

    // Time zero is the earliest track start in the first fragment (the video keyframe,
    // or audio a few ms before it); both tracks keep their offset to each other.
    double startSeconds = 1e300;
    for (const auto& tf : chunks.front().tracks)
        startSeconds = std::min(startSeconds, static_cast<double>(tf.baseTime) / tracks.at(tf.track).timescale);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return std::nullopt;
    out.write(reinterpret_cast<const char*>(init.data()), static_cast<std::streamsize>(init.size()));
    std::uint64_t written = init.size();
    std::uint32_t sequence = 1;
    double endSeconds = startSeconds;
    constexpr std::size_t kTfdtSize = 20;  // version 1: 64-bit baseMediaDecodeTime
    for (auto& chunk : chunks) {
        auto& bytes = chunk.bytes;
        const std::size_t inserted =
            kTfdtSize * static_cast<std::size_t>(std::count_if(chunk.tracks.begin(), chunk.tracks.end(),
                                                               [](const TrackFragment& tf) { return !tf.tfdtOffset; }));
        // Fix values in place first (original offsets), then insert the new tfdt boxes.
        std::vector<std::pair<std::size_t, std::uint64_t>> insertions;  // (offset, decode time)
        for (const auto& tf : chunk.tracks) {
            const auto& info = tracks.at(tf.track);
            const auto shift = static_cast<std::uint64_t>(std::llround(startSeconds * info.timescale));
            const std::uint64_t base = tf.baseTime > shift ? tf.baseTime - shift : 0;
            if (tf.tfdtOffset) {
                if (tf.tfdtIs64)
                    put64(bytes.data() + tf.tfdtOffset, base);
                else
                    put32(bytes.data() + tf.tfdtOffset, static_cast<std::uint32_t>(base));
            } else {
                insertions.emplace_back(tf.trafStart, base);
            }
            if (tf.baseDataOffsetPos) {
                // Absolute file offset of this track's sample data (inside mdat, after moof).
                const std::uint64_t old = be64(bytes.data() + tf.baseDataOffsetPos);
                put64(bytes.data() + tf.baseDataOffsetPos, old - chunk.filePosition + written + inserted);
            }
            if (info.video)
                endSeconds = std::max(endSeconds, static_cast<double>(tf.baseTime + tf.duration) / info.timescale);
        }
        if (chunk.mfhdSequencePos) put32(bytes.data() + chunk.mfhdSequencePos, sequence++);

        // Insert from the back so earlier offsets stay valid; grow traf and moof sizes.
        std::sort(insertions.begin(), insertions.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [trafStart, base] : insertions) {
            const auto tf = std::find_if(chunk.tracks.begin(), chunk.tracks.end(),
                                         [&](const TrackFragment& t) { return t.trafStart == trafStart; });
            std::uint8_t tfdt[kTfdtSize]{};
            put32(tfdt, kTfdtSize);
            std::memcpy(tfdt + 4, "tfdt", 4);
            tfdt[8] = 1;  // version 1, flags 0
            put64(tfdt + 12, base);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(tf->tfhdEnd), tfdt, tfdt + kTfdtSize);
            put32(bytes.data() + trafStart, be32(bytes.data() + trafStart) + kTfdtSize);
        }
        put32(bytes.data(), be32(bytes.data()) + static_cast<std::uint32_t>(inserted));

        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        written += bytes.size();
    }
    out.close();
    if (!out) return std::nullopt;
    return std::chrono::milliseconds(std::llround((endSeconds - startSeconds) * 1000));
}

// --- IMFByteStream -------------------------------------------------------------

STDMETHODIMP ReplayStream::GetCapabilities(DWORD* capabilities) {
    *capabilities = MFBYTESTREAM_IS_WRITABLE | MFBYTESTREAM_IS_SEEKABLE;
    return S_OK;
}

STDMETHODIMP ReplayStream::GetLength(QWORD* length) {
    std::lock_guard lock(mutex_);
    *length = length_;
    return S_OK;
}

STDMETHODIMP ReplayStream::SetLength(QWORD length) {
    std::lock_guard lock(mutex_);
    if (length == 0) {  // a writer starting over (hardware -> software fallback)
        init_.clear();
        initDone_ = false;
        pending_.clear();
        pendingStart_ = 0;
        moof_.clear();
        chunks_.clear();
        tracks_.clear();
        decodeTime_.clear();
        position_ = 0;
    }
    length_ = length;
    return S_OK;
}

STDMETHODIMP ReplayStream::GetCurrentPosition(QWORD* position) {
    std::lock_guard lock(mutex_);
    *position = position_;
    return S_OK;
}

STDMETHODIMP ReplayStream::SetCurrentPosition(QWORD position) {
    std::lock_guard lock(mutex_);
    position_ = position;
    return S_OK;
}

STDMETHODIMP ReplayStream::IsEndOfStream(BOOL* endOfStream) {
    std::lock_guard lock(mutex_);
    *endOfStream = position_ >= length_;
    return S_OK;
}

STDMETHODIMP ReplayStream::Read(BYTE*, ULONG, ULONG* read) {
    if (read) *read = 0;
    return E_NOTIMPL;
}
STDMETHODIMP ReplayStream::BeginRead(BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) { return E_NOTIMPL; }
STDMETHODIMP ReplayStream::EndRead(IMFAsyncResult*, ULONG*) { return E_NOTIMPL; }

STDMETHODIMP ReplayStream::Write(const BYTE* buffer, ULONG count, ULONG* written) {
    std::lock_guard lock(mutex_);
    try {
        std::size_t done = 0;
        // Overwrites of earlier bytes (header fix-ups): patch what is still held.
        while (done < count && position_ < length_) {
            const std::uint64_t pos = position_;
            if (pos < init_.size()) {
                init_[static_cast<std::size_t>(pos)] = buffer[done];
            } else if (pos >= pendingStart_ && pos < pendingStart_ + pending_.size()) {
                pending_[static_cast<std::size_t>(pos - pendingStart_)] = buffer[done];
            }  // else: a fragment already parsed or dropped; MF does not rewrite those
            ++done;
            ++position_;
        }
        if (done < count) {
            append(buffer + done, count - done);
            position_ += count - done;
            length_ = position_;
        }
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    if (written) *written = count;
    return S_OK;
}

STDMETHODIMP ReplayStream::BeginWrite(const BYTE* buffer, ULONG count, IMFAsyncCallback* callback, IUnknown* state) {
    ULONG written = 0;
    const HRESULT hr = Write(buffer, count, &written);
    winrt::com_ptr<IMFAttributes> result;
    if (FAILED(MFCreateAttributes(result.put(), 1))) return E_OUTOFMEMORY;
    result->SetUINT32(kWrittenKey, written);
    winrt::com_ptr<IMFAsyncResult> async;
    const HRESULT created = MFCreateAsyncResult(result.get(), callback, state, async.put());
    if (FAILED(created)) return created;
    async->SetStatus(hr);
    return MFInvokeCallback(async.get());
}

STDMETHODIMP ReplayStream::EndWrite(IMFAsyncResult* result, ULONG* written) {
    winrt::com_ptr<IUnknown> object;
    if (written) *written = 0;
    if (SUCCEEDED(result->GetObject(object.put())))
        if (const auto attributes = object.try_as<IMFAttributes>(); attributes && written)
            *written = MFGetAttributeUINT32(attributes.get(), kWrittenKey, 0);
    return result->GetStatus();
}

STDMETHODIMP ReplayStream::Seek(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD, QWORD* position) {
    std::lock_guard lock(mutex_);
    const LONGLONG base = origin == msoCurrent ? static_cast<LONGLONG>(position_) : 0;
    if (base + offset < 0) return E_INVALIDARG;
    position_ = static_cast<std::uint64_t>(base + offset);
    if (position) *position = position_;
    return S_OK;
}

STDMETHODIMP ReplayStream::Flush() { return S_OK; }
STDMETHODIMP ReplayStream::Close() { return S_OK; }

}  // namespace elv
