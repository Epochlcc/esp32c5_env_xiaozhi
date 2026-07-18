#ifndef BME690_SERVICE_H
#define BME690_SERVICE_H

#include <functional>
#include <atomic>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>
#include <esp_err.h>

#include "bme69x_defs.h"
#include "bme69x.h"
#include "bsec_datatypes.h"
#include "bsec_interface.h"

/**
 * @brief BME690 environmental sensor service with BSEC algorithm
 *
 * Runs in background, reads temperature/humidity/pressure/gas,
 * runs BSEC algorithm to compute IAQ, CO2 equivalent, etc.
 * Provides callbacks for new data and cloud upload events.
 */
class Bme690Service {
public:
    // Latest sensor data structure
    struct SensorData {
        float temperature = 0.0f;      // °C (BSEC compensated)
        float humidity = 0.0f;         // %RH (BSEC compensated)
        float pressure = 0.0f;         // Pa
        float iaq = 0.0f;              // Indoor Air Quality Index (0-500)
        uint8_t iaq_accuracy = 0;      // 0=unreliable, 1=low, 2=medium, 3=high
        float co2_equivalent = 0.0f;   // ppm
        float breath_voc_equivalent = 0.0f; // ppm
        float gas_resistance = 0.0f;   // Ohm
        bool valid = false;
    };

    // Callback type for new sensor data
    using DataCallback = std::function<void(const SensorData& data)>;

    Bme690Service();
    ~Bme690Service();

    /**
     * @brief Initialize the BME690 sensor service
     * @param i2c_bus Already initialized I2C bus handle (shared with touch)
     * @return ESP_OK on success
     */
    esp_err_t Initialize(i2c_master_bus_handle_t i2c_bus);

    /**
     * @brief Start the background sensor reading task
     */
    void Start();

    /**
     * @brief Stop the background task
     */
    void Stop();

    /**
     * @brief Get the latest sensor data (thread-safe)
     */
    SensorData GetLatestData() const;

    /**
     * @brief Set callback for new sensor data
     */
    void SetDataCallback(DataCallback callback);

    /**
     * @brief Check if service is running
     */
    bool IsRunning() const { return running_; }

    /**
     * @brief Get sensor data as JSON string (for cloud upload)
     */
    std::string GetDataJson() const;

private:
    // BSEC configuration
    static constexpr float kSampleRateLp = 0.33333f;  // Low Power: every 3s
    static constexpr float TEMP_OFFSET = 5.0f;        // Self-heating compensation

    // Sensor hardware
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t i2c_dev_ = nullptr;
    struct bme69x_dev bme_;
    struct bme69x_data bme_data_;

    // BSEC state
    uint8_t bsec_state_[BSEC_MAX_STATE_BLOB_SIZE];
    int64_t time_stamp_last_state_save_ = 0;
    static constexpr uint64_t STATE_SAVE_PERIOD_US = 360ULL * 60 * 1000000; // 6 hours

    // Latest data (protected by mutex)
    mutable SemaphoreHandle_t data_mutex_ = nullptr;
    SensorData latest_data_;

    // Task control
    std::atomic<bool> running_{false};
    TaskHandle_t task_handle_ = nullptr;
    DataCallback data_callback_;

    // Internal methods
    esp_err_t init_bme69x_sensor();
    bsec_library_return_t init_bsec();
    void bsec_task_run();
    void output_ready(int64_t timestamp, float iaq, uint8_t iaq_accuracy,
                      float temperature, float humidity, float pressure,
                      float raw_temperature, float raw_humidity,
                      float gas, bsec_library_return_t bsec_status,
                      float static_iaq, float co2_equivalent,
                      float breath_voc_equivalent);
    static int64_t get_timestamp_us();
    static void task_wrapper(void* arg);

    // I2C callbacks for Bosch API
    static int8_t i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static int8_t i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static void i2c_delay_us(uint32_t period_us, void *intf_ptr);
};

#endif // BME690_SERVICE_H
