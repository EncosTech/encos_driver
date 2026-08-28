# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 提供代码仓库的工作指导。

## 项目概述

EncosMotorDriver 是一个 C++17 电机控制库，支持多种通信协议（EtherCAT、CAN、USB-Serial），采用模块化插件架构。

## 构建命令

### 标准构建
```bash
cmake -S . -B build
cmake --build build -j
sudo cmake --install build
```

### 构建测试
```bash
cmake -S . -B build -DENCOS_BUILD_TESTS=ON
cmake --build build -j
cd build && ctest
```

### 构建 DEB 包
```bash
cmake -S . -B build
cmake --build build -j
cd build && cpack
```

### 卸载
```bash
cmake -P build/cmake_uninstall.cmake
sudo cmake -P build/cmake_uninstall.cmake
```

## 依赖项

- CMake >= 3.14
- C++17 编译器 (GCC/Clang)
- libcap-dev
- spdlog, fmt
- GTest/GMock (用于测试)

Ubuntu 安装命令：
```bash
sudo apt-get install -y libcap-dev libspdlog-dev libfmt-dev libgtest-dev libgmock-dev
```

## 架构

### 核心层次
```
BaseAdapter (传输层)
    └── Bus (网络层，管理总线上的电机)
        └── Motor (设备层，单个电机控制)
```

### 核心组件

- **BaseAdapter** (`include/adapter/base_adapter.h`): 传输协议抽象基类，处理消息收发，持有 Bus 实例
- **Bus** (`include/bus/bus.h`): 表示 CAN/EtherCAT 总线，管理 Motor 实例并处理消息路由
- **Motor** (`include/motor/motor.h`): 高层电机控制接口，提供 PVT（位置-速度-扭矩）、位置、速度、电流和扭矩控制模式
- **PluginLoader** (`plugins/plugin_loader.cc`): 适配器插件的动态加载机制

### 适配器插件

位于 `plugins/` 目录：
- `ethercat` - EtherCAT 适配器（fd broker 模式）
- `can` - SocketCAN 适配器（fd broker 模式）
- `usbSerial` - USB 转 CAN 适配器
- `slcan` - 虚拟 CAN 设备适配器

插件在运行时根据适配器类型动态加载。

### 使用示例

```cpp
#include <encos/encos_motor.h>

auto adapter = encos::MakeAdapter("Ethercat", "<InterfaceName>");
auto bus = adapter->GetBus(0, 0);
auto motors = bus->ScanMotors();

for (const auto& [idx, motor] : motors) {
    motor->SpdControl<1>(speed, current);
}
```

## CMake 集成

```cmake
find_package(EncosMotorDriver REQUIRED)
target_link_libraries(your_target PRIVATE Encos::EncosMotorDriver)
```

## 测试

- 使用 Google Test 框架
- 通过 `-DENCOS_BUILD_TESTS=ON` 启用
- 测试可执行文件：`EncosMotorDriverBaseTests`、`EncosMotorDriverPluginsTests`
- 真机 `bench`/`stress` 不属于本仓库的自动化测试流程

### 代码修改后的验证要求

修改任何 C++ 源码或头文件后，必须运行以下 clang 验证并确认通过：
```bash
cmake --build build --target format-check
cmake --build build --target tidy
```

## 平台说明

- 完整功能仅支持 Linux（CAN、EtherCAT）
- `ecg` 工具为可执行文件提供提升的网络权限
- 部分适配器需要 `setcap` 获取原始套接字访问权限：
  ```bash
  sudo setcap cap_net_raw,cap_net_admin+ep <Executable>
  ```

## 代码风格

- C++17 标准
- 头文件接口位于 `include/`，实现位于 `src/`
- 插件自包含于 `plugins/<name>/` 目录
- 使用 spdlog 进行日志记录

### 命名规范

- **类名**：使用大驼峰命名法 (PascalCase)，例如 `BaseAdapter`、`DirtyQueue`
- **函数**：使用小驼峰命名法 (camelCase)，例如 `GetMotor`、`PosControl`
- **变量**：使用连字符命名，如`motor_config`，如为成员变量需与其他变量区分，则添加 `_` 后缀，例如 `motor_`、`maxSpd_`
- **常量**：使用小驼峰命名法，并添加k前缀，例如 `kLimitSlack`

### 文档规范

- 使用 Doxygen 风格注释，统一使用中文编写
- 仅在必要情况下使用函数内注释
- 示例：
  ```cpp
  /**
   * @brief 简要描述
   * @param paramName 参数描述
   * @return 返回值描述
   */
  ```
