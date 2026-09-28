#ifndef CAIYU_EYES_RENDERER_H
#define CAIYU_EYES_RENDERER_H

#include <functional>
#include <utility>

#include "caiyu_emotion.h"
#include "caiyu_eye_face.h"

namespace caiyu {

class EyesRenderer : public IEmotionRenderer {
public:
    const char* name() const override { return "eyes"; }

    bool Init(lv_obj_t* parent, int width, int height) override;
    void Deinit() override;
    void OnEmotionChanged(Emotion emotion) override;
    void Update(uint32_t dt_ms) override;
    void SetVisible(bool visible) override;

    using LookProvider = std::function<bool(float* look_x, float* look_y)>;
    void SetLookProvider(LookProvider provider) { look_provider_ = std::move(provider); }

private:
    static constexpr int32_t kMaxWidth = 128;
    static constexpr int32_t kMaxHeight = 64;
    static constexpr int32_t kBufferBytes =
        LV_DRAW_BUF_SIZE(kMaxWidth, kMaxHeight, LV_COLOR_FORMAT_I1);

    lv_obj_t* canvas_ = nullptr;
    uint32_t last_hash_ = 0;
    lv_draw_buf_t draw_buf_ = {};
    MonoCanvas mono_;
    EyeFace face_;
    LookProvider look_provider_;
    alignas(LV_DRAW_BUF_ALIGN) uint8_t buffer_[kBufferBytes] = {};
};

}

#endif
