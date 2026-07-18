# SERVICES: BME690 Sensor Service

环境传感器服务层, 封装 BME690 + BSEC 算法, 为应用层提供 IAQ / 温度 / 湿度 / 气压数据。

## BME690 SERVICE ARCHITECTURE

```
Bme690Service::Initialize(i2c_bus)
  ├── init_bme69x_sensor()       # 配置 BME69x I2C, 初始化硬件
  │   ├── i2c_master_bus_add_device(0x76)
  │   ├── bme69x_init()          # 读取 Chip ID 0x...
  │   ├── bme69x_set_conf()      # OS 16x, filter off
  │   └── bme69x_set_heatr_conf() # 300°C, 100ms
  └── init_bsec()                # 配置 BSEC 虚拟传感器
      ├── bsec_init()
      └── bsec_update_subscription() # 8 个输出 (IAQ, CO2, VOC...)

Bme690Service::Start()
  └── xTaskCreate("bme690_task")  # 4KB 栈, 优先级 5

bsec_task_run() 循环 (每 3 秒):
  1. bsec_sensor_control() → 获取下一采样配置
  2. bme69x_set_conf() + set_heatr_conf() + set_op_mode(FORCED)
  3. 等待测量完成
  4. bme69x_get_data() → 读取原始传感器数据
  5. bsec_do_steps() → BSEC 算法计算 IAQ/CO2/VOC/温度/湿度
  6. output_ready() → 更新 latest_data_ + 触发回调
  7. 每 6 小时保存 BSEC 状态到内存
```

## DATA OUTPUT

- **采样率**: 低功耗模式 (LP), 每 3 秒
- **BSEC 输出**: IAQ (0-500), eCO2 (400-1600 ppm), bVOC (0-10 ppm), 补偿温湿度, 原始气压和气体电阻
- **校准**: acc=0 (不可靠, 前 5 分钟), acc=1-3 (逐渐稳定)
- **温漂补偿**: `TEMP_OFFSET = 5.0°C` (电路自热补偿)
- **JSON 格式**: `{"temperature":25.3,"humidity":48.5,"pressure":1013.2,"iaq":42,...}`

## KEY FILES

| File | Lines | Purpose |
|------|-------|---------|
| `bme690_service.h` | 126 | 类定义, SensorData 结构体, 回调类型 |
| `bme690_service.cc` | 510 | 完整实现: 初始化, BSEC 循环, I2C 回调, 数据 JSON |

## CAVEATS

- BSEC 状态**仅在内存中维护**, 断电丢失校准。每次重启从头校准 (约 12h 达到 acc=3)
- `g_bme690_service` 全局指针供 `Application.cc` 通过 `#ifdef CONFIG_BOARD_TYPE_ESP_SENSAIRSHUTTLE` 条件编译使用
- 数据上传在 `Application::Run()` 的 clock_tick 中每 60 秒调用 `UploadEnvironmentData()`
- 传感器初始化失败不会阻塞启动 — 语音功能完全独立
