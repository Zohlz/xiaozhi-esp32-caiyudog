#ifndef CAIYU_EMOTION_H
#define CAIYU_EMOTION_H

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

namespace caiyu {

enum class Emotion : uint8_t {
    kNeutral = 0,
    kHappy,
    kLaughing,
    kFunny,
    kSad,
    kAngry,
    kCrying,
    kLoving,
    kEmbarrassed,
    kSurprised,
    kShocked,
    kThinking,
    kWinking,
    kCool,
    kRelaxed,
    kDelicious,
    kKissy,
    kConfident,
    kSleepy,
    kSilly,
    kConfused,
    kCount,
};

constexpr size_t kEmotionCount = static_cast<size_t>(Emotion::kCount);

const char* EmotionToName(Emotion emotion);

Emotion EmotionFromName(const char* name, bool* recognized = nullptr);

struct EmotionNameEntry {
    Emotion emotion;
    const char* name;
};

const EmotionNameEntry* EmotionNameTable(size_t* count);

class Timer {
public:
    explicit Timer(uint32_t interval_ms = 0) : interval_ms_(interval_ms) {}

    void SetInterval(uint32_t interval_ms) { interval_ms_ = interval_ms; }
    uint32_t interval() const { return interval_ms_; }

    void Restart() {
        start_ms_ = lv_tick_get();
        running_ = true;
    }
    void Stop() { running_ = false; }
    bool running() const { return running_; }

    uint32_t elapsed() const { return running_ ? lv_tick_get() - start_ms_ : 0; }
    bool Expired() const { return running_ && elapsed() >= interval_ms_; }

    bool Update() {
        if (!Expired()) {
            return false;
        }
        Restart();
        return true;
    }

private:
    uint32_t interval_ms_ = 0;
    uint32_t start_ms_ = 0;
    bool running_ = false;
};

class IEmotionRenderer {
public:
    virtual ~IEmotionRenderer() = default;

    virtual const char* name() const = 0;

    virtual bool Supports(Emotion emotion) const {
        (void)emotion;
        return true;
    }

    virtual bool Init(lv_obj_t* parent, int width, int height) = 0;

    virtual void Deinit() {}

    virtual void OnEmotionChanged(Emotion emotion) = 0;

    virtual void Update(uint32_t dt_ms) = 0;

    virtual void SetVisible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    virtual bool fullscreen() const { return true; }

protected:
    bool visible_ = true;
};

class EmotionEngine {
public:
    static constexpr size_t kMaxRenderers = 4;
    static constexpr uint32_t kFrameIntervalMs = 33;

    EmotionEngine() = default;
    ~EmotionEngine();

    EmotionEngine(const EmotionEngine&) = delete;
    EmotionEngine& operator=(const EmotionEngine&) = delete;

    void AddRenderer(IEmotionRenderer* renderer);

    bool Init(lv_obj_t* parent, int width, int height);

    void Deinit();

    bool initialized() const { return initialized_; }

    bool SetEmotion(const char* name);
    bool SetEmotion(Emotion emotion, bool force = false);

    Emotion emotion() const { return emotion_; }
    const char* emotion_name() const { return EmotionToName(emotion_); }

    IEmotionRenderer* active_renderer() const;

    bool fullscreen_active() const;

    void SetEnabled(bool enabled);

private:
    static void FrameTimerCallback(lv_timer_t* timer);
    void Update(uint32_t dt_ms);
    void ApplyEmotion();

    static constexpr size_t kNoRenderer = static_cast<size_t>(-1);

    IEmotionRenderer* renderers_[kMaxRenderers] = {};
    bool available_[kMaxRenderers] = {};
    size_t renderer_count_ = 0;
    size_t active_index_ = kNoRenderer;

    Emotion emotion_ = Emotion::kNeutral;
    bool emotion_applied_ = false;

    lv_obj_t* layer_ = nullptr;
    lv_timer_t* frame_timer_ = nullptr;
    uint32_t last_frame_ms_ = 0;
    bool initialized_ = false;
    bool enabled_ = true;
};

}

#endif
