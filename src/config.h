#pragma once

namespace Config {
    
    using Enumerate_t = void(const char* path[], size_t count, const char value[]);

    bool Load (const wchar_t filename[]);

    const char* Get (const char* const path[], size_t count);
    void Set (const char* const path[], size_t count, const char str[]);
    void Enumerate (Enumerate_t enumerator);

    inline const char* Get (const std::initializer_list<const char*>& path) {
        return Get(path.begin(), path.size());
    }

    inline void Set (const std::initializer_list<const char*>& path, const char str[]) {
        Set(path.begin(), path.size(), str);
    }

} // namespace