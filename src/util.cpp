// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"
#include "util.h"

///
// Logging
///

namespace logging {

    static HANDLE s_handle;

    // Main thread
    void Open (const wchar_t filename[])
    {
        s_handle = CreateFileW(filename,
                               GENERIC_WRITE,
                               FILE_SHARE_READ,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    }

    // Main thread
    void Close ()
    {
        if (s_handle) {
            CloseHandle(s_handle);
        }
    }

    // Random threads
    void Write (const char func[], const char str[], ...)
    {
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

} // namespace logging


///
// Scoped timer
///

ScopedTimer::ScopedTimer (const char func[], const char fmt[])
    : m_func(func)
    , m_fmt(fmt)
{
    static_assert(sizeof(m_start) == sizeof(LARGE_INTEGER), "invalid cast");
    QueryPerformanceCounter((LARGE_INTEGER*)&m_start);
}

ScopedTimer::~ScopedTimer ()
{
    uint64_t end, freq;
    QueryPerformanceCounter((LARGE_INTEGER*)&end);
    QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
    auto microsec = (end - m_start) / (freq / 1000000);
    logging::Write(m_func, m_fmt, microsec / 1000, microsec % 1000);
}

///
// Misc
///

void LogAsm (void* addr, size_t size)
{
    ud_t ud;
    ud_init(&ud);
    ud_set_input_buffer(&ud, (const uint8_t*)addr, size);
    ud_set_mode(&ud, 64);
    ud_set_syntax(&ud, UD_SYN_INTEL);

    LOG("Assembly at %p (%zu)", addr, size);

    uint32_t count = 0;
    while (ud_insn_off(&ud) < size) {
        if (!ud_disassemble(&ud)) {
            break;
        }
        LOG("    %s", ud_insn_asm(&ud));
        ++count;
    }

    LOG("%u instructions, %llu bytes", count, ud_insn_off(&ud));
}

void LogCallstack (const char func[], size_t count)
{
    void* stack[64];
    auto max = min(count, ArraySize(stack));
    auto size = CaptureStackBackTrace(1, (DWORD)max, stack, nullptr);

    logging::Write(func, "Callstack:");
    for (size_t i = 0; i < size; ++i) {
        logging::Write(func, "    %#p", stack[i]);
    }
}

size_t strlcpy (char* dst, const char* src, size_t dsize)
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
        while (*src++) {}
    }

    return src - osrc - 1; /* count does not include NUL */
}
