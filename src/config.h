#pragma once

namespace Config {

    using Enumerate_t = void  (const char* path[], size_t count, const char value[]);

    bool Load (const wchar_t filename[]);
    const char* Get (const char* const path[], size_t count);
    const char* Get (const std::initializer_list<const char*>& path);
    void Set (const char* const path[], size_t count, const char str[]);
    void Set (const std::initializer_list<const char*>& path, const char str[]);
    void Enumerate (Enumerate_t enumerator);

} // namespace

