# StopWatch GrokBot

把 [GrokBot 头像解剖](https://grokbot.yin.wiki/index.html) 里的 Grok Bot
动画搬到 M5Stack **StopWatch / C152**（ESP32-S3R8 + 466×466 AMOLED）上运行。

![StopWatch GrokBot fullscreen preview](docs/fullscreen-preview.png)

1.6 倍版：将图集中中央 292.5×292.5 视口等比放大至 468×468 framebuffer，
倍率为原始图像的 **1.6 倍**，默认脸部直径从 240 像素增至 384 像素。
使用双线性插值平滑缩放，并移除底部状态名和操作提示。
上图为实际缩放代码生成的圆屏预览。


原版 IDLE 正放/倒放 GIF（未放大）：

![IDLE animation](docs/grokbot-idle.gif)

## 当前效果

- 网页中的 **53 个命名状态已全部加入**：生命周期、反应、morph、产品状态和品牌动画。
- 9 种表情、13 类 morph、28 段 clip 的视觉类型均包含在状态库中。
- 所有状态统一从第 3 帧开始播放。
- 每个状态从第 3 帧顺序播放到末帧，再返回第 3 帧，持续顺序循环。
- 单击触摸屏切换到下一个状态；A/B 键控制上一个/下一个状态。
- 原始图集为 400×400、16 级灰度；全屏输出为 468×468，播放目标 30 FPS。
- 帧间 XOR + zlib 压缩，完整图集约 **8.39 MiB**。
- 直连 CO5300；全屏版的实际帧率需在设备上验证，可从串口查看 `fps=`。

全部状态（原版预览）：

![State gallery](docs/gallery.png)

## 硬件与验证

- 目标：M5Stack StopWatch（C152），ESP32-S3R8，16 MiB Flash，8 MiB PSRAM。
- 屏幕：CO5300 QSPI AMOLED，468×468 framebuffer，可见 466×466 圆形区域。
- 原版验证：ESP Workbench 启动、framebuffer 和状态切换通过；实机显示、灰阶、30 FPS、A/B 状态切换正常。
- 主机验证：缩放插值、RGB565 字节序和内存边界检查通过；53 个状态各顺序循环 3 次，共 19221 个显示帧的顺序、循环重建与状态切换检查通过。尚未进行全屏版实机验证。

## 交互

| 输入 | 动作 |
| --- | --- |
| A / GPIO2 | 下一个状态 |
| B / GPIO1 | 上一个状态 |
| 单击触摸屏 | 下一个状态 |

## 构建与烧录

```sh
git submodule update --init --recursive
. "$HOME/esp/v5.5.3/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Web Burner 需要使用从 `0x0` 烧录的完整镜像，不能直接上传应用分区
`build/stopwatch_grokbot.bin`：

```sh
idf.py merge-bin -o stopwatch_grokbot-full.bin
```

生成后上传 `build/stopwatch_grokbot-full.bin`。该文件已合并 bootloader、
分区表和应用镜像。

本次构建的 1.6 倍顺序循环版完整镜像为 `build/stopwatch_grokbot-1.6x-forward.bin`，
烧录起始地址为 **`0x0`**；对应校验文件为 `.bin.sha256`。
构建信息与验证结果保存在 `build/fullscreen-build-info.json`。

`main/idf_component.yml` 会自动获取 M5PM1、M5IOE1、I2C bus 组件；
`components/M5GFX` 固定到 0.2.35。

启动后串口应出现：

```text
I (...) grokbot: Atlas: 400x400, 53 states, 30 fps
I (...) grokbot: GROKBOT_READY states=53 fps=30
I (...) grokbot: fps=30 state=idle frame=... start=2 decode=...us push=...us
```

## 设备端实现

1. 浏览器按 30 FPS 逐帧渲染 53 个状态。
2. 每帧量化为 4-bit 灰度，帧间做 XOR delta，再用 zlib 压缩。
3. ESP32-S3 使用 ROM 自带 miniz 逐帧解压，经 XOR 还原 4-bit 灰度帧。
4. 使用面板字节序的灰度查找表写入 sprite 的 `rgb565_2Byte` 缓冲区。
5. 使用中央 292.5×292.5 视口，以原始图像的 1.6 倍和双线性插值生成 468×468 RGB565 帧；缓存横向插值行，减少重复计算。
6. 禁用 M5GFX 的整屏 PSRAM 中间帧缓冲，468×468 sprite 直接送入 CO5300。
7. 状态动画采用顺序循环；播放到末帧后回到起始帧，并从完整帧重建 XOR 基准，避免循环衔接时重影。
8. 每个 clip 首帧为完整帧，其余帧使用 XOR delta，保持较小的 Flash 占用。
9. 所有状态从索引 2（第 3 帧）开始；切换状态或循环回起点时，从目标状态第 0 帧完整重建到起始帧，避免 XOR 基准残留造成重影。

主机缩放检查（无需 ESP-IDF）：

```sh
c++ -std=c++17 -O2 -fsanitize=address,undefined -I main \
  tools/grokbot/test_face_scaler.cpp main/face_scaler.cpp -o /tmp/test_face_scaler
/tmp/test_face_scaler
```

## 重新生成动画图集

需要 Node.js、Chrome、Python 3、Pillow 和 NumPy：

```sh
cd tools/grokbot
npm install
python3 -m pip install -r requirements.txt
node bake.mjs
```

脚本会生成 53 个状态帧，并写入 `main/grokbot.atlas`。
原始播放器与 recipe 位于 `third_party/grokbot/`。

## 目录

```text
main/board.cpp           PMIC、IO expander、触摸和按键初始化
main/display.cpp         CO5300 / M5GFX 显示初始化
main/grokbot_player.cpp  GBZ1 压缩图集和帧索引解析
main/face_scaler.cpp     中央脸部裁切、双线性插值和全屏 RGB565 输出
main/main.cpp            zlib 解压、帧重建、轻点切换和顺序循环主循环
main/grokbot.atlas       53 状态和循环数据的设备内嵌图集
tools/grokbot/           浏览器烘焙和 zlib 编码工具
third_party/grokbot/     原版浏览器播放器与 recipe
```

## 已知边界

- 全屏缩放使用固定居中视口，不随动画逐帧改变倍率；向外移动的脸部或特效可能被视口与圆屏边缘裁切。
- 多 clip 状态使用该状态的代表 clip；没有把同一状态的每个随机 clip 变体都重复烘焙。
- 所有状态从第 3 帧开始，morph 入场的前两帧被跳过。
- Grok Bot 名称和角色设计归其权利人；`third_party/grokbot/` 中的解析/播放器代码
  来自 <https://grokbot.yin.wiki/>。
