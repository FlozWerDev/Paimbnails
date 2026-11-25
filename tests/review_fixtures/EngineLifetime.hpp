#pragma once
#include <memory>

struct TestLifetime {
    std::shared_ptr<int> life = std::make_shared<int>(0);
};

template<class T> struct Ref {
    T* pointer = nullptr;
    T* data() const { return pointer; }
    T* operator->() const { return pointer; }
    explicit operator bool() const { return pointer != nullptr; }
};

template<class T> struct WeakRef {
    T* pointer = nullptr;
    std::weak_ptr<int> life;
    WeakRef() = default;
    WeakRef(T* value) { *this = value; }
    WeakRef& operator=(T* value) {
        pointer = value;
        life = value ? value->life : std::weak_ptr<int>{};
        return *this;
    }
    Ref<T> lock() const { return {life.expired() ? nullptr : pointer}; }
};
