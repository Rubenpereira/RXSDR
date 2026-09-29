#pragma once
// masdr_json.h — Parser e serializer JSON minimal, sem dependências externas.
// Substitui QJsonObject / QJsonArray / QJsonDocument no build Win7.

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cstdint>

namespace masdr {

class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Object, Array };

    Json()                        : type_(Type::Null) {}
    Json(bool v)                  : type_(Type::Bool),   bval_(v) {}
    Json(int v)                   : type_(Type::Int),    ival_(v) {}
    Json(long long v)             : type_(Type::Int),    ival_(v) {}
    Json(uint64_t v)              : type_(Type::Int),    ival_((long long)v) {}
    Json(double v)                : type_(Type::Double), dval_(v) {}
    Json(const char* v)           : type_(Type::String), sval_(v ? v : "") {}
    Json(const std::string& v)    : type_(Type::String), sval_(v) {}

    static Json object() {
        Json j; j.type_ = Type::Object;
        j.obj_ = std::make_shared<std::map<std::string,Json>>();
        return j;
    }
    static Json array() {
        Json j; j.type_ = Type::Array;
        j.arr_ = std::make_shared<std::vector<Json>>();
        return j;
    }

    Type type() const { return type_; }
    bool isNull()   const { return type_ == Type::Null; }
    bool isBool()   const { return type_ == Type::Bool; }
    bool isInt()    const { return type_ == Type::Int; }
    bool isDouble() const { return type_ == Type::Double; }
    bool isString() const { return type_ == Type::String; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray()  const { return type_ == Type::Array; }

    bool        getBool  (bool   def=false) const { return isBool()   ? bval_ : def; }
    long long   getInt   (long long def=0)  const {
        if (isInt())    return ival_;
        if (isDouble()) return (long long)dval_;
        return def;
    }
    uint64_t    getUint64(uint64_t def=0)   const { return (uint64_t)getInt((long long)def); }
    double      getDouble(double def=0.0)   const {
        if (isDouble()) return dval_;
        if (isInt())    return (double)ival_;
        return def;
    }
    const std::string& getString(const std::string& def="") const {
        return isString() ? sval_ : def;
    }

    bool contains(const std::string& key) const {
        return isObject() && obj_ && obj_->count(key);
    }
    const Json& operator[](const std::string& key) const {
        static Json null;
        if (!isObject() || !obj_) return null;
        auto it = obj_->find(key);
        return it != obj_->end() ? it->second : null;
    }
    Json& operator[](const std::string& key) {
        if (!isObject() || !obj_) {
            type_ = Type::Object;
            obj_ = std::make_shared<std::map<std::string,Json>>();
        }
        return (*obj_)[key];
    }
    void set(const std::string& key, const Json& val) {
        (*this)[key] = val;
    }

    // Array access
    size_t size() const {
        if (isArray() && arr_) return arr_->size();
        if (isObject() && obj_) return obj_->size();
        return 0;
    }
    const Json& operator[](size_t i) const {
        static Json null;
        if (!isArray() || !arr_ || i >= arr_->size()) return null;
        return (*arr_)[i];
    }
    void push(const Json& v) {
        if (!isArray() || !arr_) {
            type_ = Type::Array;
            arr_ = std::make_shared<std::vector<Json>>();
        }
        arr_->push_back(v);
    }

    // ─── serialize ───────────────────────────────────────────────────────────
    std::string serialize() const {
        std::ostringstream ss;
        serializeTo(ss);
        return ss.str();
    }

    // ─── parse ───────────────────────────────────────────────────────────────
    static Json parse(const std::string& s) {
        size_t pos = 0;
        return parseValue(s, pos);
    }

private:
    Type        type_ = Type::Null;
    bool        bval_ = false;
    long long   ival_ = 0;
    double      dval_ = 0.0;
    std::string sval_;
    std::shared_ptr<std::map<std::string,Json>>  obj_;
    std::shared_ptr<std::vector<Json>>           arr_;

    void serializeTo(std::ostringstream& ss) const {
        switch (type_) {
            case Type::Null:   ss << "null"; break;
            case Type::Bool:   ss << (bval_ ? "true" : "false"); break;
            case Type::Int:    ss << ival_; break;
            case Type::Double: {
                char buf[64]; snprintf(buf, sizeof(buf), "%.10g", dval_);
                ss << buf; break;
            }
            case Type::String: ss << '"' << escapeStr(sval_) << '"'; break;
            case Type::Object: {
                ss << '{';
                if (obj_) {
                    bool first = true;
                    for (const auto& kv : *obj_) {
                        if (!first) ss << ',';
                        ss << '"' << escapeStr(kv.first) << "\":";
                        kv.second.serializeTo(ss);
                        first = false;
                    }
                }
                ss << '}'; break;
            }
            case Type::Array: {
                ss << '[';
                if (arr_) {
                    bool first = true;
                    for (const auto& v : *arr_) {
                        if (!first) ss << ',';
                        v.serializeTo(ss);
                        first = false;
                    }
                }
                ss << ']'; break;
            }
        }
    }

    static std::string escapeStr(const std::string& s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else                out += c;
        }
        return out;
    }

    static void skipWs(const std::string& s, size_t& pos) {
        while (pos < s.size() && (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r'))
            ++pos;
    }

    static Json parseValue(const std::string& s, size_t& pos) {
        skipWs(s, pos);
        if (pos >= s.size()) return Json();
        char c = s[pos];
        if (c == '{') return parseObject(s, pos);
        if (c == '[') return parseArray(s, pos);
        if (c == '"') return parseString(s, pos);
        if (c == 't') { pos += 4; return Json(true); }
        if (c == 'f') { pos += 5; return Json(false); }
        if (c == 'n') { pos += 4; return Json(); }
        return parseNumber(s, pos);
    }

    static Json parseObject(const std::string& s, size_t& pos) {
        Json obj = Json::object();
        ++pos; // '{'
        skipWs(s, pos);
        if (pos < s.size() && s[pos] == '}') { ++pos; return obj; }
        while (pos < s.size()) {
            skipWs(s, pos);
            std::string key = parseString(s, pos).getString();
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ':') ++pos;
            Json val = parseValue(s, pos);
            obj.set(key, val);
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == '}') { ++pos; break; }
            break;
        }
        return obj;
    }

    static Json parseArray(const std::string& s, size_t& pos) {
        Json arr = Json::array();
        ++pos; // '['
        skipWs(s, pos);
        if (pos < s.size() && s[pos] == ']') { ++pos; return arr; }
        while (pos < s.size()) {
            arr.push(parseValue(s, pos));
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == ']') { ++pos; break; }
            break;
        }
        return arr;
    }

    static Json parseString(const std::string& s, size_t& pos) {
        if (pos < s.size() && s[pos] == '"') ++pos;
        std::string out;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\' && pos+1 < s.size()) {
                char e = s[pos+1]; pos += 2;
                if      (e == 'n')  out += '\n';
                else if (e == 'r')  out += '\r';
                else if (e == 't')  out += '\t';
                else if (e == '"')  out += '"';
                else if (e == '\\') out += '\\';
                else                out += e;
            } else {
                out += s[pos++];
            }
        }
        if (pos < s.size()) ++pos; // closing '"'
        return Json(out);
    }

    static Json parseNumber(const std::string& s, size_t& pos) {
        size_t start = pos;
        bool isFloat = false;
        if (pos < s.size() && s[pos] == '-') ++pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        if (pos < s.size() && s[pos] == '.') { isFloat = true; ++pos; }
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        if (pos < s.size() && (s[pos]=='e'||s[pos]=='E')) {
            isFloat = true; ++pos;
            if (pos < s.size() && (s[pos]=='+'||s[pos]=='-')) ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        std::string num = s.substr(start, pos - start);
        if (num.empty()) return Json();
        if (isFloat) return Json(std::stod(num));
        return Json((long long)std::stoll(num));
    }
};

} // namespace masdr
