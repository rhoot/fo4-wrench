#include "stdafx.h"
#include "util.h"

///
// Log
///

namespace Log {

    static HANDLE s_handle;

    // Main thread
    void Open (const wchar_t filename[]) {
        s_handle = CreateFileW(filename,
                               GENERIC_WRITE,
                               0,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    }

    // Main thread
    void Close () {
        if (s_handle)
            CloseHandle(s_handle);
    }

    // Random threads
    void Write (const char func[], const char str[], ...) {
        if (s_handle) {
            char fmt[0x100];
            snprintf(fmt, ArraySize(fmt), "%s: %s\r\n", func, str);

            char buffer[0x100];
            va_list args;
            va_start(args, str);
            auto len = (DWORD)vsnprintf(buffer, ArraySize(buffer), fmt, args);
            va_end(args);

            WriteFile(s_handle, buffer, len, &len, nullptr);
        }
    }

} // namespace Log
