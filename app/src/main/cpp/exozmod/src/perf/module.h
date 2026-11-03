#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// A tunable value. Written by the config thread, read by the render thread.
struct Setting {
    std::string key;
    float min, max;
    std::atomic<float> value;

    Setting(std::string k, float def, float lo, float hi)
        : key(std::move(k)), min(lo), max(hi), value(def) {}
    float get() const { return value.load(std::memory_order_relaxed); }
    void set(float v) { value.store(v < min ? min : (v > max ? max : v), std::memory_order_relaxed); }
};

class Module {
public:
    explicit Module(std::string name) : name_(std::move(name)) {}
    virtual ~Module() = default;

    const std::string& name() const { return name_; }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void setEnabled(bool on);
    Setting* find(const std::string& key);

    // onEnable/onDisable run on the config-watcher thread: only touch atomics here,
    // never GL objects. Create/destroy GL resources lazily on the render thread.
    virtual void onEnable() {}
    virtual void onDisable() {}
    // Once per frame on the render thread, only while enabled.
    virtual void onFrame(float /*frameMs*/) {}

protected:
    Setting* declare(const char* key, float def, float lo, float hi);

private:
    std::string name_;
    std::atomic<bool> enabled_{false};
    std::vector<std::unique_ptr<Setting>> settings_;
};

class ModuleManager {
public:
    static ModuleManager& get();

    // Register every module BEFORE startConfigWatcher(); the list is not locked.
    template <class T>
    T* add() {
        auto m = std::make_unique<T>();
        T* raw = m.get();
        modules_.push_back(std::move(m));
        return raw;
    }
    Module* find(const std::string& name);
    void onFrame(float frameMs);

    // Config format, one entry per line:  ModuleName.enabled=1   ModuleName.key=12.5
    void loadConfig(const std::string& path);
    void startConfigWatcher(const std::string& path);   // polls mtime every 500 ms

private:
    std::vector<std::unique_ptr<Module>> modules_;
};
