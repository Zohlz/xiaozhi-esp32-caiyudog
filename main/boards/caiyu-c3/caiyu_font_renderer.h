#ifndef CAIYU_FONT_RENDERER_H
#define CAIYU_FONT_RENDERER_H

#include "caiyu_emotion.h"

namespace caiyu {

class FontRenderer : public IEmotionRenderer {
public:
    const char* name() const override { return "font"; }

    bool Init(lv_obj_t* parent, int width, int height) override;
    void Deinit() override;
    void OnEmotionChanged(Emotion emotion) override;
    void Update(uint32_t dt_ms) override;
    void SetVisible(bool visible) override;

private:
    lv_obj_t* label_ = nullptr;
};

}

#endif
