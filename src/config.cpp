// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"
#include "config.h"

#include "util.h"

namespace config {

    ///
    // Statics
    ///

    static std::unordered_map<std::string, std::shared_ptr<cpptoml::base> > s_options;

    static const size_t MAX_PATH_LEN = 256;
    static const size_t MAX_SEGMENTS = 16;


    ///
    // Locals
    ///

    static bool IsSafeKeyChar (char ch)
    {
        return (ch >= 'A' && ch <= 'Z')
               || (ch >= 'a' && ch <= 'z')
               || (ch >= '0' && ch <= '9')
               || (ch == '_')
               || (ch == '-');
    }

    static bool IsSafeKeyString (const char str[])
    {
        if (*str == '\0') {
            return false;
        }

        for (auto curr = str; *curr; ++curr) {
            if (!IsSafeKeyChar(*curr)) {
                return false;
            }
        }

        return true;
    }

    template <size_t N>
    static size_t CombinePath (const char* const path[], size_t count, char (&out)[N])
    {
        auto curr = out;
        auto term = out + N;

        for (auto i = 0; i < count; ++i) {
            // We want to include the null terminator as segment separators
            curr += strlcpy(curr, path[i], term - curr) + 1;
            if (curr >= term) {
                break;
            }
        }

        if (curr >= term) {
            return 0;
        }

        return curr - out;
    }

    template <size_t N>
    static size_t ParsePath (const std::string& combined, const char* (&path)[N])
    {
        auto curr = combined.c_str();
        auto term = curr + combined.length();

        for (auto i = 0; i < N; i++) {
            if (curr >= term) {
                return i;
            }

            path[i] = curr;
            curr += strlen(curr) + 1;
        }

        return N;
    }

    static const cpptoml::base* GetValue (const char* const path[], size_t count)
    {
        char combined[0x100];
        auto pathLen = CombinePath(path, count, combined);

        if (pathLen && pathLen != -1) {
            std::string key(combined, pathLen);
            auto result = s_options.find(key);
            return result != s_options.end() ? result->second.get() : nullptr;
        }

        return nullptr;
    }

    static bool IdentifyType (const std::shared_ptr<cpptoml::base>& value, ValueType* type)
    {
        if (value->is_value()) {
            if (value->as<bool>()) {
                *type = ValueType::Bool;
                return true;
            }

            if (value->as<std::string>()) {
                *type = ValueType::String;
                return true;
            }
        }

        return false;
    }

    static bool GetRawValue (ValueType type, const std::shared_ptr<cpptoml::base>& value, const void** raw)
    {
        switch (type) {
            case ValueType::Bool:
                *raw = &(value->as<bool>()->get());
                return true;

            case ValueType::String:
                *raw = value->as<std::string>()->get().c_str();
                return true;
        }

        return false;
    }

    ///
    // Parser
    ///

    class Parser
    {
        const char* m_path[MAX_SEGMENTS];
        unsigned m_index = 0;

        template <class T>
        void VisitArray (const T& value);
        void StoreValue (std::shared_ptr<cpptoml::base>&& value);

        public:
            Parser ();
            Parser (const char* const path[], size_t count);

            void visit (const cpptoml::array& value);
            void visit (const cpptoml::table_array& str);
            void visit (const cpptoml::table& value);
            void visit (const cpptoml::value<std::string>& value);
            void visit (const cpptoml::value<int64_t>& value);
            void visit (const cpptoml::value<double>& value);
            void visit (const cpptoml::value<cpptoml::datetime>& value);
            void visit (const cpptoml::value<bool>& value);
    };

    Parser::Parser ()
        : m_index(0) { }

    Parser::Parser (const char* const path[], size_t count)
        : m_index(min(MAX_SEGMENTS, (unsigned)count))
    {
        for (size_t i = 0; i < count; ++i) {
            m_path[i] = path[i];
        }
    }

    template <class T>
    void Parser::VisitArray (const T& value)
    {
        if (m_index == MAX_SEGMENTS) {
            return;
        }

        uint32_t index = 0;

        for (auto& item : value) {
            char buffer[8];
            _ultoa_s(index, buffer, 10);
            m_path[m_index++] = buffer;
            item->accept(*this);
            --m_index;
        }
    }

    void Parser::StoreValue (std::shared_ptr<cpptoml::base>&& value)
    {
        if (m_index == MAX_SEGMENTS) {
            return;
        }

        char combined[0x100];
        auto pathLen = CombinePath(m_path, m_index, combined);

        if (pathLen && pathLen != -1) {
            std::string key(combined, pathLen);
            s_options[std::move(key)] = std::move(value);
        }
    }

    void Parser::visit (const cpptoml::array& value)
    {
        VisitArray(value);
    }

    void Parser::visit (const cpptoml::table_array& value)
    {
        VisitArray(value);
    }

    void Parser::visit (const cpptoml::table& value)
    {
        if (m_index == MAX_SEGMENTS) {
            return;
        }

        for (auto& kvp : value) {
            m_path[m_index++] = kvp.first.c_str();
            kvp.second->accept(*this);
            --m_index;
        }
    }

    void Parser::visit (const cpptoml::value<std::string>& value)
    {
        StoreValue(cpptoml::make_value(value.get()));
    }

    void Parser::visit (const cpptoml::value<int64_t>& value)
    {
        StoreValue(cpptoml::make_value(value.get()));
    }

    void Parser::visit (const cpptoml::value<double>& value)
    {
        StoreValue(cpptoml::make_value(value.get()));
    }

    void Parser::visit (const cpptoml::value<cpptoml::datetime>& value)
    {
        StoreValue(cpptoml::make_value(value.get()));
    }

    void Parser::visit (const cpptoml::value<bool>& value)
    {
        StoreValue(cpptoml::make_value(value.get()));
    }


    ///
    // Exports
    ///

    bool Load (const wchar_t filename[])
    {
        try {
            std::ifstream infile;

            infile.open(filename);
            if (!infile.is_open()) {
                return false;
            }

            cpptoml::parser tomlParser(infile);
            auto table = tomlParser.parse();
            table->accept(Parser());
        } catch (std::exception&) {
            // Honestly, I don't care why... effin exceptions...
            return false;
        }

        return true;
    }

    const char* Get (const char* const path[], size_t count)
    {
        auto value = GetValue(path, count);
        return (value && value->is_value())
               ? value->as<std::string>()->get().c_str()
               : nullptr;
    }

    const char* Get (const std::initializer_list<const char*>& path)
    {
        return Get(path.begin(), path.size());
    }

    bool GetBool (const char* const path[], size_t count)
    {
        auto value = GetValue(path, count);
        return (value && value->is_value())
               ? value->as<bool>()->get()
               : false;
    }

    bool GetBool (const std::initializer_list<const char*>& path)
    {
        return GetBool(path.begin(), path.size());
    }

    void Set (const char* const path[], size_t count, const char str[])
    {
        Parser parser(path, count);
        cpptoml::make_value(str)->accept(parser);
    }

    void Set (const std::initializer_list<const char*>& path, const char str[])
    {
        Set(path.begin(), path.size(), str);
    }

    void Set (const char* const path[], size_t count, bool value)
    {
        Parser parser(path, count);
        cpptoml::make_value(value)->accept(parser);
    }

    void Set (const std::initializer_list<const char*>& path, bool value)
    {
        Set(path.begin(), path.size(), value);
    }

    void Enumerate (Enumerator& enumerator)
    {
        for (auto& kvp : s_options) {
            const char* path[MAX_SEGMENTS];
            auto segments = ParsePath(kvp.first, path);
            ValueType type;

            if (IdentifyType(kvp.second, &type)) {
                switch (type) {
                    case ValueType::Bool:
                        enumerator.OnBool(path, segments, kvp.second->as<bool>().get()->get());
                        break;

                    case ValueType::String:
                        enumerator.OnString(path, segments, kvp.second->as<std::string>()->get().c_str());
                        break;
                }
            }
        }
    }

} // namespace config
