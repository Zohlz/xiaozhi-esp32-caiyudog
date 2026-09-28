#ifndef CAIYU_DISPLAY_H
#define CAIYU_DISPLAY_H

#include <string>

#include "caiyu_emotion.h"
#include "caiyu_eyes_renderer.h"
#include "caiyu_font_renderer.h"
#include "display/oled_display.h"

class CaiyuDisplay : public OledDisplay {
public:
    CaiyuDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                 int height, bool mirror_x, bool mirror_y);
    ~CaiyuDisplay() override;

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;

    caiyu::Emotion current_emotion() const { return emotion_engine_.emotion(); }

    void SetPreviewEmotion(const char* emotion);

    void SetLookProvider(caiyu::EyesRenderer::LookProvider provider) {
        eyes_renderer_.SetLookProvider(std::move(provider));
    }

    void ShowQrCode(const std::string& text, int duration_ms = kQrCodeDurationMs);
    void HideQrCode();
    bool qr_code_visible() const { return qr_code_visible_; }

private:
    static constexpr int kQrCodeVersion = 2;
    static constexpr size_t kQrCodeMaxBytes = 26;
    static constexpr int kQrCodeModules = kQrCodeVersion * 4 + 17;
    static constexpr int kQrCodeScale = 2;
    static constexpr int kQrCodeQuietPx = 7;
    static constexpr int kQrCodeSize = kQrCodeModules * kQrCodeScale + kQrCodeQuietPx * 2;
    static constexpr size_t kQrCodeBytes = static_cast<size_t>(kQrCodeSize) * kQrCodeSize;
    static constexpr int kQrCodeDurationMs = 15000;

    static void HideQrTimerCallback(lv_timer_t* timer);
    void HideQrCodeLocked();

    static void FlushWaitCallback(lv_display_t* disp);

    bool WantsFullscreenEmotion() const;
    void SyncLayout();
    void ApplyEmotionLocked(const char* emotion);

    caiyu::EmotionEngine emotion_engine_;
    caiyu::EyesRenderer eyes_renderer_;
    caiyu::FontRenderer font_renderer_;

    bool preview_emotion_ = false;

    lv_obj_t* qr_code_ = nullptr;
    lv_obj_t* qr_code_canvas_ = nullptr;
    lv_timer_t* qr_code_timer_ = nullptr;
    bool qr_code_visible_ = false;
    uint8_t* qr_code_buf_ = nullptr;
};

#endif
