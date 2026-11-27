#include "alamo/ai/tactical/perception_cache.hpp"

#include <bit>
#include <cstring>
#include <vector>

#include "alamo/ai/tactical/constants.hpp"
#include "alamo/core/sim_crc.hpp"

namespace alamo::ai::tactical {

namespace {
thread_local bool t_uncounted = false;  // LeafCacheUncounted
thread_local bool t_readonly = false;   // LeafCacheReadOnly
}  // namespace

LeafCacheUncounted::LeafCacheUncounted() : prev_(t_uncounted) { t_uncounted = true; }
LeafCacheUncounted::~LeafCacheUncounted() { t_uncounted = prev_; }
LeafCacheReadOnly::LeafCacheReadOnly() : prev_(t_readonly) { t_readonly = true; }
LeafCacheReadOnly::~LeafCacheReadOnly() { t_readonly = prev_; }

bool leaf_cache_hit(int32_t lifetime_s, uint32_t stamp, uint32_t now) {
    if (lifetime_s == kCacheNone || stamp == 0) return false;
    if (lifetime_s == kCacheForever) return true;
    if (lifetime_s < 0) return false;
    const uint64_t until = static_cast<uint64_t>(stamp) +
                           static_cast<uint64_t>(lifetime_s) * static_cast<uint64_t>(kFramesPerSecond);
    return static_cast<uint64_t>(now) <= until;
}

bool LeafCache::lookup(const LeafCacheKey& key, int32_t lifetime_s, uint32_t now, double& out) const {
    if (lifetime_s == kCacheNone) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = map_.find(key);
    if (it == map_.end() || !leaf_cache_hit(lifetime_s, it->second.stamp, now)) return false;
    out = it->second.value;
    if (!t_uncounted) ++hits_;
    return true;
}

void LeafCache::store(const LeafCacheKey& key, int32_t lifetime_s, uint32_t now, double value) {
    if (lifetime_s == kCacheNone || t_readonly) return;
    std::lock_guard<std::mutex> lock(mu_);
    Entry& e = map_[key];
    e.value = value;
    e.stamp = now;
}

void LeafCache::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    map_.clear();
    hits_ = 0;
}

size_t LeafCache::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return map_.size();
}

uint64_t LeafCache::hits() const {
    std::lock_guard<std::mutex> lock(mu_);
    return hits_;
}

void LeafCache::hash(StateHasher& h) const {
    std::lock_guard<std::mutex> lock(mu_);
    h.u64(static_cast<uint64_t>(map_.size()));
    // #631: the entries (key order) as a tree hash, so the capture spreads them over its workers (same digest for every
    // worker count; the lock is held by this thread while the blocks only read).
    std::vector<const std::pair<const LeafCacheKey, Entry>*> entries;
    entries.reserve(map_.size());
    for (const auto& kv : map_) entries.push_back(&kv);
    // Each block gathers the fields' little-endian bytes (the typed feeds' byte stream) and hashes them with one bytes().
    static_assert(std::endian::native == std::endian::little);
    // the bytes of one entry besides its parameter text (written below with sizeof of each field: keep the two in step)
    constexpr size_t kFixed = sizeof(LeafCacheKey::eval_kind) + sizeof(LeafCacheKey::eval_a) + sizeof(LeafCacheKey::eval_b) +
                              sizeof(LeafCacheKey::leaf) + sizeof(LeafCacheKey::self) + sizeof(LeafCacheKey::target) +
                              sizeof(LeafCacheKey::enemy) + sizeof(LeafCacheKey::human) + sizeof(LeafCacheKey::location) +
                              sizeof(uint32_t) /* params length */ + sizeof(Entry::stamp) + sizeof(uint64_t) /* value bits */;
    static_assert(kFixed == 1 + 4 * 2 + 2 + 4 * 5 + 4 + 4 + 8 && sizeof(Entry::value) == sizeof(uint64_t));
    hash_blocks(h, static_cast<uint32_t>(entries.size()), [&](StateHasher& bh, uint32_t b, uint32_t e) {
        thread_local std::vector<unsigned char> buf;
        buf.clear();
        for (uint32_t i = b; i < e; ++i) {
            const auto& [k, en] = *entries[i];
            const auto len = static_cast<uint32_t>(k.params.size());
            const size_t at = buf.size();
            buf.resize(at + kFixed + len);
            unsigned char* o = buf.data() + at;
            const auto put = [&o](const auto& v) {
                std::memcpy(o, &v, sizeof v);
                o += sizeof v;
            };
            put(k.eval_kind);
            put(k.eval_a);
            put(k.eval_b);
            put(k.leaf);
            put(k.self);
            put(k.target);
            put(k.enemy);
            put(k.human);
            put(k.location);
            put(len);
            if (len != 0) std::memcpy(o, k.params.data(), len);
            o += len;
            put(en.stamp);
            put(std::bit_cast<uint64_t>(en.value));
        }
        bh.bytes(buf.data(), buf.size());
    });
}

}  // namespace alamo::ai::tactical
