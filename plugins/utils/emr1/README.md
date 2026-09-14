# 跨插件 EMR1 协议

`relay_frame.h/.cc` 是 Ethernet 与 RelayWs 共用的唯一 EMR1 声明及编解码实现。
`test/relay_frame_test.cc` 覆盖帧格式、分帧和非法输入，不依赖 WebSocket 或 Ethernet 后端。

协议本身支持单帧最多255条记录，超出时按现有规则拆帧。Ethernet的80条记录上限、CAN0–7和帧标志约束由其`protocol/gateway_payload.cc`执行，不施加到RelayWs。

源码归属`plugins/utils/emr1`，实现仍编入`EncosMotorDriverBase`，沿用`ENCOS_BASE_API`导出，保持已有DLL依赖和函数ABI。
新内部引用使用`utils/emr1/relay_frame.h`；旧公开头`relay/relay_frame.h`保留转发入口。安装包同时提供规范头和兼容头。

全工程插件测试会自动包含本目录的测试；Ethernet独立测试也包含它们，可在Windows上验证。
