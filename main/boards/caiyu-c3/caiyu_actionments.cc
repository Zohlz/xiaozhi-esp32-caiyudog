#include "caiyu_actionments.h"

#include <esp_timer.h>
#include <esp_log.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"
#include "settings.h"

#define TAG "CaiyuDog"

namespace caiyu {

namespace {

constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
constexpr uint32_t kServoFreqHz = 50;
constexpr uint32_t kServoDutyMax = 8191;
constexpr uint32_t kServoPeriodUs = 20000;
constexpr uint32_t kServoMinPulseUs = 500;
constexpr uint32_t kServoMaxPulseUs = 2500;

bool EnsureLedcTimer() {
    static bool configured = false;
    if (configured) {
        return true;
    }
    ledc_timer_config_t timer = {};
    timer.speed_mode = kLedcMode;
    timer.duty_resolution = LEDC_TIMER_13_BIT;
    timer.timer_num = kLedcTimer;
    timer.freq_hz = kServoFreqHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer config failed");
        return false;
    }
    configured = true;
    return true;
}

constexpr int kStandAngle = 90;

constexpr int kSleepFrontAngle = 180;
constexpr int kSleepBackAngle = 0;

constexpr int kSitFrontAngle = 90;
constexpr int kSitBackAngle = 20;

constexpr int kProneFrontAngle = 0;
constexpr int kProneBackAngle = 0;

constexpr int kCozyProneFrontAngle = 0;
constexpr int kCozyProneBackAngle = 180;

constexpr int kLegUp = 45;
constexpr int kLegNeutral = 90;
constexpr int kLegExtend = 135;

constexpr int kTurnOutwardAngle = 130;
constexpr int kTurnInwardAngle = 50;
constexpr int kTurnAngle = 90;

constexpr int kWaveSitLeftBackAngle = 50;
constexpr int kWaveSitRightBackAngle = 0;
constexpr int kWaveAmplitude = 65;

constexpr int kStretchFrontAngle = 10;
constexpr int kStretchBackAngle = 170;

constexpr int kLStretchFrontAngle = 20;
constexpr int kLStretchBackAngle = 110;
constexpr int kLStretchAmplitude = 150;

constexpr int kScratchLeftFrontAngle = 90;
constexpr int kScratchRightFrontAngle = 180;
constexpr int kScratchBackAngle = 0;
constexpr int kScratchAmplitude = 45;

constexpr int kKneelFrontAngle = 90;
constexpr int kKneelBackAngle = 20;

constexpr int kSwingMinAngle = 30;
constexpr int kSwingMaxAngle = 90;
constexpr int kSwingAmplitude = 120;

constexpr int kSwayMinAngle = 65;
constexpr int kSwayMaxAngle = 115;

constexpr int kSwingCycles = 4;

constexpr int kFrontProneFrontAngle = 0;
constexpr int kFrontProneBackAngle = 0;

constexpr int kRaiseHipsLowAngle = 20;
constexpr int kRaiseHipsJitterDeg = 8;

constexpr int kShakeBackFrontAngle = 56;
constexpr int kShakeBackBackAngle = 141;
constexpr int kShakeBackJitterDeg = 8;
constexpr int kShakeBackShakeMs = 10;
constexpr int kShakeBackCycles = 4;
constexpr int kShakeBackRampMs = 260;

constexpr int kMoodShakeDeg = 5;
constexpr int kMoodDipDeg = 7;
constexpr int kMoodLeanDeg = 9;
constexpr int kMoodTuckDeg = 12;
constexpr int kMoodMoveMs = 130;
constexpr int kMoodMovePauseMs = 90;
constexpr int kMoodShakePauseMs = 45;

constexpr int kMoodDipCount = 3;
constexpr int kMoodTuckCount = 2;

constexpr int kMoodLeanStepMs = 280;
constexpr int kMoodLeanCount = 2;

constexpr int kMoodHipsDipDeg = 40;
constexpr int kMoodHipsShakeStepMs = 60;
constexpr int kMoodHipsShakeCycles = 8;

constexpr int kMoodRockDeg = 14;
constexpr int kMoodRockStepMs = 90;
constexpr int kMoodRockPauseMs = 40;
constexpr int kMoodRockCount = 3;

constexpr int kMoodSitMs = 700;
constexpr int kMoodSitSwayCount = 3;
constexpr int kMoodSitTapDeg = 10;
constexpr int kMoodSitTapCount = 3;
constexpr int kMoodStandMs = 400;

constexpr int kMoodMoveCount = static_cast<int>(Actionments::MoodMove::kCount);
constexpr const char* kMoodMoveNames[] = {
    "none", "dip", "lean", "hips_shake", "tuck", "sit_sway", "sit_tap", "leg_shake", "rock",
};
static_assert(sizeof(kMoodMoveNames) / sizeof(kMoodMoveNames[0]) ==
                  static_cast<size_t>(Actionments::MoodMove::kCount),
              "kMoodMoveNames 必须与 MoodMove 枚举一一对应");

constexpr int kWalkDefaultSteps = 2;
constexpr int kTurnDefaultSteps = 3;
constexpr int kWalkPeriodMs = 150;
constexpr int kTurnPeriodMs = 180;
constexpr int kRemoteSteps = 1;
constexpr int kMaxSteps = 10;
constexpr int kMinPeriodMs = 100;
constexpr int kMaxPeriodMs = 300;

constexpr int kActionQueueDepth = 1;

int UseOr(int value, int fallback) { return (value > 0) ? value : fallback; }

constexpr int kServoLimitDegPerSec = 240;

constexpr int kAtPoseToleranceDeg = 1;

constexpr const char* kTrimNamespace = "caiyu_servo";
constexpr const char* kTrimKeys[kLegCount] = {"trim_lf", "trim_rf", "trim_lb", "trim_rb"};

}

LegServo::~LegServo() { Detach(); }

uint32_t LegServo::NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

bool LegServo::Attach(gpio_num_t pin, ledc_channel_t channel, int init_angle, bool mirror) {
    if (pin == GPIO_NUM_NC) {
        return false;
    }
    if (!EnsureLedcTimer()) {
        return false;
    }

    Detach();

    pin_ = pin;
    channel_ = channel;
    mirror_ = mirror;
    last_write_ms_ = NowMs();

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num = pin;
    channel_config.speed_mode = kLedcMode;
    channel_config.channel = channel;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.timer_sel = kLedcTimer;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        ESP_LOGE(TAG, "ledc channel %d config failed on gpio %d", static_cast<int>(channel),
                 static_cast<int>(pin));
        return false;
    }

    attached_ = true;
    pos_ = std::min(std::max(init_angle, 0), 180);
    Write(pos_);
    ESP_LOGI(TAG, "servo attached: gpio=%d ch=%d mirror=%d init=%d", static_cast<int>(pin),
             static_cast<int>(channel), mirror ? 1 : 0, pos_);
    return true;
}

void LegServo::Detach() {
    if (!attached_) {
        return;
    }
    ledc_stop(kLedcMode, channel_, 0);
    attached_ = false;
}

void LegServo::SetAngle(int angle) { Write(angle); }

void LegServo::SetAngleNow(int angle) {
    const int saved = speed_limit_;
    speed_limit_ = 0;
    Write(angle);
    speed_limit_ = saved;
}

void LegServo::Write(int angle) {
    if (!attached_) {
        return;
    }

    const uint32_t now = NowMs();
    if (speed_limit_ > 0) {
        const int limit =
            std::max(1, static_cast<int>((now - last_write_ms_) * speed_limit_ / 1000));
        if (std::abs(angle - pos_) > limit) {
            pos_ += (angle < pos_) ? -limit : limit;
        } else {
            pos_ = angle;
        }
    } else {
        pos_ = angle;
    }
    last_write_ms_ = now;

    int out = pos_ + trim_;
    if (mirror_) {
        out = 180 - out;
    }
    out = std::min(std::max(out, 0), 180);

    const double pulse_us =
        (out / 180.0) * (kServoMaxPulseUs - kServoMinPulseUs) + kServoMinPulseUs;
    const uint32_t duty = static_cast<uint32_t>(pulse_us / kServoPeriodUs * kServoDutyMax);
    ledc_set_duty(kLedcMode, channel_, duty);
    ledc_update_duty(kLedcMode, channel_);
}

namespace {
struct ActionEntry {
    Actionments::Action action;
    const char* name;
    const char* gloss;
};

constexpr ActionEntry kActionTable[] = {
    {Actionments::Action::kStand, "stand", "站立"},
    {Actionments::Action::kSit, "sit", "坐下"},
    {Actionments::Action::kSleep, "sleep", "睡觉"},
    {Actionments::Action::kProne, "prone", "趴下"},
    {Actionments::Action::kWalk, "walk", "前进"},
    {Actionments::Action::kWalkBack, "walk_back", "后退"},
    {Actionments::Action::kTurnLeft, "turn_left", "左转"},
    {Actionments::Action::kTurnRight, "turn_right", "右转"},
    {Actionments::Action::kWave, "wave", "挥手打招呼"},
    {Actionments::Action::kStretch, "stretch", "伸懒腰1"},
    {Actionments::Action::kSwing, "swing", "左右摇摆(跳舞1)"},
    {Actionments::Action::kScratch, "scratch", "挠痒"},
    {Actionments::Action::kKneel, "kneel", "跪拜"},
    {Actionments::Action::kFrontProne, "front_prone", "前趴"},
    {Actionments::Action::kSway, "sway", "前后摇摆(跳舞2)"},
    {Actionments::Action::kRaiseHips, "raise_hips", "抬屁股"},
    {Actionments::Action::kShakeBackLegs, "shake_back_legs", "伸懒腰2"},
};
constexpr size_t kActionTableSize = sizeof(kActionTable) / sizeof(kActionTable[0]);
static_assert(kActionTableSize == static_cast<size_t>(Actionments::Action::kCount),
              "kActionTable 必须与 Action 枚举一一对应");

struct DriveEntry {
    Actionments::DriveDirection direction;
    const char* name;
};

constexpr DriveEntry kDriveTable[] = {
    {Actionments::DriveDirection::kNone, "none"},
    {Actionments::DriveDirection::kForward, "forward"},
    {Actionments::DriveDirection::kBackward, "backward"},
    {Actionments::DriveDirection::kLeft, "left"},
    {Actionments::DriveDirection::kRight, "right"},
};
constexpr size_t kDriveTableSize = sizeof(kDriveTable) / sizeof(kDriveTable[0]);
static_assert(kDriveTableSize == static_cast<size_t>(Actionments::DriveDirection::kCount),
              "kDriveTable 必须与 DriveDirection 枚举一一对应");
}

const char* Actionments::ActionName(Action action) {
    if (action == Action::kCount) {
        return "idle";
    }
    const size_t index = static_cast<size_t>(action);
    return (index < kActionTableSize) ? kActionTable[index].name : "unknown";
}

bool Actionments::ActionFromName(const char* name, Action* out) {
    if (name == nullptr) {
        return false;
    }
    char normalized[32];
    size_t n = 0;
    for (const char* p = name; *p != '\0' && n < sizeof(normalized) - 1; ++p) {
        const char c = *p;
        if (c == '-' || c == ' ') {
            normalized[n++] = '_';
        } else if (c >= 'A' && c <= 'Z') {
            normalized[n++] = static_cast<char>(c - 'A' + 'a');
        } else {
            normalized[n++] = c;
        }
    }
    normalized[n] = '\0';

    for (const auto& entry : kActionTable) {
        if (strcmp(entry.name, normalized) == 0) {
            if (out != nullptr) {
                *out = entry.action;
            }
            return true;
        }
    }
    return false;
}

const char* Actionments::ActionGlossList() {
    static const std::string list = [] {
        std::string result;
        for (size_t i = 0; i < kActionTableSize; ++i) {
            if (i != 0) {
                result += ',';
            }
            result += kActionTable[i].name;
            result += '=';
            result += kActionTable[i].gloss;
        }
        return result;
    }();
    return list.c_str();
}

const char* Actionments::DriveName(DriveDirection direction) {
    const size_t index = static_cast<size_t>(direction);
    return (index < kDriveTableSize) ? kDriveTable[index].name : "none";
}

bool Actionments::DriveFromName(const char* name, DriveDirection* out) {
    if (name == nullptr) {
        return false;
    }
    for (const auto& entry : kDriveTable) {
        if (strcmp(entry.name, name) == 0) {
            if (out != nullptr) {
                *out = entry.direction;
            }
            return true;
        }
    }
    return false;
}

Actionments::~Actionments() {
    Stop();
    for (auto& leg : legs_) {
        leg.Detach();
    }
}

void Actionments::Init(gpio_num_t left_front, gpio_num_t right_front, gpio_num_t left_back,
                       gpio_num_t right_back) {
    const gpio_num_t pins[kLegCount] = {left_front, right_front, left_back, right_back};

    const int sleep_positions[kLegCount] = {kSleepFrontAngle, kSleepFrontAngle, kSleepBackAngle,
                                            kSleepBackAngle};

    for (int i = 0; i < kLegCount; ++i) {
        pins_[i] = pins[i];
        if (pins[i] == GPIO_NUM_NC) {
            continue;
        }
        const auto channel = static_cast<ledc_channel_t>(LEDC_CHANNEL_0 + i);
        const bool mirror = (i == kRightFrontLeg || i == kRightBackLeg);
        if (legs_[i].Attach(pins[i], channel, sleep_positions[i], mirror)) {
            legs_[i].SetSpeedLimit(kServoLimitDegPerSec);
        }
    }
    LoadTrims();

    ESP_LOGI(TAG, "actionments ready: LF=%d RF=%d LB=%d RB=%d", static_cast<int>(pins[0]),
             static_cast<int>(pins[1]), static_cast<int>(pins[2]), static_cast<int>(pins[3]));
}

void Actionments::Start() {
    if (running_) {
        return;
    }
    if (queue_ == nullptr) {
        queue_ = xQueueCreate(kActionQueueDepth, sizeof(Job));
        if (queue_ == nullptr) {
            ESP_LOGE(TAG, "create action queue failed");
            return;
        }
    }
    running_ = true;
    if (xTaskCreate(TaskEntry, "caiyu_dog", 3072, this, tskIDLE_PRIORITY + 2, &task_) != pdPASS) {
        running_ = false;
        task_ = nullptr;
        ESP_LOGE(TAG, "create action task failed");
    }
}

void Actionments::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    abort_ = true;
    for (int i = 0; i < 30 && task_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    task_ = nullptr;
}

void Actionments::TaskEntry(void* arg) { static_cast<Actionments*>(arg)->Run(); }

void Actionments::RequestMoodMove(MoodMove move) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_mood_ = move;
}

Actionments::MoodMove Actionments::TakePendingMoodMove() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const MoodMove move = pending_mood_;
    pending_mood_ = MoodMove::kNone;
    return move;
}

void Actionments::RunMoodMove(MoodMove move) {
    const int stand[kLegCount] = {kStandAngle, kStandAngle, kStandAngle, kStandAngle};
    const int sit[kLegCount] = {kSitFrontAngle, kSitFrontAngle, kSitBackAngle, kSitBackAngle};
    const bool from_stand = AtPose(stand);
    const bool from_sit = AtPose(sit);
    const bool sits = (move == MoodMove::kSitSway || move == MoodMove::kSitTap);
    if (sits ? !(from_stand || from_sit) : !from_stand) {
        return;
    }

    const int index = static_cast<int>(move);
    ESP_LOGI(TAG, "mood move: %s",
             (index > 0 && index < kMoodMoveCount) ? kMoodMoveNames[index] : "?");

    switch (move) {
        case MoodMove::kLegShake: {
            for (int i = 0; i < 5; ++i) {
                if (aborted()) return;
                MoveSingle(kStandAngle - kMoodShakeDeg, kLeftFrontLeg);
                Delay(kMoodShakePauseMs);
                if (aborted()) return;
                MoveSingle(kStandAngle + kMoodShakeDeg, kLeftFrontLeg);
                Delay(kMoodShakePauseMs);
            }
            MoveSingle(kStandAngle, kLeftFrontLeg);
            Delay(kMoodMovePauseMs);
            break;
        }

        case MoodMove::kDip: {
            const int dip[kLegCount] = {kStandAngle - kMoodDipDeg, kStandAngle - kMoodDipDeg,
                                        kStandAngle + kMoodDipDeg, kStandAngle + kMoodDipDeg};
            for (int i = 0; i < kMoodDipCount; ++i) {
                if (aborted()) return;
                MoveServos(kMoodMoveMs, dip);
                Delay(kMoodMovePauseMs);
                if (aborted()) return;
                MoveServos(kMoodMoveMs, stand);
                Delay(kMoodMovePauseMs);
            }
            break;
        }

        case MoodMove::kRock: {
            const int lean_forward[kLegCount] = {kStandAngle - kMoodRockDeg,
                                                 kStandAngle - kMoodRockDeg, kStandAngle,
                                                 kStandAngle};
            const int lean_back[kLegCount] = {kStandAngle, kStandAngle,
                                              kStandAngle + kMoodRockDeg,
                                              kStandAngle + kMoodRockDeg};
            for (int n = 0; n < kMoodRockCount; ++n) {
                if (aborted()) return;
                MoveServos(kMoodRockStepMs, lean_forward);
                Delay(kMoodRockPauseMs);
                if (aborted()) return;
                MoveServos(kMoodRockStepMs, lean_back);
                Delay(kMoodRockPauseMs);
            }
            if (aborted()) return;
            MoveServos(kMoodMoveMs * 2, stand);
            Delay(kMoodMovePauseMs);
            break;
        }

        case MoodMove::kSitSway: {
            if (!from_sit) {
                MoveServos(kMoodSitMs, sit);
                Delay(kMoodMovePauseMs);
            }
            const int left_dip[kLegCount] = {sit[kLeftFrontLeg] - kMoodLeanDeg,
                                             sit[kRightFrontLeg], sit[kLeftBackLeg],
                                             sit[kRightBackLeg]};
            const int right_dip[kLegCount] = {sit[kLeftFrontLeg],
                                              sit[kRightFrontLeg] - kMoodLeanDeg,
                                              sit[kLeftBackLeg], sit[kRightBackLeg]};
            for (int n = 0; n < kMoodSitSwayCount; ++n) {
                if (aborted()) return;
                MoveServos(kMoodMoveMs, left_dip);
                Delay(kMoodMovePauseMs);
                if (aborted()) return;
                MoveServos(kMoodMoveMs, right_dip);
                Delay(kMoodMovePauseMs);
            }
            if (aborted()) return;
            MoveServos(kMoodStandMs, stand);
            Delay(kMoodMovePauseMs);
            break;
        }

        case MoodMove::kLean: {
            const int left_dip[kLegCount] = {kStandAngle - kMoodLeanDeg, kStandAngle,
                                             kStandAngle + kMoodLeanDeg, kStandAngle};
            const int right_dip[kLegCount] = {kStandAngle, kStandAngle - kMoodLeanDeg,
                                              kStandAngle, kStandAngle + kMoodLeanDeg};
            for (int n = 0; n < kMoodLeanCount; ++n) {
                if (aborted()) return;
                MoveServos(kMoodLeanStepMs, left_dip);
                Delay(kMoodMovePauseMs);
                if (aborted()) return;
                MoveServos(kMoodLeanStepMs, right_dip);
                Delay(kMoodMovePauseMs);
            }
            if (aborted()) return;
            MoveServos(kMoodLeanStepMs, stand);
            Delay(kMoodMovePauseMs);
            break;
        }

        case MoodMove::kHipsShake: {
            const int low[kLegCount] = {kStandAngle - kMoodHipsDipDeg,
                                        kStandAngle - kMoodHipsDipDeg, kStandAngle, kStandAngle};
            if (aborted()) return;
            MoveServos(kMoodMoveMs * 3, low);
            Delay(kMoodMovePauseMs);

            int shake_a[kLegCount];
            int shake_b[kLegCount];
            for (int i = 0; i < kLegCount; ++i) {
                shake_a[i] = low[i] + kMoodShakeDeg;
                shake_b[i] = low[i] - kMoodShakeDeg;
            }
            for (int n = 0; n < kMoodHipsShakeCycles; ++n) {
                if (aborted()) return;
                MoveServos(kMoodHipsShakeStepMs, shake_a);
                if (aborted()) return;
                MoveServos(kMoodHipsShakeStepMs, shake_b);
            }

            if (aborted()) return;
            MoveServos(kMoodMoveMs * 2, stand);
            Delay(kMoodMovePauseMs);
            break;
        }

        case MoodMove::kTuck: {
            const int back_tuck[kLegCount] = {kStandAngle, kStandAngle,
                                              kStandAngle + kMoodTuckDeg,
                                              kStandAngle + kMoodTuckDeg};
            const int front_tuck[kLegCount] = {kStandAngle - kMoodTuckDeg,
                                               kStandAngle - kMoodTuckDeg, kStandAngle,
                                               kStandAngle};
            const int* phases[] = {back_tuck, front_tuck};
            for (const int* phase : phases) {
                for (int n = 0; n < kMoodTuckCount; ++n) {
                    if (aborted()) return;
                    MoveServos(kMoodMoveMs, phase);
                    Delay(kMoodMovePauseMs);
                    if (aborted()) return;
                    MoveServos(kMoodMoveMs, stand);
                    Delay(kMoodMovePauseMs);
                }
            }
            break;
        }

        case MoodMove::kSitTap: {
            if (!from_sit) {
                MoveServos(kMoodSitMs, sit);
                Delay(kMoodMovePauseMs);
            }
            const int tap_legs[kMoodSitTapCount] = {kLeftFrontLeg, kRightFrontLeg, kLeftFrontLeg};
            for (int n = 0; n < kMoodSitTapCount; ++n) {
                const int leg = tap_legs[n];
                if (aborted()) return;
                MoveSingle(sit[leg] - kMoodSitTapDeg, leg);
                Delay(kMoodShakePauseMs + 25);
                if (aborted()) return;
                MoveSingle(sit[leg], leg);
                Delay(kMoodShakePauseMs + 25);
            }
            break;
        }

        default:
            break;
    }
}

void Actionments::Run() {
    while (running_) {
        Job job = {};
        if (xQueueReceive(queue_, &job, 0) == pdTRUE) {
            RunAction(job, false);
            continue;
        }

        const DriveDirection direction = drive();
        if (direction != DriveDirection::kNone) {
            const Job remote_job = {DriveToAction(direction), kRemoteSteps, 0};
            RunAction(remote_job, true);
            if (drive() == DriveDirection::kNone) {
                DoStand();
            }
            continue;
        }

        const MoodMove mood = TakePendingMoodMove();
        if (mood != MoodMove::kNone) {
            RunMoodMove(mood);
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
    task_ = nullptr;
}

void Actionments::RunAction(const Job& job, bool remote) {
    const Action action = job.action;
    abort_ = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_ = action;
    }
    ESP_LOGI(TAG, "action: %s%s", ActionName(action), remote ? " (remote)" : "");

    if (remote) {
        switch (action) {
            case Action::kWalk:
                DoWalk(false, job.steps, UseOr(job.period, kWalkPeriodMs));
                break;
            case Action::kWalkBack:
                DoWalk(true, job.steps, UseOr(job.period, kWalkPeriodMs));
                break;
            case Action::kTurnLeft:
                DoTurn(false, job.steps, UseOr(job.period, kTurnPeriodMs));
                break;
            case Action::kTurnRight:
                DoTurn(true, job.steps, UseOr(job.period, kTurnPeriodMs));
                break;
            default:
                Execute(job);
                break;
        }
    } else {
        Execute(job);
        if (action == Action::kWalk || action == Action::kWalkBack ||
            action == Action::kTurnLeft || action == Action::kTurnRight) {
            DoStand();
        }
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (current_ == action) {
            current_ = Action::kCount;
        }
    }
}

Actionments::Action Actionments::DriveToAction(DriveDirection direction) {
    switch (direction) {
        case DriveDirection::kForward:
            return Action::kWalk;
        case DriveDirection::kBackward:
            return Action::kWalkBack;
        case DriveDirection::kLeft:
            return Action::kTurnLeft;
        case DriveDirection::kRight:
            return Action::kTurnRight;
        default:
            return Action::kStand;
    }
}

bool Actionments::Request(Action action, int steps, int period) {
    if (!running_ || queue_ == nullptr || action >= Action::kCount) {
        return false;
    }
    if (current() == action) {
        return true;
    }
    abort_ = true;
    xQueueReset(queue_);

    Job job = {action, 0, 0};
    if (steps > 0) {
        job.steps = std::min(steps, kMaxSteps);
    }
    if (period > 0) {
        job.period = std::min(std::max(period, kMinPeriodMs), kMaxPeriodMs);
    }
    if (xQueueSend(queue_, &job, 0) != pdTRUE) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_ = action;
    }
    return true;
}

void Actionments::SetDrive(DriveDirection direction) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (drive_ == direction) {
            return;
        }
        drive_ = direction;
    }
    if (direction != DriveDirection::kNone) {
        abort_ = true;
    }
    ESP_LOGI(TAG, "drive: %s", DriveName(direction));
}

Actionments::DriveDirection Actionments::drive() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return drive_;
}

bool Actionments::SetLegAngle(int index, int angle) {
    if (index < 0 || index >= kLegCount || pins_[index] == GPIO_NUM_NC) {
        return false;
    }
    abort_ = true;
    legs_[index].SetAngleNow(std::min(std::max(angle, 0), 180));
    return true;
}

int Actionments::leg_angle(int index) const {
    if (index < 0 || index >= kLegCount) {
        return -1;
    }
    return legs_[index].angle();
}

Actionments::Action Actionments::current() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_;
}

bool Actionments::busy() const { return current() != Action::kCount; }

void Actionments::SetSpeedPercent(int percent) {
    if (percent < 50) {
        percent = 50;
    } else if (percent > 200) {
        percent = 200;
    }
    speed_percent_ = percent;
}

int Actionments::ScaleMs(int ms) const {
    const int percent = speed_percent_;
    if (percent <= 0 || percent == 100 || ms <= 0) {
        return ms;
    }
    const int scaled = ms * 100 / percent;
    return (scaled < 1) ? 1 : scaled;
}

void Actionments::Delay(int ms) const {
    const TickType_t ticks = pdMS_TO_TICKS(ScaleMs(ms));
    vTaskDelay(ticks > 0 ? ticks : 1);
}

bool Actionments::SetLegTrim(int index, int trim) {
    if (index < 0 || index >= kLegCount) {
        return false;
    }
    if (trim > kMaxTrimDeg) {
        trim = kMaxTrimDeg;
    } else if (trim < -kMaxTrimDeg) {
        trim = -kMaxTrimDeg;
    }
    legs_[index].SetTrim(trim);
    trims_dirty_ = true;
    return true;
}

int Actionments::leg_trim(int index) const {
    if (index < 0 || index >= kLegCount) {
        return 0;
    }
    return legs_[index].trim();
}

void Actionments::SaveTrims() {
    Settings settings(kTrimNamespace, true);
    for (size_t i = 0; i < kLegCount; ++i) {
        settings.SetInt(kTrimKeys[i], legs_[i].trim());
    }
    trims_dirty_ = false;
    ESP_LOGI(TAG, "servo trims saved: %d %d %d %d", legs_[0].trim(), legs_[1].trim(),
             legs_[2].trim(), legs_[3].trim());
}

void Actionments::LoadTrims() {
    Settings settings(kTrimNamespace);
    bool any = false;
    for (size_t i = 0; i < kLegCount; ++i) {
        int trim = settings.GetInt(kTrimKeys[i], 0);
        if (trim > kMaxTrimDeg) {
            trim = kMaxTrimDeg;
        } else if (trim < -kMaxTrimDeg) {
            trim = -kMaxTrimDeg;
        }
        legs_[i].SetTrim(trim);
        any = any || (trim != 0);
    }
    trims_dirty_ = false;
    if (any) {
        ESP_LOGI(TAG, "servo trims loaded: %d %d %d %d", legs_[0].trim(), legs_[1].trim(),
                 legs_[2].trim(), legs_[3].trim());
    }
}

void Actionments::MoveServos(int time_ms, const int target[kLegCount]) {
    const int steps = std::max(1, ScaleMs(time_ms) / 10);
    float current[kLegCount];
    float increment[kLegCount];
    for (int i = 0; i < kLegCount; ++i) {
        current[i] = static_cast<float>(legs_[i].angle());
        increment[i] = (target[i] - current[i]) / steps;
    }

    for (int step = 0; step < steps; ++step) {
        if (aborted()) {
            return;
        }
        for (int i = 0; i < kLegCount; ++i) {
            if (pins_[i] == GPIO_NUM_NC) {
                continue;
            }
            current[i] += increment[i];
            legs_[i].SetAngle(static_cast<int>(current[i] + 0.5f));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    for (int i = 0; i < kLegCount; ++i) {
        if (pins_[i] != GPIO_NUM_NC) {
            legs_[i].SetAngle(target[i]);
        }
    }
}

void Actionments::MoveSingle(int angle, int index) {
    if (index < 0 || index >= kLegCount || pins_[index] == GPIO_NUM_NC) {
        return;
    }
    legs_[index].SetAngle(std::min(std::max(angle, 0), 180));
}

void Actionments::AllTo(int angle, int time_ms) {
    const int target[kLegCount] = {angle, angle, angle, angle};
    MoveServos(time_ms, target);
}

bool Actionments::AtPose(const int target[kLegCount]) const {
    for (int i = 0; i < kLegCount; ++i) {
        if (pins_[i] == GPIO_NUM_NC) {
            continue;
        }
        if (std::abs(legs_[i].angle() - target[i]) > kAtPoseToleranceDeg) {
            return false;
        }
    }
    return true;
}

void Actionments::Execute(const Job& job) {
    switch (job.action) {
        case Action::kStand:
            DoStand();
            break;
        case Action::kSit:
            DoSit();
            break;
        case Action::kSleep:
            DoSleep();
            break;
        case Action::kProne:
            DoProne(true);
            break;
        case Action::kWalk:
            DoWalk(false, UseOr(job.steps, kWalkDefaultSteps), UseOr(job.period, kWalkPeriodMs));
            break;
        case Action::kWalkBack:
            DoWalk(true, UseOr(job.steps, kWalkDefaultSteps), UseOr(job.period, kWalkPeriodMs));
            break;
        case Action::kTurnLeft:
            DoTurn(false, UseOr(job.steps, kTurnDefaultSteps), UseOr(job.period, kTurnPeriodMs));
            break;
        case Action::kTurnRight:
            DoTurn(true, UseOr(job.steps, kTurnDefaultSteps), UseOr(job.period, kTurnPeriodMs));
            break;
        case Action::kWave:
            DoWave(3, 5);
            break;
        case Action::kStretch:
            DoStretch();
            break;
        case Action::kSwing:
            DoSwing(1, 3);
            break;
        case Action::kSway:
            DoSwing(-1, 3);
            break;
        case Action::kScratch:
            DoScratch(5);
            break;
        case Action::kKneel:
            DoKneel(3);
            break;
        case Action::kFrontProne:
            DoFrontProne();
            break;
        case Action::kRaiseHips:
            DoRaiseHips(3);
            break;
        case Action::kShakeBackLegs:
            DoShakeBackLegs(kShakeBackCycles);
            break;
        default:
            break;
    }
}

void Actionments::DoStand() {
    const int target[kLegCount] = {kStandAngle, kStandAngle, kStandAngle, kStandAngle};
    if (!AtPose(target)) {
        MoveServos(1000, target);
    }
    Delay(200);
}

void Actionments::DoSit() {
    const int target[kLegCount] = {kSitFrontAngle, kSitFrontAngle, kSitBackAngle, kSitBackAngle};
    if (!AtPose(target)) {
        MoveServos(1000, target);
    }
    Delay(300);
}

void Actionments::DoSleep() {
    const int target[kLegCount] = {kSleepFrontAngle, kSleepFrontAngle, kSleepBackAngle,
                                   kSleepBackAngle};
    if (!AtPose(target)) {
        MoveServos(1000, target);
    }
    Delay(300);
}

void Actionments::DoProne(bool cozy) {
    int target[kLegCount];
    if (cozy) {
        target[kLeftFrontLeg] = target[kRightFrontLeg] = kCozyProneFrontAngle;
        target[kLeftBackLeg] = target[kRightBackLeg] = kCozyProneBackAngle;
    } else {
        target[kLeftFrontLeg] = target[kRightFrontLeg] = kProneFrontAngle;
        target[kLeftBackLeg] = target[kRightBackLeg] = kProneBackAngle;
    }
    if (!AtPose(target)) {
        MoveServos(800, target);
    }
}

void Actionments::DoWalk(bool backward, int steps, int period) {
    const int forward[8][kLegCount] = {
        {kLegNeutral, kLegUp, kLegUp, kLegNeutral},
        {kLegExtend, kLegUp, kLegUp, kLegExtend},
        {kLegExtend, kLegNeutral, kLegNeutral, kLegExtend},
        {kLegNeutral, kLegNeutral, kLegNeutral, kLegNeutral},
        {kLegUp, kLegNeutral, kLegNeutral, kLegUp},
        {kLegUp, kLegExtend, kLegExtend, kLegUp},
        {kLegNeutral, kLegExtend, kLegExtend, kLegNeutral},
        {kLegNeutral, kLegNeutral, kLegNeutral, kLegNeutral},
    };
    const int backward_gait[8][kLegCount] = {
        {kLegNeutral, kLegExtend, kLegExtend, kLegNeutral},
        {kLegUp, kLegExtend, kLegExtend, kLegUp},
        {kLegUp, kLegNeutral, kLegNeutral, kLegUp},
        {kLegNeutral, kLegNeutral, kLegNeutral, kLegNeutral},
        {kLegExtend, kLegNeutral, kLegNeutral, kLegExtend},
        {kLegExtend, kLegUp, kLegUp, kLegExtend},
        {kLegNeutral, kLegUp, kLegUp, kLegNeutral},
        {kLegNeutral, kLegNeutral, kLegNeutral, kLegNeutral},
    };

    for (int step = 0; step < steps; ++step) {
        for (int gait = 0; gait < 8; ++gait) {
            if (aborted()) {
                return;
            }
            MoveServos(period, backward ? backward_gait[gait] : forward[gait]);
        }
    }
}

void Actionments::DoTurn(bool right, int steps, int period) {
    const int to_right[4][kLegCount] = {
        {kTurnAngle, kTurnOutwardAngle, kTurnInwardAngle, kTurnAngle},
        {kTurnOutwardAngle, kTurnOutwardAngle, kTurnInwardAngle, kTurnInwardAngle},
        {kTurnOutwardAngle, kTurnAngle, kTurnAngle, kTurnInwardAngle},
        {kTurnAngle, kTurnAngle, kTurnAngle, kTurnAngle},
    };
    const int to_left[4][kLegCount] = {
        {kTurnOutwardAngle, kTurnAngle, kTurnAngle, kTurnInwardAngle},
        {kTurnOutwardAngle, kTurnOutwardAngle, kTurnInwardAngle, kTurnInwardAngle},
        {kTurnAngle, kTurnOutwardAngle, kTurnInwardAngle, kTurnAngle},
        {kTurnAngle, kTurnAngle, kTurnAngle, kTurnAngle},
    };

    for (int step = 0; step < steps; ++step) {
        for (int gait = 0; gait < 4; ++gait) {
            if (aborted()) {
                return;
            }
            MoveServos(period, right ? to_right[gait] : to_left[gait]);
        }
    }
}

void Actionments::DoWave(int waves, int speed) {
    const int sit[kLegCount] = {kSitFrontAngle, kSitFrontAngle, kWaveSitLeftBackAngle,
                                kWaveSitRightBackAngle};
    MoveServos(800, sit);

    if (speed < 1) {
        speed = 1;
    }
    while (waves-- > 0) {
        for (int angle = 0; angle <= kWaveAmplitude; angle += speed) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftFrontLeg);
            Delay(25);
        }
        for (int angle = kWaveAmplitude; angle >= 0; angle -= speed) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftFrontLeg);
            Delay(25);
        }
    }
    Delay(100);
}

void Actionments::DoStretch() {
    DoStand();
    for (int angle = kStandAngle; angle > kStretchFrontAngle; --angle) {
        if (aborted()) {
            return;
        }
        MoveSingle(angle, kLeftFrontLeg);
        MoveSingle(angle, kRightFrontLeg);
        Delay(15);
    }
    for (int angle = kStretchFrontAngle; angle < kStandAngle; ++angle) {
        if (aborted()) {
            return;
        }
        MoveSingle(angle, kLeftFrontLeg);
        MoveSingle(angle, kRightFrontLeg);
        Delay(15);
    }
    for (int angle = kStandAngle; angle < kStretchBackAngle; ++angle) {
        if (aborted()) {
            return;
        }
        MoveSingle(angle, kLeftBackLeg);
        MoveSingle(angle, kRightBackLeg);
        Delay(15);
    }
    for (int angle = kStretchBackAngle; angle > kStandAngle; --angle) {
        if (aborted()) {
            return;
        }
        MoveSingle(angle, kLeftBackLeg);
        MoveSingle(angle, kRightBackLeg);
        Delay(15);
    }
    DoLStretch();
}

void Actionments::DoLStretch() {
    constexpr int kCycles = 3;
    DoStand();

    MoveSingle(kStandAngle, kLeftFrontLeg);
    MoveSingle(kLStretchFrontAngle, kRightFrontLeg);
    Delay(60);
    MoveSingle(kLStretchBackAngle, kRightBackLeg);
    for (int angle = kStandAngle; angle < kCozyProneBackAngle; ++angle) {
        if (aborted()) {
            return;
        }
        MoveSingle(angle, kLeftBackLeg);
        Delay(10);
    }
    for (int t = 0; t < kCycles; ++t) {
        for (int angle = kCozyProneBackAngle; angle > kLStretchAmplitude; --angle) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftBackLeg);
            Delay(15);
        }
    }
    Delay(100);
    MoveSingle(kStandAngle, kLeftFrontLeg);
    MoveSingle(kStandAngle, kRightFrontLeg);
    Delay(80);
    MoveSingle(kStandAngle, kLeftBackLeg);
    MoveSingle(kStandAngle, kRightBackLeg);
    Delay(100);

    MoveSingle(kStandAngle, kRightFrontLeg);
    MoveSingle(kLStretchFrontAngle, kLeftFrontLeg);
    Delay(60);
    MoveSingle(kLStretchBackAngle, kLeftBackLeg);
    for (int angle = kStandAngle; angle < kCozyProneBackAngle; ++angle) {
        if (aborted()) {
            return;
        }
        MoveSingle(angle, kRightBackLeg);
        Delay(10);
    }
    for (int t = 0; t < kCycles; ++t) {
        for (int angle = kCozyProneBackAngle; angle > kLStretchAmplitude; --angle) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kRightBackLeg);
            Delay(15);
        }
    }
    AllTo(kStandAngle, 300);
}

void Actionments::DoSwing(int direction, int period) {
    constexpr int kCycles = kSwingCycles;
    if (period < 1) {
        period = 1;
    }
    DoStand();

    if (direction == 1) {
        for (int angle = kStandAngle; angle > kSwingMinAngle; --angle) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftFrontLeg);
            MoveSingle(angle, kLeftBackLeg);
            Delay(10);
        }
        for (int cycle = 0; cycle < kCycles; ++cycle) {
            for (int angle = kSwingMinAngle; angle < kSwingMaxAngle; angle += period) {
                if (aborted()) {
                    return;
                }
                MoveSingle(angle, kLeftFrontLeg);
                MoveSingle(angle, kLeftBackLeg);
                MoveSingle(kSwingAmplitude - angle, kRightFrontLeg);
                MoveSingle(kSwingAmplitude - angle, kRightBackLeg);
                Delay(30);
            }
            for (int angle = kSwingMaxAngle; angle > kSwingMinAngle; angle -= period) {
                if (aborted()) {
                    return;
                }
                MoveSingle(angle, kLeftFrontLeg);
                MoveSingle(angle, kLeftBackLeg);
                MoveSingle(kSwingAmplitude - angle, kRightFrontLeg);
                MoveSingle(kSwingAmplitude - angle, kRightBackLeg);
                Delay(30);
            }
        }
    } else {
        for (int cycle = 0; cycle < kCycles; ++cycle) {
            for (int angle = kSwayMinAngle; angle < kSwayMaxAngle; angle += period) {
                if (aborted()) {
                    return;
                }
                AllTo(angle, 10);
                Delay(20);
            }
            for (int angle = kSwayMaxAngle; angle > kSwayMinAngle; angle -= period) {
                if (aborted()) {
                    return;
                }
                AllTo(angle, 10);
                Delay(20);
            }
        }
    }
    DoSit();
}

void Actionments::DoScratch(int period) {
    if (period < 1) {
        period = 1;
    }
    DoStand();

    const int pose[kLegCount] = {kScratchLeftFrontAngle, kScratchRightFrontAngle,
                                 kScratchBackAngle, kScratchBackAngle};
    MoveServos(500, pose);

    int waves = 5;
    while (waves-- > 0) {
        for (int angle = kScratchBackAngle; angle < kScratchAmplitude; angle += period) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftBackLeg);
            Delay(25);
        }
        for (int angle = kScratchAmplitude; angle > kScratchBackAngle; angle -= period) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftBackLeg);
            Delay(25);
        }
    }
    DoStand();
}

void Actionments::DoKneel(int times) {
    const int pose[kLegCount] = {kKneelFrontAngle, kKneelFrontAngle, kKneelBackAngle,
                                 kKneelBackAngle};
    MoveServos(500, pose);

    for (int n = 0; n < times; ++n) {
        for (int angle = kKneelFrontAngle; angle >= kKneelBackAngle; angle -= 3) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftFrontLeg);
            MoveSingle(angle, kRightFrontLeg);
            Delay(25);
        }
        for (int angle = kKneelBackAngle; angle < kKneelFrontAngle; angle += 3) {
            if (aborted()) {
                return;
            }
            MoveSingle(angle, kLeftFrontLeg);
            MoveSingle(angle, kRightFrontLeg);
            Delay(25);
        }
    }
    DoStand();
}

void Actionments::DoFrontProne() {
    const int target[kLegCount] = {kFrontProneFrontAngle, kFrontProneFrontAngle,
                                   kFrontProneBackAngle, kFrontProneBackAngle};
    if (!AtPose(target)) {
        MoveServos(800, target);
    }
    Delay(300);
}

void Actionments::DoRaiseHips(int times) {
    if (times < 1) {
        times = 1;
    }
    DoStand();

    int target[kLegCount] = {kRaiseHipsLowAngle, kRaiseHipsLowAngle, kStandAngle, kStandAngle};
    MoveServos(400, target);
    Delay(250);
    if (aborted()) {
        return;
    }

    for (int n = 0; n < times; ++n) {
        target[kLeftFrontLeg] = target[kRightFrontLeg] = kRaiseHipsLowAngle + kRaiseHipsJitterDeg;
        MoveServos(60, target);
        target[kLeftFrontLeg] = target[kRightFrontLeg] = kRaiseHipsLowAngle;
        MoveServos(60, target);
        if (aborted()) {
            return;
        }
    }

    DoStand();
}

void Actionments::DoShakeBackLegs(int cycles) {
    if (cycles < 1) {
        cycles = 1;
    }
    DoStand();

    const int open_target[kLegCount] = {kShakeBackFrontAngle, kShakeBackFrontAngle,
                                        kShakeBackBackAngle, kShakeBackBackAngle};
    MoveServos(kShakeBackRampMs, open_target);
    Delay(80);
    if (aborted()) {
        return;
    }

    for (int cycle = 0; cycle < cycles; ++cycle) {
        for (int i = 0; i <= kShakeBackJitterDeg; ++i) {
            if (aborted()) {
                return;
            }
            MoveSingle(kShakeBackBackAngle + i, kLeftBackLeg);
            MoveSingle(kShakeBackBackAngle + i, kRightBackLeg);
            Delay(kShakeBackShakeMs);
        }
        for (int i = kShakeBackJitterDeg - 1; i >= 1; --i) {
            if (aborted()) {
                return;
            }
            MoveSingle(kShakeBackBackAngle + i, kLeftBackLeg);
            MoveSingle(kShakeBackBackAngle + i, kRightBackLeg);
            Delay(kShakeBackShakeMs);
        }
    }

    const int stand_target[kLegCount] = {kStandAngle, kStandAngle, kStandAngle, kStandAngle};
    MoveServos(kShakeBackRampMs, stand_target);
    Delay(100);
}

}
