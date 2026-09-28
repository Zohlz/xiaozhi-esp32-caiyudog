#include "caiyu_font_renderer.h"

#include <material_symbols.h>
#include <noto_emoji.h>

LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

namespace caiyu {

bool FontRenderer::Init(lv_obj_t* parent, int width, int height) {
    if (parent == nullptr) {
        return false;
    }
    label_ = lv_label_create(parent);
    if (label_ == nullptr) {
        return false;
    }
    lv_obj_set_style_text_font(label_, &font_noto_emoji_30_1, 0);
    lv_obj_set_style_text_color(label_, lv_color_black(), 0);
    lv_label_set_text(label_, NOTO_EMOJI_NEUTRAL);
    lv_obj_center(label_);
    return true;
}

void FontRenderer::Deinit() {
    if (label_ != nullptr) {
        lv_obj_del(label_);
        label_ = nullptr;
    }
}

void FontRenderer::OnEmotionChanged(Emotion emotion) {
    if (label_ == nullptr) {
        return;
    }

    const lv_font_t* font = &font_noto_emoji_30_1;

    const char* utf8 = noto_emoji_get_utf8(EmotionToName(emotion));
    if (utf8 == nullptr) {
        utf8 = material_symbols_get_utf8(EmotionToName(emotion));
        if (utf8 != nullptr) {
            font = &font_material_symbols_30_1;
        }
    }
    if (utf8 == nullptr) {
        utf8 = NOTO_EMOJI_NEUTRAL;
    }

    lv_obj_set_style_text_font(label_, font, 0);
    lv_label_set_text(label_, utf8);
}

void FontRenderer::Update(uint32_t dt_ms) {
    (void)dt_ms;
}

void FontRenderer::SetVisible(bool visible) {
    visible_ = visible;
    if (label_ == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }
}

}
