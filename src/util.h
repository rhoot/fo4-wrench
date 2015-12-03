// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#pragma once

#include <cstdint>

#define REF(...) (void)(__VA_ARGS__)

///
// Logging
///

#define LOG(fmt, ...)   logging::Write(__FUNCTION__, fmt, ## __VA_ARGS__)
#define ERR(fmt, ...)   logging::Write(__FUNCTION__, "ERR: " fmt, ## __VA_ARGS__)

namespace logging {

    void Open (const wchar_t filename[]);
    void Close ();
    void Write (const char func[], const char str[], ...);

} // namespace log


///
// Unsafe ptr
///

// Allows for assignment/comparison, but not dereferencing.
template <class T>
class UnsafePtr
{
    const T* m_ptr;

    public:
        UnsafePtr ();
        UnsafePtr (const T* ptr);
        UnsafePtr (const UnsafePtr& other);

        UnsafePtr& operator= (const T* ptr);
        UnsafePtr& operator= (const UnsafePtr& other);

        operator bool() const;

        friend bool operator== (const UnsafePtr& a, const UnsafePtr& b);
        friend bool operator!= (const UnsafePtr& a, const UnsafePtr& b);

        template <class T, class U>
        friend bool operator== (const UnsafePtr<T>& a, const U* b);
        template <class T, class U>
        friend bool operator!= (const UnsafePtr<T>& a, const U* b);
};

template <class T>
UnsafePtr<T>::UnsafePtr ()
    : m_ptr(nullptr) { }

template <class T>
UnsafePtr<T>::UnsafePtr (const T* ptr)
    : m_ptr(ptr) { }

template <class T>
UnsafePtr<T>::UnsafePtr (const UnsafePtr<T>& other)
    : m_ptr(other.m_ptr) { }

template <class T>
UnsafePtr<T>& UnsafePtr<T>::operator= (const T* ptr)
{
    m_ptr = ptr;
    return *this;
}

template <class T>
UnsafePtr<T>& UnsafePtr<T>::operator= (const UnsafePtr& other)
{
    m_ptr = other.m_ptr;
    return *this;
}

template <class T>
UnsafePtr<T>::operator bool () const
{
    return m_ptr != nullptr;
}

template <class T>
bool operator== (const UnsafePtr<T>& a, const UnsafePtr<T>& b)
{
    return a.m_ptr == b.m_ptr;
}

template <class T>
bool operator!= (const UnsafePtr<T>& a, const UnsafePtr<T>& b)
{
    return a.m_ptr != b.m_ptr;
}

template <class T, class U>
bool operator== (const UnsafePtr<T>& a, const U* b)
{
    return a.m_ptr == static_cast<const T*>(b);
}

template <class T, class U>
bool operator!= (const UnsafePtr<T>& a, const U* b)
{
    return a.m_ptr != static_cast<const T*>(b);
}


///
// Scoped timer
///

class ScopedTimer
{
    uint64_t m_start;
    const char* m_func;
    const char* m_fmt;

    public:
        ScopedTimer (const char func[], const char fmt[]);
        ~ScopedTimer ();
};


///
// Misc
///

template <class T, size_t N>
constexpr size_t ArraySize (const T(&)[N])
{
    return N;
}

void LogAsm (void* addr, size_t size);
void LogCallstack (const char func[], size_t count = 16);
size_t strlcpy (char* dst, const char* src, size_t dsize);


///
//  float16
///

// float32
// Martin Kallman
//
// Fast half-precision to single-precision floating point conversion
//  - Supports signed zero and denormals-as-zero (DAZ)
//  - Does not support infinities or NaN
//  - Few, partially pipelinable, non-branching instructions,
//  - Core opreations ~6 clock cycles on modern x86-64
inline void float32 (float* __restrict out, const uint16_t in)
{
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;

    t1 = in & 0x7fff;                       // Non-sign bits
    t2 = in & 0x8000;                       // Sign bit
    t3 = in & 0x7c00;                       // Exponent

    t1 <<= 13;                              // Align mantissa on MSB
    t2 <<= 16;                              // Shift sign bit into position

    t1 += 0x38000000;                       // Adjust bias

    t1 = (t3 == 0 ? 0 : t1);                // Denormals-as-zero

    t1 |= t2;                               // Re-insert sign bit

    *((uint32_t*)out) = t1;
};

// float16
// Martin Kallman
//
// Fast single-precision to half-precision floating point conversion
//  - Supports signed zero, denormals-as-zero (DAZ), flush-to-zero (FTZ),
//    clamp-to-max
//  - Does not support infinities or NaN
//  - Few, partially pipelinable, non-branching instructions,
//  - Core opreations ~10 clock cycles on modern x86-64
inline void float16 (uint16_t* __restrict out, const float in)
{
    uint32_t inu = *((uint32_t*)&in);
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;

    t1 = inu & 0x7fffffff;                 // Non-sign bits
    t2 = inu & 0x80000000;                 // Sign bit
    t3 = inu & 0x7f800000;                 // Exponent

    t1 >>= 13;                             // Align mantissa on MSB
    t2 >>= 16;                             // Shift sign bit into position

    t1 -= 0x1c000;                         // Adjust bias

    t1 = (t3 < 0x38800000) ? 0 : t1;       // Flush-to-zero
    t1 = (t3 > 0x47000000) ? 0x7bff : t1;  // Clamp-to-max
    t1 = (t3 == 0 ? 0 : t1);               // Denormals-as-zero

    t1 |= t2;                              // Re-insert sign bit

    *(uint16_t*)out = (uint16_t)t1;
};
