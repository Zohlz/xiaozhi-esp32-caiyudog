#include "strip_controller.h"

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_cpu.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>

#include <algorithm>
#include <cstdint>

#include "config.h"

#define TAG "caiyu_strip"

constexpr uint32_t kT0H = 64;
constexpr uint32_t kT1H = 128;
constexpr uint32_t kBitPeriod = 200;

constexpr uint32_t kResetUs = 300;
constexpr uint32_t kSampleSettleUs = 3;
constexpr uint32_t kTouchWarmupMs = 500;
constexpr uint32_t kLoopPeriodTicks = 1;
constexpr uint8_t kDebounceSamples = 3;
constexpr uint32_t kLongPressMs = 1000;
constexpr uint32_t kTickMs = 1000 / configTICK_RATE_HZ;
constexpr uint32_t kDoubleClickMs = 300;
constexpr uint32_t kClickHoldMs = kDoubleClickMs + kDebounceSamples * kTickMs;
constexpr int kDetectSamples = 8;

constexpr uint16_t kRainbowSteps = 768;
constexpr uint16_t kRainbowStepPerTick = 3;
constexpr int kBreatheStepHalf = 2;
constexpr uint8_t kFlashHalfTicks = 50;
constexpr uint8_t kFlowStepTicks = 12;
constexpr float kFlowDecay = 0.7f;
constexpr uint8_t kFillIntervalTicks = 5;

namespace {

constexpr uint8_t kFixedColors[10][3] = {
    {0, 47, 167},
    {136, 17, 31},
    {80, 109, 69},
    {227, 57, 60},
    {230, 0, 18},
    {0, 49, 83},
    {114, 20, 44},
    {255, 204, 0},
    {119, 201, 223},
    {94, 60, 34},
};

IRAM_ATTR void SendByte(uint8_t data, uint32_t mask) {
    for (int i = 7; i >= 0; --i) {
        const uint32_t t = esp_cpu_get_cycle_count();
        REG_WRITE(GPIO_OUT_W1TS_REG, mask);
        const uint32_t high = (data & (1u << i)) ? kT1H : kT0H;
        while ((uint32_t)(esp_cpu_get_cycle_count() - t) < high) {
        }
        REG_WRITE(GPIO_OUT_W1TC_REG, mask);
        while ((uint32_t)(esp_cpu_get_cycle_count() - t) < kBitPeriod) {
        }
    }
}

int ReadPinLevel(gpio_num_t gpio, gpio_pull_mode_t pull) {
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, pull);
    esp_rom_delay_us(kSampleSettleUs);
    return gpio_get_level(gpio);
}

}

StripController::StripController(gpio_num_t gpio, uint16_t led_count)
    : gpio_(gpio), led_count_(led_count) {
    if (led_count_ == 0) {
        led_count_ = 1;
    } else if (led_count_ > kMaxLeds) {
        led_count_ = kMaxLeds;
    }
    std::fill(pixel_buf_, pixel_buf_ + sizeof(pixel_buf_), 0);
    InitializeGpio();
}

StripController::~StripController() {
    Stop();
    std::fill(pixel_buf_, pixel_buf_ + sizeof(pixel_buf_), 0);
}

void StripController::InitializeGpio() {
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << gpio_;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
}

void StripController::Start() {
    if (running_) {
        return;
    }
    running_ = true;
    BaseType_t rc = xTaskCreatePinnedToCore(TaskEntry, TAG, 3072, this, 2, &task_, tskNO_AFFINITY);
    if (rc != pdPASS) {
        running_ = false;
        task_ = nullptr;
        ESP_LOGE(TAG, "Failed to create strip task");
    }
}

void StripController::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    for (int i = 0; i < 20 && task_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    task_ = nullptr;
}

void StripController::TaskEntry(void* arg) { static_cast<StripController*>(arg)->Run(); }

void StripController::DetectTouchPolarity() {
#if TOUCH_BUTTON_ACTIVE_HIGH == 1
    touch_active_high_ = true;
    sample_pull_ = GPIO_PULLDOWN_ONLY;
    ESP_LOGI(TAG, "touch polarity: forced active-high (TOUCH_BUTTON_ACTIVE_HIGH=1)");
#elif TOUCH_BUTTON_ACTIVE_HIGH == 0
    touch_active_high_ = false;
    sample_pull_ = GPIO_PULLUP_ONLY;
    ESP_LOGI(TAG, "touch polarity: forced active-low (TOUCH_BUTTON_ACTIVE_HIGH=0)");
#else
    int up_high = 0;
    int down_high = 0;
    for (int i = 0; i < kDetectSamples; ++i) {
        up_high += ReadPinLevel(gpio_, GPIO_PULLUP_ONLY) ? 1 : 0;
        down_high += ReadPinLevel(gpio_, GPIO_PULLDOWN_ONLY) ? 1 : 0;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    touch_active_high_ = (up_high <= kDetectSamples / 2);
    sample_pull_ = touch_active_high_ ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY;
    ESP_LOGI(TAG, "touch polarity auto: pullup_high=%d/%d pulldown_high=%d/%d -> %s", up_high,
             kDetectSamples, down_high, kDetectSamples,
             touch_active_high_ ? "active-high" : "active-low");
#endif
}

void StripController::Run() {
    ESP_LOGI(TAG, "strip task running: gpio=%d, leds=%d", (int)gpio_, led_count_);

    gpio_set_direction(gpio_, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio_, GPIO_FLOATING);

    vTaskDelay(pdMS_TO_TICKS(kTouchWarmupMs));

    Fill(0, 0, 0);
    Refresh();

    DetectTouchPolarity();

    TickType_t last_wake = xTaskGetTickCount();
    while (running_) {
        const bool pressed = SampleTouch();
        UpdateButton(pressed);

        gpio_set_direction(gpio_, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(gpio_, GPIO_FLOATING);

        RenderEffect();
        Refresh();

        xTaskDelayUntil(&last_wake, kLoopPeriodTicks);
    }

    task_ = nullptr;
}

bool StripController::SampleTouch() {
    const int level = ReadPinLevel(gpio_, sample_pull_);
    return touch_active_high_ ? (level != 0) : (level == 0);
}

void StripController::UpdateButton(bool pressed) {
    if (pressed) {
        if (debounce_ < kDebounceSamples) {
            debounce_++;
            if (debounce_ == kDebounceSamples) {
                press_start_ = xTaskGetTickCount();
                long_fired_ = false;
                if (click_pending_) {
                    const uint32_t gap_ms = (uint32_t)(press_start_ - click_start_) * kTickMs;
                    click_pending_ = false;
                    double_fired_ = (gap_ms <= kDoubleClickMs);
                } else {
                    double_fired_ = false;
                }
            }
        }
        release_ = 0;

        if (debounce_ >= kDebounceSamples && !long_fired_) {
            const uint32_t held_ms = (uint32_t)(xTaskGetTickCount() - press_start_) * kTickMs;
            if (held_ms >= kLongPressMs) {
                long_fired_ = true;
                ESP_LOGI(TAG, "long press");
                if (long_press_cb_) {
                    long_press_cb_();
                }
            }
        }
    } else {
        if (release_ < kDebounceSamples) {
            release_++;
            if (release_ == kDebounceSamples) {
                if (debounce_ >= kDebounceSamples && !long_fired_) {
                    if (double_fired_) {
                        double_fired_ = false;
                        ESP_LOGI(TAG, "double click");
                        if (double_click_cb_) {
                            double_click_cb_();
                        }
                    } else {
                        click_pending_ = true;
                        click_start_ = xTaskGetTickCount();
                    }
                }
                debounce_ = 0;
                long_fired_ = false;
            }
        }
    }

    if (click_pending_) {
        const uint32_t waited_ms = (uint32_t)(xTaskGetTickCount() - click_start_) * kTickMs;
        if (waited_ms >= kClickHoldMs) {
            click_pending_ = false;
            ESP_LOGI(TAG, "single click");
            if (click_cb_) {
                click_cb_();
            }
        }
    }
}

uint8_t StripController::ScaledTicks(uint8_t base) const {
    const int speed = speed_percent_;
    if (speed <= 0 || speed == 100) {
        return base;
    }
    int ticks = static_cast<int>(base) * 100 / speed;
    if (ticks < 1) {
        ticks = 1;
    } else if (ticks > 255) {
        ticks = 255;
    }
    return static_cast<uint8_t>(ticks);
}

int StripController::ScaledStep(int base) const {
    const int speed = speed_percent_;
    if (speed <= 0 || speed == 100) {
        return base;
    }
    const int step = base * speed / 100;
    return (step < 1) ? 1 : step;
}

void StripController::RenderEffect() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    switch (mode_) {
        case kModeStatic: {
            if (fill_leds_ < led_count_ && ++fill_ticks_ >= ScaledTicks(kFillIntervalTicks)) {
                fill_ticks_ = 0;
                fill_leds_++;
            }
            for (uint16_t i = 0; i < led_count_; ++i) {
                if (i < fill_leds_) {
                    SetPixel(i, color_[0], color_[1], color_[2]);
                } else {
                    SetPixel(i, 0, 0, 0);
                }
            }
            break;
        }

        case kModeBreathe: {
            const int step = ScaledStep(kBreatheStepHalf);
            const uint8_t* c = kFixedColors[color_index_];
            if (direction_ > 0) {
                if (level_ >= 200) {
                    direction_ = -1;
                } else {
                    level_ = (uint8_t)std::min<int>(200, level_ + step);
                }
            } else if (level_ <= step) {
                level_ = 0;
                direction_ = 1;
                color_index_ = (uint8_t)((color_index_ + 1) % 10);
            } else {
                level_ = (uint8_t)(level_ - step);
            }
            const uint8_t s = (uint8_t)((uint16_t)level_ * 255 / 200);
            Fill((uint8_t)(c[0] * s / 255), (uint8_t)(c[1] * s / 255), (uint8_t)(c[2] * s / 255));
            break;
        }

        case kModeRainbow: {
            for (uint16_t i = 0; i < led_count_; ++i) {
                const uint16_t hue =
                    (uint16_t)((rainbow_step_ + (uint32_t)i * kRainbowSteps / led_count_) %
                               kRainbowSteps);
                uint8_t r = 0, g = 0, b = 0;
                if (hue < 256) {
                    r = (uint8_t)(255 - hue);
                    g = (uint8_t)hue;
                } else if (hue < 512) {
                    g = (uint8_t)(255 - (hue - 256));
                    b = (uint8_t)(hue - 256);
                } else {
                    r = (uint8_t)(hue - 512);
                    b = (uint8_t)(255 - (hue - 512));
                }
                SetPixel(i, r, g, b);
            }
            rainbow_step_ =
                (uint16_t)((rainbow_step_ + ScaledStep(kRainbowStepPerTick)) % kRainbowSteps);
            break;
        }

        case kModeFlow: {
            if (++flow_ticks_ >= ScaledTicks(kFlowStepTicks)) {
                flow_ticks_ = 0;
                flow_pos_ = (uint16_t)((flow_pos_ + 1) % led_count_);
            }
            for (uint16_t i = 0; i < led_count_; ++i) {
                const uint16_t d = (uint16_t)((i + led_count_ - flow_pos_) % led_count_);
                uint8_t f = 0;
                if (d == 0) {
                    f = 255;
                } else if (d == 1) {
                    f = (uint8_t)(255 * kFlowDecay);
                }
                SetPixel(i, (uint8_t)(color_[0] * f / 255), (uint8_t)(color_[1] * f / 255),
                         (uint8_t)(color_[2] * f / 255));
            }
            break;
        }

        case kModeFlash:
            if (++flash_ticks_ >= ScaledTicks(kFlashHalfTicks)) {
                flash_ticks_ = 0;
                flash_on_ = flash_on_ ? 0 : 1;
            }
            if (flash_on_) {
                Fill(color_[0], color_[1], color_[2]);
            } else {
                Fill(0, 0, 0);
            }
            break;

        case kModeOff:
        default:
            Fill(0, 0, 0);
            break;
    }
}

void StripController::Refresh() {
    const uint32_t mask = 1u << gpio_;

    REG_WRITE(GPIO_OUT_W1TC_REG, mask);
    esp_rom_delay_us(kResetUs);

    portENTER_CRITICAL(&strip_mux_);
    for (uint16_t i = 0; i < led_count_ * 3; ++i) {
        SendByte(pixel_buf_[i], mask);
    }
    portEXIT_CRITICAL(&strip_mux_);

    REG_WRITE(GPIO_OUT_W1TC_REG, mask);
}

void StripController::SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= led_count_) {
        return;
    }
    uint8_t* p = pixel_buf_ + index * 3;
    p[0] = Scale(g);
    p[1] = Scale(r);
    p[2] = Scale(b);
}

void StripController::Fill(uint8_t r, uint8_t g, uint8_t b) {
    for (uint16_t i = 0; i < led_count_; ++i) {
        SetPixel(i, r, g, b);
    }
}

uint8_t StripController::Scale(uint8_t value) const {
    return (uint8_t)((uint16_t)brightness_ * value / 100);
}

void StripController::TurnOn(uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    color_[0] = r;
    color_[1] = g;
    color_[2] = b;
    mode_ = kModeStatic;
    is_on_ = true;
    fill_leds_ = 0;
    fill_ticks_ = 0;
}

void StripController::TurnOff() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    mode_ = kModeOff;
    is_on_ = false;
}

void StripController::SetBreathe() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    mode_ = kModeBreathe;
    is_on_ = true;
    level_ = 0;
    direction_ = 1;
}

void StripController::SetRainbow() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    mode_ = kModeRainbow;
    is_on_ = true;
    rainbow_step_ = 0;
}

void StripController::SetFlow() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    mode_ = kModeFlow;
    is_on_ = true;
    flow_pos_ = 0;
    flow_ticks_ = 0;
}

void StripController::SetFlash() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    mode_ = kModeFlash;
    is_on_ = true;
    flash_ticks_ = 0;
    flash_on_ = 1;
}

void StripController::Toggle() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (is_on_) {
        mode_ = kModeOff;
        is_on_ = false;
    } else {
        color_[0] = 255;
        color_[1] = 255;
        color_[2] = 255;
        mode_ = kModeStatic;
        is_on_ = true;
        fill_leds_ = 0;
        fill_ticks_ = 0;
    }
}

void StripController::SetBrightness(uint8_t percent) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    brightness_ = std::max<uint8_t>(5, std::min<uint8_t>(100, percent));
}

void StripController::SetSpeedPercent(int percent) {
    if (percent < 50) {
        percent = 50;
    } else if (percent > 200) {
        percent = 200;
    }
    speed_percent_ = percent;
}

void StripController::SetClickHandler(std::function<void()> cb) { click_cb_ = cb; }

void StripController::SetLongPressHandler(std::function<void()> cb) { long_press_cb_ = cb; }

void StripController::SetDoubleClickHandler(std::function<void()> cb) { double_click_cb_ = cb; }
