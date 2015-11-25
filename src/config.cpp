#include "stdafx.h"
#include "config.h"

namespace Config {

    ///
    // Statics
    ///

    static std::unordered_map<std::string, std::string> s_options;

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

    ///
    // Parser
    ///

    class Parser
    {
        const char* m_path[MAX_SEGMENTS];
        unsigned m_index = 0;

        template <class T>
        void VisitArray (const T& value);
        void StoreValue (const char value[]);

        public:
            void visit (const cpptoml::array& value);
            void visit (const cpptoml::table_array& str);
            void visit (const cpptoml::table& value);
            void visit (const cpptoml::value<std::string>& value);
            void visit (const cpptoml::value<int64_t>& value);
            void visit (const cpptoml::value<double>& value);
            void visit (const cpptoml::value<cpptoml::datetime>& value);
            void visit (const cpptoml::value<bool>& value);
    };

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

    void Parser::StoreValue (const char value[])
    {
        if (m_index == MAX_SEGMENTS) {
            return;
        }

        Set(m_path, m_index, value);
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
        StoreValue(value.get().c_str());
    }

    void Parser::visit (const cpptoml::value<int64_t>& value)
    {
        char buffer[0x40];
        snprintf(buffer, sizeof(buffer), "%lld", value.get());
        StoreValue(buffer);
    }

    void Parser::visit (const cpptoml::value<double>& value)
    {
        char buffer[0x40];
        snprintf(buffer, sizeof(buffer), "%g", value.get());
        StoreValue(buffer);
    }

    void Parser::visit (const cpptoml::value<cpptoml::datetime>& value)
    {
        std::stringstream sstr;
        sstr << value.get();
        StoreValue(sstr.str().c_str());
    }

    void Parser::visit (const cpptoml::value<bool>& value)
    {
        StoreValue(value.get() ? "true" : "false");
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
        char combined[0x100];
        auto pathLen = CombinePath(path, count, combined);

        if (pathLen && pathLen != -1) {
            std::string key(combined, pathLen);
            auto result = s_options.find(key);
            return result != s_options.end() ? result->second.c_str() : nullptr;
        }

        return nullptr;
    }

    const char* Get (const std::initializer_list<const char*>& path)
    {
        return Get(path.begin(), path.size());
    }

    void Set (const char* const path[], size_t count, const char str[])
    {
        char combined[0x100];
        auto pathLen = CombinePath(path, count, combined);

        if (pathLen && pathLen != -1) {
            std::string key(combined, pathLen);
            s_options[std::move(key)] = str;
        }
    }

    void Set (const std::initializer_list<const char*>& path, const char str[])
    {
        Set(path.begin(), path.size(), str);
    }

    void Enumerate (Enumerate_t enumerator)
    {
        for (auto& kvp : s_options) {
            const char* path[MAX_SEGMENTS];
            auto segments = ParsePath(kvp.first, path);
            enumerator(path, segments, kvp.second.c_str());
        }
    }

} // namespace Config
