# Cardputer 便携串口终端 (Pocket Serial Terminal)

一个跑在 **M5Stack Cardputer-ADV** 上的极简串口终端 / 创客工具：
把 Cardputer 当成一个带键盘和屏幕的便携串口监视器，去现场调试别的
ESP32 / Arduino / MCU 开发板，不用开电脑串口助手。

> 不需要任何扩展硬件（LoRa 等都不用）。键盘(TCA8418)、屏幕(ST7789)、
> 目标板 UART 全部板载。

## 它能做什么（三向桥接）

```
   键盘(板载 TCA8418)  ──┐
                         ├──>  目标板 UART (EXT 口 GPIO13/15)
   PC (USB-CDC)        ──┘            │
                                       │ 回显
                                       ▼
                               Cardputer 屏幕 (ST7789 240x135)
                                  + 转发回 PC
```

- **键盘 → 目标板**：直接在 Cardputer 上敲命令发给目标板。
- **目标板 → 屏幕 + PC**：目标板回的数据实时显示在 Cardputer 屏上，
  同时通过 USB-CDC 转发到电脑（电脑端串口助手也能看）。
- **PC → 目标板**：电脑端也能直接通过 USB-CDC 控制目标板，实现透明透传。

## 接线（目标板 ↔ Cardputer EXT 2.54-14P 扩展口）

| Cardputer EXT 脚 | 信号 | 接目标板 |
|---|---|---|
| Pin 12 | G13 = UART_TX | 目标板 **RX** |
| Pin 14 | G15 = UART_RX | 目标板 **TX** |
| Pin 4  | GND          | 目标板 **GND** |

波特率默认 `115200 / 8N1`，可在 `main.cpp` 顶部 `TARGET_BAUD` 改。

## 已核实的关键引脚（Cardputer-ADV 官方 PinMap）

| 功能 | 引脚 |
|---|---|
| ST7789 显示 SPI | MOSI=G35, SCLK=G36, CS=G37, DC=G34, RST=G33, 背光=G38 |
| TCA8418 键盘 I2C | SDA=G8, SCL=G9, INT=G11, 地址 0x34 |
| 目标板 UART | TX=G13, RX=G15 |
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

## 使用

1. 按上面接线把目标板连到 Cardputer 的 EXT 口（TX/RX 交叉、共地）。
2. 电脑用 USB-C 连 Cardputer；用任意串口助手打开出现的 CDC 端口。
3. 在 Cardputer 键盘上打字 → 发给目标板；目标板回显 → 屏上 + 电脑端都能看。
4. `Fn + ; ' . /` 分别输出方向键上/下/右/左（ANSI 转义），方便连远程 shell。

## 可调参数（`main.cpp` 顶部）

- `TARGET_BAUD`：目标板波特率。
- `TARGET_TX_PIN` / `TARGET_RX_PIN`：目标板 UART 引脚（默认 13/15）。
- `LOCAL_ECHO`：`1` 把键入字符本地回显到屏幕；目标板是 shell（自身会回显）
  时设 `0` 可避免字符重复；目标板不回显（如传感器）时设 `1`。
- 屏幕方向：`M5Cardputer.Display.setRotation(1)` 若显示方向不对，改 `0/2/3`。

## 已知问题与排查

- **方向键 / 显示方向不对**：改 `setRotation()` 参数。
- **依赖拉不到 `m5stack/M5Cardputer`**：ESP-IDF 组件注册表若没有该包，可二选一：
  - 用 PlatformIO（本工程 `main.cpp` 同样可直接用，参考 M5 官方
    `env:m5stack-cardputer` 配置，加 `lib_deps = M5Cardputer`）；
  - 或把 `M5Cardputer` / `M5Unified` / `M5GFX` 三个仓库以 git submodule 放进
    `main/components/` 再编译。
- **按键重复/极性**：已由 M5Cardputer 的 TCA8418 驱动正确处理（按下/松开边沿）。

## 如果想纯 ESP-IDF 自己写驱动（不依赖 M5 库）

我们已把底层参数查实，可据此从零实现：
- **显示**：`esp_lcd` + `esp_lcd_new_panel_st7789`，引脚见上表；注意 240×135
  非标准分辨率，需设 `esp_lcd_panel_set_gap` / MADCTL 旋转。
- **键盘**：I2C 初始化 TCA8418（寄存器见 Adafruit_TCA8418），读 FIFO 事件字节
  `num=(raw&0x7F)-1; row=num/10; col=num%10`；再按 M5 的 `remap` 公式
  `km_col = row*2 + (col>3?1:0); km_row = (col+4)%4` 映射到 4×14 键表。
- **USB-CDC**：ESP-IDF `tinyusb` + `tinyusb_cdc_acm` 组件直接收发。
- **UART**：`driver/uart.h`，GPIO13/15。
