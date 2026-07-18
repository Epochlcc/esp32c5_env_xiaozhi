# BOARDS/COMMON: Shared BSP Components

Reusable board-level drivers and abstractions shared across 103+ board definitions. Every board `.cc` inherits from one of these base classes.

## COMPONENT MAP

| Component | Files | Role |
|-----------|-------|------|
| **Board Base** | `board.{h,cc}` | Abstract `Board` class — pure virtual interface for all BSPs |
| **WiFi Board** | `wifi_board.{h,cc}` | WiFi-only board base (ESP32-C3/S3) |
| **Dual Network** | `dual_network_board.{h,cc}` | WiFi + 4G(ML307) hybrid |
| **Ethernet** | `ethernet_board.{h,cc}` | Wired Ethernet (P4-NANO) |
| **ML307 4G** | `ml307_board.{h,cc}` | Cat.1 4G via UART |
| **RNDIS** | `rndis_board.{h,cc}` | USB tethering |
| **NT26** | `nt26_board.{h,cc}` | NT26 4G module |
| **I2C** | `i2c_device.{h,cc}` | Shared I2C bus manager |
| **Battery** | `adc_battery_monitor.{h,cc}` | LiPo voltage ADC monitor |
| **Battery** | `axp2101.{h,cc}` | AXP2101 PMIC driver |
| **Battery** | `sy6970.{h,cc}` | SY6970 charger driver |
| **Button** | `button.{h,cc}` | GPIO button with debounce + long-press |
| **Knob** | `knob.{h,cc}` | Rotary encoder input |
| **Backlight** | `backlight.{h,cc}` | PWM LCD backlight control |
| **Bluetooth** | `blufi.{h,cpp}` | BLUFI Wi-Fi provisioning |
| **Camera** | `camera.h`, `esp32_camera.{h,cc}` | ESP32 camera sensor |
| **Video** | `esp_video.{h,cc}` | Video pipeline (P4) |
| **Power Save** | `power_save_timer.{h,cc}` | Sleep timer management |
| **Sleep** | `sleep_timer.{h,cc}` | Auto-sleep on inactivity |
| **AFSK Demod** | `afsk_demod.{h,cc}` | Audio FSK demodulator |
| **PTT MCP** | `press_to_talk_mcp_tool.{h,cc}` | Push-to-talk MCP tool |
| **System Reset** | `system_reset.{h,cc}` | Factory reset logic |
| **Lamp** | `lamp_controller.h` | LED lamp control interface |

## CONVENTIONS

- **Board factory**: `Board::Create()` in `board.cc` maps config → concrete board class
- **I2C bus**: Single shared `i2c_device` instance, created by first board that calls `InitializeI2c()`
- **Battery**: Either ADC monitor, AXP2101, or SY6970 — never mix
- **All boards** must implement: `GetAudioCodec()`, `GetDisplay()`, `GetLed()`, `GetTouch()`, `GetNetwork()`, `GetBattery()`

## ANTI-PATTERNS

- **DO NOT** create board-specific logic here — this is common only
- **NEVER** hardcode GPIO pins in common code — always via board `config.h`
- **DO NOT** add new PMIC drivers without corresponding board integration
