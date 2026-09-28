#include "caiyu_display.h"

#include "qrcodegen.h"

#include <esp_log.h>

#include <cstdlib>
#include <cstring>

#include "application.h"

#define TAG "CaiyuDisplay"

namespace {
constexpr const char* kIdleEmotion = "sleepy";
}

CaiyuDisplay::CaiyuDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {
    if (display_ != nullptr) {
        lv_display_set_flush_wait_cb(display_, FlushWaitCallback);
    }
}

void CaiyuDisplay::FlushWaitCallback(lv_display_t* disp) {
    (void)disp;
    static uint32_t last_log_tick = 0;
    const uint32_t now = lv_tick_get();
    if (now - last_log_tick >= 1000) {
        last_log_tick = now;
        ESP_LOGW(TAG, "previous flush never completed (i2c error?); skipping the wait");
    }
}

CaiyuDisplay::~CaiyuDisplay() {
    DisplayLockGuard lock(this);

    if (qr_code_timer_ != nullptr) {
        lv_timer_delete(qr_code_timer_);
        qr_code_timer_ = nullptr;
    }
    if (qr_code_ != nullptr) {
        lv_obj_del(qr_code_);
        qr_code_ = nullptr;
        qr_code_canvas_ = nullptr;
    }
    if (qr_code_buf_ != nullptr) {
        free(qr_code_buf_);
        qr_code_buf_ = nullptr;
    }

    emotion_engine_.Deinit();
}

bool CaiyuDisplay::WantsFullscreenEmotion() const {
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateIdle:
        case kDeviceStateConnecting:
        case kDeviceStateListening:
        case kDeviceStateSpeaking:
            return true;
        default:
            return false;
    }
}

void CaiyuDisplay::SetupUI() {
    OledDisplay::SetupUI();

    if (height_ != 64) {
        ESP_LOGW(TAG, "emotion engine skipped for %dx%d layout", width_, height_);
        return;
    }

    DisplayLockGuard lock(this);

    emotion_engine_.AddRenderer(&eyes_renderer_);
    emotion_engine_.AddRenderer(&font_renderer_);

    if (!emotion_engine_.Init(lv_screen_active(), width_, height_)) {
        ESP_LOGW(TAG, "emotion engine init failed, keep the regular UI");
        return;
    }
    ESP_LOGI(TAG, "fullscreen emotion layer ready (%dx%d)", width_, height_);
    SyncLayout();
}

void CaiyuDisplay::SyncLayout() {
    if (!emotion_engine_.initialized()) {
        return;
    }
    if (qr_code_visible_) {
        SetRegularUiVisible(false);
        emotion_engine_.SetEnabled(false);
        return;
    }
    const bool fullscreen = WantsFullscreenEmotion();
    SetRegularUiVisible(!fullscreen);
    emotion_engine_.SetEnabled(fullscreen);
}

void CaiyuDisplay::SetStatus(const char* status) {
    OledDisplay::SetStatus(status);

    DisplayLockGuard lock(this);
    SyncLayout();
}

void CaiyuDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);

    if (!emotion_engine_.initialized()) {
        OledDisplay::SetEmotion(emotion);
        return;
    }

    SyncLayout();
    ApplyEmotionLocked(emotion);
}

void CaiyuDisplay::ApplyEmotionLocked(const char* emotion) {
    if (!WantsFullscreenEmotion()) {
        OledDisplay::SetEmotion(emotion);
        return;
    }

    const DeviceState state = Application::GetInstance().GetDeviceState();
    const bool idle = (state == kDeviceStateIdle);

    emotion_engine_.SetEmotion((idle && !preview_emotion_) ? kIdleEmotion : emotion);
}

void CaiyuDisplay::SetPreviewEmotion(const char* emotion) {
    if (emotion != nullptr && strcmp(emotion, "auto") == 0) {
        preview_emotion_ = false;
        SetEmotion("neutral");
        return;
    }
    preview_emotion_ = true;
    SetEmotion(emotion);
}

void CaiyuDisplay::ShowQrCode(const std::string& text, int duration_ms) {
    if (text.empty()) {
        return;
    }
    if (height_ < kQrCodeSize || width_ < kQrCodeSize) {
        ESP_LOGW(TAG, "screen %dx%d can't fit a %dpx qrcode", width_, height_, kQrCodeSize);
        return;
    }
    if (text.size() > kQrCodeMaxBytes) {
        ESP_LOGW(TAG, "qrcode payload too long (%u > %d bytes)", (unsigned)text.size(),
                 (int)kQrCodeMaxBytes);
        return;
    }

    DisplayLockGuard lock(this);

    if (qr_code_ == nullptr) {
        qr_code_ = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(qr_code_);
        lv_obj_set_size(qr_code_, width_, height_);
        lv_obj_set_pos(qr_code_, 0, 0);
        lv_obj_set_style_bg_color(qr_code_, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(qr_code_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(qr_code_, LV_OBJ_FLAG_SCROLLABLE);

        qr_code_timer_ = lv_timer_create(HideQrTimerCallback, duration_ms, this);
        lv_timer_pause(qr_code_timer_);
    }

    if (qr_code_canvas_ == nullptr) {
        qr_code_buf_ = static_cast<uint8_t*>(malloc(kQrCodeBytes));
        if (qr_code_buf_ == nullptr) {
            ESP_LOGE(TAG, "no %u bytes for the qrcode bitmap", static_cast<unsigned>(kQrCodeBytes));
            return;
        }
        qr_code_canvas_ = lv_canvas_create(qr_code_);
        lv_canvas_set_buffer(qr_code_canvas_, qr_code_buf_, kQrCodeSize, kQrCodeSize,
                             LV_COLOR_FORMAT_L8);
        lv_obj_center(qr_code_canvas_);
    }

    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrCodeVersion)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrCodeVersion)];
    if (!qrcodegen_encodeText(text.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, kQrCodeVersion,
                              kQrCodeVersion, qrcodegen_Mask_AUTO, true)) {
        ESP_LOGW(TAG, "qrcode encode failed: %s", text.c_str());
        return;
    }

    memset(qr_code_buf_, 0xFF, kQrCodeBytes);
    const int modules = qrcodegen_getSize(qr);
    for (int my = 0; my < modules; ++my) {
        for (int mx = 0; mx < modules; ++mx) {
            if (!qrcodegen_getModule(qr, mx, my)) {
                continue;
            }
            const int px = kQrCodeQuietPx + mx * kQrCodeScale;
            const int py = kQrCodeQuietPx + my * kQrCodeScale;
            for (int dy = 0; dy < kQrCodeScale; ++dy) {
                memset(qr_code_buf_ + (size_t)(py + dy) * kQrCodeSize + px, 0x00,
                       kQrCodeScale);
            }
        }
    }
    lv_obj_invalidate(qr_code_canvas_);

    lv_obj_move_foreground(qr_code_);
    lv_obj_remove_flag(qr_code_, LV_OBJ_FLAG_HIDDEN);
    qr_code_visible_ = true;
    SyncLayout();

    lv_timer_set_period(qr_code_timer_, duration_ms);
    lv_timer_reset(qr_code_timer_);
    lv_timer_resume(qr_code_timer_);

    ESP_LOGI(TAG, "qrcode shown (%u bytes): %s", (unsigned)text.size(), text.c_str());
}

void CaiyuDisplay::HideQrCode() {
    DisplayLockGuard lock(this);
    HideQrCodeLocked();
}

void CaiyuDisplay::HideQrCodeLocked() {
    if (!qr_code_visible_) {
        return;
    }
    if (qr_code_timer_ != nullptr) {
        lv_timer_pause(qr_code_timer_);
    }
    if (qr_code_canvas_ != nullptr) {
        lv_obj_del(qr_code_canvas_);
        qr_code_canvas_ = nullptr;
    }
    if (qr_code_buf_ != nullptr) {
        free(qr_code_buf_);
        qr_code_buf_ = nullptr;
    }
    lv_obj_add_flag(qr_code_, LV_OBJ_FLAG_HIDDEN);
    qr_code_visible_ = false;
    SyncLayout();
    ESP_LOGI(TAG, "qrcode hidden");
}

void CaiyuDisplay::HideQrTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<CaiyuDisplay*>(lv_timer_get_user_data(timer));
    lv_timer_pause(timer);
    self->HideQrCodeLocked();
}
