#ifndef CAIYU_EYE_FACE_H
#define CAIYU_EYE_FACE_H

#include "caiyu_emotion.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace caiyu {

constexpr uint8_t kInk = 0;
constexpr uint8_t kClear = 1;

struct EyeConfig {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float height = 40.0f;
    float width = 40.0f;
    float slope_top = 0.0f;
    float slope_bottom = 0.0f;
    float radius_top = 8.0f;
    float radius_bottom = 8.0f;
};

struct Transformation {
    float move_x = 0.0f;
    float move_y = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
};

class RampAnimation {
public:
    explicit RampAnimation(uint32_t interval) : interval_(interval) {}

    void Restart() { start_ms_ = lv_tick_get(); }
    uint32_t GetElapsed() const { return lv_tick_get() - start_ms_; }
    float GetValue() const {
        const uint32_t elapsed = GetElapsed();
        if (elapsed < interval_) {
            return static_cast<float>(elapsed) / static_cast<float>(interval_);
        }
        return 1.0f;
    }

private:
    uint32_t interval_;
    uint32_t start_ms_ = 0;
};

class TrapeziumAnimation {
public:
    TrapeziumAnimation(uint32_t t0, uint32_t t1, uint32_t t2)
        : t0_(t0), t1_(t1), t2_(t2), interval_(t0 + t1 + t2) {}
    explicit TrapeziumAnimation(uint32_t interval)
        : t0_(interval / 3), t1_(interval / 3), t2_(interval - 2 * (interval / 3)),
          interval_(interval) {}

    void Restart() { start_ms_ = lv_tick_get(); }
    uint32_t interval() const { return interval_; }
    uint32_t GetElapsed() const { return lv_tick_get() - start_ms_; }

    float GetValue() const {
        const uint32_t elapsed = GetElapsed();
        if (elapsed > interval_) {
            return 0.0f;
        }
        if (elapsed < t0_) {
            return t0_ ? static_cast<float>(elapsed) / static_cast<float>(t0_) : 1.0f;
        }
        if (elapsed < t0_ + t1_) {
            return 1.0f;
        }
        const uint32_t tail = t0_ + t1_;
        return t2_ ? 1.0f - static_cast<float>(elapsed - tail) / static_cast<float>(t2_) : 0.0f;
    }

private:
    uint32_t t0_;
    uint32_t t1_;
    uint32_t t2_;
    uint32_t interval_;
    uint32_t start_ms_ = 0;
};

class TrapeziumPulseAnimation {
public:
    TrapeziumPulseAnimation(uint32_t t0, uint32_t t1, uint32_t t2)
        : t0_(0), t1_(t0), t2_(t1), t3_(t2), t4_(0) {
        RecalcInterval();
    }
    TrapeziumPulseAnimation(uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, uint32_t t4)
        : t0_(t0), t1_(t1), t2_(t2), t3_(t3), t4_(t4) {
        RecalcInterval();
    }

    void Restart() { start_ms_ = lv_tick_get(); }
    uint32_t interval() const { return interval_; }
    uint32_t GetElapsed() const { return lv_tick_get() - start_ms_; }

    void SetInterval(uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, uint32_t t4) {
        t0_ = t0;
        t1_ = t1;
        t2_ = t2;
        t3_ = t3;
        t4_ = t4;
        RecalcInterval();
    }

    void SetTriangle(uint32_t t, uint32_t delay) {
        t0_ = 0;
        t1_ = t / 2;
        t2_ = 0;
        t3_ = t1_;
        t4_ = delay;
        RecalcInterval();
    }

    float GetValue() const {
        if (interval_ == 0) {
            return 0.0f;
        }
        const uint32_t elapsed = GetElapsed() % interval_;
        if (elapsed < t0_) {
            return 0.0f;
        }
        if (elapsed < t0_ + t1_) {
            return t1_ ? static_cast<float>(elapsed - t0_) / static_cast<float>(t1_) : 1.0f;
        }
        if (elapsed < t0_ + t1_ + t2_) {
            return 1.0f;
        }
        const uint32_t fall = t0_ + t1_ + t2_;
        if (elapsed < fall + t3_) {
            return t3_ ? 1.0f - static_cast<float>(elapsed - fall) / static_cast<float>(t3_) : 0.0f;
        }
        return 0.0f;
    }

private:
    void RecalcInterval() { interval_ = t0_ + t1_ + t2_ + t3_ + t4_; }

    uint32_t t0_;
    uint32_t t1_;
    uint32_t t2_;
    uint32_t t3_;
    uint32_t t4_;
    uint32_t interval_ = 0;
    uint32_t start_ms_ = 0;
};

class MonoCanvas {
public:
    enum class Corner {
        kTopRight,
        kTopLeft,
        kBottomLeft,
        kBottomRight,
    };

    void SetBuffer(uint8_t* pixels, int32_t width, int32_t height, int32_t stride) {
        buffer_ = pixels;
        width_ = width;
        height_ = height;
        stride_ = stride;
    }

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }

    void Clear() {
        if (buffer_ == nullptr) {
            return;
        }
        std::memset(buffer_, kClear ? 0xFF : 0x00, static_cast<size_t>(stride_) * height_);
    }

    uint32_t Hash() const;

    void SetPixel(int32_t x, int32_t y, uint8_t value) {
        if (buffer_ == nullptr || x < 0 || x >= width_ || y < 0 || y >= height_) {
            return;
        }
        SetBit(buffer_ + y * stride_, x, value);
    }

    void HLine(int32_t x, int32_t y, int32_t length, uint8_t value);

    void Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t value);

    void FillRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t value);

    void FillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      uint8_t value);

    void FillRectangularTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t value) {
        FillTriangle(x0, y0, x1, y1, x1, y0, value);
    }

    void FillEllipseCorner(Corner corner, int32_t x0, int32_t y0, int32_t rx, int32_t ry,
                           uint8_t value);

private:
    static void SetBit(uint8_t* row, int32_t x, uint8_t value) {
        uint8_t* byte = row + (x >> 3);
        const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
        if (value) {
            *byte |= mask;
        } else {
            *byte = static_cast<uint8_t>(*byte & ~mask);
        }
    }

    uint8_t* buffer_ = nullptr;
    int32_t width_ = 0;
    int32_t height_ = 0;
    int32_t stride_ = 0;
};

class EyeTransition {
public:
    EyeTransition() : animation_(500) {}

    void Update() { Apply(animation_.GetValue()); }
    void Apply(float t);
    void Restart() { animation_.Restart(); }

    EyeConfig* origin = nullptr;
    EyeConfig destin;

private:
    RampAnimation animation_;
};

class EyeTransformation {
public:
    EyeTransformation() : animation_(200) {}

    void Update();
    void Apply();
    void SetDestin(const Transformation& transformation);
    void Restart() { animation_.Restart(); }

    EyeConfig* input = nullptr;
    EyeConfig output;

private:
    Transformation origin_;
    Transformation current_;
    Transformation destin_;
    RampAnimation animation_;
};

class EyeVariation {
public:
    EyeVariation();
    ~EyeVariation() = default;

    void Clear();
    void Update();
    void Apply(float t);
    void Restart() { animation.Restart(); }

    EyeConfig* input = nullptr;
    EyeConfig output;
    EyeConfig values;
    TrapeziumPulseAnimation animation;
};

class EyeBlink {
public:
    EyeBlink() : animation_(40, 100, 40) {}

    void Update();
    void Apply(float t);
    void Restart() { animation_.Restart(); }

    EyeConfig* input = nullptr;
    EyeConfig output;

    float blink_width = 60.0f;
    float blink_height = 2.0f;

private:
    TrapeziumAnimation animation_;
};

class Eye {
public:
    Eye();

    void SetMirrored(bool mirrored) { mirrored_ = mirrored; }
    void SetCenter(int32_t x, int32_t y) {
        center_x_ = x;
        center_y_ = y;
    }

    void TransitionTo(const EyeConfig& preset);
    void Update();
    void Draw(MonoCanvas& canvas);

    void SetHeart(bool heart) { heart_ = heart; }

    EyeBlink& blink() { return blink_; }
    EyeTransformation& transformation() { return transformation_; }
    EyeVariation& variation1() { return variation1_; }
    EyeVariation& variation2() { return variation2_; }

private:
    void ChainOperators();

    bool heart_ = false;
    int32_t center_x_ = 0;
    int32_t center_y_ = 0;
    bool mirrored_ = false;

    EyeConfig config_;
    EyeConfig* final_config_ = nullptr;

    EyeTransition transition_;
    EyeTransformation transformation_;
    EyeVariation variation1_;
    EyeVariation variation2_;
    EyeBlink blink_;
};

enum class EyeExpression : uint8_t {
    kNormal = 0,
    kAngry,
    kGlee,
    kHappy,
    kSad,
    kWorried,
    kFocused,
    kAnnoyed,
    kSurprised,
    kSkeptic,
    kFrustrated,
    kUnimpressed,
    kSleepy,
    kSuspicious,
    kSquint,
    kFurious,
    kScared,
    kAwe,
    kFunny,
    kSilly,
    kLaughing,
    kCrying,
    kShocked,
    kConfused,
    kConfident,
    kRelaxed,
    kDelicious,
    kHeart,
    kKiss,
    kCount,
};

EyeExpression ExpressionFromEmotion(Emotion emotion);

void FillEllipse(MonoCanvas& canvas, int32_t center_x, int32_t center_y, int32_t rx, int32_t ry,
                 uint8_t value);

class EyeFace {
public:
    EyeFace();

    void SetViewport(int32_t width, int32_t height);

    void SetRandomLook(bool enabled) { random_look_ = enabled; }
    void SetRandomBlink(bool enabled) { random_blink_ = enabled; }

    void GoToExpression(EyeExpression expression);
    EyeExpression expression() const { return expression_; }

    void Update(MonoCanvas& canvas);

    void LookAt(float x, float y);
    void LookFront() { LookAt(0.0f, 0.0f); }

    void LookTowards(float look_x, float look_y) { LookAt(look_x, -look_y); }

    void SetLook(float look_x, float look_y, bool hold);

private:
    void Draw(MonoCanvas& canvas);
    void ClearVariations();

    int32_t width_ = 128;
    int32_t height_ = 64;
    int32_t center_x_ = 64;
    int32_t center_y_ = 32;
    int32_t eye_size_ = 40;
    int32_t eye_inter_distance_ = 8;

    Eye left_eye_;
    Eye right_eye_;

    EyeExpression expression_ = EyeExpression::kNormal;
    bool random_look_ = true;
    bool random_blink_ = true;
    bool look_held_ = false;
    float held_look_x_ = 0.0f;
    float held_look_y_ = 0.0f;
    Timer blink_timer_{3500};
    Timer look_timer_{4000};
};

}

#endif
