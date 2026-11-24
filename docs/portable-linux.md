# Linux 可移植发布

Linux 发布以 glibc 2.28 为最低基线，分别原生构建 amd64、arm64。每个架构只编译一次发布负载，将 SDK 和全部八种适配器插件放入单一 `libencosdriver` 包，生成 DEB、RPM 和 tar.gz。测试和发布阶段下载候选文件，不重建、不重命名包；所有文件由 `SHA256SUMS-<arch>` 校验，包内 `PAYLOAD.sha256` 校验实际安装内容。

fmt、spdlog、zstd、libcap 静态链接进产物。ELF 审计限制 GLIBC ≤ 2.28、GLIBCXX ≤ 3.4.25、CXXABI ≤ 1.3.11，不随包替换系统 glibc 或 libstdc++。带 capability 的辅助进程独立链接，不依赖项目共享库，避免安全加载模式下的相对路径失效。Ethernet 的 XDP/BPF/ELF/zlib 等用户态依赖静态链接，IGH 的用户态库以私有 SONAME 随包携带。

## CI 流程

`.gitea/workflows/build-native.yml` 将源码验证、候选构建、包验证和发布拆开：

1. 源码检查保留 Ubuntu 22.04/24.04、amd64/arm64、静态模式、IGH、配置负向检查，以及 format-check、clang-tidy。测试构建保留 testDevice/testClaimDevice；独立的安装检查构建不含测试插件，也不作为发布来源。
2. 候选构建使用 `git.in.encos.cn/container/ubuntu:emd-ci-portable-glibc2.28-20261027-v1-<arch>` 基线镜像。`scripts/portable/build.sh OUTPUT` 编译并整理负载，`package.sh OUTPUT` 从同一 `stage/usr` 生成三种格式的单包、一份完整 ELF 审计和运行探针。
3. Ubuntu 22.04、24.04 的两种架构各自安装同一份 DEB，并分别验证可重定位 tar.gz。AlmaLinux 8、9 的两种架构安装同一份 RPM，并分别验证 tar.gz。每个任务安装单包，校验全部负载、依赖版本信息，并确认未夹带第三方源码归档，加载全部插件并运行 Fake 电机探针，最后卸载并逐项确认包拥有的文件和符号链接均已清除。这些任务只编译下游 SDK 消费示例，不编译 EncosDriver。安装验证和 SDK 消费失败都会阻止发布。带 capability 的 CAN/EtherCAT/Ethernet broker 还会被复制到独立目录，以普通用户启动，验证安全加载模式不依赖相邻驱动共享库。
4. 发布任务必须等待全部源码检查、系统包测试和 Windows 原生 npm 包测试通过。仅 main 分支 push 且 `scripts/check-version-changed.sh` 判定版本变化时上传；PR 和手动 workflow_dispatch 均不发布。

原生 npm 后端与 Linux 候选在同一基线构建中生成，复用驱动负载，并额外打包 `.node` 和共享的 JavaScript 接口。上述发行版矩阵同时对真实 npm 归档执行 Node 22/24 离线安装测试；Windows 原生 runner 构建包含 Ethercat 的第三个平台包。发布步骤先验证系统包和三个 npm 平台包的全部清单，再上传经过测试的原始文件。接口、权限函数及独立平台包说明见 [Node 原生后端](../npm-native/README.md)。

CI 镜像统一在 `/home/bismarck/emd_container` 中维护。Ubuntu 使用 `emd-ci-<22.04|24.04>-20261027-v1-<arch>`，AlmaLinux 使用 `emd-ci-almalinux-<8|9>-20261027-v1-<arch>`（均位于 `git.in.encos.cn/container/ubuntu`）。镜像预装编译器、CMake、Git、Python、权限与压缩工具，以及固定版本的 Node 22/24；Node 使用镜像中预装的版本，网卡配置所需的 sd-bus 开发包和 Polkit 运行包在 CI 中按需补齐。Ubuntu 额外预装 `/opt/encos-igh` 和 npm 锁文件对应的依赖缓存，IGH 只在相应源码测试中显式启用。JavaScript 依赖通过 `scripts/ci/link-js-dependencies.sh` 校验锁文件并离线链接，锁文件更新时必须重建镜像，不回退到联网安装。

候选文件由 `scripts/ci/download-artifact.py` 通过 Gitea API 下载，限定当前 workflow run、artifact 名称和生产提交 SHA；下载后再次校验清单。每个架构的候选 artifact 名称包含提交 SHA，文件包括：

- `libencosdriver_<version>_linux-glibc2.28_<arch>.deb`
- 对应的 `.rpm` 与 `.tar.gz`（共三个安装包）
- `verification-<arch>.tar.gz`：预编译探针及下游 CMake 示例
- `elf-audit-<arch>.json`
- `SHA256SUMS-<arch>`：覆盖上述五个文件（含清单共六个文件）

包名不包含 Ubuntu 版本：jammy 与 noble 仓库索引引用字节完全相同的 DEB。发布脚本先验证两个架构的完整清单与版本，再上传。RPM 使用 `rpm/portable/upload`；generic 仓库 `encos-driver-linux/<version>` 保留包、探针、审计和校验清单的原件。API 契约见 Gitea 官方 [RPM](https://docs.gitea.com/usage/packages/rpm/) 与 [Generic](https://docs.gitea.com/usage/packages/generic/) 文档。重跑遇到已存在文件会失败，不会自动删除或覆盖生产包；跨仓库上传不是事务，需核对已上传文件后处理失败重跑。

GitHub 发布由 `publish-github-release` 工作流手动触发。脚本按所选源代码的版本号从 `encos-driver-linux/<version>` 下载 `amd64`、`arm64` 的 DEB、RPM 和 tar.gz，并使用两个架构的校验清单核对六个包。GitHub Release 保留原始包名，并附带涵盖六个包的 `SHA256SUMS`。所有包校验通过后才更新 GitHub 源码快照。

工作流默认同步 `example_soem`、`example_igh` 和 `example_igh_dc` 的源码快照。设置仓库变量 `OPTIONAL_BRANCHES` 可指定其他快照分支，工作流会同时同步 `example_igh_dc`。

## 本地复验

将两个架构的候选分别放入 `candidate/amd64`、`candidate/arm64`。只验证并打印发布计划：

```bash
python3 scripts/portable/publish.py candidate
```

在对应架构的隔离目标系统中测试：

```bash
bash scripts/portable/test-package.sh candidate/amd64 deb
# RPM 系统使用 rpm；tar.gz 重定位验证使用 tgz。
```

真实上传需显式 `--publish`，凭据仅从 `USER_PASSWORD` 环境变量读取。CI 私有子模块和 IGH 检出使用临时 `GIT_ASKPASS`，仓库 checkout 关闭凭据持久化；密码不写入 URL、Git 配置或命令行参数。日志不输出凭据和 artifact token。

第三方源码在项目的 GitHub 仓库中单独提供，DEB、RPM 和 TGZ 不再携带 IGH、Ethernet 依赖的源码归档。包内 `share/doc/libencosdriver/dependency-versions.txt` 和 `source-sha256sums.txt` 保留构建所用依赖版本与源码归档校验信息。IGH 用户态库采用 LGPL-2.1-only；Ethernet 的 libxdp 内嵌 BPF 程序采用 GPL-2.0-only，对应许可证随包提供。同一包还在 `share/doc/libencosdriver/relink/ethernet/` 目录提供对象文件、静态依赖和重链接脚本，按其中 README 可替换依赖后重新链接插件或 broker。

## 支持边界

`libencosdriver` 包含 SDK、Can、Ethercat、UsbSerial、Slcan、Fake、RelayWs、EthercatIGH、Ethernet，不再提供独立插件包。Fake 默认不出现在适配器枚举结果中，但安装探针会创建 Fake 电机并运行 PVT 与参数查询。TGZ 解压到一个前缀即可使用全部插件。测试设备插件不发布。

Ethernet 的桌面与 SSH 网卡配置使用系统 sd-bus 和 Polkit。DEB 依赖 `libsystemd0`、`polkitd`，RPM 依赖 `systemd-libs`、`polkit`。NetworkManager 作为运行时可选服务，包安装不依赖它。 使用 tar.gz 时，系统需提供相同的运行库和终端认证代理。
DEB 的其他可选工具依赖为 `Recommends: libcap2-bin`、`Suggests: pkexec, iproute2`，RPM 为 `Recommends: libcap`、`Suggests: iproute`。包元数据不声明 IGH 依赖。

统一包携带 IGH 1.6.9 用户态库（提交 `a5eed04a9827007cdaa7d4dc8fd3ec7dddd917b6`，ioctl ABI magic 37），不包含内核模块，也不配置主站服务。宿主机必须自行安装、配置并加载匹配 ioctl ABI 的 IGH EtherCAT 主站和网卡驱动，且提供可访问的主站设备。仅安装该包或成功加载插件并不表示主站可用。

统一包还携带 Ethernet 插件、`EthernetXdpBrokerExecutable`、`ethernet_xdp.bpf.o` 和权限配置脚本。构建依赖固定为 xdp-tools 1.4.3、libbpf 1.4.7、elfutils 0.191、zlib 1.3.1；无需在目标机另装这些开发包。运行仍要求宿主内核和网卡支持 AF_XDP generic/SKB、copy 模式及 BPF 加载，足够的 memlock 限额，以及 broker 的 `cap_net_raw,cap_net_admin,cap_bpf` 权限。安装脚本尝试设置权限；受容器 capability 边界或文件系统限制时，需在真实运行主机上配置。glibc 基线不保证各发行版默认内核均具备这些能力。具体接口与权限要求见 [Ethernet 插件说明](../plugins/adapter/ethernet/README.md)。

glibc 2.28 基线不代表支持 musl/Alpine，也不代表硬件已验证。系统仍需兼容的 Linux 内核、glibc、libstdc++ 和 libgcc；实际通信取决于网卡、串口、CAN 设备及权限。CI 使用 Fake 等无硬件探针验证加载和 SDK；容器共享宿主内核，未验证 IGH、AF_XDP/BPF 或其他内核通信，不能替代真机测试。不同架构有各自二进制，不能互换。插件及直接交换 C++ 标准库对象的接口仍要求兼容的 C++ ABI；跨 GCC 8/新版工具链使用路径时，应使用 `SetPluginPath(std::string)` 等字符串入口，避免跨库传递 `platform/os.h` 的 `std::filesystem::path`。
