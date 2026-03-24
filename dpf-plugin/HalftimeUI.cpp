// HalftimeUI.cpp — NanoVG-based UI for Halftime DPF plugin.
// Dark theme, grouped controls, custom knob/toggle/selector widgets.

#include "DistrhoUI.hpp"
#include "HalftimePresets.hpp"
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
    kInputGain, kOutputGain, kEqLow, kEqMid, kEqHigh,
    kEqOutLow, kEqOutMid, kEqOutHigh, kManualBpm,
    kSubFreq, kSubDrive, kSmooth,
    kNumWritable,
    kBpmDisplay = kNumWritable,
    kPeakIn,
    kPeakOut,
    kParamCount
};

// ── Colour palette ──────────────────────────────────────────────────────────

namespace palette {
    using DGL_NAMESPACE::Color;
    static const Color bg        (0x12, 0x12, 0x1a);
    static const Color surface   (0x1c, 0x1c, 0x28);
    static const Color raised    (0x26, 0x26, 0x36);
    static const Color border    (0x3a, 0x3a, 0x50);
    static const Color accent    (0xe9, 0x45, 0x60);     // red — input/output gain
    static const Color accentDim (0x8a, 0x2a, 0x3a);
    static const Color blue      (0x4a, 0x90, 0xe2);     // blue — EQ knobs
    static const Color blueDim   (0x2a, 0x55, 0x8a);
    static const Color green     (0x4e, 0xc9, 0x7d);     // green — general knobs
    static const Color greenDim  (0x2a, 0x8a, 0x5a);
    static const Color knobTrack (0x33, 0x33, 0x48);
    static const Color textBright(0xea, 0xea, 0xea);
    static const Color textDim   (0x88, 0x99, 0xaa);
    static const Color toggleOn  (0x4e, 0xc9, 0xb0);
    static const Color toggleOff (0x44, 0x44, 0x5a);
    static const Color sectionHdr(0x66, 0x77, 0x88);
    static const Color powerOn   (0x4e, 0xc9, 0x7d);     // green power ring
    static const Color powerOff  (0x55, 0x55, 0x66);     // dim power ring
    static const Color yellow    (0xf0, 0xc0, 0x40);     // speed mode active
    static const Color yellowDim (0x80, 0x70, 0x30);     // speed mode inactive
}

// ── Layout constants ────────────────────────────────────────────────────────

static constexpr float UI_W       = 1400.f;
static constexpr float UI_H       = 680.f;
static constexpr float HEADER_H   = 60.f;
static constexpr float PAD        = 12.f;
static constexpr float KNOB_R     = 28.f;   // knob radius
static constexpr float KNOB_BIG_R = 38.f;   // primary knobs
static constexpr float TOGGLE_W   = 44.f;
static constexpr float TOGGLE_H   = 22.f;
static constexpr float ROW_H      = 100.f;  // row height for knob + label
static constexpr float COL_GAP    = 8.f;
static constexpr int   NUM_COLS   = 8;
static constexpr float COL_W      = (UI_W - 2.f * PAD + COL_GAP) / static_cast<float>(NUM_COLS);
static constexpr float SECT_W     = COL_W - COL_GAP;
static constexpr float SECT_PAD   = 10.f;

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

struct SelectorArea {
    float x, y, w, h;
    uint32_t param;
    const float* values;
    const char* const* labels;
    int numOptions;
};

struct RadioArea {
    float x, y, w, h;
    uint32_t param;
    const char* const* labels;
    int numOptions;
};

struct TextField {
    float x, y, w, h;
    uint32_t param;
    char buffer[16];
    bool active;
};

struct ButtonArea {
    float x, y, w, h;
    int id;             // application-defined ID
    const char* label;
};


// Beat-sync selector option tables
static const float    bpmDivVals[]    = {0, 1, 2, 3, 4, 6, 8};
static const char* const bpmDivLbls[] = {"Off", "1/1", "1/2", "1/3", "1/4", "1/6", "1/8"};
static constexpr int  NUM_BPM_DIV     = 7;

static const float    stutterVals[]    = {1, 2, 3, 4, 6, 8, 12, 16};
static const char* const stutterLbls[] = {"1", "2", "3", "4", "6", "8", "12", "16"};
static constexpr int  NUM_STUTTER      = 8;

static const float    morphBtVals[]    = {0.5f, 1, 2, 3, 4, 6, 8};
static const char* const morphBtLbls[] = {"1/2", "1", "2", "3", "4", "6", "8"};
static constexpr int  NUM_MORPH_BT     = 7;

static const char* const winShapeLbls[] = {"Hann", "Blackman", "Rect"};
static constexpr int  NUM_WIN_SHAPES    = 3;

// ── UI class ────────────────────────────────────────────────────────────────

class HalftimeUI : public UI {
public:
    HalftimeUI()
        : UI(UI_W, UI_H),
          fDragging(-1),
          fDragStartY(0.f),
          fDragStartVal(0.f),
          fCurrentPreset(0),
          fScale(1.f)
    {
        std::memset(fValues, 0, sizeof(fValues));
        // defaults matching plugin
        fValues[kSpeed]        = 50.f;
        fValues[kGrainMs]      = 80.f;
        fValues[kWet]          = 1.f;
        fValues[kFormant]      = 1.f;
        fValues[kSensitivity]  = 1.f;
        fValues[kMorphBeats]   = 2.f;
        fValues[kPhaseLock]    = 0.f;
        fValues[kLookahead]    = 1.f;
        fValues[kStereoWidth]  = 1.f;
        fValues[kStutterDiv]   = 1.f;
        fValues[kInputGain]    = 0.f;
        fValues[kOutputGain]   = 0.f;
        fValues[kEqLow]        = 0.f;
        fValues[kEqMid]        = 0.f;
        fValues[kEqHigh]       = 0.f;
        fValues[kEqOutLow]     = 0.f;
        fValues[kEqOutMid]     = 0.f;
        fValues[kEqOutHigh]    = 0.f;
        fValues[kManualBpm]    = 0.f;
        fValues[kSubFreq]      = 80.f;
        fValues[kSubDrive]     = 0.f;
        fValues[kSmooth]       = 0.5f;

        setGeometryConstraints(static_cast<uint>(UI_W / 2),
                               static_cast<uint>(UI_H / 2),
                               true,   // keepAspectRatio — diagonal only
                               true);  // automaticallyScale — DPF manages transform
        loadSharedResources();
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

    void programLoaded(uint32_t index) override {
        if (index >= NUM_PRESETS) return;
        fCurrentPreset = static_cast<int>(index);
        const auto& p = halftimePresets[index];
        for (uint32_t i = 0; i < kNumWritable; ++i)
            fValues[i] = p.v[i];
        repaint();
    }

    // ── Drawing ─────────────────────────────────────────────────────────────

    void onNanoDisplay() override {
        fScale = static_cast<float>(getWidth()) / UI_W;

        // DPF automaticallyScale handles the NanoVG transform for us
        save();

        // background
        beginPath();
        rect(0, 0, UI_W, UI_H);
        fillColor(palette::bg);
        fill();
        closePath();

        drawHeader(UI_W);
        drawSections(UI_W, UI_H);
        drawKnobs();
        drawToggles();
        drawSelectors();
        drawRadios();
        drawTextFields();
        drawButtons();
        
        // Draw tooltip if hovering
        if (fHoveredParam >= 0) {
            drawTooltip(fTooltipX, fTooltipY, paramTooltip(fHoveredParam));
        } else if (fHoveredButton == -2) {
            drawTooltip(fTooltipX, fTooltipY, fPowered ? "Bypass effect" : "Enable effect");
        } else if (fHoveredButton >= 0 && fHoveredButton < fNumButtons) {
            const auto& b = fButtons[fHoveredButton];
            const char* tip = "Speed preset";
            if (b.id == BTN_SPEED_2X)  tip = "Half speed (2x slowdown)";
            if (b.id == BTN_SPEED_15X) tip = "1.5x slowdown (triplet feel)";
            if (b.id == BTN_SPEED_4X)  tip = "Quarter speed (4x slowdown)";
            drawTooltip(fTooltipX, fTooltipY, tip);
        }

        restore();
    }

    // ── Mouse interaction ───────────────────────────────────────────────────

    bool onMouse(const MouseEvent& ev) override {
        // virtual coords (scaled)
        const float mx = ev.pos.getX() / fScale;
        const float my = ev.pos.getY() / fScale;

        // right-click: reset to default
        if (ev.button == 2 && ev.press) {
            for (int i = 0; i < fNumKnobs; ++i) {
                const auto& k = fKnobs[i];
                float dx = mx - k.cx, dy = my - k.cy;
                if (dx*dx + dy*dy <= (k.r + 8.f) * (k.r + 8.f)) {
                    resetParam(k.param); return true;
                }
            }
            for (int i = 0; i < fNumToggles; ++i) {
                const auto& t = fToggles[i];
                if (mx >= t.x && mx <= t.x + t.w && my >= t.y && my <= t.y + t.h) {
                    resetParam(t.param); return true;
                }
            }
            for (int i = 0; i < fNumSelectors; ++i) {
                const auto& s = fSelectors[i];
                if (mx >= s.x && mx <= s.x + s.w && my >= s.y && my <= s.y + s.h) {
                    resetParam(s.param); return true;
                }
            }
            for (int i = 0; i < fNumRadios; ++i) {
                const auto& r = fRadios[i];
                if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
                    resetParam(r.param); return true;
                }
            }
            return false;
        }

        if (ev.button != 1) return false;

        if (ev.press) {
            // Power button hit-test (circle in header)
            {
                const float pcx = PAD + 24.f;
                const float pcy = HEADER_H * 0.5f;
                const float pr  = 22.f; // slightly larger than visual for easier clicking
                float dx = mx - pcx, dy = my - pcy;
                if (dx*dx + dy*dy <= pr * pr) {
                    fPowered = !fPowered;
                    if (fPowered) {
                        // Restore saved wet value
                        fValues[kWet] = fSavedWet;
                    } else {
                        // Save current wet and set to 0
                        fSavedWet = fValues[kWet];
                        fValues[kWet] = 0.f;
                    }
                    setParameterValue(kWet, fValues[kWet]);
                    repaint();
                    return true;
                }
            }

            // Speed mode buttons
            for (int i = 0; i < fNumButtons; ++i) {
                const auto& b = fButtons[i];
                if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
                    float newSpeed = 50.f;
                    if (b.id == BTN_SPEED_2X)  newSpeed = 50.f;
                    if (b.id == BTN_SPEED_15X) newSpeed = 67.f;
                    if (b.id == BTN_SPEED_4X)  newSpeed = 25.f;
                    fValues[kSpeed] = newSpeed;
                    setParameterValue(kSpeed, newSpeed);
                    repaint();
                    return true;
                }
            }

            // preset selector (right side of header)
            const float preW = 140.f;
            const float preH = 24.f;
            const float preX = UI_W - PAD - preW;
            const float preY = (HEADER_H - preH) * 0.5f;
            if (mx >= preX && mx <= preX + preW && my >= preY && my <= preY + preH) {
                if (mx < preX + preW * 0.5f)
                    fCurrentPreset = (fCurrentPreset + NUM_PRESETS - 1) % NUM_PRESETS;
                else
                    fCurrentPreset = (fCurrentPreset + 1) % NUM_PRESETS;
                const auto& p = halftimePresets[fCurrentPreset];
                for (uint32_t i = 0; i < kNumWritable; ++i) {
                    fValues[i] = p.v[i];
                    setParameterValue(i, p.v[i]);
                }
                repaint();
                return true;
            }
            // preset reset button (right of preset pill)
            const float rstW = 26.f;
            const float rstX = UI_W - PAD - 150.f - rstW - 6.f;
            const float rstY = (HEADER_H - 26.f) * 0.5f;
            if (mx >= rstX && mx <= rstX + rstW && my >= rstY && my <= rstY + 26.f) {
                const auto& p = halftimePresets[fCurrentPreset];
                for (uint32_t pi = 0; pi < kNumWritable; ++pi) {
                    fValues[pi] = p.v[pi];
                    setParameterValue(pi, p.v[pi]);
                }
                repaint();
                return true;
            }
            // text fields
            for (int i = 0; i < fNumTextFields; ++i) {
                const auto& tf = fTextFields[i];
                if (mx >= tf.x && mx <= tf.x + tf.w && my >= tf.y && my <= tf.y + tf.h) {
                    // For now, just cycle through some values on click
                    float val = fValues[tf.param];
                    if (val < 1.f) fValues[tf.param] = 120.f;
                    else if (val < 140.f) fValues[tf.param] = 140.f;
                    else if (val < 180.f) fValues[tf.param] = 180.f;
                    else fValues[tf.param] = 0.f;
                    setParameterValue(tf.param, fValues[tf.param]);
                    repaint();
                    return true;
                }
            }
            // radios
            for (int i = 0; i < fNumRadios; ++i) {
                const auto& r = fRadios[i];
                if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
                    const float btnH = r.h / static_cast<float>(r.numOptions);
                    int clicked = static_cast<int>((my - r.y) / btnH);
                    clicked = std::max(0, std::min(r.numOptions - 1, clicked));
                    fValues[r.param] = static_cast<float>(clicked);
                    setParameterValue(r.param, static_cast<float>(clicked));
                    repaint();
                    return true;
                }
            }
            // selectors
            for (int i = 0; i < fNumSelectors; ++i) {
                const auto& s = fSelectors[i];
                if (mx >= s.x && mx <= s.x + s.w && my >= s.y && my <= s.y + s.h) {
                    int cur = 0;
                    float bestDist = 1e9f;
                    for (int j = 0; j < s.numOptions; ++j) {
                        float d = std::abs(fValues[s.param] - s.values[j]);
                        if (d < bestDist) { bestDist = d; cur = j; }
                    }
                    if (mx < s.x + s.w * 0.5f)
                        cur = (cur + s.numOptions - 1) % s.numOptions;
                    else
                        cur = (cur + 1) % s.numOptions;
                    fValues[s.param] = s.values[cur];
                    setParameterValue(s.param, s.values[cur]);
                    repaint();
                    return true;
                }
            }
            // toggles
            for (int i = 0; i < fNumToggles; ++i) {
                const auto& t = fToggles[i];
                if (mx >= t.x && mx <= t.x + t.w && my >= t.y && my <= t.y + t.h) {
                    float nv = fValues[t.param] > 0.5f ? 0.f : 1.f;
                    fValues[t.param] = nv;
                    setParameterValue(t.param, nv);
                    repaint();
                    return true;
                }
            }
            // knobs
            for (int i = 0; i < fNumKnobs; ++i) {
                const auto& k = fKnobs[i];
                float dx = mx - k.cx, dy = my - k.cy;
                if (dx*dx + dy*dy <= (k.r + 8.f) * (k.r + 8.f)) {
                    fDragging = i;
                    fDragStartY = my;
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
        const float mx = ev.pos.getX() / fScale;
        const float my = ev.pos.getY() / fScale;
        
        // Handle dragging
        if (fDragging >= 0) {
            const auto& k = fKnobs[fDragging];
            float sensitivity = k.big ? 300.f : 200.f;

            float mn, mx_range;
            getParamRange(k.param, mn, mx_range);

            float delta = (fDragStartY - my) / sensitivity;
            float nv = fDragStartVal + delta * (mx_range - mn);
            nv = std::max(mn, std::min(mx_range, nv));

            if (isIntegerParam(k.param))
                nv = std::round(nv);

            if (nv != fValues[k.param]) {
                fValues[k.param] = nv;
                setParameterValue(k.param, nv);
                repaint();
            }
            return true;
        }
        
        // Handle hover for tooltips
        fHoveredParam = -1;
        
        // Check knobs
        for (int i = 0; i < fNumKnobs; ++i) {
            const auto& k = fKnobs[i];
            float dx = mx - k.cx, dy = my - k.cy;
            if (dx*dx + dy*dy <= (k.r + 8.f) * (k.r + 8.f)) {
                fHoveredParam = k.param;
                fTooltipX = mx;
                fTooltipY = my;
                repaint();
                return true;
            }
        }
        
        // Check toggles
        for (int i = 0; i < fNumToggles; ++i) {
            const auto& t = fToggles[i];
            if (mx >= t.x && mx <= t.x + t.w && my >= t.y && my <= t.y + t.h) {
                fHoveredParam = t.param;
                fTooltipX = mx;
                fTooltipY = my;
                repaint();
                return true;
            }
        }

        // Check buttons (speed modes)
        fHoveredButton = -1;
        for (int i = 0; i < fNumButtons; ++i) {
            const auto& b = fButtons[i];
            if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
                fHoveredButton = i;
                fTooltipX = mx;
                fTooltipY = my;
                repaint();
                return true;
            }
        }

        // Check power button
        {
            const float pcx = PAD + 24.f;
            const float pcy = HEADER_H * 0.5f;
            float dx = mx - pcx, dy = my - pcy;
            if (dx*dx + dy*dy <= 22.f * 22.f) {
                fHoveredButton = -2;
                fTooltipX = mx;
                fTooltipY = my;
                repaint();
                return true;
            }
        }
        
        repaint();
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override {
        const float mx = ev.pos.getX() / fScale;
        const float my = ev.pos.getY() / fScale;

        for (int i = 0; i < fNumKnobs; ++i) {
            const auto& k = fKnobs[i];
            float dx = mx - k.cx, dy = my - k.cy;
            if (dx*dx + dy*dy <= (k.r + 12.f) * (k.r + 12.f)) {
                float mn, mxr;
                getParamRange(k.param, mn, mxr);
                float step = (mxr - mn) * (k.big ? 0.01f : 0.015f);
                float nv = fValues[k.param] + ev.delta.getY() * step;
                nv = std::max(mn, std::min(mxr, nv));
                if (isIntegerParam(k.param)) nv = std::round(nv);
                if (nv != fValues[k.param]) {
                    fValues[k.param] = nv;
                    setParameterValue(k.param, nv);
                    repaint();
                }
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

    static constexpr int MAX_KNOBS     = 48;
    static constexpr int MAX_TOGGLES   = 20;
    static constexpr int MAX_SELECTORS = 8;
    static constexpr int MAX_RADIOS    = 4;
    static constexpr int MAX_TEXTFIELDS = 2;
    static constexpr int MAX_BUTTONS   = 8;
    KnobArea      fKnobs[MAX_KNOBS];
    int           fNumKnobs = 0;
    ToggleArea    fToggles[MAX_TOGGLES];
    int           fNumToggles = 0;
    SelectorArea  fSelectors[MAX_SELECTORS];
    int           fNumSelectors = 0;
    RadioArea     fRadios[MAX_RADIOS];
    int           fNumRadios = 0;
    TextField     fTextFields[MAX_TEXTFIELDS];
    int           fNumTextFields = 0;
    int           fCurrentPreset;
    float         fScale;
    int           fHoveredParam = -1;
    int           fHoveredButton = -1;  // -1 = none, -2 = power button, >=0 = button index
    float         fTooltipX = 0.f, fTooltipY = 0.f;

    // Variable column layout
    float         fColX[NUM_COLS];     // left edge of each section
    float         fColW[NUM_COLS];     // width of each section

    // Power button state
    bool          fPowered = true;
    float         fSavedWet = 1.f;     // wet value to restore when powering on

    // Speed mode + generic buttons
    ButtonArea    fButtons[MAX_BUTTONS];
    int           fNumButtons = 0;
    static constexpr int BTN_SPEED_2X  = 0;
    static constexpr int BTN_SPEED_15X = 1;
    static constexpr int BTN_SPEED_4X  = 2;

    // ── Layout building ─────────────────────────────────────────────────────

    void addKnob(float cx, float cy, float r, uint32_t param, bool big = false) {
        if (fNumKnobs < MAX_KNOBS)
            fKnobs[fNumKnobs++] = {cx, cy, r, param, big};
    }

    void addToggle(float x, float y, uint32_t param) {
        if (fNumToggles < MAX_TOGGLES)
            fToggles[fNumToggles++] = {x, y, TOGGLE_W, TOGGLE_H, param};
    }

    void addSelector(float x, float y, float w, float h, uint32_t param,
                     const float* vals, const char* const* lbls, int n) {
        if (fNumSelectors < MAX_SELECTORS)
            fSelectors[fNumSelectors++] = {x, y, w, h, param, vals, lbls, n};
    }

    void addRadio(float x, float y, float w, float h, uint32_t param,
                  const char* const* lbls, int n) {
        if (fNumRadios < MAX_RADIOS)
            fRadios[fNumRadios++] = {x, y, w, h, param, lbls, n};
    }

    void addButton(float x, float y, float w, float h, int id, const char* label) {
        if (fNumButtons < MAX_BUTTONS)
            fButtons[fNumButtons++] = {x, y, w, h, id, label};
    }

    void buildLayout() {
        fNumKnobs = 0;
        fNumToggles = 0;
        fNumSelectors = 0;
        fNumRadios = 0;
        fNumTextFields = 0;
        fNumButtons = 0;

        // Variable column weights: CORE is widest, INPUT/OUT_EQ narrower
        //           INPUT  CORE  PITCH  GRAIN  MOD   OUTPUT SUB   OUT_EQ
        static constexpr float wt[] = {0.85f, 1.5f, 1.1f, 1.0f, 1.0f, 0.9f, 1.0f, 0.85f};
        float totalWt = 0.f;
        for (int i = 0; i < NUM_COLS; ++i) totalWt += wt[i];
        const float availW = UI_W - 2.f * PAD - static_cast<float>(NUM_COLS - 1) * COL_GAP;
        float cx = PAD;
        for (int i = 0; i < NUM_COLS; ++i) {
            fColX[i] = cx;
            fColW[i] = availW * wt[i] / totalWt;
            cx += fColW[i] + COL_GAP;
        }

        auto colCx = [&](int col) { return fColX[col] + fColW[col] * 0.5f; };
        auto colLx = [&](int col) { return fColX[col] + 22.f; };
        auto selW  = [&](int col) { return fColW[col] - 2.f * SECT_PAD; };
        auto selX  = [&](int col) { return fColX[col] + SECT_PAD; };
        const float topY = HEADER_H + PAD + 60.f;  // More space below titles

        // ── Column 0: INPUT (Gain + 3-band EQ) ──
        float y = topY;
        addKnob(colCx(0), y,  KNOB_BIG_R, kInputGain, true);
        y += ROW_H + 20.f;
        addKnob(colCx(0), y,  KNOB_R, kEqLow);
        y += ROW_H + 8.f;
        addKnob(colCx(0), y,  KNOB_R, kEqMid);
        y += ROW_H + 8.f;
        addKnob(colCx(0), y,  KNOB_R, kEqHigh);

        // ── Column 1: CORE (BPM sync at top) ──
        y = topY;
        addSelector(selX(1), y, selW(1), 28.f, kBpmDiv,
                    bpmDivVals, bpmDivLbls, NUM_BPM_DIV);
        y += 50.f;
        // Speed mode buttons: 2x (50%), 1.5x (67%), 4x (25%)
        {
            const float btnW = (fColW[1] - 2.f * SECT_PAD - 8.f) / 3.f;
            const float bx = selX(1);
            addButton(bx,              y, btnW, 26.f, BTN_SPEED_2X,  "2x");
            addButton(bx + btnW + 4.f, y, btnW, 26.f, BTN_SPEED_15X, "1.5x");
            addButton(bx + 2.f*(btnW + 4.f), y, btnW, 26.f, BTN_SPEED_4X,  "4x");
        }
        y += 40.f;
        addKnob(colCx(1), y,  KNOB_BIG_R, kSpeed, true);
        y += ROW_H + 14.f;
        addKnob(colCx(1), y,  KNOB_BIG_R, kWet, true);
        y += ROW_H + 14.f;
        addKnob(colCx(1), y,  KNOB_R, kSmooth);
        y += ROW_H + 8.f;
        addKnob(colCx(1), y,  KNOB_R, kGrainMs);
        y += ROW_H + 8.f;
        addToggle(colLx(1), y, kFreeze);

        // ── Column 2: PITCH / SPECTRAL ──
        y = topY;
        addKnob(colCx(2), y,  KNOB_R, kPitchSemi);
        y += ROW_H + 6.f;
        addKnob(colCx(2), y,  KNOB_R, kFormant);
        y += ROW_H + 6.f;
        addToggle(colLx(2), y, kPhaseLock);
        y += 50.f;
        addKnob(colCx(2), y,  KNOB_R, kSpecFreeze);
        y += ROW_H + 6.f;
        addKnob(colCx(2), y,  KNOB_R, kSpectralTilt);
        y += ROW_H + 6.f;
        addKnob(colCx(2), y,  KNOB_R, kPhaseRandom);

        // ── Column 3: GRAIN (vertically stacked radio) ──
        y = topY;
        addRadio(selX(3), y, selW(3), 80.f, kWinShape,
                 winShapeLbls, NUM_WIN_SHAPES);
        y += 90.f;
        addToggle(colLx(3), y, kReverse);
        y += 50.f;
        addKnob(colCx(3), y,  KNOB_R, kGrainRandom);
        y += ROW_H + 6.f;
        addToggle(colLx(3), y, kTransLock);
        y += 50.f;
        addKnob(colCx(3), y,  KNOB_R, kSensitivity);
        y += ROW_H + 10.f;
        addToggle(colLx(3), y, kLookahead);

        // ── Column 4: MODULATION ──
        y = topY;
        addSelector(selX(4), y, selW(4), 28.f, kStutterDiv,
                    stutterVals, stutterLbls, NUM_STUTTER);
        y += 60.f;
        // Manual BPM text field
        fTextFields[fNumTextFields++] = {colCx(4) - 40.f, y, 80.f, 28.f, kManualBpm, "Auto", false};
        y += 40.f;
        addToggle(colLx(4), y, kMorphIn);
        y += 50.f;
        addToggle(colLx(4), y, kMorphOut);
        y += 50.f;
        addSelector(selX(4), y, selW(4), 28.f, kMorphBeats,
                    morphBtVals, morphBtLbls, NUM_MORPH_BT);
        y += 60.f;
        addToggle(colLx(4), y, kSpecLatch);

        // ── Column 5: OUTPUT (Gain only) ──
        y = topY;
        addKnob(colCx(5), y,  KNOB_BIG_R, kOutputGain, true);
        y += ROW_H + 20.f;
        addKnob(colCx(5), y,  KNOB_R, kStereoWidth);

        // ── Column 6: SUB-BASS ──
        y = topY;
        addKnob(colCx(6), y,  KNOB_BIG_R, kSubGain, true);
        y += ROW_H + 20.f;
        addToggle(colLx(6), y, kSubMode);
        y += 50.f;
        addKnob(colCx(6), y,  KNOB_R, kSubFreq);
        y += ROW_H + 6.f;
        addKnob(colCx(6), y,  KNOB_R, kSubDrive);

        // ── Column 7: OUTPUT EQ ──
        y = topY;
        addKnob(colCx(7), y,  KNOB_R, kEqOutLow);
        y += ROW_H + 8.f;
        addKnob(colCx(7), y,  KNOB_R, kEqOutMid);
        y += ROW_H + 8.f;
        addKnob(colCx(7), y,  KNOB_R, kEqOutHigh);
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

        // Power button — large circle with power icon
        {
            const float pcx = PAD + 24.f;
            const float pcy = HEADER_H * 0.5f;
            const float pr  = 18.f;
            const Color& pCol = fPowered ? palette::powerOn : palette::powerOff;

            // outer ring
            beginPath();
            arc(pcx, pcy, pr, 0.f, 2.f * static_cast<float>(M_PI), NanoVG::Winding::CW);
            strokeColor(pCol);
            strokeWidth(3.f);
            stroke();
            closePath();

            // power icon: arc with gap at top + vertical line
            beginPath();
            arc(pcx, pcy, pr * 0.55f,
                static_cast<float>(M_PI) * 0.35f,
                static_cast<float>(M_PI) * 2.65f, NanoVG::Winding::CW);
            strokeColor(pCol);
            strokeWidth(2.5f);
            lineCap(NanoVG::LineCap::ROUND);
            stroke();
            closePath();
            beginPath();
            moveTo(pcx, pcy - pr * 0.6f);
            lineTo(pcx, pcy - pr * 0.1f);
            strokeColor(pCol);
            strokeWidth(2.5f);
            lineCap(NanoVG::LineCap::ROUND);
            stroke();
            closePath();
        }

        // half-moon icon rotated -45
        save();
        translate(PAD + 64.f, HEADER_H * 0.5f);
        rotate(-M_PI / 4.f); // -45 degrees
        beginPath();
        moveTo(0, -14.f);
        arc(0, 0, 14.f, -M_PI * 0.5f, 0, NanoVG::Winding::CW);
        closePath();
        fillColor(palette::accent);
        fill();
        restore();

        // title
        fontSize(34.f);
        fillColor(fPowered ? palette::accent : palette::powerOff);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(PAD + 96.f, HEADER_H * 0.5f, "HALFTIME", nullptr);

        // subtitle
        fontSize(15.f);
        fillColor(palette::textDim);
        text(PAD + 252.f, HEADER_H * 0.5f, "WSOLA time-stretch / phase vocoder", nullptr);

        // Level meters (thin horizontal bars, left of BPM display)
        {
            const float meterW = 100.f;
            const float meterH = 5.f;
            const float meterX = w * 0.5f - meterW - 50.f;
            const float meterY1 = HEADER_H * 0.5f - meterH - 2.f;
            const float meterY2 = HEADER_H * 0.5f + 2.f;

            auto drawMeter = [&](float x, float y, float level) {
                // track
                beginPath();
                roundedRect(x, y, meterW, meterH, 2.f);
                fillColor(palette::knobTrack);
                fill();
                closePath();
                // level bar (clamped to [0,1], then scaled)
                float norm = std::min(level, 1.5f) / 1.5f;
                if (norm > 0.005f) {
                    float barW = meterW * norm;
                    beginPath();
                    roundedRect(x, y, barW, meterH, 2.f);
                    // Green below 0dB, yellow approaching, red above
                    if (level > 1.0f)
                        fillColor(palette::accent);
                    else if (level > 0.7f)
                        fillColor(palette::yellow);
                    else
                        fillColor(palette::green);
                    fill();
                    closePath();
                }
            };

            drawMeter(meterX, meterY1, fValues[kPeakIn]);
            drawMeter(meterX, meterY2, fValues[kPeakOut]);

            // labels
            fontSize(9.f);
            fillColor(palette::textDim);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(meterX - 4.f, meterY1 + meterH * 0.5f, "IN", nullptr);
            text(meterX - 4.f, meterY2 + meterH * 0.5f, "OUT", nullptr);
        }

        // BPM display (centre of header)
        {
            char bpmBuf[32];
            float bpm = fValues[kBpmDisplay];
            if (bpm > 0.f)
                std::snprintf(bpmBuf, sizeof(bpmBuf), "%.1f BPM", bpm);
            else
                std::snprintf(bpmBuf, sizeof(bpmBuf), "-- BPM");
            fontSize(14.f);
            fillColor(palette::textBright);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(w * 0.5f, HEADER_H * 0.5f, bpmBuf, nullptr);
        }

        // preset reset button (left of preset pill)
        const float preW = 150.f;
        const float preH = 26.f;
        const float preX = w - PAD - preW;
        const float preY = (HEADER_H - preH) * 0.5f;
        const float rstW = 26.f;
        const float rstX = preX - rstW - 6.f;
        beginPath();
        roundedRect(rstX, preY, rstW, preH, 4.f);
        fillColor(palette::raised);
        fill();
        closePath();
        beginPath();
        roundedRect(rstX, preY, rstW, preH, 4.f);
        strokeColor(palette::border);
        strokeWidth(1.f);
        stroke();
        closePath();
        fontSize(14.f);
        fillColor(palette::textDim);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rstX + rstW * 0.5f, preY + preH * 0.5f, "R", nullptr);

        // preset selector (right side of header)
        beginPath();
        roundedRect(preX, preY, preW, preH, 4.f);
        fillColor(palette::raised);
        fill();
        closePath();
        beginPath();
        roundedRect(preX, preY, preW, preH, 4.f);
        strokeColor(palette::border);
        strokeWidth(1.f);
        stroke();
        closePath();
        // arrows
        fontSize(14.f);
        fillColor(palette::textDim);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(preX + 6.f, preY + preH * 0.5f, "\xe2\x97\x80", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(preX + preW - 6.f, preY + preH * 0.5f, "\xe2\x96\xb6", nullptr);
        // name
        fontSize(14.f);
        fillColor(palette::textBright);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(preX + preW * 0.5f, preY + preH * 0.5f,
             halftimePresets[fCurrentPreset].name, nullptr);
    }

    void drawSections(float /*w*/, float /*h*/) {
        const float topY = HEADER_H + PAD + 20.f;  // More space above
        const float sh   = UI_H - topY - PAD;
        const char* titles[] = {"INPUT", "CORE", "PITCH / SPECTRAL", "GRAIN", "MODULATION", "OUTPUT", "SUB-BASS", "OUT EQ"};

        for (int i = 0; i < NUM_COLS; ++i) {
            const float sx = fColX[i];
            const float sw = fColW[i];
            const float sy = topY;

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

            // section title (centered)
            fontSize(18.f);
            fillColor(palette::sectionHdr);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            text(sx + sw * 0.5f, sy + 12.f, titles[i], nullptr);
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

    static bool isGainParam(uint32_t p) {
        return p == kInputGain || p == kOutputGain;
    }

    static bool isEqParam(uint32_t p) {
        return p == kEqLow || p == kEqMid || p == kEqHigh ||
               p == kEqOutLow || p == kEqOutMid || p == kEqOutHigh;
    }

    void drawSingleKnob(float cx, float cy, float r, float norm,
                         uint32_t param, bool active) {
        const float startAngle = 0.75f * M_PI;
        const float sweepAngle = 1.5f  * M_PI;
        const float endAngle   = startAngle + sweepAngle;
        const float valueAngle = startAngle + norm * sweepAngle;

        // pick colour: red for gain, blue for EQ, green for everything else
        const bool isGain = isGainParam(param);
        const bool isEq   = isEqParam(param);
        const Color& colHi  = isGain ? palette::accent : (isEq ? palette::blue : palette::green);
        const Color& colLo  = isGain ? palette::accentDim : (isEq ? palette::blueDim : palette::greenDim);

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
            strokeColor(active ? colHi : colLo);
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
        fillColor(active ? colHi : palette::textBright);
        fill();
        closePath();

        // value text
        char buf[32];
        formatValue(buf, sizeof(buf), param, fValues[param]);
        fontSize(20.f);
        fillColor(palette::textBright);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, cy, buf, nullptr);

        // label below (supports \n line breaks)
        const char* label = paramLabel(param);
        fontSize(16.f);
        fillColor(palette::textDim);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(cx, cy + r + 10.f, label, nullptr);
    }

    void drawButtons() {
        for (int i = 0; i < fNumButtons; ++i) {
            const auto& b = fButtons[i];

            // Determine if this speed button matches current speed value
            bool active = false;
            if (b.id == BTN_SPEED_2X)  active = std::abs(fValues[kSpeed] - 50.f)  < 0.5f;
            if (b.id == BTN_SPEED_15X) active = std::abs(fValues[kSpeed] - 67.f)  < 1.f;
            if (b.id == BTN_SPEED_4X)  active = std::abs(fValues[kSpeed] - 25.f)  < 0.5f;

            // pill background
            beginPath();
            roundedRect(b.x, b.y, b.w, b.h, 4.f);
            fillColor(active ? palette::yellow : palette::raised);
            fill();
            closePath();

            if (!active) {
                beginPath();
                roundedRect(b.x, b.y, b.w, b.h, 4.f);
                strokeColor(palette::border);
                strokeWidth(1.f);
                stroke();
                closePath();
            }

            // label
            fontSize(14.f);
            fillColor(active ? palette::bg : palette::textDim);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(b.x + b.w * 0.5f, b.y + b.h * 0.5f, b.label, nullptr);
        }
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
            fontSize(13.f);
            fillColor(palette::textDim);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(t.x + t.w + 6.f, t.y + rad, paramLabel(t.param), nullptr);
        }
    }

    void drawSelectors() {
        for (int i = 0; i < fNumSelectors; ++i) {
            const auto& s = fSelectors[i];

            // find current index
            int cur = 0;
            float bestDist = 1e9f;
            for (int j = 0; j < s.numOptions; ++j) {
                float d = std::abs(fValues[s.param] - s.values[j]);
                if (d < bestDist) { bestDist = d; cur = j; }
            }

            // pill background
            beginPath();
            roundedRect(s.x, s.y, s.w, s.h, 4.f);
            fillColor(palette::raised);
            fill();
            closePath();
            beginPath();
            roundedRect(s.x, s.y, s.w, s.h, 4.f);
            strokeColor(palette::border);
            strokeWidth(1.f);
            stroke();
            closePath();

            // left/right arrows
            fontSize(12.f);
            fillColor(palette::textDim);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(s.x + 6.f, s.y + s.h * 0.5f, "\xe2\x97\x80", nullptr);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(s.x + s.w - 6.f, s.y + s.h * 0.5f, "\xe2\x96\xb6", nullptr);

            // current value
            fontSize(13.f);
            fillColor(palette::textBright);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(s.x + s.w * 0.5f, s.y + s.h * 0.5f, s.labels[cur], nullptr);

            // label below
            fontSize(13.f);
            fillColor(palette::textDim);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(s.x, s.y + s.h + 4.f, paramLabel(s.param), nullptr);
        }
    }

    void drawRadios() {
        for (int i = 0; i < fNumRadios; ++i) {
            const auto& r = fRadios[i];
            int cur = std::max(0, std::min(r.numOptions - 1,
                     static_cast<int>(fValues[r.param] + 0.5f)));
            const float btnH = r.h / static_cast<float>(r.numOptions);

            for (int j = 0; j < r.numOptions; ++j) {
                float by = r.y + j * btnH;
                bool sel = (j == cur);
                beginPath();
                roundedRect(r.x + 22.f, by, r.w - 22.f, btnH - 2.f, 3.f);
                fillColor(sel ? palette::green : palette::raised);
                fill();
                closePath();
                if (!sel) {
                    beginPath();
                    roundedRect(r.x + 22.f, by, r.w - 22.f, btnH - 2.f, 3.f);
                    strokeColor(palette::border);
                    strokeWidth(1.f);
                    stroke();
                    closePath();
                }
                fontSize(14.f);
                fillColor(sel ? palette::textBright : palette::textDim);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                text(r.x + 28.f, by + btnH * 0.5f, r.labels[j], nullptr);
            }
            // label below
            fontSize(16.f);
            fillColor(palette::textDim);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(r.x, r.y + r.h + 6.f, paramLabel(r.param), nullptr);
        }
    }

    void drawTextFields() {
        for (int i = 0; i < fNumTextFields; ++i) {
            const auto& tf = fTextFields[i];
            float val = fValues[tf.param];
            if (val < 1.f) {
                std::strcpy(const_cast<char*>(tf.buffer), "Auto");
            } else {
                std::snprintf(const_cast<char*>(tf.buffer), 16, "%.0f", val);
            }

            // field background
            beginPath();
            roundedRect(tf.x, tf.y, tf.w, tf.h, 4.f);
            fillColor(tf.active ? palette::raised : palette::surface);
            fill();
            closePath();
            beginPath();
            roundedRect(tf.x, tf.y, tf.w, tf.h, 4.f);
            strokeColor(palette::border);
            strokeWidth(1.f);
            stroke();
            closePath();

            // text
            fontSize(16.f);
            fillColor(palette::textBright);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(tf.x + tf.w * 0.5f, tf.y + tf.h * 0.5f, tf.buffer, nullptr);

            // label below
            fontSize(16.f);
            fillColor(palette::textDim);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(tf.x, tf.y + tf.h + 4.f, paramLabel(tf.param), nullptr);
        }
    }

    // ── Param helpers ───────────────────────────────────────────────────────

    static float getParamDefault(uint32_t p) {
        switch (p) {
        case kSpeed:        return 50.f;
        case kGrainMs:      return 80.f;
        case kWet:          return 1.f;
        case kPitchSemi:    return 0.f;
        case kStutterDiv:   return 1.f;
        case kFreeze:       return 0.f;
        case kTransLock:    return 0.f;
        case kSensitivity:  return 1.f;
        case kWinShape:     return 0.f;
        case kBpmDiv:       return 0.f;
        case kFormant:      return 1.f;
        case kSpecFreeze:   return 0.f;
        case kSpecLatch:    return 0.f;
        case kSubGain:      return 0.f;
        case kSubMode:      return 0.f;
        case kMorphIn:      return 0.f;
        case kMorphOut:     return 0.f;
        case kMorphBeats:   return 2.f;
        case kPhaseLock:    return 0.f;
        case kLookahead:    return 1.f;
        case kReverse:      return 0.f;
        case kGrainRandom:  return 0.f;
        case kStereoWidth:  return 1.f;
        case kSpectralTilt: return 0.f;
        case kPhaseRandom:  return 0.f;
        case kInputGain:    return 0.f;
        case kOutputGain:   return 0.f;
        case kEqLow:        return 0.f;
        case kEqMid:        return 0.f;
        case kEqHigh:       return 0.f;
        case kEqOutLow:     return 0.f;
        case kEqOutMid:     return 0.f;
        case kEqOutHigh:    return 0.f;
        case kManualBpm:    return 0.f;
        case kSubFreq:      return 80.f;
        case kSubDrive:     return 0.f;
        case kSmooth:       return 0.5f;
        default:            return 0.f;
        }
    }

    void resetParam(uint32_t p) {
        float def = getParamDefault(p);
        fValues[p] = def;
        setParameterValue(p, def);
        repaint();
    }

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
        case kInputGain:    mn = -24.f; mx = 24.f;  break;
        case kOutputGain:   mn = -24.f; mx = 24.f;  break;
        case kEqLow:        mn = -48.f; mx = 12.f;  break;
        case kEqMid:        mn = -48.f; mx = 12.f;  break;
        case kEqHigh:       mn = -48.f; mx = 12.f;  break;
        case kEqOutLow:     mn = -48.f; mx = 12.f;  break;
        case kEqOutMid:     mn = -48.f; mx = 12.f;  break;
        case kEqOutHigh:    mn = -48.f; mx = 12.f;  break;
        case kManualBpm:    mn = 0.f;   mx = 300.f; break;
        case kSubFreq:      mn = 30.f;  mx = 200.f; break;
        case kSubDrive:     mn = 0.f;   mx = 1.f;   break;
        case kSmooth:       mn = 0.f;   mx = 1.f;   break;
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
        case kInputGain:    std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kOutputGain:   std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kEqLow:        std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kEqMid:        std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kEqHigh:       std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kEqOutLow:     std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kEqOutMid:     std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kEqOutHigh:    std::snprintf(buf, sz, "%+.1fdB", v); break;
        case kManualBpm: {
            if (v < 1.f) std::snprintf(buf, sz, "Auto");
            else         std::snprintf(buf, sz, "%.0f", v);
            break;
        }
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
        case kSubFreq:      std::snprintf(buf, sz, "%.0fHz", v);   break;
        case kSubDrive:     std::snprintf(buf, sz, "%.0f%%", v*100); break;
        case kSmooth:       std::snprintf(buf, sz, "%.0f%%", v*100); break;
        default: std::snprintf(buf, sz, "%.0f%%", v*100); break;
        }
    }

    static const char* paramLabel(uint32_t p) {
        switch (p) {
        case kSpeed:        return "Speed";
        case kGrainMs:      return "Grain Size";
        case kWet:          return "Wet";
        case kPitchSemi:    return "Pitch";
        case kStutterDiv:   return "Stutter";
        case kFreeze:       return "Freeze";
        case kTransLock:    return "Transient Lock";
        case kSensitivity:  return "Sensitivity";
        case kWinShape:     return "Window";
        case kBpmDiv:       return "BPM Div";
        case kFormant:      return "Formant";
        case kSpecFreeze:   return "Spectral Freeze";
        case kSpecLatch:    return "Spectral Latch";
        case kSubGain:      return "Sub Bass";
        case kSubMode:      return "Sub Mode";
        case kMorphIn:      return "Morph In";
        case kMorphOut:     return "Morph Out";
        case kMorphBeats:   return "Morph Beats";
        case kPhaseLock:    return "Phase Lock";
        case kLookahead:    return "Lookahead";
        case kReverse:      return "Reverse Grains";
        case kGrainRandom:  return "Grain Jitter";
        case kStereoWidth:  return "Stereo Width";
        case kSpectralTilt: return "Spectral Tilt";
        case kPhaseRandom:  return "Phase Scatter";
        case kInputGain:    return "Input Gain";
        case kOutputGain:   return "Output Gain";
        case kEqLow:        return "EQ Low";
        case kEqMid:        return "EQ Mid";
        case kEqHigh:       return "EQ High";
        case kEqOutLow:     return "Out EQ\nLow";
        case kEqOutMid:     return "Out EQ\nMid";
        case kEqOutHigh:    return "Out EQ\nHigh";
        case kManualBpm:    return "Manual\nBPM";
        case kSubFreq:      return "Sub Freq";
        case kSubDrive:     return "Sub Drive";
        case kSmooth:       return "Smooth";
        default:            return "?";
        }
    }

    static const char* paramTooltip(uint32_t p) {
        switch (p) {
        case kSpeed:        return "Playback speed percentage";
        case kGrainMs:      return "Grain size in milliseconds";
        case kWet:          return "Wet/dry signal mix";
        case kPitchSemi:    return "Pitch shift in semitones";
        case kStutterDiv:   return "Rhythmic subdivision";
        case kFreeze:       return "Freeze the audio buffer";
        case kTransLock:    return "Lock to transients for timing";
        case kSensitivity:  return "Transient detection sensitivity";
        case kWinShape:     return "Analysis window shape";
        case kBpmDiv:       return "BPM sync subdivision";
        case kFormant:      return "Formant preservation";
        case kSpecFreeze:   return "Freeze spectral content";
        case kSpecLatch:    return "Latch spectral freeze";
        case kSubGain:      return "Sub bass gain amount";
        case kSubMode:      return "Sub bass synthesis mode";
        case kMorphIn:      return "Start morph transition";
        case kMorphOut:     return "End morph transition";
        case kMorphBeats:    return "Morph transition duration";
        case kPhaseLock:    return "Phase coherence lock";
        case kLookahead:    return "Transient lookahead";
        case kReverse:      return "Reverse grain direction";
        case kGrainRandom:  return "Grain position randomization";
        case kStereoWidth:  return "Stereo image width";
        case kSpectralTilt: return "High frequency tilt";
        case kPhaseRandom:  return "Phase randomization amount";
        case kInputGain:    return "Input level adjustment";
        case kOutputGain:   return "Output level adjustment";
        case kEqLow:        return "Low frequency shelf";
        case kEqMid:        return "Mid frequency peak";
        case kEqHigh:       return "High frequency shelf";
        case kEqOutLow:     return "Output low shelf";
        case kEqOutMid:     return "Output mid peak";
        case kEqOutHigh:    return "Output high shelf";
        case kManualBpm:    return "Manual BPM tempo";
        case kSubFreq:      return "Sub bass filter cutoff";
        case kSubDrive:     return "Sub bass saturation";
        case kSmooth:       return "Loop-point crossfade amount";
        default:            return "";
        }
    }

    void drawTooltip(float x, float y, const char* text) {
        if (!text || !*text) return;
        
        // Estimate text width (simplified)
        fontSize(14.f);
        float tw = static_cast<float>(std::strlen(text)) * 8.f + 16.f;
        float th = 24.f;
        
        // Position tooltip
        float tx = x + 10.f;
        float ty = y - th - 5.f;
        if (tx + tw > UI_W) tx = x - tw - 10.f;
        if (ty < 0) ty = y + 25.f;
        
        // Draw background
        beginPath();
        roundedRect(tx, ty, tw, th, 4.f);
        fillColor(0x20, 0x20, 0x30, 240);
        fill();
        closePath();
        beginPath();
        roundedRect(tx, ty, tw, th, 4.f);
        strokeColor(palette::border);
        strokeWidth(1.f);
        stroke();
        closePath();
        
        // Draw text
        fillColor(palette::textBright);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        this->text(tx + 8.f, ty + th * 0.5f, text, nullptr);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HalftimeUI)
};

UI* createUI() {
    return new HalftimeUI();
}

END_NAMESPACE_DISTRHO
