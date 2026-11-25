#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
struct Error { std::string text; };
template<class T=void> struct Result {
    std::optional<T> value; std::string error;
    Result(T x): value(std::move(x)) {} Result(Error e): error(std::move(e.text)) {}
    explicit operator bool() const { return value.has_value(); }
    bool isOk() const { return value.has_value(); }
    T unwrap() const { return *value; }
    bool isErr()const{return !static_cast<bool>(*this);}
    std::string unwrapErr() const { return error; }
};
template<> struct Result<void> {
    bool good = true; std::string error;
    Result() = default; Result(Error e): good(false), error(std::move(e.text)) {}
    explicit operator bool() const { return good; }
    bool isErr()const{return !static_cast<bool>(*this);}
    std::string unwrapErr() const { return error; }
};
template<class T> Result<T> Ok(T x) { return Result<T>(std::move(x)); }
inline Result<void> Ok() { return {}; }
template<class... Args> Error Err(std::string s, Args const&...) { return {s}; }
namespace geode {
    template<class T=void> using Result = ::Result<T>;
    namespace log { template<class... A> void warn(A const&...) {} }
    namespace utils::string { inline std::string pathToString(std::filesystem::path const& p) { return p.string(); } }
}
inline std::vector<std::function<void()>> workers;
inline std::vector<std::function<void()>> mainJobs;
struct Loader {
    static Loader* get() { static Loader l; return &l; }
    void queueInMainThread(std::function<void()> f) { mainJobs.push_back(std::move(f)); }
};
namespace paimon {
    inline bool isRuntimeShuttingDown() { return false; }
    struct ThreadTracker {
        static ThreadTracker& get() { static ThreadTracker t; return t; }
        bool spawn(std::function<void()> f) { workers.push_back(std::move(f)); return true; }
    };
}
void runJob(std::vector<std::function<void()>>& jobs, size_t i=0) {
    auto f = std::move(jobs.at(i)); jobs.erase(jobs.begin()+i); f();
}
