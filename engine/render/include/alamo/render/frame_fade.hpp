// alamo_render - FrameEffectAlpha, the alpha cross-fade from the previous scene (docs/re/FRAME_EFFECTS.md).
//
// Retail keeps one transition mode (0x140a6fe80): 1 FrameEffectWipe, 2 FrameEffectAlpha, 3 FrameEffectFakeMotionBlur.
// Only mode 2 is ever requested in the shipped binary [H]: every caller of the request 0x25fb10 passes 2, and the
// wipe / motion-blur setters (0x181b30, 0x181bd0) have no callers. The callers are the tactical camera's overview level
// changes (0x2ce520 zoom notches, 0x2d07b0 slot 76, 0x2ce9c0 with its fade flag), each with a 0.25 s length (0x181bb0).
//
// Once per drawn frame (0x25fb30, called with 0.1 from the frame function 0x60330 [M]: the step is a constant, not the
// frame time) the running fade steps first (0x181c50: t += 0.1, done at t >= length, else the captured scene is drawn
// over the frame with alpha 1 - t / length, 0x1b22a0 [H]); then a pending request copies the frame into
// PrevSceneTexture and, when the copy worked (0x181c20), starts the fade at t = 0 (0x181af0). The fade is drawn over
// the world before the HUD (the GUI draw 0x28c520 follows 0x25fb30).
#pragma once

namespace alamo::render {

class FrameFade {
public:
    static constexpr float kSeconds = 0.25f;  // 0x181bb0(0.25) with every request [H]
    static constexpr float kStep = 0.1f;      // 0x25fb30(0.1) once per drawn frame [M]

    struct Step {
        float alpha = 0.0f;    // > 0: draw the captured scene over this frame with this alpha
        bool capture = false;  // copy this frame (after the fade above) into the captured scene
    };

    void request() { pending_ = true; }  // 0x25fb10(2)
    // The work for this drawn frame: the running fade's step, then the pending capture (which restarts the fade).
    Step frame() {
        Step s;
        if (active_) {
            t_ += kStep;
            if (t_ >= kSeconds) {
                active_ = false;
            } else {
                const float a = 1.0f - t_ / kSeconds;
                s.alpha = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            }
        }
        if (pending_) {
            pending_ = false;
            s.capture = true;
            active_ = true;
            t_ = 0.0f;
        }
        return s;
    }
    void capture_failed() { active_ = false; }  // 0x181c20 false: the mode is not set
    void reset() { *this = FrameFade{}; }
    bool active() const { return active_; }
    bool pending() const { return pending_; }

private:
    float t_ = 0.0f;
    bool active_ = false;
    bool pending_ = false;
};

}  // namespace alamo::render
