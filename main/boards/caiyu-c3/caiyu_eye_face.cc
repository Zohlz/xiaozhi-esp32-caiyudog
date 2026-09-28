#include "caiyu_eye_face.h"

#include <esp_random.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace caiyu {

namespace {

constexpr EyeConfig kPresetNormal{0, 0, 40, 40, 0.0f, 0.0f, 8, 8};
constexpr EyeConfig kPresetHappy{0, 0, 10, 40, 0.0f, 0.0f, 10, 0};
constexpr EyeConfig kPresetGlee{0, 0, 8, 40, 0.0f, 0.0f, 8, 0};
constexpr EyeConfig kPresetSad{0, 0, 15, 40, -0.5f, 0.0f, 1, 10};
constexpr EyeConfig kPresetWorried{0, 0, 35, 40, -0.2f, 0.0f, 6, 10};
constexpr EyeConfig kPresetFocused{0, 0, 14, 40, 0.2f, 0.0f, 3, 1};
constexpr EyeConfig kPresetAnnoyed{0, 0, 12, 40, 0.0f, 0.0f, 0, 10};
constexpr EyeConfig kPresetAnnoyedAlt{0, 0, 5, 40, 0.0f, 0.0f, 0, 4};
constexpr EyeConfig kPresetSurprised{-2, 0, 45, 45, 0.0f, 0.0f, 16, 16};
constexpr EyeConfig kPresetSkeptic{0, 0, 40, 40, 0.0f, 0.0f, 10, 10};
constexpr EyeConfig kPresetSkepticAlt{0, -6, 26, 40, 0.3f, 0.0f, 1, 10};
constexpr EyeConfig kPresetFrustrated{3, -5, 12, 40, 0.0f, 0.0f, 0, 10};
constexpr EyeConfig kPresetUnimpressed{3, 0, 12, 40, 0.0f, 0.0f, 1, 10};
constexpr EyeConfig kPresetUnimpressedAlt{3, -3, 22, 40, 0.0f, 0.0f, 1, 16};
constexpr EyeConfig kPresetSleepy{0, -2, 8, 38, -0.3f, -0.3f, 4, 4};
constexpr EyeConfig kPresetSuspicious{0, 0, 22, 40, 0.0f, 0.0f, 8, 3};
constexpr EyeConfig kPresetSuspiciousAlt{0, -3, 16, 40, 0.2f, 0.0f, 6, 3};
constexpr EyeConfig kPresetWink{0, 0, 12, 34, 0.0f, 0.0f, 12, 0};
constexpr EyeConfig kPresetAngry{-3, 0, 20, 40, 0.3f, 0.0f, 2, 12};
constexpr EyeConfig kPresetFurious{-2, 0, 30, 40, 0.4f, 0.0f, 2, 8};
constexpr EyeConfig kPresetScared{-3, 0, 40, 40, -0.1f, 0.0f, 12, 8};
constexpr EyeConfig kPresetAwe{2, 0, 35, 45, -0.1f, 0.1f, 12, 12};
constexpr EyeConfig kPresetLaughing{0, 2, 16, 38, 0.0f, 0.0f, 16, 0};
constexpr EyeConfig kPresetSillyWide{0, 0, 44, 44, 0.0f, 0.0f, 10, 10};
constexpr EyeConfig kPresetSillySlit{0, 3, 6, 28, 0.0f, 0.0f, 3, 3};
constexpr EyeConfig kPresetCrying{0, 0, 10, 36, -0.35f, 0.0f, 5, 2};
constexpr EyeConfig kPresetShocked{0, 0, 24, 24, 0.0f, 0.0f, 12, 12};
constexpr EyeConfig kPresetConfusedAlt{0, 0, 25, 40, -0.1f, 0.0f, 6, 10};
constexpr EyeConfig kPresetConfident{0, 0, 18, 36, 0.15f, 0.0f, 3, 9};
constexpr EyeConfig kPresetRelaxed{0, 0, 22, 40, 0.0f, 0.0f, 11, 11};
constexpr EyeConfig kPresetDelicious{0, 0, 13, 38, 0.0f, 0.0f, 13, 0};
constexpr EyeConfig kPresetDeliciousAlt{0, -4, 11, 34, 0.0f, 0.0f, 11, 0};
constexpr EyeConfig kPresetHeart{0, 0, 38, 38, 0.0f, 0.0f, 0.0f, 0.0f};

float RandomLookOffset() {
    return (static_cast<int32_t>(esp_random() % 100) - 50) / 100.0f;
}

void DrawEyeShape(MonoCanvas& canvas, int32_t center_x, int32_t center_y, const EyeConfig* config) {
    const int32_t offset_x = static_cast<int32_t>(config->offset_x);
    const int32_t offset_y = static_cast<int32_t>(config->offset_y);
    const int32_t height = static_cast<int32_t>(config->height);
    const int32_t width = static_cast<int32_t>(config->width);

    const int32_t delta_y_top = static_cast<int32_t>(config->height * config->slope_top / 2.0f);
    const int32_t delta_y_bottom =
        static_cast<int32_t>(config->height * config->slope_bottom / 2.0f);
    const int32_t total_height = height + delta_y_top - delta_y_bottom;

    int32_t radius_top = static_cast<int32_t>(config->radius_top);
    int32_t radius_bottom = static_cast<int32_t>(config->radius_bottom);

    if (radius_bottom > 0 && radius_top > 0 && total_height - 1 < radius_bottom + radius_top) {
        const int32_t sum = radius_bottom + radius_top;
        const int32_t corrected_top =
            static_cast<int32_t>(static_cast<float>(radius_top) * (total_height - 1) / sum);
        const int32_t corrected_bottom =
            static_cast<int32_t>(static_cast<float>(radius_bottom) * (total_height - 1) / sum);
        radius_top = corrected_top;
        radius_bottom = corrected_bottom;
    }

    const int32_t tlc_y = center_y + offset_y - height / 2 + radius_top - delta_y_top;
    const int32_t tlc_x = center_x + offset_x - width / 2 + radius_top;
    const int32_t trc_y = center_y + offset_y - height / 2 + radius_top + delta_y_top;
    const int32_t trc_x = center_x + offset_x + width / 2 - radius_top;
    const int32_t blc_y = center_y + offset_y + height / 2 - radius_bottom - delta_y_bottom;
    const int32_t blc_x = center_x + offset_x - width / 2 + radius_bottom;
    const int32_t brc_y = center_y + offset_y + height / 2 - radius_bottom + delta_y_bottom;
    const int32_t brc_x = center_x + offset_x + width / 2 - radius_bottom;

    const int32_t min_c_x = std::min(tlc_x, blc_x);
    const int32_t max_c_x = std::max(trc_x, brc_x);
    const int32_t min_c_y = std::min(tlc_y, trc_y);
    const int32_t max_c_y = std::max(blc_y, brc_y);

    canvas.FillRect(min_c_x, min_c_y, max_c_x, max_c_y, kInk);
    canvas.FillRect(trc_x, trc_y, brc_x + radius_bottom, brc_y, kInk);
    canvas.FillRect(tlc_x - radius_top, tlc_y, blc_x, blc_y, kInk);
    canvas.FillRect(tlc_x, tlc_y - radius_top, trc_x, trc_y, kInk);
    canvas.FillRect(blc_x, blc_y, brc_x, brc_y + radius_bottom, kInk);

    if (config->slope_top > 0) {
        canvas.FillRectangularTriangle(tlc_x, tlc_y - radius_top, trc_x, trc_y - radius_top,
                                       kClear);
        canvas.FillRectangularTriangle(trc_x, trc_y - radius_top, tlc_x, tlc_y - radius_top, kInk);
    } else if (config->slope_top < 0) {
        canvas.FillRectangularTriangle(trc_x, trc_y - radius_top, tlc_x, tlc_y - radius_top,
                                       kClear);
        canvas.FillRectangularTriangle(tlc_x, tlc_y - radius_top, trc_x, trc_y - radius_top, kInk);
    }
    if (config->slope_bottom > 0) {
        canvas.FillRectangularTriangle(brc_x + radius_bottom, brc_y + radius_bottom,
                                       blc_x - radius_bottom, blc_y + radius_bottom, kClear);
        canvas.FillRectangularTriangle(blc_x - radius_bottom, blc_y + radius_bottom,
                                       brc_x + radius_bottom, brc_y + radius_bottom, kInk);
    } else if (config->slope_bottom < 0) {
        canvas.FillRectangularTriangle(blc_x - radius_bottom, blc_y + radius_bottom,
                                       brc_x + radius_bottom, brc_y + radius_bottom, kClear);
        canvas.FillRectangularTriangle(brc_x + radius_bottom, brc_y + radius_bottom,
                                       blc_x - radius_bottom, blc_y + radius_bottom, kInk);
    }

    if (radius_top > 0) {
        canvas.FillEllipseCorner(MonoCanvas::Corner::kTopLeft, tlc_x, tlc_y, radius_top,
                                 radius_top, kInk);
        canvas.FillEllipseCorner(MonoCanvas::Corner::kTopRight, trc_x, trc_y, radius_top,
                                 radius_top, kInk);
    }
    if (radius_bottom > 0) {
        canvas.FillEllipseCorner(MonoCanvas::Corner::kBottomLeft, blc_x, blc_y, radius_bottom,
                                 radius_bottom, kInk);
        canvas.FillEllipseCorner(MonoCanvas::Corner::kBottomRight, brc_x, brc_y, radius_bottom,
                                 radius_bottom, kInk);
    }
}

void DrawEyeHeart(MonoCanvas& canvas, int32_t center_x, int32_t center_y,
                  const EyeConfig* config) {
    const int32_t cx = center_x + static_cast<int32_t>(config->offset_x);
    const int32_t cy = center_y + static_cast<int32_t>(config->offset_y);
    const int32_t w = static_cast<int32_t>(config->width);
    const int32_t h = static_cast<int32_t>(config->height);

    if (h < 6) {
        canvas.HLine(cx - w / 2, cy, w, kInk);
        return;
    }

    const int32_t r = w / 4;
    const int32_t lobe_y = cy - h / 4;
    FillEllipse(canvas, cx - r, lobe_y, r, r, kInk);
    FillEllipse(canvas, cx + r, lobe_y, r, r, kInk);
    canvas.FillTriangle(cx - w / 2, lobe_y, cx + w / 2, lobe_y, cx, cy + h / 2, kInk);
}

constexpr uint32_t kBreathPeriodMs = 3000;
constexpr int32_t kBreathAmplitude = 2;

int32_t BreathOffset() {
    const uint32_t phase = lv_tick_get() % kBreathPeriodMs;
    const uint32_t half = kBreathPeriodMs / 2;
    if (phase < half) {
        return -kBreathAmplitude + static_cast<int32_t>(2 * kBreathAmplitude * phase / half);
    }
    return kBreathAmplitude - static_cast<int32_t>(2 * kBreathAmplitude * (phase - half) / half);
}

void DrawZ(MonoCanvas& canvas, int32_t x, int32_t y, int32_t size) {
    if (size < 3) {
        return;
    }
    canvas.HLine(x, y, size, kInk);
    canvas.HLine(x, y + size - 1, size, kInk);
    canvas.Line(x + size - 1, y + 1, x, y + size - 2, kInk);
}

constexpr int32_t kSleepMarks[3][3] = {
    {90, 18, 6},
    {100, 11, 7},
    {110, 4, 8},
};
constexpr size_t kSleepMarkCount = sizeof(kSleepMarks) / sizeof(kSleepMarks[0]);

void DrawSleepMarks(MonoCanvas& canvas) {
    constexpr uint32_t kMarkStepMs = kBreathPeriodMs / kSleepMarkCount;
    const size_t visible = lv_tick_get() % kBreathPeriodMs / kMarkStepMs + 1;
    for (size_t i = 0; i < visible && i < kSleepMarkCount; ++i) {
        DrawZ(canvas, kSleepMarks[i][0], kSleepMarks[i][1], kSleepMarks[i][2]);
    }
}

}

void FillEllipse(MonoCanvas& canvas, int32_t center_x, int32_t center_y, int32_t rx, int32_t ry,
                 uint8_t value) {
    canvas.FillEllipseCorner(MonoCanvas::Corner::kTopLeft, center_x, center_y, rx, ry, value);
    canvas.FillEllipseCorner(MonoCanvas::Corner::kTopRight, center_x, center_y, rx, ry, value);
    canvas.FillEllipseCorner(MonoCanvas::Corner::kBottomLeft, center_x, center_y, rx, ry, value);
    canvas.FillEllipseCorner(MonoCanvas::Corner::kBottomRight, center_x, center_y, rx, ry, value);
}

void MonoCanvas::HLine(int32_t x, int32_t y, int32_t length, uint8_t value) {
    if (buffer_ == nullptr || length <= 0 || y < 0 || y >= height_) {
        return;
    }
    int32_t from = std::max<int32_t>(x, 0);
    int32_t to = std::min<int32_t>(x + length, width_);
    if (from >= to) {
        return;
    }

    uint8_t* row = buffer_ + y * stride_;
    while (from < to && (from & 7) != 0) {
        SetBit(row, from, value);
        ++from;
    }
    while (from + 8 <= to) {
        row[from >> 3] = value ? 0xFF : 0x00;
        from += 8;
    }
    while (from < to) {
        SetBit(row, from, value);
        ++from;
    }
}

uint32_t MonoCanvas::Hash() const {
    if (buffer_ == nullptr) {
        return 0;
    }
    uint32_t hash = 0x811c9dc5u;
    const uint8_t* p = buffer_;
    const int32_t bytes = stride_ * height_;
    for (int32_t i = 0; i < bytes; ++i) {
        hash = (hash ^ p[i]) * 0x01000193u;
    }
    return hash;
}

void MonoCanvas::Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t value) {
    if (buffer_ == nullptr) {
        return;
    }
    const int32_t dx = std::abs(x1 - x0);
    const int32_t dy = -std::abs(y1 - y0);
    const int32_t step_x = (x0 < x1) ? 1 : -1;
    const int32_t step_y = (y0 < y1) ? 1 : -1;
    int32_t error = dx + dy;

    while (true) {
        SetPixel(x0, y0, value);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t error2 = 2 * error;
        if (error2 >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (error2 <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

void MonoCanvas::FillRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t value) {
    const int32_t left = std::min(x0, x1);
    const int32_t right = std::max(x0, x1);
    const int32_t top = std::min(y0, y1);
    const int32_t bottom = std::max(y0, y1);
    for (int32_t y = top; y < bottom; ++y) {
        HLine(left, y, right - left, value);
    }
}

void MonoCanvas::FillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                              int32_t y2, uint8_t value) {
    if (buffer_ == nullptr) {
        return;
    }
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    if (y1 > y2) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    if (y2 < 0 || y0 >= height_) {
        return;
    }

    const int32_t y_start = std::max<int32_t>(y0, 0);
    const int32_t y_end = std::min<int32_t>(y2, height_ - 1);

    for (int32_t y = y_start; y <= y_end; ++y) {
        int32_t hits[3] = {0, 0, 0};
        int32_t hit_count = 0;

        auto intersect_edge = [&](int32_t ax, int32_t ay, int32_t bx, int32_t by) {
            if (ay == by) {
                return;
            }
            if (y < std::min(ay, by) || y > std::max(ay, by)) {
                return;
            }
            if (ay > by) {
                std::swap(ax, bx);
                std::swap(ay, by);
            }
            const int64_t numerator = static_cast<int64_t>(bx - ax) * (y - ay);
            const int32_t denominator = by - ay;
            const int64_t rounded = (numerator >= 0) ? (numerator + denominator / 2)
                                                     : (numerator - denominator / 2);
            hits[hit_count++] = ax + static_cast<int32_t>(rounded / denominator);
        };

        intersect_edge(x0, y0, x1, y1);
        intersect_edge(x1, y1, x2, y2);
        intersect_edge(x2, y2, x0, y0);
        if (hit_count < 2) {
            continue;
        }

        int32_t left = hits[0];
        int32_t right = hits[0];
        for (int32_t i = 1; i < hit_count; ++i) {
            left = std::min(left, hits[i]);
            right = std::max(right, hits[i]);
        }
        HLine(left, y, right - left + 1, value);
    }
}

void MonoCanvas::FillEllipseCorner(Corner corner, int32_t x0, int32_t y0, int32_t rx, int32_t ry,
                                   uint8_t value) {
    if (rx < 2 || ry < 2) {
        return;
    }

    const int32_t rx2 = rx * rx;
    const int32_t ry2 = ry * ry;
    const int32_t fx2 = 4 * rx2;
    const int32_t fy2 = 4 * ry2;
    int32_t x = 0;
    int32_t y = 0;
    int32_t s = 0;

    switch (corner) {
        case Corner::kTopRight:
            for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++) {
                HLine(x0, y0 - y, x, value);
                if (s >= 0) {
                    s += fx2 * (1 - y);
                    y--;
                }
                s += ry2 * ((4 * x) + 6);
            }
            for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++) {
                HLine(x0, y0 - y, x, value);
                if (s >= 0) {
                    s += fy2 * (1 - x);
                    x--;
                }
                s += rx2 * ((4 * y) + 6);
            }
            break;

        case Corner::kBottomRight:
            for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++) {
                HLine(x0, y0 + y - 1, x, value);
                if (s >= 0) {
                    s += fx2 * (1 - y);
                    y--;
                }
                s += ry2 * ((4 * x) + 6);
            }
            for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++) {
                HLine(x0, y0 + y - 1, x, value);
                if (s >= 0) {
                    s += fy2 * (1 - x);
                    x--;
                }
                s += rx2 * ((4 * y) + 6);
            }
            break;

        case Corner::kTopLeft:
            for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++) {
                HLine(x0 - x, y0 - y, x, value);
                if (s >= 0) {
                    s += fx2 * (1 - y);
                    y--;
                }
                s += ry2 * ((4 * x) + 6);
            }
            for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++) {
                HLine(x0 - x, y0 - y, x, value);
                if (s >= 0) {
                    s += fy2 * (1 - x);
                    x--;
                }
                s += rx2 * ((4 * y) + 6);
            }
            break;

        case Corner::kBottomLeft:
            for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++) {
                HLine(x0 - x, y0 + y - 1, x, value);
                if (s >= 0) {
                    s += fx2 * (1 - y);
                    y--;
                }
                s += ry2 * ((4 * x) + 6);
            }
            for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++) {
                HLine(x0 - x, y0 + y, x, value);
                if (s >= 0) {
                    s += fy2 * (1 - x);
                    x--;
                }
                s += rx2 * ((4 * y) + 6);
            }
            break;
    }
}

void EyeTransition::Apply(float t) {
    if (origin == nullptr) {
        return;
    }
    origin->offset_x = origin->offset_x * (1.0f - t) + destin.offset_x * t;
    origin->offset_y = origin->offset_y * (1.0f - t) + destin.offset_y * t;
    origin->height = origin->height * (1.0f - t) + destin.height * t;
    origin->width = origin->width * (1.0f - t) + destin.width * t;
    origin->slope_top = origin->slope_top * (1.0f - t) + destin.slope_top * t;
    origin->slope_bottom = origin->slope_bottom * (1.0f - t) + destin.slope_bottom * t;
    origin->radius_top = origin->radius_top * (1.0f - t) + destin.radius_top * t;
    origin->radius_bottom = origin->radius_bottom * (1.0f - t) + destin.radius_bottom * t;
}

void EyeTransformation::Update() {
    const float t = animation_.GetValue();
    current_.move_x = (destin_.move_x - origin_.move_x) * t + origin_.move_x;
    current_.move_y = (destin_.move_y - origin_.move_y) * t + origin_.move_y;
    current_.scale_x = (destin_.scale_x - origin_.scale_x) * t + origin_.scale_x;
    current_.scale_y = (destin_.scale_y - origin_.scale_y) * t + origin_.scale_y;
    Apply();
}

void EyeTransformation::Apply() {
    if (input == nullptr) {
        return;
    }
    output.offset_x = input->offset_x + current_.move_x;
    output.offset_y = input->offset_y - current_.move_y;
    output.width = input->width * current_.scale_x;
    output.height = input->height * current_.scale_y;

    output.slope_top = input->slope_top;
    output.slope_bottom = input->slope_bottom;
    output.radius_top = input->radius_top;
    output.radius_bottom = input->radius_bottom;
}

void EyeTransformation::SetDestin(const Transformation& transformation) {
    origin_ = current_;
    destin_ = transformation;
}

EyeVariation::EyeVariation() : animation(0, 1000, 0, 1000, 0) {}

void EyeVariation::Clear() {
    values.offset_x = 0.0f;
    values.offset_y = 0.0f;
    values.height = 0.0f;
    values.width = 0.0f;
    values.slope_top = 0.0f;
    values.slope_bottom = 0.0f;
    values.radius_top = 0.0f;
    values.radius_bottom = 0.0f;
}

void EyeVariation::Update() { Apply(2.0f * animation.GetValue() - 1.0f); }

void EyeVariation::Apply(float t) {
    if (input == nullptr) {
        return;
    }
    output.offset_x = input->offset_x + values.offset_x * t;
    output.offset_y = input->offset_y + values.offset_y * t;
    output.height = input->height + values.height * t;
    output.width = input->width + values.width * t;
    output.slope_top = input->slope_top + values.slope_top * t;
    output.slope_bottom = input->slope_bottom + values.slope_bottom * t;
    output.radius_top = input->radius_top + values.radius_top * t;
    output.radius_bottom = input->radius_bottom + values.radius_bottom * t;
}

void EyeBlink::Update() {
    float t = animation_.GetValue();
    if (animation_.GetElapsed() > animation_.interval()) {
        t = 0.0f;
    }
    Apply(t * t);
}

void EyeBlink::Apply(float t) {
    if (input == nullptr) {
        return;
    }
    output.offset_x = input->offset_x;
    output.offset_y = input->offset_y;

    output.width = (blink_width - input->width) * t + input->width;
    output.height = (blink_height - input->height) * t + input->height;

    output.slope_top = input->slope_top * (1.0f - t);
    output.slope_bottom = input->slope_bottom * (1.0f - t);
    output.radius_top = input->radius_top * (1.0f - t);
    output.radius_bottom = input->radius_bottom * (1.0f - t);
}

Eye::Eye() {
    ChainOperators();
    variation1_.animation.SetInterval(200, 200, 200, 200, 0);
    variation2_.animation.SetInterval(0, 200, 200, 200, 200);
}

void Eye::ChainOperators() {
    transition_.origin = &config_;
    transformation_.input = &config_;
    variation1_.input = &transformation_.output;
    variation2_.input = &variation1_.output;
    blink_.input = &variation2_.output;
    final_config_ = &blink_.output;
}

void Eye::TransitionTo(const EyeConfig& preset) {
    transition_.destin.offset_x = mirrored_ ? -preset.offset_x : preset.offset_x;
    transition_.destin.offset_y = -preset.offset_y;
    transition_.destin.height = preset.height;
    transition_.destin.width = preset.width;
    transition_.destin.slope_top = mirrored_ ? preset.slope_top : -preset.slope_top;
    transition_.destin.slope_bottom = mirrored_ ? preset.slope_bottom : -preset.slope_bottom;
    transition_.destin.radius_top = preset.radius_top;
    transition_.destin.radius_bottom = preset.radius_bottom;
    transition_.Restart();
}

void Eye::Update() {
    transition_.Update();
    transformation_.Update();
    variation1_.Update();
    variation2_.Update();
    blink_.Update();
}

void Eye::Draw(MonoCanvas& canvas) {
    Update();
    if (final_config_ == nullptr) {
        return;
    }
    if (heart_) {
        DrawEyeHeart(canvas, center_x_, center_y_, final_config_);
    } else {
        DrawEyeShape(canvas, center_x_, center_y_, final_config_);
    }
}

EyeExpression ExpressionFromEmotion(Emotion emotion) {
    switch (emotion) {
        case Emotion::kHappy:
            return EyeExpression::kHappy;
        case Emotion::kFunny:
            return EyeExpression::kFunny;
        case Emotion::kSilly:
            return EyeExpression::kSilly;
        case Emotion::kLaughing:
            return EyeExpression::kLaughing;
        case Emotion::kKissy:
            return EyeExpression::kKiss;
        case Emotion::kSad:
            return EyeExpression::kSad;
        case Emotion::kCrying:
            return EyeExpression::kCrying;
        case Emotion::kAngry:
            return EyeExpression::kAngry;
        case Emotion::kSurprised:
            return EyeExpression::kSurprised;
        case Emotion::kShocked:
            return EyeExpression::kShocked;
        case Emotion::kSleepy:
            return EyeExpression::kSleepy;
        case Emotion::kThinking:
            return EyeExpression::kSkeptic;
        case Emotion::kConfused:
            return EyeExpression::kConfused;
        case Emotion::kEmbarrassed:
            return EyeExpression::kWorried;
        case Emotion::kWinking:
            return EyeExpression::kSquint;
        case Emotion::kCool:
            return EyeExpression::kUnimpressed;
        case Emotion::kConfident:
            return EyeExpression::kConfident;
        case Emotion::kRelaxed:
            return EyeExpression::kRelaxed;
        case Emotion::kLoving:
            return EyeExpression::kHeart;
        case Emotion::kDelicious:
            return EyeExpression::kDelicious;
        default:
            return EyeExpression::kNormal;
    }
}

EyeFace::EyeFace() {
    left_eye_.SetMirrored(true);
    blink_timer_.Restart();
    look_timer_.Restart();
}

void EyeFace::SetViewport(int32_t width, int32_t height) {
    width_ = width;
    height_ = height;
    center_x_ = width / 2;
    center_y_ = height / 2;
}

void EyeFace::LookAt(float x, float y) {
    const float move_x = -25.0f * x;
    const float move_y = 20.0f * y;
    const float scale_y_x = 1.0f - x * 0.2f;
    const float scale_y_y = 1.0f - (y > 0 ? y : -y) * 0.4f;

    Transformation transformation;
    transformation.move_x = move_x;
    transformation.move_y = move_y;
    transformation.scale_x = 1.0f;

    transformation.scale_y = scale_y_x * scale_y_y;
    right_eye_.transformation().SetDestin(transformation);
    transformation.scale_y = (1.0f + x * 0.2f) * scale_y_y;
    left_eye_.transformation().SetDestin(transformation);

    right_eye_.transformation().Restart();
    left_eye_.transformation().Restart();
}

void EyeFace::ClearVariations() {
    right_eye_.variation1().Clear();
    right_eye_.variation2().Clear();
    left_eye_.variation1().Clear();
    left_eye_.variation2().Clear();
    right_eye_.variation1().Restart();
    left_eye_.variation1().Restart();
}

void EyeFace::GoToExpression(EyeExpression expression) {
    expression_ = expression;

    right_eye_.SetHeart(false);
    left_eye_.SetHeart(false);

    switch (expression) {
        case EyeExpression::kNormal:
            ClearVariations();
            right_eye_.variation1().values.height = 3;
            right_eye_.variation2().values.width = 1;
            left_eye_.variation1().values.height = 2;
            left_eye_.variation2().values.width = 2;
            right_eye_.variation1().animation.SetTriangle(1000, 0);
            left_eye_.variation1().animation.SetTriangle(1000, 0);
            right_eye_.TransitionTo(kPresetNormal);
            left_eye_.TransitionTo(kPresetNormal);
            break;

        case EyeExpression::kRelaxed:
            ClearVariations();
            right_eye_.variation1().values.offset_y = 2;
            left_eye_.variation1().values.offset_y = 2;
            right_eye_.variation1().animation.SetTriangle(2000, 0);
            left_eye_.variation1().animation.SetTriangle(2000, 0);
            right_eye_.TransitionTo(kPresetRelaxed);
            left_eye_.TransitionTo(kPresetRelaxed);
            break;

        case EyeExpression::kAngry:
            ClearVariations();
            right_eye_.variation1().values.offset_y = 2;
            left_eye_.variation1().values.offset_y = 2;
            right_eye_.variation1().animation.SetTriangle(300, 0);
            left_eye_.variation1().animation.SetTriangle(300, 0);
            right_eye_.TransitionTo(kPresetAngry);
            left_eye_.TransitionTo(kPresetAngry);
            break;

        case EyeExpression::kFunny:
            ClearVariations();
            right_eye_.variation1().values.offset_y = 5;
            left_eye_.variation1().values.offset_y = 5;
            right_eye_.variation1().animation.SetTriangle(300, 0);
            left_eye_.variation1().animation.SetTriangle(300, 0);
            right_eye_.TransitionTo(kPresetGlee);
            left_eye_.TransitionTo(kPresetGlee);
            break;

        case EyeExpression::kSilly:
            ClearVariations();
            right_eye_.variation1().values.offset_x = 3;
            left_eye_.variation1().values.offset_x = 3;
            right_eye_.variation1().animation.SetTriangle(240, 0);
            left_eye_.variation1().animation.SetTriangle(240, 0);
            right_eye_.TransitionTo(kPresetSillyWide);
            left_eye_.TransitionTo(kPresetSillySlit);
            break;

        case EyeExpression::kLaughing:
            ClearVariations();
            right_eye_.variation1().values.offset_y = 8;
            left_eye_.variation1().values.offset_y = 8;
            right_eye_.variation1().animation.SetTriangle(220, 0);
            left_eye_.variation1().animation.SetTriangle(220, 0);
            right_eye_.TransitionTo(kPresetLaughing);
            left_eye_.TransitionTo(kPresetLaughing);
            break;

        case EyeExpression::kHappy:
            ClearVariations();
            right_eye_.TransitionTo(kPresetHappy);
            left_eye_.TransitionTo(kPresetHappy);
            break;

        case EyeExpression::kDelicious:
            ClearVariations();
            right_eye_.variation1().values.offset_x = 3;
            left_eye_.variation1().values.offset_x = 3;
            right_eye_.variation1().animation.SetTriangle(1400, 0);
            left_eye_.variation1().animation.SetTriangle(1400, 0);
            right_eye_.TransitionTo(kPresetDelicious);
            left_eye_.TransitionTo(kPresetDeliciousAlt);
            break;

        case EyeExpression::kSad:
            ClearVariations();
            right_eye_.TransitionTo(kPresetSad);
            left_eye_.TransitionTo(kPresetSad);
            break;

        case EyeExpression::kCrying:
            ClearVariations();
            right_eye_.variation1().values.offset_y = 2;
            left_eye_.variation1().values.offset_y = 2;
            right_eye_.variation1().animation.SetTriangle(180, 0);
            left_eye_.variation1().animation.SetTriangle(180, 0);
            right_eye_.TransitionTo(kPresetCrying);
            left_eye_.TransitionTo(kPresetCrying);
            break;

        case EyeExpression::kWorried:
            ClearVariations();
            right_eye_.TransitionTo(kPresetWorried);
            left_eye_.TransitionTo(kPresetWorried);
            break;

        case EyeExpression::kConfused:
            ClearVariations();
            right_eye_.variation1().values.offset_x = 2;
            left_eye_.variation1().values.offset_x = 2;
            right_eye_.variation1().animation.SetTriangle(900, 0);
            left_eye_.variation1().animation.SetTriangle(900, 0);
            right_eye_.TransitionTo(kPresetWorried);
            left_eye_.TransitionTo(kPresetConfusedAlt);
            break;

        case EyeExpression::kFocused:
            ClearVariations();
            right_eye_.TransitionTo(kPresetFocused);
            left_eye_.TransitionTo(kPresetFocused);
            break;

        case EyeExpression::kAnnoyed:
            ClearVariations();
            right_eye_.TransitionTo(kPresetAnnoyed);
            left_eye_.TransitionTo(kPresetAnnoyedAlt);
            break;

        case EyeExpression::kSurprised:
            ClearVariations();
            right_eye_.TransitionTo(kPresetSurprised);
            left_eye_.TransitionTo(kPresetSurprised);
            break;

        case EyeExpression::kShocked:
            ClearVariations();
            right_eye_.variation1().values.offset_y = 3;
            left_eye_.variation1().values.offset_y = 3;
            right_eye_.variation1().animation.SetTriangle(150, 0);
            left_eye_.variation1().animation.SetTriangle(150, 0);
            right_eye_.TransitionTo(kPresetShocked);
            left_eye_.TransitionTo(kPresetShocked);
            break;

        case EyeExpression::kSkeptic:
            ClearVariations();
            right_eye_.TransitionTo(kPresetSkeptic);
            left_eye_.TransitionTo(kPresetSkepticAlt);
            break;

        case EyeExpression::kFrustrated:
            ClearVariations();
            right_eye_.TransitionTo(kPresetFrustrated);
            left_eye_.TransitionTo(kPresetFrustrated);
            break;

        case EyeExpression::kUnimpressed:
            ClearVariations();
            right_eye_.TransitionTo(kPresetUnimpressed);
            left_eye_.TransitionTo(kPresetUnimpressedAlt);
            break;

        case EyeExpression::kConfident:
            ClearVariations();
            right_eye_.variation1().values.offset_y = 2;
            left_eye_.variation1().values.offset_y = 2;
            right_eye_.variation1().animation.SetTriangle(1200, 0);
            left_eye_.variation1().animation.SetTriangle(1200, 0);
            right_eye_.TransitionTo(kPresetConfident);
            left_eye_.TransitionTo(kPresetConfident);
            break;

        case EyeExpression::kSleepy:
            ClearVariations();
            LookAt(0.0f, 0.0f);
            right_eye_.TransitionTo(kPresetSleepy);
            left_eye_.TransitionTo(kPresetSleepy);
            break;

        case EyeExpression::kSuspicious:
            ClearVariations();
            right_eye_.TransitionTo(kPresetSuspicious);
            left_eye_.TransitionTo(kPresetSuspiciousAlt);
            break;

        case EyeExpression::kSquint:
            ClearVariations();
            right_eye_.variation1().values.height = 3;
            right_eye_.variation1().animation.SetTriangle(1000, 0);
            right_eye_.TransitionTo(kPresetNormal);
            left_eye_.TransitionTo(kPresetWink);
            break;

        case EyeExpression::kFurious:
            ClearVariations();
            right_eye_.TransitionTo(kPresetFurious);
            left_eye_.TransitionTo(kPresetFurious);
            break;

        case EyeExpression::kScared:
            ClearVariations();
            right_eye_.TransitionTo(kPresetScared);
            left_eye_.TransitionTo(kPresetScared);
            break;

        case EyeExpression::kAwe:
            ClearVariations();
            right_eye_.TransitionTo(kPresetAwe);
            left_eye_.TransitionTo(kPresetAwe);
            break;

        case EyeExpression::kHeart:
            ClearVariations();
            right_eye_.SetHeart(true);
            left_eye_.SetHeart(true);
            right_eye_.TransitionTo(kPresetHeart);
            left_eye_.TransitionTo(kPresetHeart);
            right_eye_.variation1().values.width = 8;
            right_eye_.variation1().values.height = 8;
            left_eye_.variation1().values.width = 8;
            left_eye_.variation1().values.height = 8;
            right_eye_.variation1().animation.SetInterval(0, 120, 60, 260, 260);
            left_eye_.variation1().animation.SetInterval(0, 120, 60, 260, 260);
            break;

        case EyeExpression::kKiss:
            ClearVariations();
            left_eye_.SetHeart(true);
            left_eye_.TransitionTo(kPresetHeart);
            left_eye_.variation1().values.width = 2;
            left_eye_.variation1().values.height = 2;
            left_eye_.variation1().animation.SetTriangle(700, 0);
            right_eye_.TransitionTo(kPresetGlee);
            break;

        default:
            break;
    }
}

void EyeFace::Draw(MonoCanvas& canvas) {
    canvas.Clear();

    const bool sleeping = (expression_ == EyeExpression::kSleepy);
    const int32_t breath = sleeping ? BreathOffset() : 0;

    left_eye_.SetCenter(center_x_ - eye_size_ / 2 - eye_inter_distance_, center_y_ + breath);
    right_eye_.SetCenter(center_x_ + eye_size_ / 2 + eye_inter_distance_, center_y_ + breath);

    left_eye_.Draw(canvas);
    right_eye_.Draw(canvas);

    if (sleeping) {
        DrawSleepMarks(canvas);
    }
}

void EyeFace::SetLook(float look_x, float look_y, bool hold) {
    if (!hold) {
        if (look_held_) {
            look_held_ = false;
            look_timer_.Restart();
        }
        return;
    }

    if (look_held_ && look_x == held_look_x_ && look_y == held_look_y_) {
        return;
    }
    look_held_ = true;
    held_look_x_ = look_x;
    held_look_y_ = look_y;
    LookTowards(look_x, look_y);
}

void EyeFace::Update(MonoCanvas& canvas) {
    const bool sleeping = (expression_ == EyeExpression::kSleepy);

    if (random_blink_ && !sleeping && blink_timer_.Update()) {
        left_eye_.blink().Restart();
        right_eye_.blink().Restart();
    }
    if (!look_held_ && random_look_ && !sleeping && look_timer_.Update()) {
        LookAt(RandomLookOffset(), RandomLookOffset());
    }

    Draw(canvas);
}

}
