# 威哥 CW 小智 v2.2.6 大表情补丁

适用 ESP32-S3、bread-compact-wifi-lcd、ST7789 240×240、默认消息布局。基线为官方 v2.2.6（`49ac8a6`），并在威哥的 `feature/wg-cw-v226`（`2fa309a`，已含 GPIO17 及 GPIO38＋GPIO39 组合键）副本上验证了应用和回滚。

## 安装

用资源管理器把 ZIP 里的整个 `WG-CW-v226-BigFaces` 文件夹解到 `D:\Android\WG-CW-Xiaozhi-226`。打开 Git Bash，逐条运行。检查通过后再应用。

```bash
cd /d/Android/WG-CW-Xiaozhi-226
git branch --show-current
python WG-CW-v226-BigFaces/apply_bigfaces.py --check
python WG-CW-v226-BigFaces/apply_bigfaces.py
git diff --check
git status --short
```

应用和回滚都先检查完整补丁。遇到冲突出错会保留原文件，重复运行会报告当前状态。脚本接受 `--repo <目录>`，也可以从仓库外应用。Linux LF 和 Windows CRLF 都已验证。三项源码变更如下。

| 文件 | 用途 |
|---|---|
| `main/display/lcd_display.cc` | 居中显示与状态映射，保留原状态栏、通知、字幕和图片预览 |
| `main/display/lcd_display.h` | 仅为指定板型、屏幕与默认消息布局启用 |
| `main/display/wg_cw_faces.h` | 12 张 LVGL I8 静态图与别名映射，图像数据约 392 KiB |

## 构建

确认分支是 `feature/wg-cw-v226`。把这三项做成单独提交，方便以后只撤销表情。

```bash
git add -- main/display/lcd_display.cc main/display/lcd_display.h main/display/wg_cw_faces.h
git diff --cached --check
git diff --cached --stat
git commit -m "Add WG CW 180px expressions for Xiaozhi v2.2.6"
```

如还有预先暂存的文件，先检查 `git diff --cached --stat`，确保这次提交的范围正确。解压目录和 `.bak` 文件可留在本地。

用你这个仓库已经配置好的 SSH 方式推送。

```bash
GIT_CONFIG_GLOBAL=/dev/null \
GIT_SSH_COMMAND='ssh -i /d/Android/.ssh/id_ed25519 -o IdentitiesOnly=yes -o UserKnownHostsFile=/d/Android/.ssh/known_hosts' \
git push origin feature/wg-cw-v226
```

分支推送会触发 GitHub Actions 的 **Build WG CW v2.2.6 Baseline**。现有流程使用 ESP-IDF v5.5.2，调用 `scripts/release.py bread-compact-wifi-lcd --name bread-compact-wifi-lcd-240x240`，再发布合并固件。按**这次表情提交**找成功的任务，下载现有名称 **WG-CW-v226-Original-240x240** 下的 `merged-binary.bin`。沿用已能进入配网的 v2.2.6 240×240 合并包刷机和重启方法。ZIP 内提供源码和图片。

## 表情来源和状态

`art/` 中的 12 张独立 PNG 来自对话里的同一张设计预览，保留机器人和状态标记，去掉展示文字。源码中的 I8 图可直接交给 LVGL 9.5 的 `lv_image_set_src()`。图片画布是 180×180，放在 240×240 屏幕的中央，状态栏和字幕继续使用原组件。

| 表情 | 触发来源 |
|---|---|
| 待机 | 启动画面、空闲状态 |
| 聆听 | 小智进入 Listening |
| 说话 | 小智显示说话状态，后续服务器表情可以替换 |
| 思考 | 对话通道连接中，或服务器 `thinking` / `confused` |
| 开心 | `happy`、`laughing` 等 |
| 眨眼 | `winking`、`silly` 等 |
| 惊讶 | `surprised`、`shocked` 等 |
| 睡觉 | `sleepy` |
| 难过 | `sad`、`crying` |
| 生气 | `angry` |
| 已连接 | 状态栏读到 Wi-Fi 连接后，空闲画面短暂显示 |
| 未连接 | Wi-Fi 配网或空闲时检测到断网 |

小智 v2.2.6 的默认资源分区仍由原构建流程打包。其字体、主题、原始表情和提示图标仍可按原路径加载。已覆盖的状态由 LCD 优先选择固件内置图，其他名称走原资源和图标处理。补丁只读取既有设备状态与网络图标，GPIO17、组合键、麦克风、音频、网络和板型配置均留在原分支的实现中。

## 回滚

表情改动还在工作区时，从原仓库根目录执行以下命令。若三项已经暂存，先用 `git restore --staged --` 加上这三个显示路径取消暂存，再用脚本撤回工作区补丁。

```bash
python WG-CW-v226-BigFaces/apply_bigfaces.py --rollback --check
python WG-CW-v226-BigFaces/apply_bigfaces.py --rollback
git status --short
```

如果已经提交，用 `git revert <表情提交哈希>` 撤销单独的表情提交，然后推送触发重新打包。已经刷过设备时，可用先前验证能启动的 240×240 合并包恢复屏幕表现。

## 验证范围

- 检查了官方标签的 LCD、应用状态、资源分区加载、CMake 和 release.py，以及威哥分支实际使用的板型变体和 Actions 配置。
- 在实际分支副本上应用并回滚。板型目录、CW 控制器和现有工作流文件的 SHA-256 一致。通过 LF、CRLF、重复调用及冲突时拒绝写入的测试。
- 使用桌面 LVGL 9.5.0 解码并渲染 12 张不同的图，验证 180×180 尺寸、坐标 (30,30) 与透明边角。ESP-IDF 固件构建、分区余量和实体板运行尚未验证。

[官方 v2.2.6 源码](https://github.com/78/xiaozhi-esp32/tree/v2.2.6) · [威哥的 v2.2.6 分支](https://github.com/weilai5188/WG-CW-Xiaozhi/tree/feature/wg-cw-v226)
