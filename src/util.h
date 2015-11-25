// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#pragma once

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


///
// Misc
///

template <class T, size_t N>
constexpr size_t ArraySize (const T(&)[N])
{
    return N;
}

inline size_t strlcpy (char* dst, const char* src, size_t dsize)
{
    auto osrc = src;
    auto nleft = dsize;

    /* Copy as many bytes as will fit. */
    if (nleft != 0) {
        while (--nleft != 0) {
            if ((*dst++ = *src++) == '\0') {
                break;
            }
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src. */
    if (nleft == 0) {
        if (dsize != 0) {
            *dst = '\0';        /* NUL-terminate dst */
        }
        while (*src++) {
            ;
        }
    }

    return src - osrc - 1; /* count does not include NUL */
}
