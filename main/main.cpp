/*
 * Cardputer 便携串口终端 / Pocket Serial Terminal
 * ------------------------------------------------------------
 * 硬件: M5Stack Cardputer-ADV (ESP32-S3FN8, 8MB flash, 无 PSRAM)
 * 功能: 键盘 -> 目标板 UART；目标板 UART -> 屏幕 + PC(USB-CDC)；
 *       PC(USB-CDC) -> 目标板 UART。三向桥接。
 *
 * 显示与键盘由 M5Cardputer 库驱动（其内部已处理 ST7789 240x135
 * 初始化与 TCA8418 4x14 键盘矩阵映射），本文件只实现桥接逻辑。
 *
 * 目标板 UART (EXT 扩展口):
 *   Cardputer G13 = UART_TX -> 接目标板 RX
 *   Cardputer G15 = UART_RX -> 接目标板 TX
 *   Cardputer GND -> 接目标板 GND
 */

#include <Arduino.h>
#include <M5Cardputer.h>
#include <vector>

// ---------------- 配置 ----------------
#define TARGET_BAUD      115200
#define TARGET_TX_PIN    13      // EXT pin12 = G13 (Cardputer TX)
#define TARGET_RX_PIN    15      // EXT pin14 = G15 (Cardputer RX)
#define TERM_VIEW_CHARS  480     // 屏幕上最多保留的字符数(约一屏)

// 本地回显: 1=把键入的字符也显示在屏幕上; 0=只靠目标板回显。
// 目标板是 shell(会回显)时建议设 0，避免字符重复; 目标板不回显(如传感器)时设 1。
#define LOCAL_ECHO       1

// ---------------- 全局 ----------------
String termBuf = "";                       // 终端滚动缓冲
String prevWord = "";                       // 上一帧按键字符(用于边沿检测)
bool  prevEnter = false, prevBackspace = false, prevTab = false;
bool  prevUp = false, prevDown = false, prevLeft = false, prevRight = false;

// ---------------- 显示 ----------------
// 性能: 高频串口数据下不在每个字节都整屏重绘，而是标记 dirty，
// 由主循环按 ~30fps 节流重绘(见 pumpRedraw)。
bool   termDirty = false;
uint32_t lastDraw = 0;

void termRedraw() {
    // 只绘制最后 TERM_VIEW_CHARS 个字符，避免整屏重绘过慢
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
    if (len > 2000) {  // 缓冲上限，超出丢弃最旧部分
        termBuf = termBuf.substring(len - 2000);
    }
    termDirty = true;
}

void termPrintChar(char c) {
    char buf[2] = { c, '\0' };
    termAppend(String(buf));
}

// 主循环调用: 以约 30fps 节流重绘，避免高速串口下卡死
void pumpRedraw() {
    uint32_t now = millis();
    if (termDirty && (now - lastDraw >= 33)) {
        termRedraw();
        lastDraw = now;
    }
}

// ---------------- 键盘 -> 目标板 ----------------
void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;

    auto ks = M5Cardputer.Keyboard.keysState();

    // 字母/符号: 与上一帧做差，只发送“新按下”的字符(屏蔽自动重复)
    String curWord = "";
    for (char c : ks.word) curWord += c;
    for (char c : curWord) {
        if (prevWord.indexOf(c) < 0) {
            Serial1.write((uint8_t)c);
            if (LOCAL_ECHO) termPrintChar(c);
        }
    }
    prevWord = curWord;

    // 特殊键(边沿触发)
    if (ks.enter && !prevEnter) {
        Serial1.write('\r');
        if (LOCAL_ECHO) termAppend("\r\n");
    }
    prevEnter = ks.enter;

    if (ks.backspace && !prevBackspace) {
        Serial1.write(0x08);
        if (LOCAL_ECHO) termPrintChar(0x08);
    }
    prevBackspace = ks.backspace;

    if (ks.tab && !prevTab) {
        Serial1.write('\t');
        if (LOCAL_ECHO) termPrintChar('\t');
    }
    prevTab = ks.tab;

    // fn + ;'./ 方向键 -> ANSI 转义序列
    if (ks.up    && !prevUp)    { Serial1.write("\x1b[A"); }
    if (ks.down  && !prevDown)  { Serial1.write("\x1b[B"); }
    if (ks.right && !prevRight) { Serial1.write("\x1b[C"); }
    if (ks.left  && !prevLeft)  { Serial1.write("\x1b[D"); }
    prevUp = ks.up; prevDown = ks.down; prevLeft = ks.left; prevRight = ks.right;
}

// ---------------- 目标板 UART -> 屏幕 + PC ----------------
void pumpTarget() {
    while (Serial1.available()) {
        uint8_t b = (uint8_t)Serial1.read();
        termPrintChar((char)b);   // 本地显示
        Serial.write(b);          // 转发给 PC (USB-CDC)
    }
}

// ---------------- PC (USB-CDC) -> 目标板 ----------------
void pumpHost() {
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        Serial1.write(b);        // 透明转发给目标板
        // 不本地回显，避免与目标板回显重复
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);

    M5Cardputer.Display.setRotation(1);          // 横屏; 如方向不对改成 0/2/3
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(BLACK);

    // 启动横幅写入终端缓冲(之后随数据一起滚动保留)
    termAppend("Cardputer Serial Terminal\r\n");
    termAppend("Target: GPIO13(TX) GPIO15(RX) @115200\r\n");
    termAppend("Keyboard -> target | target/PC -> screen\r\n");
    termAppend("------------------------------------\r\n");
    termRedraw();

    // 目标板 UART (HardwareSerial on EXT header)
    Serial1.begin(TARGET_BAUD, SERIAL_8N1, TARGET_RX_PIN, TARGET_TX_PIN);

    // USB-CDC (PC 桥接)。ARDUINO_USB_CDC_ON_BOOT=1 时 Serial 即 USB CDC
    Serial.begin(TARGET_BAUD);  // 波特率对 USB-CDC 无意义，仅占位
}

void loop() {
    M5Cardputer.update();   // 刷新键盘状态(必须在 loop 内周期调用)

    handleKeyboard();
    pumpTarget();
    pumpHost();
    pumpRedraw();           // 节流重绘(约 30fps)

    delay(10);  // ~100Hz 轮询，足够键盘与串口
}
