#include "perf/module.h"
#include "perf/log.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <thread>

void Module::setEnabled(bool on) {
    if (enabled_.exchange(on) == on) return;
    if (on) onEnable(); else onDisable();
}

Setting* Module::declare(const char* key, float def, float lo, float hi) {
    settings_.push_back(std::make_unique<Setting>(key, def, lo, hi));
    return settings_.back().get();
}

Setting* Module::find(const std::string& key) {
    for (auto& s : settings_) if (s->key == key) return s.get();
    return nullptr;
}

ModuleManager& ModuleManager::get() {
    static ModuleManager m;
    return m;
}

Module* ModuleManager::find(const std::string& name) {
    for (auto& m : modules_) if (m->name() == name) return m.get();
    return nullptr;
}

void ModuleManager::onFrame(float frameMs) {
    for (auto& m : modules_) if (m->enabled()) m->onFrame(frameMs);
}

void ModuleManager::loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        auto dot = line.find('.');
        auto eq = line.find('=');
        if (dot == std::string::npos || eq == std::string::npos || dot > eq) continue;

        Module* m = find(line.substr(0, dot));
        if (!m) continue;
        std::string key = line.substr(dot + 1, eq - dot - 1);
        float v = std::strtof(line.c_str() + eq + 1, nullptr);

        if (key == "enabled") m->setEnabled(v >= 0.5f);
        else if (Setting* s = m->find(key)) s->set(v);
    }
}

void ModuleManager::startConfigWatcher(const std::string& path) {
    std::thread([this, path] {
        struct timespec last = {};
        for (;;) {
            struct stat st;
            if (stat(path.c_str(), &st) == 0 &&
                (st.st_mtim.tv_sec != last.tv_sec || st.st_mtim.tv_nsec != last.tv_nsec)) {
                last = st.st_mtim;
                loadConfig(path);
                LOGI("config reloaded");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }).detach();
}
