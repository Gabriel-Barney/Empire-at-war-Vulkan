// alamo_render FrameEffectAlpha on the GPU (internal; RendererCore::apply_frame_fade). Retail's
// alD3dTransitionEffectAlphaFade (0x1b20b0, Engine\FrameEffectAlpha): PrevSceneTexture holds a copy of the frame the
// request was made in, drawn over later frames as one full-screen quad with alpha (docs/re/FRAME_EFFECTS.md).
#pragma once
#include <cstdint>

#include "alamo/rhi/rhi.hpp"

namespace alamo::render {

class FrameFadePass {
public:
    FrameFadePass() = default;
    ~FrameFadePass() { shutdown(); }
    FrameFadePass(const FrameFadePass&) = delete;
    FrameFadePass& operator=(const FrameFadePass&) = delete;

    bool init(rhi::Context& ctx, rhi::Allocator& alloc);
    void shutdown();  // GPU idle
    // `color` (view `view`, `format`, `extent`) is in `state` and is left in `after`. alpha > 0 draws the captured scene
    // over it (nothing when there is none of this size / format); capture then copies the result into the captured
    // scene. False when a requested part could not be recorded (no pipeline, descriptor or image).
    bool record(rhi::Frame& frame, VkCommandBuffer cmd, VkImage color, VkImageView view, VkFormat format, VkExtent2D extent,
                rhi::ImageState state, rhi::ImageState after, float alpha, bool capture);
    bool has_capture(VkFormat format, VkExtent2D ext) const;

private:
    bool ensure_prev(VkFormat format, VkExtent2D ext);
    bool ensure_pipeline(VkFormat format);

    rhi::Context* ctx_ = nullptr;
    rhi::Allocator* alloc_ = nullptr;
    rhi::PipelineCache cache_;
    rhi::SamplerCache samplers_;
    VkDescriptorSetLayout set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkFormat pipeline_format_ = VK_FORMAT_UNDEFINED;
    rhi::RenderTarget prev_;  // PrevSceneTexture
    VkFormat prev_format_ = VK_FORMAT_UNDEFINED;
    bool prev_valid_ = false;  // a capture was recorded into prev_ at its current size
};

}  // namespace alamo::render
