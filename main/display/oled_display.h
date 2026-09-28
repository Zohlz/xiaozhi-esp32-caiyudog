#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>


class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t *emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t* fullscreen_image_ = nullptr;  // Fullscreen layer for idle GIF animation

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;

    // Fullscreen image layer used for the idle GIF animation.
    // The image is stretched to cover the whole screen.
    void ShowFullscreenImage(const lv_img_dsc_t* img_dsc);
    void HideFullscreenImage();
    // Called from the LVGL task while a GIF frame is updated.
    void InvalidateFullscreenImage();

protected:
    // Show/hide the regular UI: the top bar, the status bar and the chat/content
    // area. A board that draws its own fullscreen content (see the caiyu-c3 emotion
    // layer) hides it while the device is running and shows it again when text is
    // required, e.g. during Wi-Fi provisioning.
    // Must be called with the LVGL lock held.
    void SetRegularUiVisible(bool visible);
};

#endif // OLED_DISPLAY_H
