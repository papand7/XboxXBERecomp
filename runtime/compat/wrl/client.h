#pragma once
// off Windows: the part of WRL's ComPtr the portable renderer code uses, over anything with AddRef and Release. This
// folder is on the include path only off Windows.
#include <cstddef>
#include <utility>

namespace Microsoft::WRL {

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(T* p) : p_(p) {
        if (p_) p_->AddRef();
    }
    ComPtr(const ComPtr& o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ~ComPtr() { Reset(); }
    ComPtr& operator=(ComPtr o) noexcept {
        std::swap(p_, o.p_);
        return *this;
    }
    ComPtr& operator=(std::nullptr_t) {
        Reset();
        return *this;
    }

    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** GetAddressOf() { return &p_; }
    T* const* GetAddressOf() const { return &p_; }
    T** ReleaseAndGetAddressOf() {
        Reset();
        return &p_;
    }
    T* Detach() { return std::exchange(p_, nullptr); }
    void Attach(T* p) {
        Reset();
        p_ = p;
    }
    unsigned long Reset() {
        unsigned long n = 0;
        if (T* p = std::exchange(p_, nullptr)) n = p->Release();
        return n;
    }

private:
    T* p_ = nullptr;
};

}  // namespace Microsoft::WRL
