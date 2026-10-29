#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

// The bookkeeping of the bindless texture table without any Vulkan calls (so a test can run it): each distinct texture (an image view
// with a sampler) is given the next free slot, once, and keeps it for good. Slot 0 is reserved for the table's own fallback texture.
namespace Propulsion {

class TextureSlots {
public:
    static constexpr int32_t kNone = -1;

    explicit TextureSlots(uint32_t capacity) : m_capacity(capacity), m_used(capacity > 0 ? 1 : 0) {}

    /// The slot for a texture, giving it the next free one if it has none. kNone if the table is full.
    int32_t slotFor(uint64_t imageView, uint64_t sampler) {
        const Key key{imageView, sampler};
        auto it = m_slots.find(key);
        if (it != m_slots.end()) return it->second;
        if (m_used >= m_capacity) return kNone;
        const int32_t slot = static_cast<int32_t>(m_used++);
        m_slots.emplace(key, slot);
        return slot;
    }

    bool has(uint64_t imageView, uint64_t sampler) const { return m_slots.find(Key{imageView, sampler}) != m_slots.end(); }

    uint32_t used() const { return m_used; }  // including the fallback
    uint32_t capacity() const { return m_capacity; }

private:
    struct Key {
        uint64_t view;
        uint64_t sampler;
        bool operator==(const Key& o) const { return view == o.view && sampler == o.sampler; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<uint64_t>()(k.view) * 31u + std::hash<uint64_t>()(k.sampler);
        }
    };

    uint32_t m_capacity;
    uint32_t m_used;
    std::unordered_map<Key, int32_t, KeyHash> m_slots;
};

} // namespace Propulsion
