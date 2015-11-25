// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#pragma once

struct DetourInfo {
    union {
        uint8_t* buffer;
        void* trampoline;
    };

    DetourInfo ();
    DetourInfo (const DetourInfo&) = delete;
    DetourInfo (DetourInfo&& source);
    ~DetourInfo ();

    DetourInfo& operator= (const DetourInfo&) = delete;
    DetourInfo& operator= (DetourInfo&& source);
    void Reset ();
};

template <class F>
DetourInfo Detour (F* src, F* dst) {
    DetourInfo DetourImpl (void*, void*);

    return DetourImpl(src, dst);
}
