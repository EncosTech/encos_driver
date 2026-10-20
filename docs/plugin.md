# 插件编写指南

EncosDriver 的插件分两类；动态构建在运行期加载独立共享库，静态构建通过生成的注册表提供插件：

| 类别 | 作用 | 导出符号 | Linux 文件名 |
| --- | --- | --- | --- |
| adapter | 传输层（CAN / EtherCAT / 串口 / 网络…） | `MakeAdapter` | `lib<Type>Plugin.so` |
| device | 设备层（电机之外的简单设备、总线级协议消费者） | `MakeDeviceDriver` | `lib<Type>Device.so` |

两类插件在同一个扁平插件目录中按**文件后缀**与**导出符号**双重判别，因此源码目录的分组不影响
运行期布局。

设备插件在全部平台无条件可用，包括 Emscripten/wasm（wasm 强制静态模式，设备类型经生成的
静态注册表提供）：`Bus::GetDevice` / `EncosDriverManager::CreateDevice` 与设备插件目录扫描
都不依赖任何构建选项。JSON（`nlohmann/json`）以单头文件形式随仓库提供
（`include/external/json.hpp`），不需要系统包。

## 1. 目录与构建期注册（源码树）

```
plugins/
├── adapter/            # 适配器插件
│   ├── can/
│   ├── ethercat/
│   └── utils/          # 适配器共享工具（不属于任何单个插件）
└── device/             # 设备插件
    └── testDevice/
```

每个插件目录包含 `plugin.cmake`（清单）与 `CMakeLists.txt`（目标定义）。顶层构建默认自动扫描
`plugins/adapter/*/plugin.cmake` 与 `plugins/device/*/plugin.cmake`；也可显式指定：

```bash
cmake -S . -B build -DENCOS_PLUGINS_LIST="can;ethercat;fake"
cmake -S . -B build -DENCOS_PLUGINS_BLACKLIST="usbSerial"     # 仅对自动扫描生效
```

## 2. `plugin.cmake` 清单字段

| 字段 | 源码树 | 安装消费 | 说明 |
| --- | --- | --- | --- |
| `ENCOS_PLUGIN_CHECK_FUNCTION` | 必填 | 必填 | 可用性检查；不可用则自动扫描跳过 |
| `ENCOS_PLUGIN_DYNAMIC_TARGET` | 必填 | 必填 | 动态目标名，须与实际 `add_library` 目标一致 |
| `ENCOS_PLUGIN_DEVICE_TYPE` / `ENCOS_PLUGIN_ADAPTER_TYPE` | 必填 | 按类别必填 | 对外类型名 |
| `ENCOS_PLUGIN_STATIC_TARGET` | 必填 | 不需要 | 静态模式目标名 |
| `ENCOS_PLUGIN_STATIC_HEADER` | 必填 | 不需要 | 静态注册表 include 的头文件 |
| `ENCOS_PLUGIN_STATIC_FACTORY` | 必填 | 不需要 | 静态注册表调用的工厂 |
| `ENCOS_PLUGIN_ENUMERATE_FUNCTION` | 适配器必填 | 不需要 | 接口枚举函数（`adapter` 类别），供 `GetAvailableInterface()` 调用 |
| `ENCOS_PLUGIN_STATIC_FACTORY_EXTEND` | 可选 | 不需要 | 接收 `extend` 配置的静态工厂；未声明时注册表工厂忽略该形参 |
| `ENCOS_PLUGIN_KIND` | 可选 | 可选 | `adapter` / `device`，须与注册类别一致 |
| `ENCOS_PLUGIN_VISIBLE` | 可选 | 可选 | `OFF` 时不出现在 `GetAvailable*Types()`，默认 `ON` |

检查函数约定：`function(check out_available out_reason)`，可用时返回 `TRUE` 且 reason 为空。

```cmake
function(encos_mydev_check out_available out_reason)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(${out_available} FALSE PARENT_SCOPE)
        set(${out_reason} "MyDev plugin is only supported on Linux" PARENT_SCOPE)
        return()
    endif()
    set(${out_available} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
endfunction()
```

## 3. 编写适配器插件

继承 `encos::BaseAdapter` 实现收发，导出 C 工厂：

```cpp
// plugins/adapter/myDev/plugin_entry.cc
#include "my_dev_adapter.h"

#include "adapter/adapter_plugin.h"
#include "export.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return encos::MyDevAdapter::Create(interface_name, logger_name,
                                       encos::LogLevelFromInt(log_level));
}

// 每个适配器插件都必须导出 EnumerateInterfaces，供 GetAvailableInterface() 列举可用接口
extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateMyDevInterfaces(emit, context);
}
```

清单里必须声明该枚举函数，否则源码树模式在配置阶段直接失败：

```cmake
set(ENCOS_PLUGIN_ENUMERATE_FUNCTION "encos::EnumerateMyDevInterfaces")
```

枚举策略由插件自行决定：可以调用 `encos::platform` 的共享 OS 接口（如 `GetSerialPortNames()`、
`GetCanInterfaceNames()`），也可以用插件内部专用实现（`gsUsb` 就使用自己的 WinUSB 设备枚举）。
`GetAvailableInterface(type)` 会加载插件并调用它；插件文件或该符号缺失时抛出 `std::runtime_error`。

`CMakeLists.txt` 定义动态目标（`MyDevPlugin`，SHARED）与静态目标（`MyDevPluginCore`），动态目标
需经 `encos_configure_plugin_library()` 处理导出宏与 PIC。

### 3.1 扩展配置（extend）

适配器初始化可携带一个默认为空对象 `{}` 的 JSON 扩展配置，采用**标准化的信封结构**：顶层键为该适配器**全部总线**的公共参数，保留键 `buses` 为按总线的覆盖表。

```jsonc
{
  "bitrate": 1000000,                 // 公共参数：作用于全部总线
  "buses": {
    "0":   { "bitrate": 500000 },     // 裸运行时索引
    "2:5": { "bitrate": 250000,       // <slave>:<bus>
             "sample_point": 0.875 }
  }
}
```

```cpp
#include <encos/encos_driver.h>

auto* adapter = encos::MakeAdapter("Can", "can0");                        // 内置默认参数
auto* tuned = encos::MakeAdapter("Can", "can0", "", encos::LogLevel::Info,
                                 {{"bitrate", 500000}, {"sample_point", 0.875}});
```

**生效参数按「插件默认值 ← 顶层公共参数 ← `buses` 中该总线的覆盖」三层字段级合并**，因此空对象、只给公共参数、只给某总线的部分字段都走同一条路径。`buses` 的键接受两种等价拼写：`<slave>:<bus>` 与表示运行时索引 `(slave << 16) | bus` 的裸十进制整数；两种拼写解析到同一总线时判为重复并报错。

信封的解析、拼写校验与合并由 `include/adapter/adapter_extend.h` 的 `encos::AdapterExtend` **单一实现**，插件不得自行重复：

```cpp
#include "adapter/adapter_extend.h"

const auto extend = encos::AdapterExtend::Parse(extend_json);  // 空串 = 空信封
for (const auto& [slave, bus] : extend.DeclaredBuses()) {
    // 校验该适配器确实有这条总线，否则抛异常
}
can::CanTiming timing;                                          // 成员初始化值即默认值
can::ApplyCanTiming(extend.Scope(slave, bus), &timing);          // 存在则覆盖，未知字段抛异常
```

#### 插件侧入口

插件侧有两个入口，二者必须构造等价适配器：新增可选的 `MakeAdapterV2` 接收配置串，`MakeAdapter` 保留并转发空配置，以便旧宿主继续加载新 `.so`：

```cpp
// plugins/adapter/myDev/plugin_entry.cc
#include "adapter/adapter_extend.h"
#include "adapter/adapter_plugin.h"
#include "export.h"
#include "my_dev_adapter.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return encos::MyDevAdapter::Create(interface_name, logger_name,
                                       encos::LogLevelFromInt(log_level));
}

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapterV2(const char* interface_name,
                                                              const char* logger_name,
                                                              int log_level,
                                                              const char* extend_json) {
    return encos::MyDevAdapter::CreateWithExtend(interface_name, logger_name,
                                                encos::LogLevelFromInt(log_level),
                                                extend_json != nullptr ? extend_json : "");
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateMyDevInterfaces(emit, context);
}
```

静态模式用清单的可选字段声明扩展工厂，生成的注册表工厂会以同一份配置串调用它；未声明该字段的插件保持原行为，形参被忽略：

```cmake
set(ENCOS_PLUGIN_STATIC_FACTORY "encos::MyDevAdapter::Create")
set(ENCOS_PLUGIN_STATIC_FACTORY_EXTEND "encos::MyDevAdapter::CreateWithExtend")
```

#### 约定与错误规则

- **只有实现 `MakeAdapterV2` 的插件会收到配置**。宿主优先探测该符号，缺失时回退到 `MakeAdapter`，并在配置非空时记录"该插件不支持 extend"的警告。
- **`buses` 是保留字**：插件自己的参数名不得与之相同（否则会被信封层剥离，插件的字段清单里也不会再看到它）。
- **未知字段报错**：作用域内出现插件不认识的字段（如拼错的 `bitrat`）一律抛异常，不静默忽略。
- **认识但无法生效的字段**：取值等于其标准默认值时容忍并忽略，否则报错。例如 SLCAN 支持 classic CAN，`{"sample_point": 0.765}` 被容忍，而 `{"sample_point": 0.8}` 抛异常——这样一份"全用标准默认值"的信封可以原样喂给 CAN 与 SLCAN。
- **未知总线报错**：`buses` 指向该适配器不存在的总线（单总线适配器收到 `"1:0"`、Ethernet 收到未发现从站的键）时抛异常。
- **字段类型或取值非法报错**，不静默取近似值；缺省才回落默认值。
- **适配器身份仍只由 `interface_name` 决定**。同一接口名再次创建时命中缓存；此时新 extend 中
  **`buses` 的条目**会经 `BaseAdapter::UpdateBusExtend` 增量应用到既有实例，**顶层公共参数**
  无法重新应用，被忽略并记录警告（见 §3.2）。
- **同一概念在不同插件上使用同名同量纲字段**。例如 Can 与 Ethernet 都是 CAN 总线时序，统一用 `bitrate`、`sample_point`、`dbitrate`、`dsample_point`，采样点为 0-1 小数。
- **配置在 `MakeAdapter` 时传入、不经 `GetBus`**：适配器在构造函数里就配好接口，因此 `GetBus` 保持非虚、签名不变。已存在实例的总线级增量配置走 `BaseAdapter::UpdateBusExtend`，不经 `GetBus`。

### 3.2 已初始化实例的总线增量更新（`UpdateBusExtend`）

适配器身份只由 `interface_name` 决定，因此同一进程内第二次 `MakeAdapter` 会命中缓存返回上一次
的实例。但不同消费者可能只知道自己那几路总线的参数，于是有了按总线增量更新的入口：

```cpp
virtual bool UpdateBusExtend(const std::string& extend_json);
```

宿主在命中缓存且新 extend 非空时自动调用它；消费者也可以直接调用。契约：

- **只应用 `buses` 中显式点名的总线**，其余总线一律不动。顶层公共参数作用于全部总线，无法在
  既有实例上安全地重新应用（会连带改掉其他消费者正在用的总线），因此**忽略并记录警告**——
  注意这与初始化路径“顶层字段作用于全部总线”不同。
- **部分更新按字段合并到该总线的当前生效值**，而不是插件的内置默认值。只写
  `{"bitrate": 250000}` 不会把采样点重置回默认。插件需要自己缓存“当前生效时序”作为合并种子。
- **不做跨总线回滚**：某条总线失败即抛异常，已成功的总线保持新配置。
- **失败即抛异常**（信封非法、总线不存在、下游拒绝）。返回值 `true` 表示“支持该能力并已处理”，
  默认实现返回 `false` 并记录一条诊断，因此不支持的适配器无需改动代码。
- **空串表示“本次没有更新请求”**，默认实现对空串返回 `true`，不算“不支持”。
- **会中断目标总线的报文**。Ethernet 走独立的管理线程，不占用数据通道；EtherCAT 走邮箱 SDO，
  会占住过程数据交换一个周期（实测约 9 ms）。调用方应先停掉该总线的流量。

支持情况：

| 插件 | 运行期总线重配 |
| --- | --- |
| `Ethernet`、`Ethercat`、`EthercatIGH`、`GsUsb` | 支持（`buses` 增量） |
| `Can`、`Slcan`、`UsbSerial`、`RelayWs`、`Fake` | 不支持，走默认空实现 |

`Can` 不支持的取舍：动态模式下主进程没有 `CAP_NET_ADMIN`，位时序由一次性的 fd broker 子进程用
`ip link` 配置，没有重配 RPC；需要不同时序时应使用不同的 `interface_name`。

## 4. 编写设备插件

### 4.1 设备类

继承 `encos::Device`。公开方法入口需取得在飞操作保护（`AcquireOperation()`）；`OnMessage` 由核心
在其在飞计数保护下调用，**不得**再取保护：

```cpp
class MySensor : public encos::Device {
public:
    MySensor(const encos::DeviceCreateContext& context, const nlohmann::json&)
        : Device(context) {}

    void OnMessage(const encos::MotorPackMsg& message) override;   // 接收匹配到的帧
    void CancelWaiters() override;                                 // 有请求-响应等待时覆写

    int ReadValue() {                                              // 公开方法入口
        auto operation = AcquireOperation();
        // ...
    }
};
```

### 4.2 现代 C++ 包装层（推荐）

`include/device/device_plugin_builder.h` 提供链式构建器。配置**直接以 `nlohmann::json`
传递**，不使用独立配置结构：

```cpp
#include "device/device_plugin_builder.h"

extern "C" const encos::DeviceDriverDescriptorV1* MakeDeviceDriver() {
    static const auto plugin =
        encos::DevicePlugin<MySensor>("MySensor")
            .Routes([](const nlohmann::json& config) {
                return std::vector<std::uint32_t>{config.value("base_id", 0x400u)};
            })
            .Create([](const encos::DeviceCreateContext& ctx, const nlohmann::json& config) {
                return std::make_unique<MySensor>(ctx, config);
            });
    return plugin.descriptor();
}
```

| 方法 | 语义 | 必需 |
| --- | --- | --- |
| `.Routes(fn)` | 由配置 JSON 计算本设备占用的路由 ID，返回 `std::vector<std::uint32_t>` | 否 |
| `.Create(fn)` | 由配置 JSON 构造实例，返回 `std::unique_ptr<Device>` | 是 |
| `.Destroy(fn)` | 自定义释放（默认 `delete`） | 否 |
| `.Capabilities(bits)` / `.ClaimBus()` | 设置能力位 / 认领整条总线 | 否 |

包装层内部生成静态 trampoline：解析宿主传入的配置串、捕获 `Create` 抛出的异常并转为
构造失败。插件注册的 Routes/Create 回调及其 JSON 对象留在插件内部。

`MakeDeviceDriver` 是 C linkage 导出入口，但设备接口仍依赖 **C++ ABI**：传入插件的
`DeviceCreateContext` 含 `std::string`、`std::function` 和 `LoggerPtr`，并返回 C++ `Device` 对象。
宿主与插件必须使用兼容的编译器 ABI、标准库/运行库和公开头；描述符版本检查不保证跨工具链兼容。

### 4.3 C linkage 入口与 C++ 描述符（不使用包装层时）

```cpp
struct DeviceDriverDescriptorV1 {
    std::uint32_t abi_version;                                  // 必须等于 kDeviceDriverAbiVersion
    const char* type_name;
    std::uint32_t capabilities;                                 // 能力位，未知位被宿主忽略
    std::size_t (*fill_route_ids)(const char* config_json, std::uint32_t* out, std::size_t cap);
    Device* (*create)(const DeviceCreateContext&) noexcept;     // 不得抛出异常
    void (*destroy)(Device*) noexcept;
};
extern "C" const encos::DeviceDriverDescriptorV1* MakeDeviceDriver();
```

`fill_route_ids` 先以 `out == nullptr` 探测数量，数量非零时再以足量缓冲填充；`out` 为空或
`capacity == 0` 时只返回数量、不得写入。返回数量对同一配置必须稳定。

### 4.4 配置透传

宿主入口为 `Bus::GetDevice(type, config)`，`config` 为 `nlohmann::json`：

```cpp
auto* sensor = bus->GetDevice("MySensor", {{"base_id", 0x410}});
Device* plain = bus->GetDevice("MySensor");          // 未传入时视为空对象 {}
```

- 宿主把配置规范化序列化为 JSON 字符串，同时用于身份键、`fill_route_ids` 与
  `DeviceCreateContext::config`；
- **设备身份为 `(bus, type, config)`**：配置规范化后相等才复用既有实例，配置不同即创建独立实例
  （独立路由与生命周期）；
- `DevicePlugin<MySensor>` 的模板参数必须是插件唯一类型（通常直接使用设备类），用于隔离描述符和 trampoline 回调；旧式 `DevicePlugin("MySensor")` 调用需补上该模板参数。
- 插件在 `Routes`/`Create` 回调中直接拿到 `const nlohmann::json&`，按需读取字段。

### 4.5 认领整条总线

声明认领能力位并把路由固定为整总线标识符，设备即成为所在总线的兜底接收者：

```cpp
extern "C" const encos::DeviceDriverDescriptorV1* MakeDeviceDriver() {
    static const auto plugin =
        encos::DevicePlugin<WholeBusLogger>("WholeBusLogger")
            .ClaimBus()
            .Create([](const encos::DeviceCreateContext& ctx, const nlohmann::json&) {
                return std::make_unique<WholeBusLogger>(ctx);
            });
    return plugin.descriptor();
}
```

语义：

- 接收本总线上**所有未被其它设备路由命中**的帧，以及未命中路由的系统帧 `0x7FF`；
- 精确路由优先：已被电机/其它设备路由命中的帧不会重复投递给认领设备；
- 认领**不独占**：其它设备与电机的创建、路由与扫描语义完全不变，未命中帧仍进入未知邮箱；
- **同一总线至多一个认领设备**，第二次认领会失败并回滚；
- 未声明认领能力位的插件**不得**返回整总线标识符，否则创建失败。

## 5. 运行期发现与加载

| 类别 | Linux | Windows | 符号 | 查询 |
| --- | --- | --- | --- | --- |
| adapter | `lib<Type>Plugin.so` | `<Type>Plugin.dll` | `MakeAdapter` | `GetAvailableAdapterTypes()` |
| device | `lib<Type>Device.so` | `<Type>Device.dll` | `MakeDeviceDriver` | `GetAvailableDeviceTypes()` |

插件搜索目录可用 `encos::SetPluginPath()` 覆盖；未设置时 Linux 使用构建时写入的安装插件目录，
Windows 使用当前工作目录下 `./plugins` 的绝对路径。

## 6. 宿主侧使用

```cpp
#include <encos/encos_driver.h>

encos::SetPluginPath("/opt/encos/encosPlugins");

// 适配器
for (const auto& type : encos::GetAvailableAdapterTypes()) { /* ... */ }
auto* adapter = encos::MakeAdapter("Can", "can0");
auto* bus = adapter->GetBus(0);

// 设备
for (const auto& type : encos::GetAvailableDeviceTypes()) { /* ... */ }
auto* sensor = bus->GetDevice("MySensor", {{"base_id", 0x410}});

// 销毁：设备可单独删除，也可由总线/适配器级联删除
encos::DeleteDevice(sensor);
encos::DeleteAdapter(adapter);      // 级联释放总线、电机与设备
```

删除会阻止新的公开方法进入并等待在飞调用与接收回调排空；删除后不得再访问旧指针。

## 7. 基于 DEB 安装编写插件（无需驱动源码）

安装 `libencosdriver` 后即可在**没有驱动源码**的环境编写插件。一个插件工程涉及**三个 CMake
相关文件**，职责互不重叠：

| # | 文件 | 作用 | 由谁读取 |
| --- | --- | --- | --- |
| (1) | 工程顶层 `CMakeLists.txt` | `find_package(encos_driver)`，并对每个插件目录调用一次 `add_emd_plugin(...)` | 你执行 `cmake -S . -B build` 时 |
| (2) | 插件目录 `plugin.cmake` | 清单：类型名、动态目标名 | `add_emd_plugin` 内部 `include()` |
| (3) | 插件目录 `CMakeLists.txt` | 定义实际目标：`add_library(... SHARED)` 并链接驱动 | `add_emd_plugin` 内部 `add_subdirectory()` |

目录结构：

```
my_sensor_plugin/
├── CMakeLists.txt                    # (1)
└── plugins/device/mySensor/
    ├── plugin.cmake                  # (2)
    ├── CMakeLists.txt                # (3)
    ├── my_sensor.h
    ├── my_sensor.cc
    └── plugin_entry.cc
```

### (1) 工程顶层 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.18)
project(my_sensor_plugin)
set(CMAKE_CXX_STANDARD 17)

find_package(encos_driver REQUIRED)     # 同时解析 spdlog / fmt

# 每个插件目录调用一次；第二个参数是 (2)(3) 所在目录
add_emd_plugin(mySensor "${CMAKE_CURRENT_SOURCE_DIR}/plugins/device/mySensor" DEVICE)
```

无需自行 `add_subdirectory` 或 `install()`，`add_emd_plugin` 会完成目标校验、输出命名与安装。

### (2) `plugin.cmake`（清单）

安装消费模式必需的项：

```cmake
function(my_sensor_check out_available out_reason)
    set(${out_available} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
endfunction()

set(ENCOS_PLUGIN_CHECK_FUNCTION my_sensor_check)    # 必填
set(ENCOS_PLUGIN_DEVICE_TYPE "MySensor")            # 必填（adapter 用 ENCOS_PLUGIN_ADAPTER_TYPE）
set(ENCOS_PLUGIN_DYNAMIC_TARGET "MySensorPlugin")   # 必填：与 (3) 的 add_library 目标名一致
set(ENCOS_PLUGIN_VISIBLE ON)                        # 可选，默认 ON
```

- 静态注册表字段（`ENCOS_PLUGIN_STATIC_TARGET` / `_STATIC_HEADER` / `_STATIC_FACTORY`）**只有源码树
  模式需要**，安装消费模式可省略；
- (2) 在 `add_emd_plugin` 的函数作用域内被 `include()`，随后调用 (3) 的
  `add_subdirectory()`；(3) 可读取该作用域中的清单字段，其目标名必须与
  `ENCOS_PLUGIN_DYNAMIC_TARGET` 一致。

### (3) 插件目录 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.18)
project(my_sensor)

add_library(MySensorPlugin SHARED          # 必须等于 (2) 的 ENCOS_PLUGIN_DYNAMIC_TARGET
    plugin_entry.cc
    my_sensor.cc
)
encos_configure_plugin_library(MySensorPlugin)            # 导出宏 + PIC
target_link_libraries(MySensorPlugin PRIVATE Encos::encos_driver)
target_include_directories(MySensorPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

### 构建与安装

```bash
cmake -S . -B build
cmake --build build -j
```

`add_emd_plugin` 会把输出名设为 `lib<Type>Device.so`（适配器为 `lib<Type>Plugin.so`）并放在
`build/plugins/` 下；默认同时安装到驱动插件目录，`-DENCOS_ENABLE_INSTALL=OFF` 可关闭安装。
将 `encos::SetPluginPath()` 指向该构建的插件目录，或安装到宿主默认插件目录后，
`GetAvailableDeviceTypes()` 才能发现该类型。Windows 对应文件名为 `<Type>Device.dll` /
`<Type>Plugin.dll`。

## 8. 约束与常见问题

- **JSON 依赖**：`nlohmann/json` 以单头文件随仓库提供
  （`include/external/json.hpp`，tag `v3.11.3`）。插件与消费方都用
  `#include "external/json.hpp"` 引用，不需要 `find_package` 或系统包。
- **ABI**：描述符 `abi_version` 不匹配会被拒绝加载；宿主与插件还必须使用兼容的 C++ ABI 和同一版本的公开头。
- **插件名唯一**：插件名与类型名跨类别全局唯一，重名会使配置阶段报错。
- **测试插件**：`plugins/device/testDevice` 与 `plugins/device/testClaimDevice` 仅在
  `ENCOS_BUILD_TESTS=ON` 时可用（由检查函数门控）。同时开启安装时它们会进入安装/DEB
  产物；发布构建应关闭测试，或显式排除这两个插件。
- **wasm**：强制静态模式，设备插件经静态注册表提供；适配器插件不受影响。
