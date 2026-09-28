#ifndef CAIYU_ACTIONMENTS_H
#define CAIYU_ACTIONMENTS_H

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>
#include <mutex>

namespace caiyu {

enum LegIndex : uint8_t {
    kLeftFrontLeg = 0,
    kRightFrontLeg = 1,
    kLeftBackLeg = 2,
    kRightBackLeg = 3,
    kLegCount = 4,
};

class LegServo {
public:
    LegServo() = default;
    ~LegServo();

    LegServo(const LegServo&) = delete;
    LegServo& operator=(const LegServo&) = delete;

    bool Attach(gpio_num_t pin, ledc_channel_t channel, int init_angle, bool mirror);
    void Detach();
    bool attached() const { return attached_; }

    void SetAngle(int angle);
    void SetAngleNow(int angle);
    int angle() const { return pos_; }

    void SetSpeedLimit(int degrees_per_sec) { speed_limit_ = degrees_per_sec; }
    void SetTrim(int trim) {
        trim_ = trim;
        Write(pos_);
    }
    int trim() const { return trim_; }

private:
    void Write(int angle);
    static uint32_t NowMs();

    bool attached_ = false;
    gpio_num_t pin_ = GPIO_NUM_NC;
    ledc_channel_t channel_ = LEDC_CHANNEL_0;
    bool mirror_ = false;
    volatile int trim_ = 0;

    int pos_ = 90;
    uint32_t last_write_ms_ = 0;
    int speed_limit_ = 0;
};

class Actionments {
public:
    enum class Action : uint8_t {
        kStand = 0,
        kSit,
        kSleep,
        kProne,
        kWalk,
        kWalkBack,
        kTurnLeft,
        kTurnRight,
        kWave,
        kStretch,
        kSwing,
        kScratch,
        kKneel,
        kFrontProne,
        kSway,
        kRaiseHips,
        kShakeBackLegs,
        kCount,
    };

    enum class DriveDirection : uint8_t {
        kNone = 0,
        kForward,
        kBackward,
        kLeft,
        kRight,
        kCount,
    };

    static const char* ActionName(Action action);
    static bool ActionFromName(const char* name, Action* out);
    static const char* ActionGlossList();

    static const char* DriveName(DriveDirection direction);
    static bool DriveFromName(const char* name, DriveDirection* out);

    Actionments() = default;
    ~Actionments();

    Actionments(const Actionments&) = delete;
    Actionments& operator=(const Actionments&) = delete;

    void Init(gpio_num_t left_front, gpio_num_t right_front, gpio_num_t left_back,
              gpio_num_t right_back);

    void Start();
    void Stop();

    bool Request(Action action, int steps = 0, int period = 0);

    void SetDrive(DriveDirection direction);
    DriveDirection drive() const;

    bool SetLegAngle(int index, int angle);
    int leg_angle(int index) const;

    Action current() const;
    bool busy() const;

    enum class MoodMove : uint8_t {
        kNone = 0,
        kDip,
        kLean,
        kHipsShake,
        kTuck,
        kSitSway,
        kSitTap,
        kLegShake,
        kRock,
        kCount,
    };

    void RequestMoodMove(MoodMove move);

    void SetSpeedPercent(int percent);
    int speed_percent() const { return speed_percent_; }

    static constexpr int kMaxTrimDeg = 20;
    bool SetLegTrim(int index, int trim);
    int leg_trim(int index) const;
    bool trims_dirty() const { return trims_dirty_; }
    void SaveTrims();
    void LoadTrims();

private:
    struct Job {
        Action action;
        int steps;
        int period;
    };

    static void TaskEntry(void* arg);
    void Run();
    void RunAction(const Job& job, bool remote);
    void Execute(const Job& job);
    static Action DriveToAction(DriveDirection direction);

    void MoveServos(int time_ms, const int target[kLegCount]);
    void MoveSingle(int angle, int index);
    void AllTo(int angle, int time_ms);
    bool aborted() const { return abort_; }

    bool AtPose(const int target[kLegCount]) const;

    int ScaleMs(int ms) const;
    void Delay(int ms) const;

    MoodMove TakePendingMoodMove();
    void RunMoodMove(MoodMove move);

    void DoStand();
    void DoSit();
    void DoSleep();
    void DoProne(bool cozy);
    void DoWalk(bool backward, int steps, int period);
    void DoTurn(bool right, int steps, int period);
    void DoWave(int waves, int speed);
    void DoStretch();
    void DoLStretch();
    void DoSwing(int direction, int period);
    void DoScratch(int period);
    void DoKneel(int times);
    void DoFrontProne();
    void DoRaiseHips(int times);
    void DoShakeBackLegs(int cycles);

    LegServo legs_[kLegCount];
    gpio_num_t pins_[kLegCount] = {GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC};

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    volatile bool running_ = false;
    volatile bool abort_ = false;

    Action current_ = Action::kCount;
    DriveDirection drive_ = DriveDirection::kNone;
    volatile int speed_percent_ = 100;
    volatile bool trims_dirty_ = false;
    MoodMove pending_mood_ = MoodMove::kNone;

    mutable std::mutex state_mutex_;
};

}

#endif
