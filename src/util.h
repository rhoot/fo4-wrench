#pragma once

template <class T, size_t N>
constexpr size_t ArraySize (const T (&)[N])
{
    return N;
}


///
// Log
///

#define LOG(fmt, ...)   Log::Write(__FUNCTION__, fmt, ## __VA_ARGS__)
#define ERR(fmt, ...)   Log::Write(__FUNCTION__, "ERR: " fmt, ## __VA_ARGS__)

namespace Log {

    void Open (const wchar_t filename[]);
    void Close ();
    void Write (const char func[], const char str[], ...);

} // namespace Log
