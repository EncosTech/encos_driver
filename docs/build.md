# 构建说明

本文档说明 EncosDriver 当前支持的构建模式，以及常用 CMake 参数的含义。

## 依赖

基础依赖：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcap-dev
```

本仓库要求 CMake >= 3.18。

默认日志后端会使用 `spdlog` 和 `fmt`：

```bash
sudo apt-get install -y libspdlog-dev libfmt-dev
```

如果需要构建测试，还需要：

```bash
sudo apt-get install -y libgtest-dev libgmock-dev python3
```

## 动态插件模式

动态插件模式是 Linux 原生构建的默认工作方式。主库 `EncosDriver` 负责加载运行期插件，插件会构建为独立 `.so` 文件并放在构建目录的 `plugins/` 下。

```bash
cmake -S . -B build
cmake --build build -j
```

Linux 动态模式默认自动扫描 `plugins/` 目录，发现并构建所有平台可用的插件，通常包括：

- `Can`
- `Ethercat`
- `UsbSerial`
- `Slcan`
- `Fake`
- `RelayWs`（需要初始化 `external/IXWebSocket` 子模块）

如果 IGH EtherCAT 用户态库已安装，则 `EthercatIGH` 也会被自动发现并构建。

要显式指定插件而不依赖自动扫描，使用 `ENCOS_PLUGINS_LIST`：

```bash
cmake -S . -B build -DENCOS_PLUGINS_LIST="can;ethercat;usbSerial;slcan;fake"
```

要在自动扫描中排除某个插件，使用 `ENCOS_PLUGINS_BLACKLIST`（仅对自动扫描生效）：

```bash
cmake -S . -B build -DENCOS_PLUGINS_BLACKLIST="usbSerial"
```

运行未安装的构建产物时，通常需要指定插件目录：

```bash
export ENCOS_PLUGIN_PATH="$PWD/build/plugins"
```

也可以在代码中调用 `encos::SetPluginPath()` 设置插件路径。

## 安装模式

安装规则只支持 `Linux + 动态插件模式`。虽然当前 Linux 动态模式默认会启用安装规则，但发布或 CI 中建议显式指定 `ENCOS_ENABLE_INSTALL=ON`。

```bash
cmake -S . -B build \
  -DENCOS_ENABLE_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
sudo cmake --install build
```

安装后会包含：

- 头文件：`include/encos`
- 主库：`libencosdriver.so`（带版本号 SONAME）
- 基础库：`libencosdriver_base.so`（带版本号 SONAME）
- 动态插件：`lib/encosPlugins/*.so`
- fd broker 可执行文件：`CanFdBrokerExecutable`、`EthercatFdBrokerExecutable`
- 静态 CAN/EtherCAT 的 capability 需要管理员对受信任的最终可执行文件显式执行 `setcap`；动态插件使用 fd broker。

> `emrs`（WebSocket relay helper）和 Fake Plot GUI 已从本仓库提取到独立的 `motor_relay_server` 仓库。`bench`/`stress` 工具已合并到 `motor_cli` 的 `emcli bench` 和 `emcli stress` 命令。请参阅对应仓库的文档。

卸载：

```bash
sudo cmake --build build --target uninstall
```

## DEB 打包

DEB 打包同样只支持 `Linux + 动态插件模式`，并依赖安装规则。

```bash
cmake -S . -B build \
  -DENCOS_ENABLE_INSTALL=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build
cpack -G DEB
```

生成的包名格式类似：

```text
libencosdriver_2.2.5_amd64_noble.deb
```

### RelayWs 子模块

Native 的 `RelayWs` 插件客户端依赖 `IXWebSocket`：

```bash
git submodule update --init --recursive external/IXWebSocket
```

如果子模块未初始化，CMake 配置阶段会跳过 `RelayWs` 或报错（取决于是否显式要求该插件）。Emscripten 构建不需要此子模块。

> `emrs` relay helper 已从本仓库提取到独立的 `motor_relay_server` 仓库。请参阅该仓库的文档获取 helper 构建和使用说明。

## 外部工具

`emrs`（WebSocket relay helper）和 Fake Plot GUI 已从本仓库提取到独立的 `motor_relay_server` 仓库。

`bench` / `stress` 工具已从本仓库的 `emd` 合并到 `motor_cli` 的 `emcli bench` 和 `emcli stress` 命令。

请参阅对应仓库的文档获取使用说明。

## 静态模式

静态模式会把启用的适配器直接编进 `encos_driver`，不再依赖运行期插件目录。

```bash
cmake -S . -B build-static \
  -DENCOS_STATIC_MODE=ON \
  -DENCOS_ENABLE_INSTALL=OFF
cmake --build build-static -j
```

静态模式特点：

- `encos_driver` 构建为静态库。
- `SetPluginPath()` 保留接口，但不会影响适配器加载。
- `GetAvailableAdapterTypes()` 返回编译进主库的适配器。
- 不支持安装和 DEB 打包。
- 使用 `Can` 或 `Ethercat` 时，需要给最终可执行文件设置网络权限。

示例：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep ./your_app
```

`SetCurrentThreadPriority()` 在设置 `SCHED_FIFO` 优先级后会调用 `mlockall()` 锁定当前
进程的全部当前与未来内存页，因此调用该函数的进程必须具备 `CAP_IPC_LOCK` 能力（或
`RLIMIT_MEMLOCK` 为 unlimited）。静态模式下优先级由进程自己通过 `sched_setscheduler()`
设置，因此还需要 `CAP_SYS_NICE`：

```bash
sudo setcap cap_net_raw,cap_net_admin,cap_sys_nice,cap_ipc_lock+ep ./your_app
```

动态模式下线程实时优先级仍由受限的 `ThreadPriorityHelper` 设置，不要把 `CAP_SYS_NICE`
授予整个业务程序；helper 安装时会尝试获得 `cap_sys_nice=ep`，失败时首次调用
`SetCurrentThreadPriority()` 可通过 GUI 显式授权。动态模式下业务进程本身仍需要
`CAP_IPC_LOCK` 来完成内存锁页。

## 测试构建

测试默认不构建，需要显式开启：

```bash
cmake -S . -B build-test \
  -DENCOS_BUILD_TESTS=ON
cmake --build build-test -j
ctest --test-dir build-test --output-on-failure
```

`ENCOS_BUILD_TESTS=ON` 要求 `ENCOS_ENABLE_PYTHON=ON`，因为测试里包含 CMake 配置策略检查和生成文件校验。

代码修改后建议运行：

```bash
cmake --build build-test --target format-check
cmake --build build-test --target tidy
```

> `bench` / `stress` 工具已合并到 `motor_cli` 的 `emcli bench` 和 `emcli stress` 命令。请参阅 `motor_cli` 仓库的文档。

## LaTeX API 文档

公开 API 头文件统一位于 `include/`。安装 Doxygen 后，可通过可选目标生成 LaTeX API 文档：

```bash
cmake -S . -B build-docs -DENCOS_BUILD_DOCS=ON
cmake --build build-docs --target docs-latex
```

生成的 LaTeX 源文件位于 `build-docs/docs/latex/`。文档目标默认关闭，普通库构建不依赖 Doxygen 或 LaTeX。

如需进一步编译 PDF，Ubuntu 可安装以下工具链：

```bash
sudo apt-get install -y \
  doxygen graphviz ghostscript make pandoc librsvg2-bin fonts-noto-cjk \
  texlive-latex-base texlive-latex-recommended texlive-latex-extra \
  texlive-fonts-recommended texlive-lang-chinese texlive-xetex
```

其中 `graphviz` 用于生成关系图，`texlive-lang-chinese` 提供中文 LaTeX 支持；只导出 `.tex` 而不编译 PDF 时不需要安装 TeX Live。生成 LaTeX 后，可用 Doxygen 写入输出目录的 Makefile 编译 PDF：

```bash
cmake --build build-docs --target docs-latex
make -C build-docs/docs/latex pdf
```

PDF 位于 `build-docs/docs/latex/refman.pdf`。

也可以使用仓库脚本一次生成 API、架构设计、版本变更记录和使用指南四份 PDF：

```bash
./scripts/build-docs.sh
```

输出文件为 `docs/dist/api.pdf`、`docs/dist/arch.pdf`、`docs/dist/changes.pdf` 和
`docs/dist/using_guide.pdf`。脚本使用 Pandoc 和 XeLaTeX 编译 Markdown，默认中文字体为
`Noto Serif CJK SC`，等宽字体为 `Noto Sans Mono CJK SC`；可通过
`PANDOC_CJK_MAINFONT`、`PANDOC_MONOFONT` 和 `PANDOC_PDF_ENGINE` 环境变量覆盖。
构建目录和输出目录也可分别通过 `ENCOS_DOCS_BUILD_DIR`、`ENCOS_DOCS_DIST_DIR` 覆盖。
代码块使用 Tango 语法高亮和浅灰蓝背景，背景色定义位于
`docs/pandoc/code-blocks.tex`。

## 关闭 spdlog

如果目标环境不方便安装 `spdlog` 和 `fmt`，可以关闭 spdlog 后端：

```bash
cmake -S . -B build-no-spdlog \
  -DENCOS_ENABLE_SPDLOG=OFF
cmake --build build-no-spdlog -j
```

关闭后：

- 不再查找和链接 `spdlog`、`fmt`。
- `debug` 日志调用会被编译为空操作。
- 其他日志级别会降级输出到标准输出或标准错误。

## 关闭 Python

默认情况下，CMake 会用 Python 根据 `src/motor/motor_models.csv` 刷新生成文件：

- `include/motor/motor_model_generated.h`
- `src/motor/motor_model_generated.cc`

如果构建环境没有 Python，可以关闭生成步骤并使用仓库内已提交的生成文件：

```bash
cmake -S . -B build-no-python \
  -DENCOS_ENABLE_PYTHON=OFF
cmake --build build-no-python -j
```

关闭 Python 时，CMake 会校验 CSV 与生成文件首行记录的 SHA1 是否一致。只有在明确知道生成文件可信且需要临时绕过时，才使用：

```bash
-DENCOS_SKIP_GENERATED_SHA1_CHECK=ON
```

## Emscripten 构建

Emscripten 构建用于 wasm 环境，只支持静态模式。基础 C++ 库可以不启用 wrapper 直接构建：

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm \
  -DENCOS_ENABLE_INSTALL=OFF
cmake --build build-wasm -j
```

如果需要生成 npm 包和 Node/浏览器测试使用的 WASM wrapper，需要打开 `ENCOS_BUILD_WASM_BINDINGS`：

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm \
  -DENCOS_BUILD_WASM_BINDINGS=ON \
  -DENCOS_ENABLE_INSTALL=OFF
cmake --build build-wasm -j
```

该模式会生成：

- `build-wasm/encosdriver_wasm.js`
- `build-wasm/encosdriver_wasm.wasm`
- TypeScript wrapper 入口：`npm/src/index.ts`

Emscripten 下的规则：

- `ENCOS_STATIC_MODE` 默认 `ON`，且不能设为 `OFF`。
- `ENCOS_ENABLE_SPDLOG` 默认 `OFF`。
- `ENCOS_BUILD_TESTS` 必须为 `OFF`。
- `Can`、`Ethercat`、`EthercatIGH`、`EthercatWindows`、`UsbSerial`、`Slcan` 都不可用。
- `RelayWs` 在 Emscripten 下可以作为静态插件启用；它通过浏览器 WebSocket API 或 Node 测试环境中的 Emscripten WebSocket shim 建连。`/start` HTTP 请求由 TypeScript 运行时异步完成，再传入 C++ 的 `encos_create_adapter`。
- 构建系统会添加 Asyncify 链接选项，以支持 `emscripten_sleep`。
- WASM wrapper 入口中的会等待电机响应的 API 使用 Promise；不要把 Emscripten `SleepFor` 改成同步 busy wait，否则会阻塞真实适配器的事件循环和回调模型。

### WASM npm 测试

WASM npm 测试使用 pnpm、TypeScript、Vitest 和 Playwright。Node 单元测试位于
`tests/wasm-node/*.test.ts`，浏览器测试位于 `npm/tests/browser/`。

首次运行前安装 Node 依赖：

```bash
pnpm --dir npm install
pnpm --dir npm setup:browsers
```

完整测试命令：

```bash
pnpm --dir npm test
```

该命令会调用 `scripts/test-wasm.sh`，流程如下：

1. `source ~/emsdk/emsdk_env.sh`
2. `emcmake cmake -S . -B build-wasm -DENCOS_BUILD_WASM_BINDINGS=ON -DENCOS_BUILD_WASM_TESTS=ON -DENCOS_ENABLE_INSTALL=OFF`
3. `cmake --build build-wasm`
4. `pnpm --dir npm typecheck`
5. `pnpm --dir npm test:unit`
6. `pnpm --dir npm test:browser`

`ENCOS_BUILD_WASM_TESTS` 目前用于标记本次 CMake 配置服务于 WASM 测试流程；实际 npm 测试由 pnpm/Vitest 运行，不会启用 Emscripten C++ GoogleTest。

## IGH EtherCAT 插件

`EthercatIGH` 默认参与自动扫描，但只有在已安装 IGH EtherCAT 主站用户态头文件和库时才会被构建。如果需要显式启用而不依赖扫描结果，将其加入 `ENCOS_PLUGINS_LIST`：

```bash
cmake -S . -B build-igh \
  -DENCOS_PLUGINS_LIST="can;ethercat;ethercatIGH;usbSerial;slcan;fake"
cmake --build build-igh -j
```

CMake 会通过 `cmake/FindIGHEtherCAT.cmake` 查找：

- 头文件：`ecrt.h`
- 库：`libethercat`

如果依赖不存在，配置阶段会失败。

## CMake 参数

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | 未指定时设为 `Release` | 单配置生成器的构建类型。常用值为 `Debug`、`Release`、`RelWithDebInfo`。 |
| `CMAKE_INSTALL_PREFIX` | CMake 默认值，常用 `/usr/local` | 安装前缀。安装模式和 DEB 打包都会使用它。 |
| `ENCOS_BUILD_TESTS` | `OFF` | 构建 GoogleTest 测试目标，并注册 CTest。开启时要求 `ENCOS_ENABLE_PYTHON=ON`。 |
| `ENCOS_BUILD_DOCS` | `OFF` | 增加 Doxygen `docs-latex` 目标，从 `include/` 生成 LaTeX API 文档。 |
| `ENCOS_ENABLE_PYTHON` | `ON` | 启用 Python，用于刷新电机型号生成文件和运行相关配置测试。 |
| `ENCOS_SKIP_GENERATED_SHA1_CHECK` | `OFF` | 当 `ENCOS_ENABLE_PYTHON=OFF` 时，跳过 CSV 与生成文件 SHA1 一致性检查。 |
| `ENCOS_ENABLE_SPDLOG` | 原生构建 `ON`，Emscripten `OFF` | 是否使用 spdlog/fmt 日志后端。关闭后使用轻量 fallback logger。 |
| `ENCOS_STATIC_MODE` | 原生构建 `OFF`，Emscripten `ON` | 是否构建静态模式，把启用的适配器编进主库。 |
| `ENCOS_ENABLE_INSTALL` | Linux 动态模式 `ON`，其他模式 `OFF` | 是否生成安装、卸载和 CPack 规则。仅支持 Linux 动态模式。 |
| `ENCOS_ENABLE_RELAY` | `ON` | 启用 `RelayWs` 插件客户端与 `IXWebSocket` 依赖。关闭时不构建 relay 相关目标与第三方依赖。 |
| `ENCOS_BUILD_WASM_BINDINGS` | `OFF` | 构建 Emscripten WASM wrapper 目标 `encosdriver_wasm`。仅 Emscripten 可用。 |
| `ENCOS_BUILD_WASM_TESTS` | `OFF` | 标记 Emscripten 配置用于 WASM Node 测试流程。开启时要求 `ENCOS_BUILD_WASM_BINDINGS=ON`。 |
| `ENCOS_PLUGINS_LIST` | 未定义（自动扫描） | 显式指定要构建的插件列表，例如 `"can;ethercat;fake"`。设置后自动扫描和黑名单均失效。 |
| `ENCOS_PLUGINS_BLACKLIST` | 未定义 | 自动扫描模式下排除的插件名列表，例如 `"usbSerial"`。对显式列表无效。 |

CMake API：

```cmake
add_emd_plugin(<name> <path> [OPTIONAL])
```

在 `EncosPlugins.cmake` 可用后（根项目已 include 或 `add_subdirectory` 后），可通过该函数注册外部插件目录。`<path>` 下必须包含 `plugin.cmake` 清单文件。`OPTIONAL` 表示插件不可用时跳过，否则报错。注册的外部插件会进入最终生成的静态注册表或动态隐藏类型列表，无需额外 finalize 调用。

安装后的 encos_driver CMake 包同样暴露该 API。外部项目执行 `find_package(encos_driver REQUIRED)` 后可直接调用：

```cmake
find_package(encos_driver REQUIRED)

add_emd_plugin(my_adapter "${CMAKE_CURRENT_SOURCE_DIR}/my_adapter")
```

此时 `ENCOS_PLUGIN_INSTALL_DIR` 已定义为当前 encos_driver 安装前缀下的绝对插件目录（例如 `/usr/local/lib/encosPlugins`）。`add_emd_plugin` 会自动将动态外部插件安装到该目录；消费者也可以在自己的 install 规则中显式使用：

```cmake
install(TARGETS my_adapterPlugin
    LIBRARY DESTINATION "${ENCOS_PLUGIN_INSTALL_DIR}"
)
```

> 安装后的 `add_emd_plugin` 仅支持动态外部插件。静态模式无法安装，因此不存在安装后静态插件注册路径。

## 常用组合

动态开发构建：

```bash
cmake -S . -B build
cmake --build build -j
```

动态测试构建：

```bash
cmake -S . -B build \
  -DENCOS_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

静态库构建：

```bash
cmake -S . -B build-static \
  -DENCOS_STATIC_MODE=ON \
  -DENCOS_ENABLE_INSTALL=OFF
cmake --build build-static -j
```

最小依赖构建：

```bash
cmake -S . -B build-minimal \
  -DENCOS_ENABLE_SPDLOG=OFF \
  -DENCOS_ENABLE_PYTHON=OFF \
  -DENCOS_ENABLE_INSTALL=OFF
cmake --build build-minimal -j
```

发布打包构建：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENCOS_ENABLE_INSTALL=ON \
  -DENCOS_BUILD_TESTS=ON
cmake --build build -j
cmake --build build --target format-check
cmake --build build --target tidy
ctest --test-dir build --output-on-failure
cd build
cpack -G DEB
```
