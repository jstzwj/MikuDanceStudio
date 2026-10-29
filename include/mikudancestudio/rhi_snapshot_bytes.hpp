#pragma once
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace mikudancestudio::rhi {
// Value-semantic CPU bytes. Seal publishes immutable storage that snapshots
// can share. Any writable reference detaches and disables sharing; copying an
// unsealed buffer is a deep copy, including when a caller retained data().
// Seal invalidates previously obtained writable references, like reallocation.
class SnapshotBytes {
    using Vector = std::vector<std::uint8_t>;
    struct Storage {
        Vector bytes;
        bool sealed = false;
        mutable std::mutex mutex;
        mutable std::uint64_t fingerprint = 0;
        mutable bool hasFingerprint = false;
        mutable std::map<std::tuple<std::size_t, std::size_t, unsigned>, std::uint32_t> indexMaxima;
        Storage(Vector b, bool s) : bytes(std::move(b)), sealed(s) {}
    };
    std::shared_ptr<Storage> storage;
    const Vector& Read() const {
        static const Vector empty;
        return storage ? storage->bytes : empty;
    }
    Vector& Write() {
        if (!storage)
            storage = std::make_shared<Storage>(Vector{}, false);
        else if (storage.use_count() != 1)
            storage = std::make_shared<Storage>(storage->bytes, false);
        storage->indexMaxima.clear();
        storage->hasFingerprint = false;
        storage->sealed = false;
        return storage->bytes;
    }

public:
    using value_type = Vector::value_type;
    using iterator = Vector::iterator;
    using const_iterator = Vector::const_iterator;
    SnapshotBytes() = default;
    SnapshotBytes(const SnapshotBytes& b) : storage(b.storage) {
        if (storage && !storage->sealed)
            storage = std::make_shared<Storage>(storage->bytes, false);
    }
    SnapshotBytes(SnapshotBytes&&) noexcept = default;
    SnapshotBytes(Vector b) : storage(std::make_shared<Storage>(std::move(b), false)) {}
    SnapshotBytes(std::initializer_list<value_type> b) : SnapshotBytes(Vector(b)) {}
    explicit SnapshotBytes(std::size_t n, value_type value = 0) : SnapshotBytes(Vector(n, value)) {}
    template <class Iter, std::enable_if_t<!std::is_integral_v<Iter>, int> = 0>
    SnapshotBytes(Iter first, Iter last) : SnapshotBytes(Vector(first, last)) {}
    SnapshotBytes& operator=(const SnapshotBytes& b) {
        if (this != &b) {
            SnapshotBytes copy(b);
            storage.swap(copy.storage);
        }
        return *this;
    }
    SnapshotBytes& operator=(SnapshotBytes&&) noexcept = default;
    SnapshotBytes& operator=(Vector b) {
        storage = std::make_shared<Storage>(std::move(b), false);
        return *this;
    }
    SnapshotBytes& operator=(std::initializer_list<value_type> b) { return *this = Vector(b); }
    void Seal() {
        if (storage && !storage->sealed)
            storage = std::make_shared<Storage>(storage->bytes, true);
    }
    bool Sealed() const { return storage && storage->sealed; }
    std::uint64_t Fingerprint() const {
        const auto scan = [&] {
            std::uint64_t hash = 1469598103934665603ull;
            for (auto byte : Read())
                hash = (hash ^ byte) * 1099511628211ull;
            return hash;
        };
        if (!Sealed())
            return scan();
        std::lock_guard<std::mutex> lock(storage->mutex);
        if (!storage->hasFingerprint) {
            storage->fingerprint = scan();
            storage->hasFingerprint = true;
        }
        return storage->fingerprint;
    }
    // Bounds are cached only on sealed bytes, never on an externally writable
    // buffer. The maximum remains reusable for a different vertex count.
    bool IndicesFit(std::size_t first, std::size_t count, unsigned step,
                    std::uint32_t vertices) const {
        if (!vertices || !count || (step != 2 && step != 4) || first > size() / step ||
            count > size() / step - first)
            return false;
        const auto scan = [&] {
            std::uint32_t maximum = 0;
            const auto* bytes = data();
            for (auto n = first; n < first + count; ++n) {
                std::uint32_t index = 0;
                std::memcpy(&index, bytes + n * step, step);
                maximum = maximum > index ? maximum : index;
            }
            return maximum;
        };
        if (!Sealed())
            return scan() < vertices;
        const auto key = std::make_tuple(first, count, step);
        std::lock_guard<std::mutex> lock(storage->mutex);
        auto found = storage->indexMaxima.find(key);
        if (found == storage->indexMaxima.end())
            found = storage->indexMaxima.emplace(key, scan()).first;
        return found->second < vertices;
    }
    std::size_t size() const { return Read().size(); }
    std::size_t capacity() const { return Read().capacity(); }
    bool empty() const { return Read().empty(); }
    const value_type* data() const { return Read().data(); }
    value_type* data() { return Write().data(); }
    const value_type& operator[](std::size_t n) const { return Read()[n]; }
    value_type& operator[](std::size_t n) { return Write()[n]; }
    const value_type& at(std::size_t n) const { return Read().at(n); }
    value_type& at(std::size_t n) { return Write().at(n); }
    const value_type& front() const { return Read().front(); }
    value_type& front() { return Write().front(); }
    const value_type& back() const { return Read().back(); }
    value_type& back() { return Write().back(); }
    const_iterator begin() const { return Read().begin(); }
    const_iterator end() const { return Read().end(); }
    const_iterator cbegin() const { return Read().cbegin(); }
    const_iterator cend() const { return Read().cend(); }
    iterator begin() { return Write().begin(); }
    iterator end() { return Write().end(); }
    void clear() { storage.reset(); }
    void resize(std::size_t n, value_type v = 0) { Write().resize(n, v); }
    void reserve(std::size_t n) { Write().reserve(n); }
    void push_back(value_type v) { Write().push_back(v); }
    void pop_back() { Write().pop_back(); }
    void assign(std::size_t n, value_type v) { Write().assign(n, v); }
    template <class Iter, std::enable_if_t<!std::is_integral_v<Iter>, int> = 0>
    void assign(Iter first, Iter last) {
        Write().assign(first, last);
    }
    template <class Iter> iterator insert(const_iterator pos, Iter first, Iter last) {
        const auto offset = pos - Read().begin();
        auto& bytes = Write();
        return bytes.insert(bytes.begin() + offset, first, last);
    }
    iterator insert(const_iterator pos, value_type value) {
        const auto offset = pos - Read().begin();
        auto& bytes = Write();
        return bytes.insert(bytes.begin() + offset, value);
    }
    iterator insert(const_iterator pos, std::initializer_list<value_type> values) {
        return insert(pos, values.begin(), values.end());
    }
    void swap(SnapshotBytes& b) noexcept { storage.swap(b.storage); }
    operator const Vector&() const { return Read(); }
    operator Vector&() { return Write(); }
    friend bool operator==(const SnapshotBytes& a, const SnapshotBytes& b) {
        return a.storage == b.storage || a.Read() == b.Read();
    }
    friend bool operator!=(const SnapshotBytes& a, const SnapshotBytes& b) { return !(a == b); }
    friend bool operator==(const SnapshotBytes& a, const Vector& b) { return a.Read() == b; }
    friend bool operator==(const Vector& a, const SnapshotBytes& b) { return a == b.Read(); }
    friend bool operator!=(const SnapshotBytes& a, const Vector& b) { return !(a == b); }
    friend bool operator!=(const Vector& a, const SnapshotBytes& b) { return !(a == b); }
};
}
