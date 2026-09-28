#include "caiyu_eyes_renderer.h"

#include <esp_log.h>

#define TAG "CaiyuEyes"

namespace caiyu {

bool EyesRenderer::Init(lv_obj_t* parent, int width, int height) {
    if (parent == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (width > kMaxWidth || height > kMaxHeight) {
        ESP_LOGW(TAG, "viewport %dx%d is too large for the eye canvas", width, height);
        return false;
    }

    canvas_ = lv_canvas_create(parent);
    if (canvas_ == nullptr) {
        return false;
    }

    lv_obj_set_size(canvas_, width, height);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_set_style_pad_all(canvas_, 0, 0);
    lv_obj_set_style_border_width(canvas_, 0, 0);
    lv_obj_set_style_radius(canvas_, 0, 0);
    lv_obj_set_style_bg_opa(canvas_, LV_OPA_TRANSP, 0);

    lv_draw_buf_init(&draw_buf_, width, height, LV_COLOR_FORMAT_I1,
                     lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_I1), buffer_,
                     sizeof(buffer_));
    lv_draw_buf_set_flag(&draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);
    lv_canvas_set_draw_buf(canvas_, &draw_buf_);

    lv_canvas_set_palette(canvas_, kInk, lv_color32_make(0x00, 0x00, 0x00, LV_OPA_COVER));
    lv_canvas_set_palette(canvas_, kClear, lv_color32_make(0x00, 0x00, 0x00, LV_OPA_TRANSP));

    auto* pixels = static_cast<uint8_t*>(lv_draw_buf_goto_xy(&draw_buf_, 0, 0));
    if (pixels == nullptr) {
        ESP_LOGE(TAG, "eye canvas draw buf has no pixel area");
        return false;
    }

    mono_.SetBuffer(pixels, width, height, static_cast<int32_t>(draw_buf_.header.stride));
    mono_.Clear();
    face_.SetViewport(width, height);
    lv_obj_invalidate(canvas_);
    last_hash_ = mono_.Hash();

    ESP_LOGI(TAG, "eye canvas ready (%dx%d, %u bytes buf, %d bytes pixels)", width, height,
             static_cast<unsigned>(sizeof(buffer_)),
             static_cast<int>(draw_buf_.header.stride) * height);
    return true;
}

void EyesRenderer::Deinit() {
    if (canvas_ != nullptr) {
        lv_obj_del(canvas_);
        canvas_ = nullptr;
    }
    mono_.SetBuffer(nullptr, 0, 0, 0);
    face_.SetViewport(0, 0);
}

void EyesRenderer::OnEmotionChanged(Emotion emotion) {
    face_.GoToExpression(ExpressionFromEmotion(emotion));
}

void EyesRenderer::Update(uint32_t dt_ms) {
    (void)dt_ms;
    if (!visible_ || canvas_ == nullptr) {
        return;
    }

    if (look_provider_) {
        float look_x = 0.0f;
        float look_y = 0.0f;
        const bool hold = look_provider_(&look_x, &look_y);
        face_.SetLook(look_x, look_y, hold);
    }

    face_.Update(mono_);

    const uint32_t hash = mono_.Hash();
    if (hash != last_hash_) {
        last_hash_ = hash;
        lv_obj_invalidate(canvas_);
    }
}

void EyesRenderer::SetVisible(bool visible) {
    visible_ = visible;
    if (canvas_ == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    }
}

}
