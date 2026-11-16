// pool.hpp -- a growable array of trivially copyable records, addressed by index.
//
// All tree nodes of all versions live in one such array.  Growth must not
// transiently double the memory footprint (a std::vector would hold the old
// and the new buffer at once), so on Linux the block is grown with mremap,
// which moves page-table entries instead of bytes.  Elsewhere it falls back
// to realloc.
//
// Growing may move the block: never keep a T& / T* across push().
#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>

#if defined(__linux__)
#include <sys/mman.h>
#endif

namespace tte {

template <class T>
class Pool {
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() { release(); }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return cap_; }
    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
    T* data() { return data_; }
    const T* data() const { return data_; }

    std::size_t push(const T& value) {
        if (size_ == cap_) grow(size_ + 1);
        data_[size_] = value;
        return size_++;
    }
    // Drops every record with index >= n.
    void truncate(std::size_t n) { size_ = n; }
    void reserve(std::size_t n) {
        if (n > cap_) grow(n);
    }

  private:
#ifdef TTE_POOL_STRESS
    // Test configuration: grow in tiny steps (one record at a time while the
    // pool is small, 1/64 later) and always move the block, so that a
    // reference kept across push() is caught by AddressSanitizer.
    void grow(std::size_t need) {
        const std::size_t cap = need + need / 64;
        T* fresh = static_cast<T*>(std::malloc(cap * sizeof(T)));
        if (!fresh) throw std::bad_alloc();
        for (std::size_t i = 0; i < size_; ++i) fresh[i] = data_[i];
        std::free(data_);
        data_ = fresh;
        cap_ = cap;
    }
    void release() { std::free(data_); }
#elif defined(__linux__)
    void grow(std::size_t need) {
        std::size_t cap = cap_ + cap_ / 4;
        if (cap < need) cap = need;
        if (cap < kMinCapacity) cap = kMinCapacity;
        void* p = data_ ? mremap(data_, cap_ * sizeof(T), cap * sizeof(T), MREMAP_MAYMOVE)
                        : mmap(nullptr, cap * sizeof(T), PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) throw std::bad_alloc();
        data_ = static_cast<T*>(p);
        cap_ = cap;
    }
    void release() {
        if (data_) munmap(data_, cap_ * sizeof(T));
    }
#else
    void grow(std::size_t need) {
        std::size_t cap = cap_ + cap_ / 4;
        if (cap < need) cap = need;
        if (cap < kMinCapacity) cap = kMinCapacity;
        void* p = std::realloc(data_, cap * sizeof(T));
        if (!p) throw std::bad_alloc();
        data_ = static_cast<T*>(p);
        cap_ = cap;
    }
    void release() { std::free(data_); }
#endif

    static constexpr std::size_t kMinCapacity = std::size_t{1} << 16;

    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cap_ = 0;
};

}  // namespace tte
