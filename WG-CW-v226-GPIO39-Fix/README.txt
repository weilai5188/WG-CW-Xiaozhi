WG-CW v2.2.6 / GPIO39 单键模式切换修复（安全补丁）
================================================
硬件事实：
- GPIO40 是 ST7789 LCD DC，不允许作为按钮输入。
- GPIO39 对应目前仍接通的实体音量减键，低电平有效。
- GPIO38 不再用于切换，不初始化、不配置、不扫描。
- GPIO17 仍是 CW 直键：一端 GPIO17，另一端 GND。

本补丁只修改：main/boards/bread-compact-wifi-lcd/cw_mode_controller.h
原有状态通知、串口 KEY DOWN/KEY UP、CW 流程均保持。
目前没有添加监听音！MAX98357A 由原小智 I2S 驱动，不能直接往 GPIO7 送 LEDC 方波。
要做 GPIO17 按下立刻发音、松开立刻停音，需要明确接入当前小智音频服务的 PCM 生产与输出接口。

把 ZIP 中文件夹 WG-CW-v226-GPIO39-Fix 放到工程根目录；运行：
  cd /d/Android/WG-CW-Xiaozhi-226
  python WG-CW-v226-GPIO39-Fix/apply_gpio39.py --check
  python WG-CW-v226-GPIO39-Fix/apply_gpio39.py
  git diff --check
  git diff -- main/boards/bread-compact-wifi-lcd/cw_mode_controller.h

确认无误再提交：
  git add main/boards/bread-compact-wifi-lcd/cw_mode_controller.h
  git commit -m "Use GPIO39 long press for CW mode switch"
  GIT_CONFIG_GLOBAL=/dev/null GIT_SSH_COMMAND='ssh -i /d/Android/.ssh/id_ed25519 -o IdentitiesOnly=yes -o UserKnownHostsFile=/d/Android/.ssh/known_hosts' git push origin feature/wg-cw-v226

恢复（未提交前）：
  python WG-CW-v226-GPIO39-Fix/apply_gpio39.py --rollback

真实硬件验证：
  1. 单独长按 GPIO39（音量减）约 2 秒 -> 显示 CW MODE。
  2. 放开 GPIO39；GPIO17 对 GND 短接 0.5 秒 -> 串口 KEY DOWN/UP。
  3. 单独长按 GPIO39 约 2 秒 -> XIAOZHI MODE。
  4. 保持其他麦克风、音频、网络、屏幕操作正常。

说明：脚本能检查代码模式并回滚，不代表已经云编译/真机测试。
