#include "bme690_service.h"
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <cstring>
#include <cstdio>
#include <string>

static const char *TAG = "Bme690Svc";

Bme690Service* g_bme690_service = nullptr;

#ifndef BSEC_SAMPLE_RATE_LP
#define BSEC_SAMPLE_RATE_LP 0.33333f
#endif

#ifndef BSEC_SAMPLE_RATE_ULP
#define BSEC_SAMPLE_RATE_ULP 0.0033333f
#endif

Bme690Service::Bme690Service()
{
    data_mutex_ = xSemaphoreCreateMutex();
    g_bme690_service = this;
}

Bme690Service::~Bme690Service()
{
    Stop();
    if (data_mutex_) {
        vSemaphoreDelete(data_mutex_);
    }
}

esp_err_t Bme690Service::Initialize(i2c_master_bus_handle_t i2c_bus)
{
    i2c_bus_ = i2c_bus;
    if (i2c_bus_ == nullptr) {
        ESP_LOGE(TAG, "Invalid I2C bus handle");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = init_bme69x_sensor();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BME690 sensor init failed");
        return ret;
    }

    bsec_library_return_t bsec_ret = init_bsec();
    if (bsec_ret != BSEC_OK) {
        ESP_LOGE(TAG, "BSEC init failed: %d", bsec_ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BME690 service initialized successfully");
    return ESP_OK;
}

void Bme690Service::Start()
{
    if (running_) {
        ESP_LOGW(TAG, "BME690 service already running");
        return;
    }

    running_ = true;
    BaseType_t ret = xTaskCreate(
        task_wrapper,
        "bme690_task",
        4096,
        this,
        5,
        &task_handle_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BME690 task");
        running_ = false;
    } else {
        ESP_LOGI(TAG, "BME690 background task started");
    }
}

void Bme690Service::Stop()
{
    if (running_) {
        running_ = false;
        if (task_handle_) {
            vTaskDelay(pdMS_TO_TICKS(100));
            task_handle_ = nullptr;
        }
        ESP_LOGI(TAG, "BME690 service stopped");
    }
}

Bme690Service::SensorData Bme690Service::GetLatestData() const
{
    SensorData data;
    if (data_mutex_ && xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = latest_data_;
        xSemaphoreGive(data_mutex_);
    }
    return data;
}

void Bme690Service::SetDataCallback(DataCallback callback)
{
    data_callback_ = callback;
}

std::string Bme690Service::GetDataJson() const
{
    auto data = GetLatestData();
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "{"
        "\"temperature\":%.1f,"
        "\"humidity\":%.1f,"
        "\"pressure\":%.1f,"
        "\"iaq\":%.0f,"
        "\"iaq_accuracy\":%u,"
        "\"co2_equivalent\":%.0f,"
        "\"breath_voc\":%.1f,"
        "\"gas_resistance\":%.0f"
        "}",
        data.temperature,
        data.humidity,
        data.pressure / 100.0f,
        data.iaq,
        data.iaq_accuracy,
        data.co2_equivalent,
        data.breath_voc_equivalent,
        data.gas_resistance
    );
    return std::string(buf, len);
}

/* Bosch BME69x driver callbacks — signatures match bme69x_read_fptr_t etc. */
int8_t Bme690Service::i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)intf_ptr;
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg_addr, 1, reg_data, len, -1);
    return (ret == ESP_OK) ? BME69X_OK : BME69X_E_COM_FAIL;
}

int8_t Bme690Service::i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)intf_ptr;
    size_t buf_len = (size_t)len + 1;
    uint8_t *buf = (uint8_t *)malloc(buf_len);
    if (!buf) {
        return BME69X_E_COM_FAIL;
    }
    buf[0] = reg_addr;
    memcpy(buf + 1, reg_data, len);
    esp_err_t ret = i2c_master_transmit(dev, buf, buf_len, -1);
    free(buf);
    return (ret == ESP_OK) ? BME69X_OK : BME69X_E_COM_FAIL;
}

void Bme690Service::i2c_delay_us(uint32_t period_us, void *intf_ptr)
{
    (void)intf_ptr;
    if (period_us < 1000) {
        esp_rom_delay_us(period_us);
    } else {
        vTaskDelay(pdMS_TO_TICKS(period_us / 1000));
    }
}

esp_err_t Bme690Service::init_bme69x_sensor()
{
    ESP_LOGI(TAG, "Initializing BME69x sensor...");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME69X_I2C_ADDR_LOW,
        .scl_speed_hz = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
    if (ret != ESP_OK || i2c_dev_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add BME690 I2C device");
        return ESP_FAIL;
    }

    bme_.intf = BME69X_I2C_INTF;
    bme_.intf_ptr = (void *)i2c_dev_;
    bme_.read = i2c_read;
    bme_.write = i2c_write;
    bme_.delay_us = i2c_delay_us;
    bme_.amb_temp = 25;

    int8_t rslt = bme69x_init(&bme_);
    if (rslt != BME69X_OK) {
        ESP_LOGE(TAG, "bme69x_init failed: %d", rslt);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BME690 Chip ID: 0x%02x", bme_.chip_id);

    struct bme69x_conf conf;
    conf.filter = BME69X_FILTER_OFF;
    conf.odr = BME69X_ODR_NONE;
    conf.os_hum = BME69X_OS_16X;
    conf.os_pres = BME69X_OS_16X;
    conf.os_temp = BME69X_OS_16X;
    rslt = bme69x_set_conf(&conf, &bme_);
    if (rslt != BME69X_OK) {
        ESP_LOGE(TAG, "bme69x_set_conf failed: %d", rslt);
        return ESP_FAIL;
    }

    struct bme69x_heatr_conf heatr_conf;
    heatr_conf.enable = BME69X_ENABLE;
    heatr_conf.heatr_temp = 300;
    heatr_conf.heatr_dur = 100;
    rslt = bme69x_set_heatr_conf(BME69X_FORCED_MODE, &heatr_conf, &bme_);
    if (rslt != BME69X_OK) {
        ESP_LOGE(TAG, "bme69x_set_heatr_conf failed: %d", rslt);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bsec_library_return_t Bme690Service::init_bsec()
{
    bsec_library_return_t bsec_status;

    bsec_status = bsec_init();
    if (bsec_status != BSEC_OK) {
        ESP_LOGE(TAG, "bsec_init failed: %d", bsec_status);
        return bsec_status;
    }

    bsec_sensor_configuration_t requested_virtual_sensors[8];
    uint8_t n_requested = 8;

    requested_virtual_sensors[0].sensor_id = BSEC_OUTPUT_IAQ;
    requested_virtual_sensors[0].sample_rate = BSEC_SAMPLE_RATE_LP;

    requested_virtual_sensors[1].sensor_id = BSEC_OUTPUT_STATIC_IAQ;
    requested_virtual_sensors[1].sample_rate = BSEC_SAMPLE_RATE_LP;

    requested_virtual_sensors[2].sensor_id = BSEC_OUTPUT_CO2_EQUIVALENT;
    requested_virtual_sensors[2].sample_rate = BSEC_SAMPLE_RATE_LP;

    requested_virtual_sensors[3].sensor_id = BSEC_OUTPUT_BREATH_VOC_EQUIVALENT;
    requested_virtual_sensors[3].sample_rate = BSEC_SAMPLE_RATE_LP;

    requested_virtual_sensors[4].sensor_id = BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE;
    requested_virtual_sensors[4].sample_rate = BSEC_SAMPLE_RATE_LP;

    requested_virtual_sensors[5].sensor_id = BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY;
    requested_virtual_sensors[5].sample_rate = BSEC_SAMPLE_RATE_LP;

    requested_virtual_sensors[6].sensor_id = BSEC_OUTPUT_RAW_PRESSURE;
    requested_virtual_sensors[6].sample_rate = BSEC_SAMPLE_RATE_LP;

    requested_virtual_sensors[7].sensor_id = BSEC_OUTPUT_RAW_GAS;
    requested_virtual_sensors[7].sample_rate = BSEC_SAMPLE_RATE_LP;

    bsec_sensor_configuration_t required_settings[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required = BSEC_MAX_PHYSICAL_SENSOR;

    bsec_status = bsec_update_subscription(
        requested_virtual_sensors, n_requested,
        required_settings, &n_required
    );

    if (bsec_status == BSEC_W_SU_SAMPLERATEMISMATCH) {
        ESP_LOGW(TAG, "BSEC sample rate mismatch (warning, continuing)");
    } else if (bsec_status < 0) {
        ESP_LOGE(TAG, "bsec_update_subscription failed: %d", bsec_status);
        return bsec_status;
    }

    ESP_LOGI(TAG, "BSEC initialized successfully");
    return BSEC_OK;
}

void Bme690Service::task_wrapper(void* arg)
{
    Bme690Service* self = static_cast<Bme690Service*>(arg);
    self->bsec_task_run();
    vTaskDelete(nullptr);
}

void Bme690Service::bsec_task_run()
{
    ESP_LOGI(TAG, "BSEC task started");

    int8_t bme_rslt;
    bsec_library_return_t bsec_status;
    bsec_bme_settings_t sensor_settings;
    bsec_input_t bsec_inputs[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_bsec_inputs = 0;
    bsec_output_t bsec_outputs[BSEC_NUMBER_OUTPUTS];
    uint8_t n_bsec_outputs = 0;

    time_stamp_last_state_save_ = get_timestamp_us();

    while (running_) {
        int64_t curr_time_ns = get_timestamp_us() * 1000;

        bsec_status = bsec_sensor_control(curr_time_ns, &sensor_settings);
        if (bsec_status != BSEC_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (sensor_settings.trigger_measurement) {
            struct bme69x_conf conf;
            conf.filter = BME69X_FILTER_OFF;
            conf.odr = BME69X_ODR_NONE;
            conf.os_hum = sensor_settings.humidity_oversampling;
            conf.os_pres = sensor_settings.pressure_oversampling;
            conf.os_temp = sensor_settings.temperature_oversampling;

            bme_rslt = bme69x_set_conf(&conf, &bme_);
            if (bme_rslt != BME69X_OK) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            struct bme69x_heatr_conf heatr_conf;
            heatr_conf.enable = BME69X_ENABLE;
            heatr_conf.heatr_temp = sensor_settings.heater_temperature;
            heatr_conf.heatr_dur = sensor_settings.heater_duration;

            bme_rslt = bme69x_set_heatr_conf(BME69X_FORCED_MODE, &heatr_conf, &bme_);
            if (bme_rslt != BME69X_OK) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            bme_rslt = bme69x_set_op_mode(BME69X_FORCED_MODE, &bme_);
            if (bme_rslt != BME69X_OK) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            uint32_t del_period = bme69x_get_meas_dur(BME69X_FORCED_MODE, &conf, &bme_)
                                  + (heatr_conf.heatr_dur * 1000);
            bme_.delay_us(del_period, bme_.intf_ptr);

            uint8_t n_data = 0;
            bme_rslt = bme69x_get_data(BME69X_FORCED_MODE, &bme_data_, &n_data, &bme_);
            if (bme_rslt != BME69X_OK || n_data == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            if (!(bme_data_.status & BME69X_GASM_VALID_MSK)) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            n_bsec_inputs = 0;
            int64_t time_stamp_ns = get_timestamp_us() * 1000;

            if (sensor_settings.process_data & BSEC_PROCESS_TEMPERATURE) {
                bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_TEMPERATURE;
                bsec_inputs[n_bsec_inputs].signal = bme_data_.temperature;
                bsec_inputs[n_bsec_inputs].time_stamp = time_stamp_ns;
                n_bsec_inputs++;

                bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_HEATSOURCE;
                bsec_inputs[n_bsec_inputs].signal = TEMP_OFFSET;
                bsec_inputs[n_bsec_inputs].time_stamp = time_stamp_ns;
                n_bsec_inputs++;
            }

            if (sensor_settings.process_data & BSEC_PROCESS_HUMIDITY) {
                bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_HUMIDITY;
                bsec_inputs[n_bsec_inputs].signal = bme_data_.humidity;
                bsec_inputs[n_bsec_inputs].time_stamp = time_stamp_ns;
                n_bsec_inputs++;
            }

            if (sensor_settings.process_data & BSEC_PROCESS_PRESSURE) {
                bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_PRESSURE;
                bsec_inputs[n_bsec_inputs].signal = bme_data_.pressure;
                bsec_inputs[n_bsec_inputs].time_stamp = time_stamp_ns;
                n_bsec_inputs++;
            }

            if (sensor_settings.process_data & BSEC_PROCESS_GAS) {
                bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_GASRESISTOR;
                bsec_inputs[n_bsec_inputs].signal = bme_data_.gas_resistance;
                bsec_inputs[n_bsec_inputs].time_stamp = time_stamp_ns;
                n_bsec_inputs++;
            }

            n_bsec_outputs = BSEC_NUMBER_OUTPUTS;
            bsec_status = bsec_do_steps(bsec_inputs, n_bsec_inputs, bsec_outputs, &n_bsec_outputs);

            if (bsec_status != BSEC_OK) {
                ESP_LOGW(TAG, "bsec_do_steps failed: %d", bsec_status);
            } else if (n_bsec_outputs > 0) {
                float iaq = 0, static_iaq = 0, co2_eq = 0, breath_voc = 0;
                float temperature = 0, humidity = 0, pressure = 0;
                float raw_temp = 0, raw_hum = 0, gas = 0;
                uint8_t iaq_acc = 0;

                for (uint8_t i = 0; i < n_bsec_outputs; i++) {
                    switch (bsec_outputs[i].sensor_id) {
                    case BSEC_OUTPUT_IAQ:
                        iaq = bsec_outputs[i].signal;
                        iaq_acc = bsec_outputs[i].accuracy;
                        break;
                    case BSEC_OUTPUT_STATIC_IAQ:
                        static_iaq = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_CO2_EQUIVALENT:
                        co2_eq = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                        breath_voc = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                        temperature = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                        humidity = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_RAW_PRESSURE:
                        pressure = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_RAW_GAS:
                        gas = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_RAW_TEMPERATURE:
                        raw_temp = bsec_outputs[i].signal;
                        break;
                    case BSEC_OUTPUT_RAW_HUMIDITY:
                        raw_hum = bsec_outputs[i].signal;
                        break;
                    }
                }

                output_ready(time_stamp_ns / 1000, iaq, iaq_acc, temperature, humidity,
                             pressure, raw_temp, raw_hum, gas, bsec_status,
                             static_iaq, co2_eq, breath_voc);
            }

            // Periodic BSEC state save (every 6 hours)
            int64_t curr_time_us = get_timestamp_us();
            if ((curr_time_us - time_stamp_last_state_save_) >= STATE_SAVE_PERIOD_US) {
                uint8_t work_buffer[BSEC_MAX_PROPERTY_BLOB_SIZE];
                uint32_t n_serialized_state = 0;
                bsec_status = bsec_get_state(0, bsec_state_, sizeof(bsec_state_),
                                             work_buffer, sizeof(work_buffer),
                                             &n_serialized_state);
                if (bsec_status == BSEC_OK) {
                    time_stamp_last_state_save_ = curr_time_us;
                    ESP_LOGI(TAG, "BSEC state saved (%u bytes)", n_serialized_state);
                }
            }
        }

        int64_t next_call_ns = sensor_settings.next_call;
        int64_t curr_time_ns_end = get_timestamp_us() * 1000;
        int64_t sleep_ms = (next_call_ns - curr_time_ns_end) / 1000000;

        if (sleep_ms > 0 && sleep_ms < 10000) {
            vTaskDelay(pdMS_TO_TICKS(sleep_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "BSEC task stopped");
}

void Bme690Service::output_ready(
    int64_t timestamp, float iaq, uint8_t iaq_accuracy,
    float temperature, float humidity, float pressure,
    float raw_temperature, float raw_humidity,
    float gas, bsec_library_return_t bsec_status,
    float static_iaq, float co2_equivalent,
    float breath_voc_equivalent)
{
    SensorData data;
    data.temperature = temperature;
    data.humidity = humidity;
    data.pressure = pressure;
    data.iaq = iaq;
    data.iaq_accuracy = iaq_accuracy;
    data.co2_equivalent = co2_equivalent;
    data.breath_voc_equivalent = breath_voc_equivalent;
    data.gas_resistance = gas;
    data.valid = true;

    if (data_mutex_) {
        xSemaphoreTake(data_mutex_, portMAX_DELAY);
        latest_data_ = data;
        xSemaphoreGive(data_mutex_);
    }

    if (data_callback_) {
        data_callback_(data);
    }

    ESP_LOGD(TAG, "T=%.1f H=%.1f P=%.0f IAQ=%.0f(acc=%u) CO2=%.0f",
             temperature, humidity, pressure, iaq, iaq_accuracy, co2_equivalent);
}

int64_t Bme690Service::get_timestamp_us()
{
    return esp_timer_get_time();
}
