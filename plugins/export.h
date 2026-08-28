#pragma once

/**
 * @file export.h
 * @brief 插件导出宏定义
 *
 * 提供跨平台的动态库导出/导入宏，用于插件接口声明
 */

// ENCOS plugin export macro - cross-platform
#ifndef ENCOS_PLUGIN_API
#ifdef _WIN32
#ifdef ENCOS_PLUGIN_EXPORTS
#define ENCOS_PLUGIN_API __declspec(dllexport)
#else
#define ENCOS_PLUGIN_API __declspec(dllimport)
#endif
#else
#ifdef ENCOS_PLUGIN_EXPORTS
#define ENCOS_PLUGIN_API __attribute__((visibility("default")))
#else
#define ENCOS_PLUGIN_API
#endif
#endif
#endif
