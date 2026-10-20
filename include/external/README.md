# include/external

本目录存放随仓库发布的外部依赖头文件。这些文件**不是**本项目自有代码，各自的许可见同目录或本节说明。

## nlohmann/json

- 文件：`json.hpp`、`json_fwd.hpp`（上游 `single_include/` 的单头文件版本，逐字节复制，未做本地修改）
- 来源：`https://git.in.encos.cn/bismarckkk/json.git`
- 版本：tag `v3.11.3`（上游 commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`）
- 许可：MIT，见 `LICENSE.MIT`
- 校验值：

  ```
  9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6  json.hpp
  5cefbba751baf5243033fd894c70be9b37103d64b3d3a959e4173197d71137b9  json_fwd.hpp
  ```

`json.hpp` 随公开头一同安装，因此消费者无需额外安装 JSON 包；项目其余部分**不得**通过 `find_package`
或系统包解析该依赖。

### 升级方式

```bash
git clone --depth 1 --branch <新tag> https://git.in.encos.cn/bismarckkk/json.git /tmp/json-upgrade
cp /tmp/json-upgrade/single_include/nlohmann/json.hpp     include/external/json.hpp
cp /tmp/json-upgrade/single_include/nlohmann/json_fwd.hpp include/external/json_fwd.hpp
cp /tmp/json-upgrade/LICENSE.MIT                          include/external/LICENSE.MIT
sha256sum include/external/json.hpp include/external/json_fwd.hpp   # 更新本节校验值
```

升级后需检查 `NLOHMANN_JSON_VERSION_*` 宏与本节记录一致，并重新跑一次全量构建与测试。
