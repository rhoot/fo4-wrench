// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#pragma once

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

        operator bool () const;

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
UnsafePtr<T>::operator bool () const {
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
// Misc
///

template <class T, size_t N>
constexpr size_t ArraySize (const T(&)[N])
{
    return N;
}

void LogAsm (void* addr, size_t size);
size_t strlcpy (char* dst, const char* src, size_t dsize);
