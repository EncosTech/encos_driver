# EncosDriver

EncosDriver 是用于控制 Encos 电机和其他设备的 C++17 库。通过插件支持 EtherCAT、CAN、以太网网关、USB 串口和 WebSocket 中继。

## 安装

### 预编译包

已配置内网镜像时，可直接用 `apt` 或 `dnf` 安装 `libencosdriver`。

#### 内网 Ubuntu（APT）

Ubuntu 24.04 使用 `noble`，22.04 将下方的 `noble` 改为 `jammy`。

```bash
sudo install -d -m 0755 /etc/apt/keyrings
sudo curl -fsSL https://git.in.encos.cn/api/packages/packages/debian/repository.key \
  -o /etc/apt/keyrings/encos.asc
echo 'deb [signed-by=/etc/apt/keyrings/encos.asc] https://git.in.encos.cn/api/packages/packages/debian noble main' \
  | sudo tee /etc/apt/sources.list.d/encos-driver.list
sudo apt update
sudo apt install libencosdriver
```

#### 内网 AlmaLinux 8/9（DNF）

```bash
sudo curl -fsSL https://git.in.encos.cn/api/packages/packages/rpm/portable.repo \
  -o /etc/yum.repos.d/encos-portable.repo
sudo dnf install libencosdriver
```

#### 公网下载

按系统和 CPU 架构（`amd64` 或 `arm64`）选择该版本提供的安装包。 [GitHub Releases](https://github.com/EncosTech/encos_driver/releases)

- Ubuntu 下载 `.deb` 后运行 `sudo apt install ./文件名.deb`。
- AlmaLinux 8/9 下载 `.rpm` 后运行 `sudo dnf install ./文件名.rpm`。
- 如附件提供 `.tar.gz`，可解压到自选目录。 [Linux 可移植包说明](docs/portable-linux.md#支持边界)

### 从源码安装

在 Linux 上从源码安装前，先准备[构建依赖](docs/build.md#依赖)。

```bash
cmake -S . -B build -DENCOS_ENABLE_INSTALL=ON
cmake --build build --parallel
sudo cmake --install build
```

## 快速开始

在应用的 CMake 项目中链接驱动。

```cmake
find_package(encos_driver REQUIRED)
target_link_libraries(your_target PRIVATE Encos::encos_driver)
```

以下示例扫描 SocketCAN 接口 `can0` 上的电机。

```cpp
#include <iostream>
#include <encos/encos_driver.h>

int main() {
    auto* adapter = encos::MakeAdapter("Can", "can0");
    auto* bus = adapter->GetBus(0);
    std::cout << "发现电机数：" << bus->ScanMotors().size() << '\n';
    return encos::DeleteAdapter(adapter) ? 0 : 1;
}
```

## 文档

- Windows、WASM、静态构建、测试和打包见[构建说明](docs/build.md)。
- 适配器选择、电机控制和 Linux 运行权限见[使用指南](docs/using_guide.md)。
- 扩展配置和插件开发见[插件指南](docs/plugin.md)。
- 日志配置与故障排查见[日志指南](docs/logging.md)。
- Node.js 原生包见[安装与使用说明](npm-native/README.md)。

## 许可证

Ethercat 插件采用 [GPL-3.0-or-later](COPYING)，其余项目自有代码采用 [MIT](LICENSE)。第三方组件保留各自的许可证。
