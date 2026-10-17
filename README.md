# Cardputer 口袋串口终端 (Pocket Serial Terminal) — USB 主机版

一个跑在 **M5Stack Cardputer-ADV** 上的极简串口终端 / 创客工具：
把 Cardputer 当成**带键盘和屏幕的 USB 串口监视器**，直接用 USB 线连到别的
ESP32 / Arduino / MCU 开发板的 USB 口，去现场调试不用开电脑串口助手。

> 不需要任何扩展硬件（LoRa 等都不用）。键盘(TCA8418)、屏幕(ST7789) 板载；
> 与开发板的连接走 **Cardputer 的 USB-C 当主机，枚举开发板的 USB-CDC**。

## 它能做什么

```
   键盘(板载 TCA8418)  ──USB CDC 写──>  开发板 USB-CDC
                                       │
                            USB CDC 读  │
                                       ▼
                                Cardputer 屏幕 (ST7789 240x135)
```

- **键盘 → 开发板**：直接在 Cardputer 上敲命令，经 USB CDC 发给开发板。
- **开发板 → 屏幕**：开发板回的数据实时显示在 Cardputer 屏上。
- **没有 PC 透传**：Cardputer 只有一个 USB-C 口，做 USB 主机连开发板时，
  不能同时当 USB 设备插电脑。要回到"插电脑"的用法请看下方「历史版本」。

## 接线：Cardputer USB-C ⇄ 开发板 USB

用一根 USB-C（或 USB-C↔USB-A）线把 **Cardputer 的 USB-C** 连到**开发板的 USB 口**。

⚠️ **供电(VBUS)是必过的一关**：Cardputer 默认是 USB 受电端，做主机时必须
**给开发板供 5V(VBUS)**，否则开发板不会枚举、屏幕一直停在 "Plug dev board..."。
常见解决办法（任选其一）：
- 用 **带供电的 OTG 转接 / 集线器**，由它给开发板供 VBUS；
- 开发板**自带电源**（如插墙/电池），同时只连 D+/D-/GND（VBUS 不接或短接）；
- 给 Cardputer 做小改：把其 5V 引到 USB-C 的 VBUS（需硬件改动，谨慎）。

> USB 数据走 ESP32-S3 内部 PHY，D+/D- 固定为 GPIO19 / GPIO20，无需手动配引脚。

## 已核实 / 用到的关键参数（Cardputer-ADV 官方 PinMap）

| 功能 | 引脚 |
|---|---|
| ST7789 显示 SPI | MOSI=G35, SCLK=G36, CS=G37, DC=G34, RST=G33, 背光=G38 |
| TCA8418 键盘 I2C | SDA=G8, SCL=G9, INT=G11, 地址 0x34 |
| USB(主机) | D+=G20, D-=G19（内部 PHY，ESP32-S3 唯一 USB 外设） |
| 麦克风/音频/IMU | 同 I2C 总线（本项目未用） |

> 显示与键盘底层驱动由 M5Cardputer 库完成（它内部已处理 ST7789 240×135
> 初始化与 TCA8418 的 4×14 键盘矩阵→字符映射）。本项目只实现桥接逻辑。

## 构建（ESP-IDF v5.x）

```bash
# 1. 安装 ESP-IDF v5.x 并激活环境(idf.py 可用)
# 2. 进入工程目录
cd cardputer-serial-term

# 3. 选择目标芯片
idf.py set-target esp32s3

# 4. 拉取依赖(M5Cardputer / arduino-esp32)，首次会下载
idf.py reconfigure        # 或 idf.py build 会自动触发

# 5. 编译 + 烧录 + 监视
idf.py flash monitor
```

烧录时若进不了下载模式：把侧面电源开关拨到 **OFF**，按住 **G0**(正面按钮)，
再上电/插 USB，松开即可进入下载模式。

> `sdkconfig.defaults` 已把 USB 设为**主机模式**并关闭设备 CDC：
> `CONFIG_TINYUSB_HOST_ENABLED=y`、`CONFIG_TINYUSB_HOST_CDC=y`、
> `CONFIG_TINYUSB_CDC_ENABLED=n`、`CONFIG_ARDUINO_USB_CDC_ON_BOOT=n`、`CONFIG_USB_OTG_ENABLE=y`。

## 使用

1. 用 USB 线把开发板连到 Cardputer 的 USB-C（注意先解决 VBUS 供电，见上）。
2. 屏幕上出现 `[USB] CDC mounted @x` 即表示开发板枚举成功。
3. 在 Cardputer 键盘上打字 → 经 USB CDC 发给开发板；开发板回显 → 屏上实时显示。
4. `Fn + ; ' . /` 分别输出方向键上/下/右/左（ANSI 转义），方便连远程 shell。

## 可调参数（`main.cpp` 顶部）

- `LOCAL_ECHO`：`1` 把键入字符本地回显到屏幕；开发板是 shell（自身会回显）
  时设 `0` 可避免字符重复；开发板不回显（如传感器）时设 `1`。
- 屏幕方向：`M5Cardputer.Display.setRotation(1)` 若显示方向不对，改 `0/2/3`。

## 已知问题与排查

- **屏幕一直 "Plug dev board..." 不出现 mounted**：99% 是 VBUS 没供上。
  确认开发板被供电、且它的 USB 是 CDC-ACM 设备（多数 ESP32-S3 原生 USB 开发板、
  Arduino Leonardo/Micro、树莓派 Pico 等都可；老版 Arduino UNO(独立串口芯片)不行）。
- **`tinyusb_host_init` 报已初始化 / USB 冲突**：说明 USB 设备模式没关干净。
  确认上面 5 个 sdkconfig 项已生效（`idf.py reconfigure` 后 `idf.py menuconfig`
  搜 `TINYUSB` 核对），且 `M5.begin()` 没有再初始化 USB 设备。
- **接收偶尔丢字节**：默认轮询缓冲 64 字节；高速大量输出时可调大 `pumpTarget` 的 buf。
- **无法编译（回调/结构体差异）**：本代码按 ESP-IDF v5.2+ 的 TinyUSB 主机 API
  编写（`tuh_cdc_*` + `tinyusb_host_init`）。若你的 IDF 版本较旧，按编译器报错
  微调 `tinyusb_config_t` 字段或回调签名即可。

## 历史版本（GPIO 硬串口版）

最早一版是用 **EXT 扩展口 GPIO13/15 硬串口**连目标板、并保留 PC(USB-CDC) 透传的
三向桥接。该版本在 git 历史中，可随时取回：

```bash
git show <早期commit> -- main/main.cpp      # 查看
# 或基于当时的提交另开分支
git checkout <早期commit> -b uart-version
```

## 如果想纯 ESP-IDF 自己写驱动（不依赖 M5 库）

我们已把底层参数查实，可据此从零实现：
- **显示**：`esp_lcd` + `esp_lcd_new_panel_st7789`，引脚见上表；注意 240×135
  非标准分辨率，需设 `esp_lcd_panel_set_gap` / MADCTL 旋转。
- **键盘**：I2C 初始化 TCA8418（寄存器见 Adafruit_TCA8418），读 FIFO 事件字节
  `num=(raw&0x7F)-1; row=num/10; col=num%10`；再按 M5 的 `remap` 公式
  `km_col = row*2 + (col>3?1:0); km_row = (col+4)%4` 映射到 4×14 键表。
- **USB 主机**：`tinyusb_host` + `tinyusb_class_cdc_acm`，枚举开发板 CDC-ACM。
