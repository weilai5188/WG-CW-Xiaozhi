WG-CW v2.2.6 / feature/wg-cw-v226
CW 实时译码屏幕覆盖层 V1（GPIO39 + CW隔离/600Hz发声版）
===========================================================

功能
- GPIO39 长按 2 秒：现有程序切换 CW / XiaoZhi，不修改按键逻辑。
- 进入 CW：在现有 LVGL 屏幕顶部叠加一整屏 240x240 白底英文实时译码面板（不删除原屏幕控件）。
- GPIO17 接 GND：沿用已验证的声音处理，同时将 KEY 状态显示 DOWN。
- GPIO17 断开：沿用已验证的声音处理，显示 UP，加入当前点或划。
- 静默 350ms：译码一个字母；静默 900ms：插入单词间隔。
- 退出 CW：删除 CW 叠加页面，恢复原来小智表情画面。
- 此次不碰 GPIO40 LCD DC、GPIO38、应用状态机、音频服务、WiFi、原表情库。

V1 译码规则是固定时长阈值（并非自适应速度）
按下 <=180ms => 点 .；>180ms => 划 -
松开后 >=350ms => 字符结束；>=900ms => 单词结束
支持 A-Z, 0-9 和部分标点；不认识的组合显示 ?。
本版使用 LVGL 默认英文字库，避免中文字体不齐引起白屏或缺字。

前置条件
- 主分支 feature/wg-cw-v226
- GPIO39 单键切换已经上板验证。
- 已应用此前 CW 监听隔离/600Hz 发声 V3 补丁（SetWgCwMode、SetWgCwKey）。
- 当前源码中 GPIO38 不再参与按键输入。

安装步骤 (Windows Git Bash)
1. 把 ZIP 中 WG-CW-v226-LiveDecode-UI 整个文件夹解压到 D:\Android\WG-CW-Xiaozhi-226\
2. 运行：

cd /d/Android/WG-CW-Xiaozhi-226
python WG-CW-v226-LiveDecode-UI/apply_cw_live_ui.py --check
python WG-CW-v226-LiveDecode-UI/apply_cw_live_ui.py
git diff --check
git status --short

只预期新增这两个文件：
main/boards/bread-compact-wifi-lcd/wg_cw_decoder.h
main/boards/bread-compact-wifi-lcd/wg_cw_live_ui.h
及修改：
main/boards/bread-compact-wifi-lcd/cw_mode_controller.h

3. 检查无误后提交：
git add main/boards/bread-compact-wifi-lcd/cw_mode_controller.h main/boards/bread-compact-wifi-lcd/wg_cw_decoder.h main/boards/bread-compact-wifi-lcd/wg_cw_live_ui.h
git commit -m "feat: CW live decode 240x240 overlay"
git push origin feature/wg-cw-v226

4. GitHub Actions 绿灯后再烧录测试，不要在编译结果不明时刷机。

回滚（未修改过补丁文件的情况下）：
python WG-CW-v226-LiveDecode-UI/apply_cw_live_ui.py --rollback

回滚只还原本次补丁控制器和新增的两个文件，不还原你之前音频隔离补丁。
在提交后若要直接回退，请在干净工作树中使用 git revert 该次提交。

测试
1. AI 模式唤醒和显示正常。
2. GPIO39 长按切 CW；屏幕显示 CW LIVE DECODE。
3. GPIO17 短按几次，看 MORSE、KEY 和 LAST。
4. 短点 .，空闲 >=350ms 译 E；长划 -，空闲 >=350ms 译 T。
5. 发 .- ，空闲 >=350ms 译 A。
6. GPIO39 长按退出，恢复原小智机器人。

注意
- 这个版本的显示覆盖层依赖 LVGL 9.x 标准接口以及原版 DisplayLockGuard。
- 仅做 Python 检查与基于旧控制器的模拟应用/回滚测试，尚未跑 ESP-IDF 云编译/真机。
- 当前仓库 LCD 显示文件可能有之前自定义表情的修改，所以采用叠加页面避免覆盖旧文件。
- 如 --check 拒绝，请不要硬改；把报错和控制器文件提供以便适配实际版本。
- 之前 git diff --stat 出现的 README.md 删除不是本补丁产生的，不要顺带提交。
