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
// Misc
///

template <class T, size_t N>
constexpr size_t ArraySize (const T(&)[N])
{
    return N;
}

void LogAsm (void* addr, size_t size);
size_t strlcpy (char* dst, const char* src, size_t dsize);
