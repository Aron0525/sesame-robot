# yinpin 遗留网关测试

这些测试来自与当前 `main` 无共同祖先的 `yinpin` 历史，针对
`firmware-work/Sesame_Robot_V3_IDF` 和已移除的播放统计、流式下行、本地
PCM 测试及实验室身份接口。

它们作为历史兼容性证据保留在此目录，不属于当前正式 Gateway 的
`gateway/tests/` 回归集。当前正式组合是 `gateway/` 与
`firmware/esp32_voice_idf/`；若需要恢复旧接口，请从
`backup/yinpin-before-v1.6.2-20260824` 分支创建专门的兼容性工作，而不要
把旧协议混入 v1.6.2 的活动测试。
