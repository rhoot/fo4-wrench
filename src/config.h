#pragma once

namespace Config {
    
    using Enumerate_t = void(const char name[], const char value[]);

    bool Load (const wchar_t filename[]);

    const char* Get (const char name[], const char def[]);
    void Set (const char name[], const char str[]);
    void Enumerate (Enumerate_t enumerator);

} // namespace