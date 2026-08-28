#pragma once

/**
 * @file export.h
 * @brief 跨库接口导出宏定义
 *
 * 提供主库与 Base 库的跨 DLL 导出/导入宏，用于 Windows 动态构建和 ELF 动态构建。
 */

#ifndef ENCOS_API
#ifdef ENCOS_STATIC_MODE
#define ENCOS_API
#elif defined(_WIN32)
#ifdef EncosMotorDriver_EXPORTS
#define ENCOS_API __declspec(dllexport)
#else
#define ENCOS_API __declspec(dllimport)
#endif
#else
#define ENCOS_API __attribute__((visibility("default")))
#endif
#endif

#ifndef ENCOS_BASE_API
#ifdef ENCOS_STATIC_MODE
#define ENCOS_BASE_API
#elif defined(_WIN32)
#ifdef EncosMotorDriverBase_EXPORTS
#define ENCOS_BASE_API __declspec(dllexport)
#else
#define ENCOS_BASE_API __declspec(dllimport)
#endif
#else
#define ENCOS_BASE_API __attribute__((visibility("default")))
#endif
#endif
