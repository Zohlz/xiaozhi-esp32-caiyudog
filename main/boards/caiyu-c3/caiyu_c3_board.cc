#include "wifi_board.h"
#include "caiyu_actionments.h"
#include "caiyu_display.h"
#include "caiyu_emotion.h"
#include "caiyu_web_server.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "config.h"
#include "mcp_server.h"
#include "strip_controller.h"

#include <esp_log.h>
#include <wifi_manager.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_sh1106.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <string>

#define TAG "CaiyuC3Board"

namespace {

std::string IpOfInterface(const char* key) {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey(key);
    if (netif == nullptr) {
        return {};
    }
    esp_netif_ip_info_t info = {};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return {};
    }
    char buffer[16];
    snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&info.ip));
    return buffer;
}

constexpr float kLookSideways = 0.5f;
constexpr float kLookVertical = 0.35f;

constexpr int kRandomEmotionHoldMs = 4000;

constexpr uint32_t kConfigModeGuardMs = 5000;

}

class CaiyuC3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    CaiyuDisplay* caiyu_display_ = nullptr;
    StripController* strip_ = nullptr;
    CaiyuWebServer* web_server_ = nullptr;
    caiyu::Actionments* actionments_ = nullptr;
    esp_timer_handle_t emotion_timer_ = nullptr;
    PowerSaveLevel applied_power_save_ = PowerSaveLevel::LOW_POWER;
    TickType_t config_mode_guard_until_ = 0;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSh1106Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = 0x3C;
        io_config.scl_speed_hz = 400 * 1000;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.on_color_trans_done = nullptr;
        io_config.user_ctx = nullptr;
        io_config.flags.dc_low_on_data = 0;
        io_config.flags.disable_control_phase = 0;

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SH1106 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_sh1106_config_t sh1106_config = {};
        panel_config.vendor_config = &sh1106_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SH1106 driver installed");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new CaiyuDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        caiyu_display_ = static_cast<CaiyuDisplay*>(display_);
    }

    void StopControlServer() {
        if (web_server_ != nullptr) {
            web_server_->Stop();
        }
    }

    void RefreshPowerSaveLevel() {
        const auto state = Application::GetInstance().GetDeviceState();
        const bool app_active = state == kDeviceStateConnecting ||
                                state == kDeviceStateListening ||
                                state == kDeviceStateSpeaking ||
                                state == kDeviceStateUpgrading;
        const bool qr_visible = caiyu_display_ != nullptr && caiyu_display_->qr_code_visible();
        const bool serving = web_server_ != nullptr && web_server_->HasClients();
        const PowerSaveLevel target = (app_active || qr_visible || serving)
                                          ? PowerSaveLevel::PERFORMANCE
                                          : PowerSaveLevel::LOW_POWER;
        if (target == applied_power_save_) {
            return;
        }
        ESP_LOGI(TAG, "power save -> %s",
                 (target == PowerSaveLevel::PERFORMANCE) ? "performance" : "low");
        SetPowerSaveLevel(target);
    }

    void StartControlServerTask() {
        xTaskCreate([](void* arg) {
            auto* board = static_cast<CaiyuC3Board*>(arg);
            board->web_server_ = new CaiyuWebServer(board->caiyu_display_, board->strip_,
                                                    board->actionments_);

            bool address_logged = false;
            DeviceState last_state = Application::GetInstance().GetDeviceState();
            caiyu::Emotion last_emotion = caiyu::Emotion::kNeutral;
            TickType_t last_mood_tick = 0;
            constexpr uint32_t kMoodCooldownMs = 2000;
            while (true) {
                const bool in_config = board->IsInWifiConfigMode();
                if (in_config) {
                    board->config_mode_guard_until_ = 0;
                }
                const bool config_guard =
                    board->config_mode_guard_until_ != 0 &&
                    static_cast<int32_t>(board->config_mode_guard_until_ -
                                         xTaskGetTickCount()) > 0;
                const bool can_serve = !config_guard && !in_config &&
                                       WifiManager::GetInstance().IsConnected();
                if (!can_serve) {
                    board->StopControlServer();
                    address_logged = false;
                } else {
                    if (!board->web_server_->started() && !board->web_server_->Start()) {
                        vTaskDelay(pdMS_TO_TICKS(10000));
                        continue;
                    }
                    if (!address_logged) {
                        const std::string ip = IpOfInterface("WIFI_STA_DEF");
                        if (!ip.empty()) {
                            ESP_LOGI(TAG, "control page: http://%s/", ip.c_str());
                            address_logged = true;
                        }
                    }
                }
                board->RefreshPowerSaveLevel();
                board->web_server_->SyncChatState();

                const DeviceState state = Application::GetInstance().GetDeviceState();
                if (state != last_state) {
                    const bool was_talking = last_state == kDeviceStateConnecting ||
                                             last_state == kDeviceStateListening ||
                                             last_state == kDeviceStateSpeaking;
                    if (state == kDeviceStateIdle && was_talking &&
                        board->actionments_ != nullptr &&
                        board->actionments_->current() != caiyu::Actionments::Action::kSleep) {
                        board->actionments_->Request(caiyu::Actionments::Action::kSleep);
                    }
                    last_state = state;
                }

                const bool in_chat = state == kDeviceStateConnecting ||
                                     state == kDeviceStateListening ||
                                     state == kDeviceStateSpeaking;
                if (in_chat && board->caiyu_display_ != nullptr &&
                    board->actionments_ != nullptr &&
                    board->actionments_->current() == caiyu::Actionments::Action::kCount) {
                    const caiyu::Emotion emotion = board->caiyu_display_->current_emotion();
                    if (emotion != last_emotion &&
                        (xTaskGetTickCount() - last_mood_tick) >= pdMS_TO_TICKS(kMoodCooldownMs)) {
                        const caiyu::Actionments::MoodMove move = MoodMoveForEmotion(emotion);
                        board->actionments_->RequestMoodMove(move);
                        if (move != caiyu::Actionments::MoodMove::kNone) {
                            last_mood_tick = xTaskGetTickCount();
                        }
                    }
                    last_emotion = emotion;
                }

                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }, "caiyu_web", 3072, this, tskIDLE_PRIORITY + 2, nullptr);
    }

    void InitializeStrip() {
        strip_ = new StripController(LIGHT_STRIP_GPIO, LED_NUM);

        strip_->SetClickHandler([this]() {
            if (caiyu_display_ != nullptr && caiyu_display_->qr_code_visible()) {
                Application::GetInstance().Schedule([this]() {
                    if (caiyu_display_ != nullptr) {
                        caiyu_display_->HideQrCode();
                    }
                });
                return;
            }

            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                Application::GetInstance().Schedule([this]() {
                    if (Application::GetInstance().GetDeviceState() == kDeviceStateStarting) {
                        EnterWifiConfigMode();
                    }
                });
                return;
            }
            if (actionments_ != nullptr &&
                app.GetDeviceState() == kDeviceStateIdle) {
                actionments_->Request(caiyu::Actionments::Action::kStand);
            }
            app.ToggleChatState();
        });

        strip_->SetLongPressHandler([this]() {
            if (strip_->IsOn()) {
                ESP_LOGI(TAG, "long press: lamp off");
                strip_->TurnOff();
                return;
            }
            Application::GetInstance().Schedule([this]() {
                if (caiyu_display_ == nullptr) {
                    return;
                }
                const std::string ip = IpOfInterface("WIFI_STA_DEF");
                if (ip.empty()) {
                    ESP_LOGW(TAG, "long press: no station IP yet, skip qrcode");
                    return;
                }
                caiyu_display_->ShowQrCode("http://" + ip + "/");
                RefreshPowerSaveLevel();
            });
        });

        strip_->SetDoubleClickHandler([this]() {
            if (caiyu_display_ == nullptr) {
                return;
            }
            size_t count = 0;
            const caiyu::EmotionNameEntry* table = caiyu::EmotionNameTable(&count);
            if (count <= 1) {
                return;
            }
            const char* name = table[1 + esp_random() % (count - 1)].name;
            ESP_LOGI(TAG, "double click: preview '%s' for %d ms", name, kRandomEmotionHoldMs);
            caiyu_display_->SetPreviewEmotion(name);
            StartEmotionRestoreTimer();
        });

        strip_->Start();
    }

    void StartEmotionRestoreTimer() {
        if (emotion_timer_ == nullptr) {
            esp_timer_create_args_t args = {};
            args.callback = &CaiyuC3Board::EmotionRestoreCallback;
            args.arg = this;
            args.name = "caiyu_emotion";
            if (esp_timer_create(&args, &emotion_timer_) != ESP_OK) {
                ESP_LOGW(TAG, "create emotion restore timer failed");
                return;
            }
        }
        esp_timer_stop(emotion_timer_);
        esp_timer_start_once(emotion_timer_, static_cast<uint64_t>(kRandomEmotionHoldMs) * 1000);
    }

    static void EmotionRestoreCallback(void* arg) {
        auto* board = static_cast<CaiyuC3Board*>(arg);
        if (board->caiyu_display_ != nullptr) {
            board->caiyu_display_->SetPreviewEmotion("auto");
        }
        ESP_LOGI(TAG, "double click: preview restored");
    }

    void InitializeActionments() {
        actionments_ = new caiyu::Actionments();
        actionments_->Init(LEFT_FRONT_LEG_PIN, RIGHT_FRONT_LEG_PIN, LEFT_BACK_LEG_PIN,
                           RIGHT_BACK_LEG_PIN);
        actionments_->Start();
    }

    void RegisterStripTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.lamp.control",
            "灯带控制与状态查询。action: on=常亮, off=关灯, breathe=呼吸灯, rainbow=彩虹灯, "
            "flow=流水灯, flash=闪光灯, set_brightness=设置亮度, get_status=开关状态(on/off), "
            "get_mode=当前模式(0-5), get_brightness=当前亮度; r/g/b: 颜色(0-255); "
            "brightness: 亮度(5-100)",
            PropertyList({
                Property("action", kPropertyTypeString),
                Property("r", kPropertyTypeInteger, 255, 0, 255),
                Property("g", kPropertyTypeInteger, 255, 0, 255),
                Property("b", kPropertyTypeInteger, 255, 0, 255),
                Property("brightness", kPropertyTypeInteger, 80, 5, 100)
            }),
            [this](const PropertyList& props) -> ReturnValue {
                if (strip_ == nullptr) {
                    return false;
                }
                const std::string action = props["action"].value<std::string>();
                const auto r = static_cast<uint8_t>(props["r"].value<int>());
                const auto g = static_cast<uint8_t>(props["g"].value<int>());
                const auto b = static_cast<uint8_t>(props["b"].value<int>());

                if (action == "on") {
                    strip_->TurnOn(r, g, b);
                } else if (action == "off") {
                    strip_->TurnOff();
                } else if (action == "breathe") {
                    strip_->TurnOn(r, g, b);
                    strip_->SetBreathe();
                } else if (action == "rainbow") {
                    strip_->SetRainbow();
                } else if (action == "flow") {
                    strip_->TurnOn(r, g, b);
                    strip_->SetFlow();
                } else if (action == "flash") {
                    strip_->TurnOn(r, g, b);
                    strip_->SetFlash();
                } else if (action == "set_brightness") {
                    strip_->SetBrightness(static_cast<uint8_t>(props["brightness"].value<int>()));
                } else if (action == "get_status") {
                    return strip_->IsOn() ? std::string("on") : std::string("off");
                } else if (action == "get_mode") {
                    return std::string("{\"mode\":") + std::to_string(strip_->Mode()) + "}";
                } else if (action == "get_brightness") {
                    return std::string("{\"brightness\":") + std::to_string(strip_->Brightness()) + "}";
                } else {
                    ESP_LOGW(TAG, "Unknown lamp action: %s", action.c_str());
                    return false;
                }
                ESP_LOGI(TAG, "lamp control: %s rgb=(%d,%d,%d)", action.c_str(), r, g, b);
                return true;
            });
    }

    void RegisterDogTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.dog.action",
            std::string("小狗动作控制与查询。每次调用只能执行一个动作，并且会等这个动作做完才返回。"
                        "用户要求多个动作时（如'先前进再左转'），你必须逐个调用本工具：先调用一个，"
                        "等它返回后用口语告诉用户刚做完什么，再调用下一个；"
                        "绝对不要一次连续调用多个动作而不在中间回复用户。"
                        "动作: ") + caiyu::Actionments::ActionGlossList() +
                ",get_current=当前正在执行的动作,get_legs=四条腿的角度"
                "；steps=走/转几个步态周期(1-10)，period=每拍毫秒数(100-300，越小越快)，"
                "这两个只对 walk / walk_back / turn_left / turn_right 有效，不填就用默认",
            PropertyList({
                Property("action", kPropertyTypeString),
                Property("steps", kPropertyTypeInteger, 0, 0, 10),
                Property("period", kPropertyTypeInteger, 0, 0, 300)
            }),
            [this](const PropertyList& props) -> ReturnValue {
                if (actionments_ == nullptr) {
                    return std::string("错误：动作系统未初始化");
                }
                const std::string name = props["action"].value<std::string>();

                if (name == "get_current") {
                    return std::string(caiyu::Actionments::ActionName(actionments_->current()));
                }
                if (name == "get_legs") {
                    std::string json;
                    json.reserve(96);
                    json += "{\"left_front\":";
                    json += std::to_string(actionments_->leg_angle(caiyu::kLeftFrontLeg));
                    json += ",\"right_front\":";
                    json += std::to_string(actionments_->leg_angle(caiyu::kRightFrontLeg));
                    json += ",\"left_back\":";
                    json += std::to_string(actionments_->leg_angle(caiyu::kLeftBackLeg));
                    json += ",\"right_back\":";
                    json += std::to_string(actionments_->leg_angle(caiyu::kRightBackLeg));
                    json += "}";
                    return json;
                }

                caiyu::Actionments::Action action;
                if (!caiyu::Actionments::ActionFromName(name.c_str(), &action)) {
                    ESP_LOGW(TAG, "unknown dog action: %s", name.c_str());
                    return std::string("错误：未知动作 ") + name;
                }
                if (!actionments_->Request(action, props["steps"].value<int>(),
                                           props["period"].value<int>())) {
                    return std::string("错误：动作没下发成功 ") + name;
                }
                return WaitDogAction(action, name);
            });
    }

    std::string WaitDogAction(caiyu::Actionments::Action action, const std::string& name) {
        constexpr int kPollMs = 50;
        constexpr int kMaxWaitMs = 60000;
        int waited = 0;
        while (waited < kMaxWaitMs && actionments_->current() == action) {
            vTaskDelay(pdMS_TO_TICKS(kPollMs));
            waited += kPollMs;
        }

        const caiyu::Actionments::Action now = actionments_->current();
        if (now == action) {
            ESP_LOGW(TAG, "action %s still running after %d ms", name.c_str(), kMaxWaitMs);
            return name + " 还在执行（等待超时）";
        }
        if (now != caiyu::Actionments::Action::kCount) {
            return name + " 被新指令打断，改成了 " + caiyu::Actionments::ActionName(now);
        }
        ESP_LOGI(TAG, "action %s done in %d ms", name.c_str(), waited);
        return name + " 已完成";
    }

    bool DriveLook(float* look_x, float* look_y) const {
        using caiyu::Actionments;
        switch (actionments_->drive()) {
            case Actionments::DriveDirection::kForward:
                *look_x = 0.0f;
                *look_y = kLookVertical;
                return true;
            case Actionments::DriveDirection::kBackward:
                *look_x = 0.0f;
                *look_y = -kLookVertical;
                return true;
            case Actionments::DriveDirection::kLeft:
                *look_x = -kLookSideways;
                *look_y = 0.0f;
                return true;
            case Actionments::DriveDirection::kRight:
                *look_x = kLookSideways;
                *look_y = 0.0f;
                return true;
            default:
                return false;
        }
    }

    bool ActionLook(float* look_x, float* look_y) const {
        using caiyu::Actionments;
        switch (actionments_->current()) {
            case Actionments::Action::kWalk:
                *look_x = 0.0f;
                *look_y = kLookVertical;
                return true;
            case Actionments::Action::kWalkBack:
                *look_x = 0.0f;
                *look_y = -kLookVertical;
                return true;
            case Actionments::Action::kTurnLeft:
                *look_x = -kLookSideways;
                *look_y = 0.0f;
                return true;
            case Actionments::Action::kTurnRight:
                *look_x = kLookSideways;
                *look_y = 0.0f;
                return true;
            default:
                return false;
        }
    }

    static caiyu::Actionments::MoodMove MoodMoveForEmotion(caiyu::Emotion emotion) {
        using MoodMove = caiyu::Actionments::MoodMove;
        switch (emotion) {
            case caiyu::Emotion::kHappy:
            case caiyu::Emotion::kSilly:
            case caiyu::Emotion::kDelicious:
                return MoodMove::kDip;
            case caiyu::Emotion::kLaughing:
            case caiyu::Emotion::kFunny:
                return MoodMove::kRock;
            case caiyu::Emotion::kAngry:
            case caiyu::Emotion::kSurprised:
            case caiyu::Emotion::kShocked:
                return MoodMove::kHipsShake;
            case caiyu::Emotion::kSad:
            case caiyu::Emotion::kCrying:
                return MoodMove::kTuck;
            case caiyu::Emotion::kEmbarrassed:
            case caiyu::Emotion::kConfused:
            case caiyu::Emotion::kThinking:
                return MoodMove::kSitSway;
            case caiyu::Emotion::kLoving:
            case caiyu::Emotion::kKissy:
            case caiyu::Emotion::kWinking:
                return MoodMove::kSitTap;
            case caiyu::Emotion::kCool:
                return MoodMove::kLegShake;
            case caiyu::Emotion::kRelaxed:
                return MoodMove::kLean;
            case caiyu::Emotion::kConfident:
                return MoodMove::kHipsShake;
            default:
                return MoodMove::kNone;
        }
    }

    bool MotionLook(float* look_x, float* look_y) const {
        if (actionments_ == nullptr) {
            return false;
        }
        return DriveLook(look_x, look_y) || ActionLook(look_x, look_y);
    }

    void InitializeMotionLook() {
        if (caiyu_display_ == nullptr) {
            return;
        }
        caiyu_display_->SetLookProvider([this](float* look_x, float* look_y) {
            return MotionLook(look_x, look_y);
        });
    }

public:
    CaiyuC3Board() {
        InitializeDisplayI2c();
        InitializeSh1106Display();
        InitializeStrip();
        InitializeActionments();
        InitializeMotionLook();
        RegisterStripTools();
        RegisterDogTools();
    }

    void EnterWifiConfigMode() {
        config_mode_guard_until_ = xTaskGetTickCount() + pdMS_TO_TICKS(kConfigModeGuardMs);
        StopControlServer();
        WifiBoard::EnterWifiConfigMode();
    }

    void SetPowerSaveLevel(PowerSaveLevel level) override {
        applied_power_save_ = level;
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual void StartNetwork() override {
        WifiBoard::StartNetwork();
        StartControlServerTask();
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

static_assert(static_cast<int>(TOUCH_BUTTON_GPIO) == static_cast<int>(LIGHT_STRIP_GPIO),
              "caiyu-c3: TOUCH_BUTTON_GPIO and LIGHT_STRIP_GPIO must be the same pin");

DECLARE_BOARD(CaiyuC3Board);
