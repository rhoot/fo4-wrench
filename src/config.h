// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#pragma once

namespace config {

    struct Enumerator {
        virtual void OnBool (const char* const path[], size_t count, bool value) { }
        virtual void OnString (const char* const path[], size_t count, const char str[]) { }
    };

    enum class ValueType {
        Bool,
        String,
    };

    bool Load (const wchar_t filename[]);
    const char* Get (const char* const path[], size_t count);
    const char* Get (const std::initializer_list<const char*>& path);
    bool GetBool (const char* const path[], size_t count);
    bool GetBool (const std::initializer_list<const char*>& path);
    void Set (const char* const path[], size_t count, const char str[]);
    void Set (const std::initializer_list<const char*>& path, const char str[]);
    void Set (const char* const path[], size_t count, bool value);
    void Set (const std::initializer_list<const char*>& path, bool value);
    void Enumerate (Enumerator& enumerator);

} // namespace config
