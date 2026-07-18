# PROJECT KNOWLEDGE BASE

**Generated:** 2026-07-18
**Commit:** _(latest)_
**Branch:** main

## OVERVIEW

小智 AI Chatbot (xiaozhi-esp32) — 基于 MCP 协议的 AI 语音对话固件,支持 ESP32-C3/C5/C6/S3/P4 系列芯片。通过流式 ASR+LLM+TTS 架构实现语音交互,支持离线唤醒词、多协议通信(WiFi/4G)、多种显示面板和环境传感器集成。

## STRUCTURE

```
xiaozhi-esp32/
├── main/                     # 核心应用层 (C++)
│   ├── application.cc        # 主应用逻辑、状态机、事件循环
│   ├── main.cc               # 入口 (app_main)
│   ├── boards/               # 103 个开发板 BSP 定义
│   │   ├── esp-sensairshuttle/  # 你的板: ESP32-C5 + BME690
│   │   └── common/              # 共享板级组件
│   ├── services/             # 传感器服务 (BME690 BSEC)
│   ├── audio/                # 音频服务、编解码器(OPUS)、唤醒词 (3× FreeRTOS 任务流水线)
│   ├── display/              # LCD/LVGL/OLED 显示
│   ├── protocols/            # WebSocket / MQTT+UDP 协议
│   ├── led/                  # 单 LED / 灯带 / GPIO LED
│   └── assets/               # 字体、语言包、音效
├── components/
│   └── bme690/               # BME690 传感器驱动 + BSEC
├── managed_components/       # ESP-IDF 组件注册表依赖 (LVGL, LCD 驱动, SR, 编解码器等 30+ 组件)
├── partitions/               # v1 / v2 分区表
├── scripts/                  # 编译脚本、资产工具
├── docs/                     # 开发文档
└── sdkconfig.defaults.*      # 各芯片 SDK 默认配置
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 你的板级初始化 | `main/boards/esp-sensairshuttle/` | I2C/SPI/LCD/Touch/Audio/BME690 |
| BME690 传感器数据 | `main/services/bme690_service.{h,cc}` | BSEC IAQ 算法 + 云上传 |
| 传感器驱动 API | `components/bme690/BME690_SensorAPI/` | Bosch BME69x C 驱动 |
| BSEC 库 | `components/bme690/bsec_IAQ/` | 预编译 `libalgobsec.a` |
| 主事件循环 | `main/application.cc` | Run() 中的 event_group 模式 |
| 设备状态机 | `main/device_state_machine.cc` | 10 种状态转换 |
| 语音管道 | `main/audio/` | Wake word → ASR → LLM → TTS (3 任务流水线) |
| 通信协议 | `main/protocols/` | WebSocket / MQTT+UDP 双栈 |
| 环境数据显示 | `main/display/lvgl_display/` | LVGL 环境面板 |
| 显示驱动 | `main/display/` | LCD/OLED/LVGL/Emoji 渲染栈 |
| LED 控制 | `main/led/` | 单 LED / 灯带 / GPIO LED |
| 构建脚本 | `scripts/release.py` | 一键编译 (自动 menuconfig) |
| SPIFFS 资产 | `scripts/spiffs_assets/` | 字体/主题/表情包打包 |
| BSP 公共层 | `main/boards/common/` | 46 个共享驱动 (WiFi/4G/以太网/电池/摄像头...) |

## CONVENTIONS

- **语言**: C++17, Google 代码风格
- **构建**: ESP-IDF 5.5.4+, CMake, `idf.py build`
- **分区表**: v2 使用 SPIFFS assets 分区存储主题/字体/表情
- **板级选择**: `menuconfig` → Board Type → 选择对应开发板
- **BME690 初始化**: 非阻塞失败 — 传感器故障不阻塞语音功能
- **数据上报**: 环境数据每 60 秒通过 MCP 协议上传服务器

## ANTI-PATTERNS (THIS PROJECT)

- **DO NOT** 阻塞 BME690 初始化失败 — 已设计为非致命错误
- **NEVER** 修改 `managed_components/` 中的文件 — 通过 `idf_component.yml` 管理依赖
- **DO NOT** 直接操作 BME690 寄存器 — 必须通过 BSEC 算法处理原始数据
- **NEVER** 在中断中调用 `i2c_master_transmit` — 需在任务上下文中执行
- **BSEC 校准**: 状态保存在内存中,断电丢失。计划加入 NVS 持久化但不阻塞功能

## COMMANDS

```bash
# 设置目标芯片 (ESP32-C5)
idf.py set-target esp32c5

# 完整清理 + 编译
idf.py fullclean
idf.py build

# 编译 (用脚本, 自动配置所有选项)
python ./scripts/release.py esp-sensairshuttle

# 烧录 + 监视
idf.py flash monitor

# ESP-SensairShuttle 板配置参考
# config.json: PSRAM, WiFi 6, 唤醒词 WN9S, 自定义资产 URL
```

## NOTES

- 项目版本 v2.2.6, IDF v5.5.4
- ESP32-C5 是 RISC-V 架构 (非 Xtensa), 链接 `esp32_risc_v/libalgobsec.a`
- ADC 连续模式在 C5 上使用 `ADC_CONV_SINGLE_UNIT_1`, 非 `ADC_CONV_BOTH_UNIT`
- PDM 扬声器输出使用 `I2S_PDM_TX_CLK_DEFAULT_CONFIG`
- 触摸 CST816D 和 BME690 共用 I2C 总线 (地址 0x15 vs 0x76)
