# 威哥 CW 小智 v2.2.6 动态表情升级

适配 ESP32-S3 N16R8、`bread-compact-wifi-lcd`、ST7789 240×240 和默认消息布局。此包给已经刷成功的 **大表情版 `7b74f69`** 升级，所在分支为 `feature/wg-cw-v226`。

机器人白壳、深蓝面罩和蓝色发光表情来自已确认的设计稿。屏幕使用小智原生浅色主题。表情透明背景下保留浅蓝光影，图像在屏幕中间，状态栏、字幕、通知及配网提示或二维码沿用小智的显示路径。图像预览打开时暂停表情动画，关闭后继续播放。

## 安装

把 ZIP 内整个 `WG-CW-v226-AnimatedFaces` 文件夹解压到 `D:\Android\WG-CW-Xiaozhi-226`。在 Git Bash 中逐行执行，预检通过后应用。

```bash
cd /d/Android/WG-CW-Xiaozhi-226
git branch --show-current
git log -1 --oneline
python WG-CW-v226-AnimatedFaces/apply_animated_faces.py --check
python WG-CW-v226-AnimatedFaces/apply_animated_faces.py
git diff --check
git status --short
```

脚本用 Git 检查补丁上下文，兼容一致的 LF 或 CRLF。冲突时报告并保留工作区，重复应用会提示已经应用。若工程缺少上一版大表情源码，请先恢复到已验证的 `7b74f69` 基线再检查。改动只涉及 `main/display/lcd_display.cc`、`lcd_display.h`、`wg_cw_faces.h`，没有向板型、GPIO17 电键、GPIO38/39 组合键、音频、麦克风或网络代码写入变更。

## 云编译

确认当前分支为 `feature/wg-cw-v226`。单独提交三个显示文件，方便回滚。

```bash
git add -- main/display/lcd_display.cc main/display/lcd_display.h main/display/wg_cw_faces.h
git diff --cached --check
git diff --cached --stat
git commit -m "Animate Xiaozhi expressions on the native light screen"
```

确认暂存列表只有这三项。如果仓库仍用之前的 SSH 443 配置，推送命令如下。

```bash
GIT_CONFIG_GLOBAL=/dev/null \
GIT_SSH_COMMAND='ssh -i /d/Android/.ssh/id_ed25519 -o IdentitiesOnly=yes -o UserKnownHostsFile=/d/Android/.ssh/known_hosts' \
git push origin feature/wg-cw-v226
```

推送会触发 **Build WG CW v2.2.6 Baseline**。它使用 ESP-IDF v5.5.2，打包 `bread-compact-wifi-lcd-240x240`；确认 Actions 任务的提交哈希是本次动画提交，成功后从现有 **WG-CW-v226-Original-240x240** 产物里取 `merged-binary.bin`，沿用此前成功的合并包刷机方法。保留上一版已经真机验证的 BIN 便于恢复。

## 表情与触发

| 表情 | 触发与动作 |
|---|---|
| 待机 | 开机和空闲，约每两秒眨眼 |
| 聆听 | 小智进入聆听状态，带声波图案的机器人轻微浮动 |
| 思考 | `thinking` / `confused` 表情，面罩内的点阵亮度交替 |
| 说话 | 小智进入 Speaking，面罩内音柱亮度交替 |
| 播放 | `playing` / `playback` / `reading` 表情，均衡器闪动 |
| 开心 | 开心、笑等服务器表情，机器人轻微浮动 |
| 感谢 | 爱心、感谢等服务器表情，机器人轻微浮动 |
| 提醒 | 眨眼、惊讶、提醒等服务器表情，机器人轻微浮动 |
| 连接中 | 对话通道连接或下载提示，机器人轻微浮动 |
| 网络 | 配网、断网或刚重新连上，顶部网络图标继续区分实际状态 |
| 错误 | 错误告警或错误类表情，缓慢浮动 |
| 休息 | 睡眠或服务器 `sleepy` 表情，缓慢浮动 |

播放动画需要对应的表情事件。固件原生 TTS 使用说话动画，音柱按设定节奏变化。运行时用一个 LVGL 计时器切换画面，音频电平采集和分析无需变更。16 张编译内置 I8 图约 410 KiB，内容为 12 张主体图和 4 张变化帧，解码沿用 LVGL 原生图像路径。ZIP 的 `preview.gif` 用于电脑预览；固件直接使用内置资源。可编辑透明 PNG 位于 `art/`。若以后要重新生成图像头文件，可安装 Pillow 后执行 `python WG-CW-v226-AnimatedFaces/build_assets.py main/display/wg_cw_faces.h`，正常安装补丁不需要 Pillow。

## 回滚

若尚未提交，在仓库根目录执行：

```bash
python WG-CW-v226-AnimatedFaces/apply_animated_faces.py --rollback --check
python WG-CW-v226-AnimatedFaces/apply_animated_faces.py --rollback
git status --short
```

若三个文件已暂存，回滚前先只对这三项执行 `git restore --staged -- <路径>`。若已经提交，用 `git revert <动画提交哈希>` 撤销该独立提交，并推送触发云编译。已经刷到设备上时，可以刷回上一版已验证的合并 BIN。

## 验证范围

- 在实际分支 `7b74f69` 副本上测试了补丁预检、应用、重复应用和回滚，覆盖 LF 与 CRLF。
- 用桌面 LVGL 9.5.0 对 12 款全部动画阶段进行了 RGB565 渲染检查，确认画面有变化、坐标居中且外部保持透明。
- 当前包为源码升级补丁。新增版本的 ESP-IDF 编译、分区余量、真机帧率和语音并行表现仍待云编译和上板验证。

[对应的分支源码](https://github.com/weilai5188/WG-CW-Xiaozhi/tree/7b74f69) · [LVGL 9.5.0 源码](https://github.com/lvgl/lvgl/tree/v9.5.0)
