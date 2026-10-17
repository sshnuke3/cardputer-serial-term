/*
 * Cardputer-ADV 口袋串口终端 / Pocket Serial Terminal —— USB 主机版
 * ------------------------------------------------------------
 * 硬件: M5Stack Cardputer-ADV (ESP32-S3FN8, 8MB flash, 无 PSRAM)
 * 拓扑: Cardputer 作为 USB 主机，直连开发板的 USB-CDC(CDC-ACM)。
 *       键盘 -> 开发板(USB CDC)；开发板(USB CDC) -> 屏幕。
 *       不再有 PC 透传：Cardputer 只有 1 个 USB-C，做主机时不能同时当设备连电脑。
 *
 * 显示与键盘由 M5Cardputer 库驱动（其内部已处理 ST7789 240x135
 * 初始化与 TCA8418 4x14 键盘矩阵映射），本文件只实现桥接逻辑。
 *
 * USB 传输层: TinyUSB 主机 + CDC-ACM 类。去枚举开发板的 USB 串口。
 *   接收用轮询(tuh_cdc_read)，避免不同 IDF 版本的 rx 回调签名差异。
 *
 * 硬件注意(重要):
 *   Cardputer 默认是 USB 受电端。做主机时必须给开发板供 VBUS(5V)，
 *   否则开发板不枚举。常见做法: OTG 转接 + 外部 5V，或开发板自带电源
 *   同时只接 D+/D-/GND。请确认开发板 USB 口能被主机识别(CDC-ACM 设备)。
 */

#include <Arduino.h>
#include <M5Cardputer.h>
#include <vector>
#include "tinyusb.h"

// ---------------- 配置 ----------------
#define LOCAL_ECHO       1      // 1=键入字符本地回显; 开发板自身回显(shell)时设 0
#define TERM_VIEW_CHARS  480    // 屏幕上最多保留的字符数(约一屏)

// ---------------- 全局 ----------------
String termBuf = "";
bool  termDirty = false;
uint32_t lastDraw = 0;

// ---------------- USB 主机状态 ----------------
static uint8_t  g_cdc_addr  = 0;
static bool     g_cdc_ready = false;

// ---------------- 显示 ----------------
// 性能: 高频数据不在每个字节整屏重绘，标记 dirty 后由主循环按 ~30fps 节流。
void termRedraw() {
    int32_t len = termBuf.length();
    int32_t start = (len > TERM_VIEW_CHARS) ? (len - TERM_VIEW_CHARS) : 0;
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.print(termBuf.substring(start));
    termDirty = false;
}

void termAppend(const String& s) {
    termBuf += s;
    int32_t len = termBuf.length();
    if (len > 2000) {
        termBuf = termBuf.substring(len - 2000);
    }
    termDirty = true;
}

void termPrintChar(char c) {
    char buf[2] = { c, '\0' };
    termAppend(String(buf));
}

void pumpRedraw() {
    uint32_t now = millis();
    if (termDirty && (now - lastDraw >= 33)) {
        termRedraw();
        lastDraw = now;
    }
}

// ---------------- 键盘 -> 开发板(USB CDC) ----------------
static String prevWord = "";
static bool prevEnter=false, prevBackspace=false, prevTab=false;
static bool prevUp=false, prevDown=false, prevLeft=false, prevRight=false;

void sendTarget(const char* data, size_t len) {
    if (!g_cdc_ready) return;            // 设备未挂载时丢弃(可改成队列)
    tuh_cdc_write(g_cdc_addr, data, len);
    tuh_cdc_write_flush(g_cdc_addr);
}

void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    auto ks = M5Cardputer.Keyboard.keysState();

    if (!g_cdc_ready) {                  // 设备未挂载: 不处理键盘, 避免丢字符
        return;
    }

    // 字母/符号: 与上一帧做差，只发"新按下"的字符(屏蔽自动重复)
    String curWord = "";
    for (char c : ks.word) curWord += c;
    for (char c : curWord) {
        if (prevWord.indexOf(c) < 0) {
            sendTarget(&c, 1);
            if (LOCAL_ECHO) termPrintChar(c);
        }
    }
    prevWord = curWord;

    if (ks.enter && !prevEnter)     { sendTarget("\r", 1); if (LOCAL_ECHO) termAppend("\r\n"); }
    prevEnter = ks.enter;

    if (ks.backspace && !prevBackspace) { sendTarget("\b", 1); if (LOCAL_ECHO) termPrintChar(0x08); }
    prevBackspace = ks.backspace;

    if (ks.tab && !prevTab)         { sendTarget("\t", 1); if (LOCAL_ECHO) termPrintChar('\t'); }
    prevTab = ks.tab;

    // fn + ;'./ 方向键 -> ANSI 转义序列(方便连远程 shell)
    if (ks.up    && !prevUp)    sendTarget("\x1b[A", 3);
    if (ks.down  && !prevDown)  sendTarget("\x1b[B", 3);
    if (ks.right && !prevRight) sendTarget("\x1b[C", 3);
    if (ks.left  && !prevLeft)  sendTarget("\x1b[D", 3);
    prevUp = ks.up; prevDown = ks.down; prevLeft = ks.left; prevRight = ks.right;
}

// ---------------- 开发板(USB CDC) -> 屏幕 ----------------
// 轮询读取 CDC IN 端点(TinyUSB 主机 CDC FIFO)，不依赖 rx 回调。
void pumpTarget() {
    if (!g_cdc_ready) return;
    uint8_t buf[64];
    uint32_t n = tuh_cdc_read(g_cdc_addr, buf, sizeof(buf));
    for (uint32_t i = 0; i < n; i++) {
        termPrintChar((char)buf[i]);
    }
}

// ---------------- TinyUSB 主机回调(装载/卸载，单参数，跨版本稳定) ----------------
extern "C" void tuh_cdc_mounted_cb(uint8_t dev_addr) {
    g_cdc_addr  = dev_addr;
    g_cdc_ready = true;
    termAppend("\r\n[USB] CDC mounted @");
    termAppend(String(dev_addr));
    termAppend("\r\n");
}

extern "C" void tuh_cdc_unmounted_cb(uint8_t dev_addr) {
    if (dev_addr == g_cdc_addr) {
        g_cdc_ready = false;
        termAppend("\r\n[USB] CDC unmounted\r\n");
    }
}

// ---------------- 初始化 ----------------
void usbHostInit() {
    tinyusb_config_t cfg = {
        .device_speed     = TUSB_SPEED_FULL,
        .descriptor       = nullptr,
        .string_descriptor = nullptr,
        .external_phy     = false,
    };
    ESP_ERROR_CHECK(tinyusb_host_init(&cfg));
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);          // 显示/键盘; USB 设备 CDC 已在 sdkconfig 关闭

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(BLACK);

    termAppend("Cardputer USB Serial Terminal\r\n");
    termAppend("USB HOST -> dev board CDC-ACM\r\n");
    termAppend("(no PC passthrough)\r\n");
    termAppend("Plug dev board USB into Cardputer\r\n");
    termAppend("------------------------------\r\n");
    termRedraw();

    usbHostInit();
}

void loop() {
    M5Cardputer.update();   // 刷新键盘状态(必须在 loop 内周期调用)

    handleKeyboard();
    pumpTarget();
    pumpRedraw();           // 节流重绘(约 30fps)

    delay(10);
}
