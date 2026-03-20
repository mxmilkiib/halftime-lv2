// HalftimeUI.cpp — NanoVG-based UI for Halftime DPF plugin.
// Dark theme, grouped controls, custom knob/toggle/selector widgets.

#include "DistrhoUI.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

// Parameter indices (must match HalftimeDPF.cpp)
enum ParamId : uint32_t {
    kSpeed = 0, kGrainMs, kWet, kPitchSemi, kStutterDiv,
    kFreeze, kTransLock, kSensitivity, kWinShape, kBpmDiv,
    kFormant, kSpecFreeze, kSpecLatch, kSubGain, kSubMode,
    kMorphIn, kMorphOut, kMorphBeats, kPhaseLock, kLookahead,
    kReverse, kGrainRandom, kStereoWidth, kSpectralTilt, kPhaseRandom,
    kParamCount
};

// ── Colour palette ──────────────────────────────────────────────────────────

namespace palette {
    using DGL_NAMESPACE::Color;
    static const Color bg        (0x12, 0x12, 0x1a);
    static const Color surface   (0x1c, 0x1c, 0x28);
    static const Color raised    (0x26, 0x26, 0x36);
    static const Color border    (0x3a, 0x3a, 0x50);
    static const Color accent    (0xe9, 0x45, 0x60);
    static const Color accentDim (0x8a, 0x2a, 0x3a);
    static const Color knobTrack (0x33, 0x33, 0x48);
    static const Color textBright(0xea, 0xea, 0xea);
    static const Color textDim   (0x88, 0x99, 0xaa);
    static const Color toggleOn  (0x4e, 0xc9, 0xb0);
    static const Color toggleOff (0x44, 0x44, 0x5a);
    static const Color sectionHdr(0x66, 0x77, 0x88);
}

// ── Layout constants ────────────────────────────────────────────────────────

static constexpr float UI_W       = 860.f;
static constexpr float UI_H       = 540.f;
static constexpr float HEADER_H   = 44.f;
static constexpr float PAD        = 12.f;
static constexpr float KNOB_R     = 22.f;   // knob radius
static constexpr float KNOB_BIG_R = 32.f;   // primary knobs
static constexpr float TOGGLE_W   = 36.f;
static constexpr float TOGGLE_H   = 18.f;
static constexpr float ROW_H      = 68.f;   // row height for knob + label
static constexpr float COL_W      = 170.f;
static constexpr float SECT_PAD   = 8.f;

// ── Hit-test helper ─────────────────────────────────────────────────────────

struct KnobArea {
    float cx, cy, r;
    uint32_t param;
    bool big;
};

struct ToggleArea {
    float x, y, w, h;
    uint32_t param;
};

// ── UI class ────────────────────────────────────────────────────────────────

class HalftimeUI : public UI {
public:
    HalftimeUI()
        : UI(UI_W, UI_H),
          fDragging(-1),
          fDragStartY(0.f),
          fDragStartVal(0.f)
    {
        std::memset(fValues, 0, sizeof(fValues));
        // defaults matching plugin
        fValues[kSpeed]        = 50.f;
        fValues[kGrainMs]      = 80.f;
        fValues[kWet]          = 1.f;
        fValues[kFormant]      = 1.f;
        fValues[kSensitivity]  = 1.f;
        fValues[kMorphBeats]   = 2.f;
        fValues[kPhaseLock]    = 1.f;
        fValues[kLookahead]    = 1.f;
        fValues[kStereoWidth]  = 1.f;
        fValues[kStutterDiv]   = 1.f;

        buildLayout();
    }

protected:
    // ── DPF callbacks ───────────────────────────────────────────────────────

    void parameterChanged(uint32_t index, float value) override {
        if (index < kParamCount) {
            fValues[index] = value;
            repaint();
        }
    }

    // ── Drawing ─────────────────────────────────────────────────────────────

    void onNanoDisplay() override {
        const float w = getWidth();
        const float h = getHeight();

        // background
        beginPath();
        rect(0, 0, w, h);
        fillColor(palette::bg);
        fill();
        closePath();

        drawHeader(w);
        drawSections(w, h);
        drawKnobs();
        drawToggles();
    }

    // ── Mouse interaction ───────────────────────────────────────────────────

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;

        if (ev.press) {
            // check toggles first
            for (int i = 0; i < fNumToggles; ++i) {
                const auto& t = fToggles[i];
                if (ev.pos.getX() >= t.x && ev.pos.getX() <= t.x + t.w &&
                    ev.pos.getY() >= t.y && ev.pos.getY() <= t.y + t.h) {
                    float nv = fValues[t.param] > 0.5f ? 0.f : 1.f;
                    fValues[t.param] = nv;
                    setParameterValue(t.param, nv);
                    repaint();
                    return true;
                }
            }
            // check knobs
            for (int i = 0; i < fNumKnobs; ++i) {
                const auto& k = fKnobs[i];
                float dx = ev.pos.getX() - k.cx;
                float dy = ev.pos.getY() - k.cy;
                if (dx*dx + dy*dy <= (k.r + 8.f) * (k.r + 8.f)) {
                    fDragging = i;
                    fDragStartY = ev.pos.getY();
                    fDragStartVal = fValues[k.param];
                    return true;
                }
            }
        } else {
            if (fDragging >= 0) {
                fDragging = -1;
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override {
        if (fDragging < 0) return false;

        const auto& k = fKnobs[fDragging];
        float sensitivity = k.big ? 300.f : 200.f;

        // get parameter range
        float mn, mx;
        getParamRange(k.param, mn, mx);

        float delta = (fDragStartY - ev.pos.getY()) / sensitivity;
        float nv = fDragStartVal + delta * (mx - mn);
        nv = std::max(mn, std::min(mx, nv));

        // snap integers
        if (isIntegerParam(k.param))
            nv = std::round(nv);

        if (nv != fValues[k.param]) {
            fValues[k.param] = nv;
            setParameterValue(k.param, nv);
            repaint();
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        for (int i = 0; i < fNumKnobs; ++i) {
            const auto& k = fKnobs[i];
            float dx = ev.pos.getX() - k.cx;
            float dy = ev.pos.getY() - k.cy;
            if (dx*dx + dy*dy <= (k.r + 8.f) * (k.r + 8.f)) {
                float mn, mx;
                getParamRange(k.param, mn, mx);
                float step = (mx - mn) * 0.02f;
                if (isIntegerParam(k.param)) step = 1.f;
                float nv = fValues[k.param] + ev.delta.getY() * step;
                nv = std::max(mn, std::min(mx, nv));
                if (isIntegerParam(k.param)) nv = std::round(nv);
                fValues[k.param] = nv;
                setParameterValue(k.param, nv);
                repaint();
                return true;
            }
        }
        return false;
    }

private:
    float fValues[kParamCount];
    int   fDragging;
    float fDragStartY;
    float fDragStartVal;

    static constexpr int MAX_KNOBS   = 32;
    static constexpr int MAX_TOGGLES = 16;
    KnobArea    fKnobs[MAX_KNOBS];
    int         fNumKnobs = 0;
    ToggleArea  fToggles[MAX_TOGGLES];
    int         fNumToggles = 0;

    // ── Layout building ─────────────────────────────────────────────────────

    void addKnob(float cx, float cy, float r, uint32_t param, bool big = false) {
        if (fNumKnobs < MAX_KNOBS)
            fKnobs[fNumKnobs++] = {cx, cy, r, param, big};
    }

    void addToggle(float x, float y, uint32_t param) {
        if (fNumToggles < MAX_TOGGLES)
            fToggles[fNumToggles++] = {x, y, TOGGLE_W, TOGGLE_H, param};
    }

    void buildLayout() {
        fNumKnobs = 0;
        fNumToggles = 0;

        // Section positions
        const float col1_x = PAD + 20.f;
        const float col2_x = PAD + COL_W + 20.f;
        const float col3_x = PAD + 2.f * COL_W + 20.f;
        const float col4_x = PAD + 3.f * COL_W + 20.f;
        const float col5_x = PAD + 4.f * COL_W + 20.f;
        const float topY   = HEADER_H + PAD + 16.f;

        // ── Column 1: CORE ──
        float y = topY + 16.f;
        addKnob(col1_x + 40.f, y + 10.f,  KNOB_BIG_R, kSpeed, true);     // Speed (big)
        y += ROW_H + 24.f;
        addKnob(col1_x + 40.f, y + 10.f,  KNOB_BIG_R, kWet, true);       // Wet (big)
        y += ROW_H + 24.f;
        addKnob(col1_x + 40.f, y + 10.f,  KNOB_R, kGrainMs);             // Grain
        y += ROW_H + 12.f;
        addToggle(col1_x + 22.f, y + 10.f, kFreeze);                      // Freeze

        // ── Column 2: PITCH & SPECTRAL ──
        y = topY + 16.f;
        addKnob(col2_x + 40.f, y + 10.f,  KNOB_R, kPitchSemi);           // Pitch
        y += ROW_H;
        addKnob(col2_x + 40.f, y + 10.f,  KNOB_R, kFormant);             // Formant
        y += ROW_H;
        addToggle(col2_x + 22.f, y + 6.f, kPhaseLock);                    // Phase Lock
        y += 36.f;
        addKnob(col2_x + 40.f, y + 10.f,  KNOB_R, kSpecFreeze);          // Spectral Freeze
        y += ROW_H;
        addKnob(col2_x + 40.f, y + 10.f,  KNOB_R, kSpectralTilt);        // Tilt
        y += ROW_H;
        addKnob(col2_x + 40.f, y + 10.f,  KNOB_R, kPhaseRandom);         // Phase Random

        // ── Column 3: GRAIN ──
        y = topY + 16.f;
        addKnob(col3_x + 40.f, y + 10.f,  KNOB_R, kWinShape);            // Window (integer 0-2)
        y += ROW_H;
        addToggle(col3_x + 22.f, y + 6.f, kReverse);                      // Reverse
        y += 36.f;
        addKnob(col3_x + 40.f, y + 10.f,  KNOB_R, kGrainRandom);         // Grain Random
        y += ROW_H;
        addToggle(col3_x + 22.f, y + 6.f, kTransLock);                    // Transient Lock
        y += 36.f;
        addKnob(col3_x + 40.f, y + 10.f,  KNOB_R, kSensitivity);         // Sensitivity
        y += ROW_H;
        addToggle(col3_x + 22.f, y + 6.f, kLookahead);                    // Lookahead

        // ── Column 4: MODULATION ──
        y = topY + 16.f;
        addKnob(col4_x + 40.f, y + 10.f,  KNOB_R, kStutterDiv);          // Stutter
        y += ROW_H;
        addKnob(col4_x + 40.f, y + 10.f,  KNOB_R, kBpmDiv);              // BPM Div
        y += ROW_H;
        addToggle(col4_x + 22.f, y + 6.f, kMorphIn);                      // Morph In
        y += 36.f;
        addToggle(col4_x + 22.f, y + 6.f, kMorphOut);                     // Morph Out
        y += 36.f;
        addKnob(col4_x + 40.f, y + 10.f,  KNOB_R, kMorphBeats);          // Morph Beats
        y += ROW_H;
        addToggle(col4_x + 22.f, y + 6.f, kSpecLatch);                    // Spec Latch

        // ── Column 5: OUTPUT ──
        y = topY + 16.f;
        addKnob(col5_x + 40.f, y + 10.f,  KNOB_R, kStereoWidth);         // Stereo Width
        y += ROW_H;
        addKnob(col5_x + 40.f, y + 10.f,  KNOB_R, kSubGain);             // Sub Gain
        y += ROW_H;
        addToggle(col5_x + 22.f, y + 6.f, kSubMode);                      // Sub Mode
    }

    // ── Section drawing ─────────────────────────────────────────────────────

    void drawHeader(float w) {
        // header bar
        beginPath();
        rect(0, 0, w, HEADER_H);
        fillColor(palette::surface);
        fill();
        closePath();

        // bottom border
        beginPath();
        moveTo(0, HEADER_H);
        lineTo(w, HEADER_H);
        strokeColor(palette::border);
        strokeWidth(1.f);
        stroke();
        closePath();

        // title
        fontSize(22.f);
        fontFace("sans-bold");
        fillColor(palette::accent);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(PAD + 4.f, HEADER_H * 0.5f, "HALFTIME", nullptr);

        // subtitle
        fontSize(11.f);
        fontFace("sans");
        fillColor(palette::textDim);
        text(PAD + 140.f, HEADER_H * 0.5f, "WSOLA time-stretch / phase vocoder", nullptr);
    }

    void drawSections(float w, float /*h*/) {
        const float topY = HEADER_H + PAD;
        const char* titles[] = {"CORE", "PITCH / SPECTRAL", "GRAIN", "MODULATION", "OUTPUT"};
        float xs[] = {
            PAD, PAD + COL_W, PAD + 2.f * COL_W,
            PAD + 3.f * COL_W, PAD + 4.f * COL_W
        };

        for (int i = 0; i < 5; ++i) {
            float sx = xs[i];
            float sw = COL_W - 4.f;
            float sy = topY;
            float sh = UI_H - topY - PAD;

            // section bg
            beginPath();
            roundedRect(sx, sy, sw, sh, 6.f);
            fillColor(palette::surface);
            fill();
            closePath();

            // section border
            beginPath();
            roundedRect(sx, sy, sw, sh, 6.f);
            strokeColor(palette::border);
            strokeWidth(1.f);
            stroke();
            closePath();

            // section title
            fontSize(10.f);
            fontFace("sans-bold");
            fillColor(palette::sectionHdr);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(sx + SECT_PAD, sy + 4.f, titles[i], nullptr);
        }
    }

    void drawKnobs() {
        for (int i = 0; i < fNumKnobs; ++i) {
            const auto& k = fKnobs[i];
            float mn, mx;
            getParamRange(k.param, mn, mx);
            float norm = (mx > mn) ? (fValues[k.param] - mn) / (mx - mn) : 0.f;

            drawSingleKnob(k.cx, k.cy, k.r, norm, k.param, i == fDragging);
        }
    }

    void drawSingleKnob(float cx, float cy, float r, float norm,
                         uint32_t param, bool active) {
        // arc angles: 135 deg to 405 deg (from bottom-left, 270 deg sweep)
        const float startAngle = 0.75f * M_PI;    // 135 deg
        const float sweepAngle = 1.5f  * M_PI;    // 270 deg
        const float endAngle   = startAngle + sweepAngle;
        const float valueAngle = startAngle + norm * sweepAngle;

        // track
        beginPath();
        arc(cx, cy, r, startAngle, endAngle, NanoVG::Winding::CW);
        strokeColor(palette::knobTrack);
        strokeWidth(r > 26.f ? 5.f : 3.5f);
        lineCap(NanoVG::LineCap::ROUND);
        stroke();
        closePath();

        // value arc
        if (norm > 0.005f) {
            beginPath();
            arc(cx, cy, r, startAngle, valueAngle, NanoVG::Winding::CW);
            strokeColor(active ? palette::accent : palette::accentDim);
            strokeWidth(r > 26.f ? 5.f : 3.5f);
            lineCap(NanoVG::LineCap::ROUND);
            stroke();
            closePath();
        }

        // dot at value position
        float dx = cx + r * std::cos(valueAngle);
        float dy = cy + r * std::sin(valueAngle);
        beginPath();
        circle(dx, dy, r > 26.f ? 4.f : 3.f);
        fillColor(active ? palette::accent : palette::textBright);
        fill();
        closePath();

        // value text
        char buf[32];
        formatValue(buf, sizeof(buf), param, fValues[param]);
        fontSize(10.f);
        fontFace("sans");
        fillColor(palette::textBright);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, cy, buf, nullptr);

        // label below
        const char* label = paramLabel(param);
        fontSize(9.f);
        fillColor(palette::textDim);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(cx, cy + r + 8.f, label, nullptr);
    }

    void drawToggles() {
        for (int i = 0; i < fNumToggles; ++i) {
            const auto& t = fToggles[i];
            bool on = fValues[t.param] > 0.5f;

            // pill background
            float rad = t.h * 0.5f;
            beginPath();
            roundedRect(t.x, t.y, t.w, t.h, rad);
            fillColor(on ? palette::toggleOn : palette::toggleOff);
            fill();
            closePath();

            // thumb circle
            float thumbX = on ? (t.x + t.w - rad) : (t.x + rad);
            float thumbY = t.y + rad;
            beginPath();
            circle(thumbX, thumbY, rad - 2.f);
            fillColor(palette::textBright);
            fill();
            closePath();

            // label
            fontSize(9.f);
            fontFace("sans");
            fillColor(palette::textDim);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(t.x + t.w + 6.f, t.y + rad, paramLabel(t.param), nullptr);
        }
    }

    // ── Param helpers ───────────────────────────────────────────────────────

    static void getParamRange(uint32_t p, float& mn, float& mx) {
        switch (p) {
        case kSpeed:        mn = 25.f;  mx = 100.f; break;
        case kGrainMs:      mn = 20.f;  mx = 500.f; break;
        case kWet:          mn = 0.f;   mx = 1.f;   break;
        case kPitchSemi:    mn = -12.f; mx = 12.f;  break;
        case kStutterDiv:   mn = 1.f;   mx = 16.f;  break;
        case kSensitivity:  mn = 0.5f;  mx = 2.f;   break;
        case kWinShape:     mn = 0.f;   mx = 2.f;   break;
        case kBpmDiv:       mn = 0.f;   mx = 8.f;   break;
        case kFormant:      mn = 0.f;   mx = 1.f;   break;
        case kSpecFreeze:   mn = 0.f;   mx = 1.f;   break;
        case kSubGain:      mn = 0.f;   mx = 1.f;   break;
        case kMorphBeats:   mn = 0.5f;  mx = 8.f;   break;
        case kGrainRandom:  mn = 0.f;   mx = 1.f;   break;
        case kStereoWidth:  mn = 0.f;   mx = 2.f;   break;
        case kSpectralTilt: mn = -1.f;  mx = 1.f;   break;
        case kPhaseRandom:  mn = 0.f;   mx = 1.f;   break;
        default:            mn = 0.f;   mx = 1.f;   break;
        }
    }

    static bool isIntegerParam(uint32_t p) {
        return p == kStutterDiv || p == kWinShape || p == kBpmDiv;
    }

    static void formatValue(char* buf, int sz, uint32_t p, float v) {
        switch (p) {
        case kSpeed:        std::snprintf(buf, sz, "%.0f%%", v);    break;
        case kGrainMs:      std::snprintf(buf, sz, "%.0fms", v);    break;
        case kWet:          std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kPitchSemi:    std::snprintf(buf, sz, "%+.1fst", v);   break;
        case kStutterDiv:   std::snprintf(buf, sz, "%.0f", v);      break;
        case kSensitivity:  std::snprintf(buf, sz, "%.1f", v);      break;
        case kFormant:      std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kSpecFreeze:   std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kSubGain:      std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kMorphBeats:   std::snprintf(buf, sz, "%.1f", v);      break;
        case kGrainRandom:  std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kStereoWidth:  std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kSpectralTilt: std::snprintf(buf, sz, "%+.0f%%", v*100); break;
        case kPhaseRandom:  std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kWinShape: {
            const char* names[] = {"Hann", "Blk", "Rect"};
            int idx = std::max(0, std::min(2, static_cast<int>(v)));
            std::snprintf(buf, sz, "%s", names[idx]);
            break;
        }
        case kBpmDiv: {
            int d = static_cast<int>(v);
            if (d == 0) std::snprintf(buf, sz, "Off");
            else        std::snprintf(buf, sz, "1/%d", d);
            break;
        }
        default: std::snprintf(buf, sz, "%.0f%%", v*100); break;
        }
    }

    static const char* paramLabel(uint32_t p) {
        switch (p) {
        case kSpeed:        return "Speed";
        case kGrainMs:      return "Grain";
        case kWet:          return "Wet";
        case kPitchSemi:    return "Pitch";
        case kStutterDiv:   return "Stutter";
        case kFreeze:       return "Freeze";
        case kTransLock:    return "Trans Lock";
        case kSensitivity:  return "Sensitivity";
        case kWinShape:     return "Window";
        case kBpmDiv:       return "BPM Div";
        case kFormant:      return "Formant";
        case kSpecFreeze:   return "Spec Freeze";
        case kSpecLatch:    return "Spec Latch";
        case kSubGain:      return "Sub Bass";
        case kSubMode:      return "Sub Mode";
        case kMorphIn:      return "Morph In";
        case kMorphOut:     return "Morph Out";
        case kMorphBeats:   return "Morph Beats";
        case kPhaseLock:    return "Phase Lock";
        case kLookahead:    return "Lookahead";
        case kReverse:      return "Reverse";
        case kGrainRandom:  return "Random";
        case kStereoWidth:  return "Width";
        case kSpectralTilt: return "Tilt";
        case kPhaseRandom:  return "Phase Rnd";
        default:            return "?";
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HalftimeUI)
};

UI* createUI() {
    return new HalftimeUI();
}

END_NAMESPACE_DISTRHO
