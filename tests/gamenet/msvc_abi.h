#pragma once
// The game's own library types, laid out by hand as the MSVC standard library lays them out (x64, /MD, no iterator
// debugging), so this harness can hand them to EDF.dll whatever standard library it is built with. A MinGW build
// uses libstdc++, whose std::string, std::wstring and std::function do not match.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <string>

namespace gamenet::msvc {

// std::basic_string: 16 bytes holding the characters while they fit (15 chars, 7 wchar_ts) or a pointer to them,
// then the size and the capacity.
template <typename Char>
struct String {
    static constexpr std::size_t kInline = 16 / sizeof(Char) - 1;
    union {
        Char inlineChars[kInline + 1];
        Char* heapChars;
    };
    std::size_t size = 0;
    std::size_t capacity = kInline;

    String() { std::memset(inlineChars, 0, sizeof(inlineChars)); }
    // `allocate` is the game's operator new: the game may free or grow what it is given.
    template <typename Allocate>
    void Assign(const std::basic_string<Char>& text, Allocate allocate) {
        size = text.size();
        if (size <= kInline) {
            std::memset(inlineChars, 0, sizeof(inlineChars));
            std::memcpy(inlineChars, text.data(), size * sizeof(Char));
            capacity = kInline;
            return;
        }
        heapChars = static_cast<Char*>(allocate((size + 1) * sizeof(Char)));
        std::memcpy(heapChars, text.c_str(), (size + 1) * sizeof(Char));
        capacity = size;
    }
};
static_assert(sizeof(String<char>) == 0x20 && sizeof(String<wchar_t>) == 0x20, "std::string's layout");

// std::vector: first, last, end of storage. Only ever read by the game here.
template <typename T>
struct VectorView {
    const T* first;
    const T* last;
    const T* end;
};

// std::function<R(Args...)>: 56 bytes of storage, then the pointer to the callable (_Func_base*), which points into
// the storage when it fits there. A _Func_base's virtuals, in declaration order: _Copy, _Move, _Do_call,
// _Target_type, _Delete_this, _Get. _Do_call takes every argument as a reference, so each is a pointer here.
template <typename R, typename... Args>
struct Function {
    using Body = std::function<R(Args*...)>;

    void* storage[7]{};
    void* impl = nullptr;

    explicit Function(Body body) { impl = new (storage) Impl{Vtable(), new Body(std::move(body))}; }

private:
    struct Impl {
        void* const* vtable;
        Body* body;
    };
    static Impl* Copy(Impl* self, void* where) { return new (where) Impl{self->vtable, new Body(*self->body)}; }
    static Impl* Move(Impl* self, void* where) {
        Impl* moved = new (where) Impl{self->vtable, self->body};
        self->body = nullptr;
        return moved;
    }
    static R Call(Impl* self, Args*... args) { return (*self->body)(args...); }
    static const void* TargetType(Impl*) {
        static const void* none[4]{};
        return none;
    }
    static void DeleteThis(Impl* self, bool) { delete self->body; }  // always in the storage: nothing to free
    static const void* Get(Impl* self) { return self->body; }
    static void* const* Vtable() {
        static void* const table[] = {reinterpret_cast<void*>(&Copy),       reinterpret_cast<void*>(&Move),
                                      reinterpret_cast<void*>(&Call),       reinterpret_cast<void*>(&TargetType),
                                      reinterpret_cast<void*>(&DeleteThis), reinterpret_cast<void*>(&Get)};
        return table;
    }
};
static_assert(sizeof(Function<void>) == 0x40, "std::function's layout");

}  // namespace gamenet::msvc
