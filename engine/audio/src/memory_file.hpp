// Bounded in-memory miniaudio VFS file, shared by decode.cpp (probe / full decode) and miniaudio_backend.cpp (streamed
// music / speech voices). Internal to engine/audio.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

#include "ma_include.hpp"

namespace alamo::audio::detail {

// The input as a read-only miniaudio VFS file, handed to ma_decoder_init_vfs instead of ma_decoder_init_memory.
// dr_wav's fmt parser skips the rest of the chunk with a relative seek of (int)(chunk size - 18 - cbSize), which goes
// backwards when cbSize runs past the chunk or the size is >= 2^31. ma_decoder_init_memory first tries each backend's
// own memory reader (an in-range backward seek is accepted there), then retries with ma_decoder's generic reader,
// which clamps a seek past the end to the end; in both the stream lands back on or before the fmt chunk and the chunk
// walk repeats forever (fuzz hang 2026-11-21). The clamping reader also turns any wrapped chunk size into ~2^33
// successful 2 GB seeks. This file refuses seeks outside the buffer, as the backends' memory readers do, and backward
// relative seeks, which no backend issues for a well-formed file, so every chunk walk advances or stops.
//
// miniaudio keeps the ma_vfs* (this object) for the decoder's whole life and reads through it on every
// ma_decoder_read_pcm_frames / seek, so the MemoryFile must not move and `bytes` must stay valid until
// ma_decoder_uninit. One MemoryFile serves one decoder (the read cursor lives here).
struct MemoryFile {
    ma_vfs_callbacks cb;  // first: miniaudio passes the ma_vfs* as a ma_vfs_callbacks*
    std::span<const uint8_t> bytes;
    size_t pos = 0;

    static MemoryFile& of(ma_vfs* vfs) { return *static_cast<MemoryFile*>(vfs); }
    static ma_result open(ma_vfs* vfs, const char*, ma_uint32, ma_vfs_file* file) {
        of(vfs).pos = 0;
        *file = vfs;
        return MA_SUCCESS;
    }
    static ma_result close(ma_vfs*, ma_vfs_file) { return MA_SUCCESS; }
    static ma_result read(ma_vfs* vfs, ma_vfs_file, void* dst, size_t n, size_t* got) {
        MemoryFile& f = of(vfs);
        const size_t left = f.bytes.size() - f.pos;
        n = std::min(n, left);
        if (n > 0) std::memcpy(dst, f.bytes.data() + f.pos, n);
        f.pos += n;
        *got = n;
        return left > 0 ? MA_SUCCESS : MA_AT_END;
    }
    static ma_result seek(ma_vfs* vfs, ma_vfs_file, ma_int64 offset, ma_seek_origin origin) {
        MemoryFile& f = of(vfs);
        const auto size = static_cast<ma_int64>(f.bytes.size());
        const ma_int64 base = origin == ma_seek_origin_start     ? 0
                              : origin == ma_seek_origin_current ? static_cast<ma_int64>(f.pos)
                                                                 : size;
        if (origin == ma_seek_origin_current && offset < 0) return MA_BAD_SEEK;
        if (offset < -base || offset > size - base) return MA_BAD_SEEK;
        f.pos = static_cast<size_t>(base + offset);
        return MA_SUCCESS;
    }
    static ma_result tell(ma_vfs* vfs, ma_vfs_file, ma_int64* cursor) {
        *cursor = static_cast<ma_int64>(of(vfs).pos);
        return MA_SUCCESS;
    }
    static ma_result info(ma_vfs* vfs, ma_vfs_file, ma_file_info* out) {
        out->sizeInBytes = of(vfs).bytes.size();
        return MA_SUCCESS;
    }

    explicit MemoryFile(std::span<const uint8_t> b = {})
        : cb{open, nullptr, close, read, nullptr, seek, tell, info}, bytes(b) {}
    MemoryFile(const MemoryFile&) = delete;  // a live decoder holds this object's address
    MemoryFile& operator=(const MemoryFile&) = delete;
};

// ma_decoder_init_vfs over `file` (whose bytes must be set). The name has no extension, so the backends are tried in
// miniaudio's order (WAV, then MP3), as ma_decoder_init_memory does. Empty input is MA_INVALID_ARGS.
inline ma_result init_decoder(MemoryFile& file, const ma_decoder_config& cfg, ma_decoder& dec) {
    if (file.bytes.empty()) return MA_INVALID_ARGS;
    return ma_decoder_init_vfs(&file, "memory", &cfg, &dec);
}

}  // namespace alamo::audio::detail
