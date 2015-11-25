#pragma once

#pragma warning(push)
#pragma warning(disable:4091) // 'typedef ': ignored on left of '' when no variable is declared

// Windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <shlobj.h>

// Standard headers
#include <cassert>
#include <streambuf>
#include <varargs.h>
#include <vector>

// WinSDK headers
#include <d3d11.h>

// 3rdparty headers
#include <XInput.h>
#include <cpptoml.h>
#include <udis86.h>

// Local headers
#include "targetver.h"

#pragma warning(pop)


/*
 * Copy string src to buffer dst of size dsize.  At most dsize-1
 * chars will be copied.  Always NUL terminates (unless dsize == 0).
 * Returns strlen(src); if retval >= dsize, truncation occurred.
 */
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
