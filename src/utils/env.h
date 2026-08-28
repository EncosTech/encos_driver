#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef ENCOS_PLUGIN_INSTALL_PATH
#define ENCOS_PLUGIN_INSTALL_PATH "/usr/local/lib/encosPlugins"
#endif

namespace encos {

namespace fs = std::filesystem;

/**
 * @brief 环境变量工具类
 *
 * 提供插件路径配置和环境变量访问功能
 */
class Env {
public:
    /**
     * @brief 获取插件目录路径
     * @return 插件目录路径
     * @throws std::runtime_error 如果未设置 ENCOS_PLUGIN_PATH 且默认路径不存在
     *
     * 查找顺序：
     * 1. 环境变量 ENCOS_PLUGIN_PATH
     * 2. Linux: 构建时由 CMAKE_INSTALL_LIBDIR 配置的插件目录
     * 3. Windows: ./plugins
     *
     * 结果会被缓存，后续调用直接返回缓存值
     */
    static fs::path PluginDir() {
        static fs::path cached_path = []() -> fs::path {
            std::string dir;
            if (get("ENCOS_PLUGIN_PATH", dir)) {
                return fs::path(dir);
            } else {
#ifdef _WIN32
                const fs::path default_plugin_dir = fs::absolute("./plugins");
                if (fs::exists(default_plugin_dir)) {
                    return default_plugin_dir;
                }
#else
                const fs::path default_plugin_dir = ENCOS_PLUGIN_INSTALL_PATH;
                if (fs::exists(default_plugin_dir)) {
                    return default_plugin_dir;
                }
#endif
                throw std::runtime_error("Environment variable ENCOS_PLUGIN_PATH not set");
            }
        }();
        return cached_path;
    }

    /**
     * @brief 设置插件目录路径
     * @param path 插件目录路径
     */
    static void SetPluginDir(const std::string& path) {
#ifdef _WIN32
        _putenv_s("ENCOS_PLUGIN_PATH", path.c_str());
#else
        setenv("ENCOS_PLUGIN_PATH", path.c_str(), 1);
#endif
    }

private:
    /**
     * @brief 获取环境变量值
     * @param name 环境变量名
     * @param out 输出值
     * @return 是否成功获取
     */
    static bool get(const std::string& name, std::string& out) noexcept {
        const char* val = std::getenv(name.c_str());
        if (val) {
            out = val;
            return true;
        }
        return false;
    }

    /**
     * @brief 获取环境变量值或默认值
     * @param name 环境变量名
     * @param def 默认值
     * @return 环境变量值或默认值
     */
    static std::string GetOr(const std::string& name, const std::string& def = "") noexcept {
        std::string v;
        return get(name, v) ? v : def;
    }
};

}  // namespace encos
