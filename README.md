# encos_driver

encos_driver 是用于Encos电机、电池、PMS控制的 C++17 库，支持多种通信协议。通过模块化设计，允许通过插件机制添加新功能。

## 先决条件

- CMake >= 3.18
- C++17 编译器 (GCC/Clang)
- 提前安装依赖 libcap 和 Zstd。默认日志后端使用 spdlog/fmt；如果通过 `ENCOS_ENABLE_SPDLOG=OFF` 关闭，则无需安装 spdlog/fmt
```bash
sudo apt-get update
sudo apt-get install -y libcap-dev libzstd-dev libspdlog-dev libfmt-dev
```

### 插件选择

动态模式和静态模式都通过 `ENCOS_PLUGINS_LIST` 显式选择插件，或让构建系统自动扫描 `plugins/` 目录。自动扫描时，可用 `ENCOS_PLUGINS_BLACKLIST` 排除特定插件。

```bash
# 显式启用 CAN、EtherCAT 和 Fake
cmake -S . -B build -DENCOS_PLUGINS_LIST="can;ethercat;fake"

# 自动扫描并排除 UsbSerial
cmake -S . -B build -DENCOS_PLUGINS_BLACKLIST="usbSerial"
```

外部插件可以通过 `add_emd_plugin` 在父 CMake 项目中注册。源码树模式下在 `add_subdirectory` 后调用：

```cmake
add_subdirectory(EncosDriver)
add_emd_plugin(my_adapter "${CMAKE_CURRENT_SOURCE_DIR}/my_adapter")
```

安装后的包同样支持 `find_package` 后注册外部动态插件，并可通过 `ENCOS_PLUGIN_INSTALL_DIR` 将插件安装到驱动运行时扫描目录：

```cmake
find_package(encos_driver REQUIRED)

add_emd_plugin(my_adapter "${CMAKE_CURRENT_SOURCE_DIR}/my_adapter")

# 可选：在消费者自己的 install 规则中引用该变量
install(TARGETS my_adapterPlugin
    LIBRARY DESTINATION "${ENCOS_PLUGIN_INSTALL_DIR}"
)
```

> 安装后的 `add_emd_plugin` 仅用于动态外部插件。EncosDriver 的静态模式不能安装，因此不存在安装后静态插件注册路径。

更多细节请参阅 [构建说明](docs/build.md)。

> `emrs`（WebSocket relay helper）和 Fake Plot GUI 已从本仓库提取到独立的 `motor_relay_server` 仓库。`bench`/`stress` 工具已合并到 `motor_cli` 的 `emcli bench` 和 `emcli stress` 命令。请参阅对应仓库的文档。

### 代码格式化与静态检查

本项目支持使用`clang-format`和`clang-tidy`进行格式化与静态检查，使用方法如下

```bash
cmake --build build --target format
cmake --build build --target format-check
cmake --build build --target tidy
```

## 安装

安装和打包仅支持 `Linux + 动态模式`，并且需要显式开启 `ENCOS_ENABLE_INSTALL=ON`。
你可以通过设置 `CMAKE_INSTALL_PREFIX` 来更改安装路径。

```bash
cmake -S . -B build -DENCOS_ENABLE_INSTALL=ON
cmake --build build -j
sudo cmake --install build
```

### 静态模式

静态模式会把启用的适配器直接编进 `encos_driver` 主库，不再依赖运行期插件目录：

```bash
cmake -S . -B build -DENCOS_STATIC_MODE=ON
cmake --build build -j
```

静态模式下不提供安装和 DEB 打包规则。

### Emscripten 构建

Emscripten 构建只支持静态模式，不能构建 C++ GoogleTest 测试。除了 `Fake` 适配器外，`RelayWs` 也可以在 Emscripten 下启用；手动启用 CAN、EtherCAT、UsbSerial、Slcan 等适配器会在 CMake 配置阶段报错。Emscripten 下默认关闭 spdlog，日志会降级到标准输出/错误输出，debug 级别调用会被宏展开为空。

如果 emsdk 需要手动激活，可先执行：

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm
cmake --build build-wasm -j
```

构建系统会自动启用 Asyncify，以支持平台延时接口在 wasm 下使用 `emscripten_sleep`。

如果需要构建 TypeScript WASM wrapper（`npm/src/index.ts`），需要显式启用 `ENCOS_BUILD_WASM_BINDINGS`：

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm \
  -DENCOS_BUILD_WASM_BINDINGS=ON \
  -DENCOS_ENABLE_INSTALL=OFF
cmake --build build-wasm -j
```

WASM npm 测试使用 pnpm、Vitest、Vite 和 Playwright。首次运行前安装 wrapper 测试依赖：

```bash
pnpm --dir npm install
pnpm --dir npm setup:browsers
```

然后运行完整 WASM 构建和测试流程：

```bash
pnpm --dir npm test
```

该命令会执行 `scripts/test-wasm.sh`，自动激活 `~/emsdk/emsdk_env.sh`，构建 `build-wasm/encosdriver_wasm.js/.wasm`，再运行 TypeScript 类型检查、Node/Vitest 和 Playwright 浏览器测试。

### 构建 DEB 包

你可以使用 CPack 生成 Debian 安装包：

```bash
cmake -S . -B build -DENCOS_ENABLE_INSTALL=ON
cmake --build build -j
cd build
cpack
```

这将在 `build` 目录下生成一个 `.deb` 文件，例如 `libencosdriver_1.0.0_amd64.deb`。

安装 DEB 包：

```bash
sudo dpkg -i libencosdriver_1.0.0_amd64.deb
```

或使用 apt 自动处理依赖：

```bash
sudo apt install ./libencosdriver_1.0.0_amd64.deb
```

卸载 DEB 包：

```bash
sudo apt remove libencosdriver
```

## 卸载

- 若你使用本地前缀（例如 `./local_install`），直接删除该目录：

```bash
rm -rf ./local_install
```

- 若你使用 system install 并且保留了 `build` 目录，使用 CMake 生成的卸载脚本：

```bash
cmake -P build/cmake_uninstall.cmake
sudo cmake -P build/cmake_uninstall.cmake
```

> 如果在安装时没有保留 `build` 目录或卸载脚本不可用，请谨慎手动删除安装路径下的文件。

## 使用
在你的 CMake 项目中，添加以下内容以链接 encos_driver：

```cmake
find_package(encos_driver REQUIRED)
target_link_libraries(your_target PRIVATE Encos::encos_driver)
```

示例代码
```cpp
#include <string>
#include <thread>
#include <iostream>

#include <encos/encos_driver.h>

int main() {
    auto* adapter = encos::MakeAdapter("Ethercat", "<InterfaceName>");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto* bus = adapter->GetBus(0, 0);

    auto motors = bus->ScanMotors();
    std::cout << "Discovered motors: \n";
    for (const auto& [idx, motor] : motors) {
        std::cout << idx << " ";
    }
    std::cout << std::endl;
    for (int cycle = 0; cycle < 1000; ++cycle) {
        for (const auto& [idx, motor] : motors) {
            motor->SpdControl<0>(1, 2);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return encos::DeleteAdapter(adapter) ? 0 : 1;
}
```

> 更详细的使用方式请查看[使用指南](/docs/using_guide.md)

## 适配器插件
当前可用的适配器插件：
- Ethercat  EtherCAT 适配器，使用 fd broker 模式自动获取原始套接字权限
- EthercatIGH  IGH EtherCAT 主站适配器（仅在已安装 IGH 用户态库时可用）
- Can  SocketCAN 适配器，使用 fd broker 模式自动获取原始套接字权限
- UsbSerial  适配 Encos USB 转 CAN 适配器，仅用于临时调试，性能不可靠
- Slcan  适配 slcan 虚拟 CAN 设备，适用于 Canable 等 USB 转 CAN 适配器
- Fake  用于单元测试的虚拟适配器，默认不出现在 `GetAvailableAdapterTypes()` 中
- RelayWs  通过 WebSocket relay helper 远程控制电机的适配器；WASM 构建中也可用

在动态模式下，上述适配器以独立插件构建；在静态模式下，只有启用的适配器会被编进主库。`Fake` 可通过 `ENCOS_PLUGINS_LIST` 或 `ENCOS_PLUGINS_BLACKLIST` 排除；若开启 `ENCOS_BUILD_TESTS=ON`，则最终插件列表必须包含 `Fake`，否则配置阶段会报错。

### RelayWs 远程电机控制

`RelayWs` 适配器通过 `emrs`（位于独立的 `motor_relay_server` 仓库）将本地 CAN/EtherCAT 总线暴露为 WebSocket 服务，客户端可在浏览器或 Node 中通过 `ws://` 连接。启动 helper 前需要初始化子模块：

```bash
git submodule update --init --recursive external/IXWebSocket
```

启动 helper（在 `motor_relay_server` 仓库中构建后运行）：

```bash
./emrs --host 0.0.0.0 --port 9001
```

启动日志会打印所有可用接口的 `/start` URL。客户端先用 HTTP GET 访问 `/start?token=...&AdapterType=...&AdapterName=...` 获取 session，再连接 `/ws?session=...&freq=...`。

WASM/TypeScript 中可在 Node 或浏览器中通过同一个 npm 包入口使用 `/start` URL 创建 `RelayWs` 适配器，TypeScript 运行时会异步完成 `/start` HTTP 请求并转换为 `ws://...?session=...&freq=...` 后调用 C++ 插件加载路径：

```ts
import { createEncosRuntime } from '@encos/encos-driver'

const runtime = await createEncosRuntime()
const adapter = await runtime.createAdapter({
  type: 'RelayWs',
  interfaceName: 'http://host:port/start?token=...&AdapterType=...&AdapterName=...',
})
// 仅 Adapter 是 WASM 所有权边界；其子包装器会随之失效。
adapter.dispose()
runtime.dispose()
```

也可以直接传入已解析的 `ws://` URL。

### 外部工具

- `emrs`（WebSocket relay helper）和 Fake Plot GUI 已从本仓库提取到独立的 `motor_relay_server` 仓库。
- `bench` / `stress` 工具已从本仓库的 `emd` 合并到 `motor_cli` 的 `emcli bench` 和 `emcli stress` 命令。

请参阅对应仓库的文档获取使用说明。

## 权限问题
- 动态模式下，Ethercat 和 Can 适配器通过单独的 fd broker 可执行文件管理套接字权限，安装后会自动设置所需 capability
- Can 适配器在 Linux 下会在 fd broker 中自动执行 SocketCAN 初始化（`ip link set <ifname> type can bitrate 1000000` + `ip link set <ifname> up`），默认固定波特率为 `1000000`
- 静态模式下使用 Ethercat/Can 前，需要对最终可执行文件手动设置 `cap_net_raw,cap_net_admin`；
  `SetCurrentThreadPriority()` 直接调用 `sched_setscheduler()` 并 `mlockall()`，因此还需要
  `cap_sys_nice` 与 `cap_ipc_lock`
- 动态模式下，插件通信 loop 通过受限的 `ThreadPriorityHelper` 请求实时优先级；安装会尝试只给
  helper 设置 `cap_sys_nice`，失败时可通过 GUI 显式授权。业务进程本身仍需要 `cap_ipc_lock`
  以完成 `SetCurrentThreadPriority()` 中的内存锁页
- UsbSerial和Slcan需要对应的串口设备对当前运行的用户或组开放读写权限，可通过 `udev` 规则实现自动化管理，或是在每次重新插入设备后手动设置权限
    `sudo chmod 666 /dev/ttyUSB0` （假设设备节点为 `/dev/ttyUSB0`）
  
静态模式下需要 raw socket、实时优先级或内存锁页 capability 时，请由管理员对受信任的最终可执行文件显式设置权限：
```bash
sudo setcap cap_net_raw,cap_net_admin,cap_sys_nice,cap_ipc_lock+ep <Executable>
```
动态 CAN/EtherCAT 插件通过 fd broker 获取已配置的套接字；线程优先级 helper 不使用
setuid，也不能修改调用进程之外的线程。
