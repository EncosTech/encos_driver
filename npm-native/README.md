# Node 原生后端

本目录实现集合包 `@encos/encos-driver-native`。它通过同版本的 `optionalDependencies` 自动安装当前系统对应的平台包，并统一导出完整原生 API。现有 `@encos/encos-driver` 仍是独立的 WASM 包。

```sh
npm install @encos/encos-driver-native --registry=https://git.in.encos.cn/api/packages/packages/npm/
```

也可以在项目 `.npmrc` 中配置 `@encos:registry=https://git.in.encos.cn/api/packages/packages/npm/`；私有 registry 的认证沿用你的 npm 配置。不要使用 `--omit=optional`。

| npm 包 | 平台 |
| --- | --- |
| `@encos/encos-driver-native-linux-x64-gnu` | Linux x64，glibc ≥ 2.28 |
| `@encos/encos-driver-native-linux-arm64-gnu` | Linux arm64，glibc ≥ 2.28 |
| `@encos/encos-driver-native-win32-x64` | Windows x64 |

Node-API 版本为 8，发布测试覆盖 Node 22、24。保留 WASM 的既有高层接口，并提供完整的内置设备应用接口：Adapter、Bus、Motor、Battery、Imu、Pms、Glove、Fake、状态回调、设备管理和日志工具。自定义 Device 不在本次范围内；接口映射见 [API.md](API.md)。包内包含 `.node`、匹配版本的驱动和插件，不需要预装系统 EncosDriver。Linux 包包含八个适配器，Windows 包包含 Ethercat、Ethernet、UsbSerial、Slcan、Fake、GsUsb。Windows Ethercat 仍需要宿主安装兼容的 Npcap 驱动及运行库。

```ts
import { createEncosRuntime, checkPermissions, requestPermissions }
  from '@encos/encos-driver-native'

const permissions = await checkPermissions()
if (!permissions.authorized) await requestPermissions()
const runtime = await createEncosRuntime()
const adapter = runtime.createFakeAdapter()
adapter.fake.enableAutoCreateMotor()
// 既有 Adapter / Bus / Motor 调用方式保持兼容。
runtime.dispose()
```

`checkPermissions()` 只读取包内辅助程序的 capability；`requestPermissions()` 在需要时通过一次 `pkexec` 执行包内脚本，完成后重新检查。两者返回 `{ required, authorized, missing }`。缺少工具、用户取消或授权失败会给出错误。Windows 返回 `{ required: false, authorized: true, missing: [] }`，不会执行提权。npm 安装阶段没有授权脚本，不会修改 Node 可执行文件权限。升级或重新安装后应重新检查授权。

### 打包与单可执行文件（SEA）

`createEncosRuntime` 默认用 `import.meta.url` 推导包根，并通过 `require()` 加载
`native/encosdriver.node`。打包器或 Node.js SEA 中这两个假设都不成立，需要显式传入：

```ts
import { Module } from 'node:module'
import { createEncosRuntime } from '@encos/encos-driver-native'

const runtime = await createEncosRuntime({
  // 解压/安装后的平台包根目录（包含 native/ 与 runtime/）
  packageRoot: '/path/to/@encos/encos-driver-native-linux-x64-gnu',
  loadAddon(addonPath) {
    const module = new Module(addonPath)
    process.dlopen(module, addonPath)
    return module.exports
  },
})
```

`loadAddon` 必须返回与平台包 `native/encosdriver.node` 导出面一致的对象
（`createRuntime(pluginPath)`）。两个选项都可省略，以保持普通 Node 安装下的原行为。


集合包会检查系统、CPU 架构和 Linux libc，并验证平台包版本与自身一致。当前支持 Linux glibc ≥ 2.28 的 x64/arm64 和 Windows x64；macOS、musl、Windows ARM64 等平台会明确报错。缺失平台包会提示重新安装 optionalDependencies；不自动切换到 WASM。集合包自带完整类型声明，类型检查不依赖某个固定平台包。

Linux Ethernet 插件使用系统的 `libsystemd.so.0`。通过 SSH 使用 NetworkManager 自动配置且需要认证时，系统需提供 `pkttyagent`（Ubuntu 为 `polkitd`，AlmaLinux 为 `polkit`）。

为保持现有同步接口，适配器创建和空闲时的销毁仍同步执行；EtherCAT 启动等待可能阻塞事件循环。原生异步设备操作在后台执行，同一运行时按顺序执行；在后台操作尚未完成时发起同步设备查询会返回忙碌错误，应先等待正在执行的 Promise。不同运行时不能同时拥有同一适配器接口；插件目录是进程级配置，同一进程的运行时必须使用同一目录。对象销毁会立即禁止新操作，正在执行的操作安全结束后释放底层资源。Worker 退出也会清理该环境的运行时。

## 构建与测试

启用 `ENCOS_BUILD_NODE_BINDINGS=ON`，用 `ENCOS_NODE_INCLUDE_DIR` 指定 Node 头文件目录；必须使用动态驱动模式并包含 Fake 插件。Windows 另需 `ENCOS_NODE_IMPORT_LIBRARY`，由 `scripts/windows/build-node-ci.ps1` 从官方 Node 可执行文件生成 MinGW 导入库。

`scripts/node/prepare-node.py` 可用于本地下载固定版本的官方 Node 工具及头文件并验证 SHA256。CI 直接使用镜像中 `/opt/encos-node` 预装的工具；Windows runner 使用 `work/tools` 中预装的版本。TypeScript 使用 `pnpm --dir npm build:ts` 编译；`scripts/node/pack.mjs` 接收 `--dist`、`--runtime`、`--addon`、`--output`、`--platform`、`--arch`，生成平台 npm 归档和校验清单。Linux 打包必须在 glibc 2.28 基线中完成，统一审计 `.node` 和随包共享库。

```sh
node scripts/node/test-package.mjs /path/to/candidate
```

测试在带空格的临时目录中离线安装真实 npm 归档，验证包入口、高层 API、错误处理、事件循环、销毁和 Worker 清理；不依赖全局安装的驱动。`scripts/node/publish.py` 默认只校验三个平台候选，显式 `--publish` 才上传经过测试的原始归档。

CI 镜像预装系统测试工具和 npm 依赖，运行任务时只离线链接依赖，不下载 Node 或执行 `dnf`。CI 复用 Linux 可移植候选构建的驱动产物，分别在 Ubuntu 22.04/24.04、AlmaLinux 8/9 上运行 Node 22/24 包测试；Windows 使用原生 MinGW runner。全部测试通过且 main push 版本变化才发布。容器中的插件加载和 Fake 测试不代表实际 XDP、IGH 或 EtherCAT 通信已经通过验证。

## 集合包构建和发布

```sh
pnpm --dir npm build:ts
node scripts/node/pack-selector.mjs --dist npm/dist --output ../work/node-selector
node scripts/node/test-selector-types.mjs ../work/node-selector
node scripts/node/test-selector.mjs /path/to/platform-candidate ../work/node-selector
```

打包脚本从 `CMakeLists.txt` 读取版本号，生成与平台后端完全一致的命名导出，并复制完整 TypeScript 声明。`npm-native/package.json` 是开发模板，发布归档中的版本及三个 optionalDependencies 都替换为精确发布版本。产物包括 npm 归档和 `selector-manifest.json` 校验清单。

CI 在共享 JS 阶段只构建一次集合包，并把同一归档传递到全部 Linux/Windows、Node 22/24 安装测试。测试使用仅监听回环地址的临时 registry 提供本轮真实平台归档，验证 npm 只下载并安装匹配平台的依赖、完整转出 API、实际 Fake 指令及缺失依赖错误；独立测试确认没有任何平台包时也能使用类型声明。

只有 main push 修改项目版本号且全部检查通过时，CI 才发布原始已测试归档：先发布三个平台包，全部成功后最后发布集合包。发布前统一检查四个包的版本、精确依赖、入口和 SHA256。PR 和手动验证流程不发布；发布脚本默认只校验。
