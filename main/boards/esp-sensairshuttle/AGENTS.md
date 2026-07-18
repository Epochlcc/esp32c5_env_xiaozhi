# BOARD: ESP-SensairShuttle (ESP32-C5 + BME690)

Board Support Package for the Espressif ESP-SensairShuttle, co-developed with Bosch Sensortec. Main chip: ESP32-C5-WROOM-1-N16R8 (16MB Flash, 8MB PSRAM).

## PERIPHERAL MAP

| Peripheral | Interface | Pins | Details |
|-----------|-----------|------|---------|
| **LCD** ILI9341 284×240 | SPI (40MHz) | MOSI=23, CLK=24, DC=26, CS=25 | 通过 `esp_lcd_ili9341` 驱动 |
| **Touch** CST816D | I2C (addr 0x15) | SDA=2, SCL=3 | 50ms 轮询, 触摸切换聊天/环境模式 |
| **BME690** | I2C (addr 0x76) | SDA=2, SCL=3 | 共用 I2C 总线, 400kHz |
| **ADC Mic** | ADC1_CH5 | GPIO NA | SAR ADC 连续模式, 16kHz |
| **PDM Speaker** | I2S PDM | P=7, N=8 | 差分输出, 24kHz |
| **PA Enable** | GPIO | 1 | 高电平使能功放 |
| **BOOT Button** | GPIO | 28 | 低电平触发, 启动中按住进配网模式 |

## INIT SEQUENCE

`EspSensairShuttle` 构造函数按顺序初始化:
1. `InitializeI2c()` — I2C bus (SDA=2, SCL=3, pullup enabled)
2. `InitializeCst816dTouchPad()` — 触摸, 创建 `touch_task` (50ms poll)
3. `InitializeButtons()` — BOOT 按键回调
4. `InitializeSpi()` — SPI bus (SPI2_HOST, 40MHz)
5. `InitializeLcdDisplay()` — ILI9341 + 供应商特定初始化序列
6. `InitializeBme690()` — BME690 传感器 (非阻塞, 失败仅警告)

## DISPLAY CONFIG

- LCD: ST7789 协议兼容 ILI9341, 284×240, 偏移(36,0)
- 镜像: Y 轴镜像, XY 交换 (横屏)
- 颜色反转: true, RGB 顺序
- 背光: 当前未软件控制, 硬件常亮

## KEY FILES

| File | Purpose |
|------|---------|
| `config.h` | 全部 GPIO 定义和显示参数 |
| `esp-sensairshuttle.cc` | 板级 BSP 类, 全部初始化逻辑 |
| `adc_pdm_audio_codec.cc` | ADC 输入 + PDM I2S 输出音频编解码 |
| `config.json` | release 脚本构建配置 |
| `README.md` | 板级简介和编译命令 |
