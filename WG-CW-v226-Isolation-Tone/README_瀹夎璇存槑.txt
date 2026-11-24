威哥 CW 报务训练机 — v2.2.6 / feature/wg-cw-v226 / GPIO39 + CW 监听隔离与 600Hz 码音
===============================================================
针对已推送提交 ef1c0ca 的 GPIO39 单键固件。

【改动范围】
  main/application.h
  main/application.cc
  main/audio/audio_service.h
  main/audio/audio_service.cc
  main/boards/bread-compact-wifi-lcd/cw_mode_controller.h

【功能】
1. GPIO39 长按 2 秒进入/退出 CW，GPIO40（LCD DC）不变，GPIO38 不使用。
2. 进入 CW：关闭本地 WakeNet/VoiceProcessing、清空未发送音频包、关闭 AI 通道，拦截新唤醒、启动聆听、异步回调的重入。
3. 退出 CW：重新启用唤醒词检测和原有小智工作流。
4. GPIO17 接 GND 后：通过小智原有 AudioOutputTask/AudioCodec->OutputData 输出约 600Hz 正弦码音；松开后停止。10ms 输出块会带来少量延迟。
5. 不新增独立 I2S 驱动，不对 GPIO7/15/16/40 改配置；原屏幕/表情文件不动。

【安装前】
先确认当前分支：
  git branch --show-current
  git log -1 --oneline
  git status --short
确认代码基于 ef1c0ca。特别是此前表情补丁如果在本地尚未提交，不要误一起提交。
建议先 git tag backup-before-cw-tone-20261124 ef1c0ca

【解压路径】
把整个 WG-CW-v226-Isolation-Tone 文件夹放到 D:\Android\WG-CW-Xiaozhi-226 目录下。

【Git Bash 命令】
cd /d/Android/WG-CW-Xiaozhi-226
python WG-CW-v226-Isolation-Tone/apply_cw_audio.py --check
python WG-CW-v226-Isolation-Tone/apply_cw_audio.py
git diff --check
git diff --stat

【非常重要：分两步验收】
1. 云编译通过之后再烧录，不要在编译错误时随意删原程序代码。
2. 刷机后进入 AI：说“你好小智”可以唤醒。
3. GPIO39 长按 2s：显示 CW MODE，串口期望 WG_CW: ISOLATION=ON wake=OFF...
4. CW 模式说“你好小智”：不应唤醒 / 发起 AI 会话。
5. GPIO17 接 GND 0.5s：串口 KEY DOWN，功放输出 600Hz，松开 KEY UP 且声音结束。
6. GPIO39 再长按 2s：串口 ISOLATION=OFF wake=ON，再次唤醒小智。

【检查串口】
COM11、115200，注意检查：WG_CW / AfeWakeWord / AudioService 错误，以及重启/死机。

【撤销补丁】
尚未修改源码时：无需撤销。
已经应用但未手动编辑原文件时：
python WG-CW-v226-Isolation-Tone/apply_cw_audio.py --rollback

回滚程序对比备份和补丁后 SHA256，发现你在应用后又手动改了代码会拒绝覆盖，避免误删你的修改。
备份保存于 .wg_cw_isolation_tone_backup 目录；成功撤销后可自行删除这个备份目录。

【验证边界】
本补丁是根据上游 v2.2.6 application.cc、audio_service.cc/.h、用户提供的控制器内容设计的。
由于无法直接读取你本地完整工作树，因此脚本对每个源码锚点做严格检查，任何不匹配将报错并不写入文件。
目前仅进行了补丁脚本的语法和模拟源码转换测试；尚未运行 ESP-IDF 5.5.2 编译和硬件音频时序测量。
即使 --check 通过，也不能代表固件一定编译/运行通过。请以 Github Actions 编译和真机结果为准。

【潜在行为】
当前版本采用 10ms PCM 帧，GPIO17 输入 30ms 消抖，因此短点最小大约 30ms；高速 CW 时后续可把消抖降到 5~10ms。
码音音量固定 PCM 峰值约 5000（16bit），真实扬声器响度仍受原来数字音量与功放输出控制影响。
监听关闭仅指语音识别/上传处理链路关闭，底层 I2S 麦克风仍可能维持驱动初始化，不等同断开麦克风物理供电。
