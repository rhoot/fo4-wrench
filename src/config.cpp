#include "stdafx.h"
#include "config.h"

namespace Config {

    ///
    // Statics
    ///

    static std::unordered_map<std::string, std::string> s_options;


    ///
    // Locals
    ///

    static void Merge (const std::shared_ptr<cpptoml::table>& target, const std::shared_ptr<cpptoml::table>& source) {
        for (auto& kvp : *source) {
            if (!target->contains(kvp.first)) {
                target->insert(kvp.first, kvp.second);
                continue;
            }

            if (kvp.second->is_table()) {
                auto existing = target->get(kvp.first);
                if (existing->is_table()) {
                    Merge(existing->as_table(), kvp.second->as_table());
                    continue;
                }
            }

            target->insert(kvp.first, kvp.second);
        }
    }

    class Parser {
        char m_path[0x100] = { 0 };
        char* m_curr = this->m_path;
        char* m_term = this->m_path + sizeof(this->m_path);

        template <class T>
        void visit_array (const T& value);

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
    void Parser::visit_array (const T& value) {
        uint32_t index = 0;

        for (auto& item : value) {
            auto len = snprintf(m_curr, m_term - m_curr, ".%u", index++);
            m_curr += len;
            item->accept(*this);
            m_curr -= len;
        }

        *m_curr = 0;
    }

    void Parser::visit (const cpptoml::array& value) {
        visit_array(value);
    }

    void Parser::visit (const cpptoml::table_array& value) {
        visit_array(value);
    }

    void Parser::visit (const cpptoml::table& value) {
        for (auto& kvp : value) {
            auto len = snprintf(m_curr, m_term - m_curr, ".%s", kvp.first.c_str());
            m_curr += len;
            kvp.second->accept(*this);
            m_curr -= len;
        }

        *m_curr = 0;
    }

    void Parser::visit (const cpptoml::value<std::string>& value) {
        s_options[m_path + 1] = value.get();
    }

    void Parser::visit (const cpptoml::value<int64_t>& value) {
        char buffer[0x40];
        snprintf(buffer, sizeof(buffer), "%lld", value.get());
        s_options[m_path + 1] = buffer;
    }

    void Parser::visit (const cpptoml::value<double>& value) {
        char buffer[0x40];
        snprintf(buffer, sizeof(buffer), "%g", value.get());
        s_options[m_path + 1] = buffer;
    }

    void Parser::visit (const cpptoml::value<cpptoml::datetime>& value) {
        std::stringstream sstr;
        sstr << value.get();
        s_options[m_path + 1] = sstr.str();
    }

    void Parser::visit (const cpptoml::value<bool>& value) {
        s_options[m_path + 1] = value.get() ? "true" : "false";
    }


    ///
    // Exports
    ///

    bool Load (const wchar_t filename[]) {
        try {
            std::ifstream infile;

            infile.open(filename);
            if (!infile.is_open())
                return false;

            cpptoml::parser tomlParser(infile);
            auto table = tomlParser.parse();
            table->accept(Parser());
        } catch (std::exception&) {
            // Honestly, I don't care why... effin exceptions...
            return false;
        }

        return true;
    }

    const char* Get (const char name[], const char def[]) {
        auto result = s_options.find(name);
        // TODO: This return could be bad if we modify the map *after* getting a string... but I'll
        // worry about that when that is actually a thing.
        return result != s_options.end() ? result->second.c_str() : def;
    }

    void Set (const char name[], const char str[]) {
        s_options[name] = str;
    }

    void Enumerate (Enumerate_t enumerator) {
        for (auto& kvp : s_options) {
            enumerator(kvp.first.c_str(), kvp.second.c_str());
        }
    }


} // namespace Config
