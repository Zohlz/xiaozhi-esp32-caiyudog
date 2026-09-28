#ifndef _STRIP_CONTROLLER_H_
#define _STRIP_CONTROLLER_H_

#include <driver/gpio.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>

#include <functional>
#include <mutex>

class StripController {
public:
    StripController(gpio_num_t gpio, uint16_t led_count);
    ~StripController();

    StripController(const StripController&) = delete;
    StripController& operator=(const StripController&) = delete;

    void Start();
    void Stop();

    void TurnOn(uint8_t r, uint8_t g, uint8_t b);
    void TurnOff();
    void SetBreathe();
    void SetRainbow();
    void SetFlow();
    void SetFlash();
    void Toggle();
    void SetBrightness(uint8_t percent);
    void SetSpeedPercent(int percent);
    int speed_percent() const { return speed_percent_; }

    void SetClickHandler(std::function<void()> cb);
    void SetLongPressHandler(std::function<void()> cb);
    void SetDoubleClickHandler(std::function<void()> cb);

    bool IsOn() const { return is_on_; }
    int Mode() const { return mode_; }
    uint8_t Brightness() const { return brightness_; }
    bool TouchActiveHigh() const { return touch_active_high_; }

    void GetColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
        r = color_[0];
        g = color_[1];
        b = color_[2];
    }

private:
    static constexpr uint16_t kMaxLeds = 64;

    enum : uint8_t {
        kModeOff = 0,
        kModeStatic = 1,
        kModeBreathe = 2,
        kModeRainbow = 3,
        kModeFlow = 4,
        kModeFlash = 5,
    };

    gpio_num_t gpio_;
    uint16_t led_count_;
    uint8_t pixel_buf_[kMaxLeds * 3]{};

    TaskHandle_t task_ = nullptr;
    volatile bool running_ = false;

    portMUX_TYPE strip_mux_ = portMUX_INITIALIZER_UNLOCKED;
    std::mutex state_mutex_;

    int mode_ = kModeOff;
    bool is_on_ = false;
    uint8_t brightness_ = 80;
    uint8_t color_[3] = {255, 255, 255};
    volatile int speed_percent_ = 100;

    uint8_t color_index_ = 0;
    uint8_t level_ = 0;
    int8_t direction_ = 1;
    uint16_t rainbow_step_ = 0;
    uint16_t flow_pos_ = 0;
    uint8_t flow_ticks_ = 0;
    uint8_t flash_ticks_ = 0;
    uint8_t flash_on_ = 0;
    uint8_t fill_leds_ = 0;
    uint8_t fill_ticks_ = 0;

    std::function<void()> click_cb_;
    std::function<void()> long_press_cb_;
    std::function<void()> double_click_cb_;

    bool touch_active_high_ = true;
    gpio_pull_mode_t sample_pull_ = GPIO_PULLDOWN_ONLY;

    uint8_t debounce_ = 0;
    uint8_t release_ = 0;
    bool long_fired_ = false;
    TickType_t press_start_ = 0;
    bool click_pending_ = false;
    bool double_fired_ = false;
    TickType_t click_start_ = 0;

    void InitializeGpio();
    void DetectTouchPolarity();
    void Run();
    static void TaskEntry(void* arg);
    bool SampleTouch();
    void UpdateButton(bool pressed);
    void RenderEffect();
    void IRAM_ATTR Refresh();
    void Fill(uint8_t r, uint8_t g, uint8_t b);
    void SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
    uint8_t Scale(uint8_t value) const;

    uint8_t ScaledTicks(uint8_t base) const;
    int ScaledStep(int base) const;
};

#endif
