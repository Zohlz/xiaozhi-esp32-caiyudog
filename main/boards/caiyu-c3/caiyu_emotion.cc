#include "caiyu_emotion.h"

#include <esp_log.h>

#include <strings.h>

#define TAG "CaiyuEmotion"

namespace caiyu {

namespace {

constexpr EmotionNameEntry kEmotionNames[] = {
    {Emotion::kNeutral, "neutral"},
    {Emotion::kHappy, "happy"},
    {Emotion::kLaughing, "laughing"},
    {Emotion::kFunny, "funny"},
    {Emotion::kSad, "sad"},
    {Emotion::kAngry, "angry"},
    {Emotion::kCrying, "crying"},
    {Emotion::kLoving, "loving"},
    {Emotion::kEmbarrassed, "embarrassed"},
    {Emotion::kSurprised, "surprised"},
    {Emotion::kShocked, "shocked"},
    {Emotion::kThinking, "thinking"},
    {Emotion::kWinking, "winking"},
    {Emotion::kCool, "cool"},
    {Emotion::kRelaxed, "relaxed"},
    {Emotion::kDelicious, "delicious"},
    {Emotion::kKissy, "kissy"},
    {Emotion::kConfident, "confident"},
    {Emotion::kSleepy, "sleepy"},
    {Emotion::kSilly, "silly"},
    {Emotion::kConfused, "confused"},
};

constexpr bool EmotionTableMatchesEnum() {
    for (size_t i = 0; i < kEmotionCount; ++i) {
        if (kEmotionNames[i].emotion != static_cast<Emotion>(i)) {
            return false;
        }
    }
    return true;
}
static_assert(EmotionTableMatchesEnum(), "表情名字表与 Emotion 枚举顺序不一致");

}

const EmotionNameEntry* EmotionNameTable(size_t* count) {
    if (count != nullptr) {
        *count = sizeof(kEmotionNames) / sizeof(kEmotionNames[0]);
    }
    return kEmotionNames;
}

const char* EmotionToName(Emotion emotion) {
    for (const auto& entry : kEmotionNames) {
        if (entry.emotion == emotion) {
            return entry.name;
        }
    }
    return kEmotionNames[0].name;
}

Emotion EmotionFromName(const char* name, bool* recognized) {
    if (recognized != nullptr) {
        *recognized = false;
    }
    if (name == nullptr || name[0] == '\0') {
        return Emotion::kNeutral;
    }
    for (const auto& entry : kEmotionNames) {
        if (strcasecmp(entry.name, name) == 0) {
            if (recognized != nullptr) {
                *recognized = true;
            }
            return entry.emotion;
        }
    }
    return Emotion::kNeutral;
}

EmotionEngine::~EmotionEngine() { Deinit(); }

void EmotionEngine::AddRenderer(IEmotionRenderer* renderer) {
    if (renderer == nullptr) {
        return;
    }
    if (renderer_count_ >= kMaxRenderers) {
        ESP_LOGW(TAG, "too many renderers, drop '%s'", renderer->name());
        return;
    }
    renderers_[renderer_count_++] = renderer;
}

bool EmotionEngine::Init(lv_obj_t* parent, int width, int height) {
    if (initialized_ || parent == nullptr || width <= 0 || height <= 0 || renderer_count_ == 0) {
        return false;
    }

    layer_ = lv_obj_create(parent);
    lv_obj_set_size(layer_, width, height);
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_add_flag(layer_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(layer_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(layer_, 0, 0);
    lv_obj_set_style_border_width(layer_, 0, 0);
    lv_obj_set_style_radius(layer_, 0, 0);
    lv_obj_set_style_bg_opa(layer_, LV_OPA_TRANSP, 0);
    lv_obj_move_to_index(layer_, 0);

    active_index_ = kNoRenderer;
    size_t usable = 0;
    for (size_t i = 0; i < renderer_count_; ++i) {
        available_[i] = renderers_[i]->Init(layer_, width, height);
        if (available_[i]) {
            ++usable;
            renderers_[i]->SetVisible(false);
        } else {
            ESP_LOGW(TAG, "renderer '%s' init failed", renderers_[i]->name());
        }
    }
    if (usable == 0) {
        ESP_LOGE(TAG, "no renderer available, emotion layer disabled");
        lv_obj_del(layer_);
        layer_ = nullptr;
        return false;
    }

    initialized_ = true;
    emotion_applied_ = true;
    SetEnabled(enabled_);
    ApplyEmotion();

    last_frame_ms_ = lv_tick_get();
    frame_timer_ = lv_timer_create(FrameTimerCallback, kFrameIntervalMs, this);
    ESP_LOGI(TAG, "emotion engine started (%dx%d, %ums/frame)", width, height, kFrameIntervalMs);
    return true;
}

void EmotionEngine::Deinit() {
    if (frame_timer_ != nullptr) {
        lv_timer_del(frame_timer_);
        frame_timer_ = nullptr;
    }
    for (size_t i = 0; i < renderer_count_; ++i) {
        renderers_[i]->Deinit();
        available_[i] = false;
    }
    if (layer_ != nullptr) {
        lv_obj_del(layer_);
        layer_ = nullptr;
    }
    initialized_ = false;
    emotion_applied_ = false;
    active_index_ = kNoRenderer;
}

bool EmotionEngine::SetEmotion(const char* name) {
    bool recognized = false;
    Emotion emotion = EmotionFromName(name, &recognized);
    if (!recognized) {
        ESP_LOGW(TAG, "unknown emotion '%s', fallback to '%s'", name ? name : "(null)",
                 EmotionToName(emotion));
    }
    return SetEmotion(emotion, false);
}

bool EmotionEngine::SetEmotion(Emotion emotion, bool force) {
    if (!force && emotion_applied_ && emotion == emotion_) {
        return false;
    }
    emotion_ = emotion;
    emotion_applied_ = initialized_;
    if (initialized_) {
        ApplyEmotion();
    }
    return true;
}

IEmotionRenderer* EmotionEngine::active_renderer() const {
    if (active_index_ == kNoRenderer) {
        return nullptr;
    }
    return renderers_[active_index_];
}

bool EmotionEngine::fullscreen_active() const {
    IEmotionRenderer* renderer = active_renderer();
    return renderer != nullptr && renderer->fullscreen();
}

void EmotionEngine::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (layer_ == nullptr) {
        return;
    }
    if (enabled_) {
        lv_obj_remove_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EmotionEngine::ApplyEmotion() {
    size_t chosen = kNoRenderer;
    for (size_t i = 0; i < renderer_count_; ++i) {
        if (available_[i] && renderers_[i]->Supports(emotion_)) {
            chosen = i;
            break;
        }
    }
    if (chosen == kNoRenderer) {
        for (size_t i = 0; i < renderer_count_; ++i) {
            if (available_[i]) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen != active_index_) {
        if (chosen != kNoRenderer) {
            ESP_LOGI(TAG, "renderer -> %s ('%s')", renderers_[chosen]->name(),
                     EmotionToName(emotion_));
        }
        active_index_ = chosen;
    }
    for (size_t i = 0; i < renderer_count_; ++i) {
        const bool active = available_[i] && (i == active_index_);
        renderers_[i]->SetVisible(active);
        if (active) {
            renderers_[i]->OnEmotionChanged(emotion_);
        }
    }
}

void EmotionEngine::FrameTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<EmotionEngine*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    const uint32_t now = lv_tick_get();
    const uint32_t dt_ms = now - self->last_frame_ms_;
    self->last_frame_ms_ = now;
    self->Update(dt_ms);
}

void EmotionEngine::Update(uint32_t dt_ms) {
    if (!initialized_ || !enabled_ || active_index_ == kNoRenderer) {
        return;
    }
    renderers_[active_index_]->Update(dt_ms);
}

}
