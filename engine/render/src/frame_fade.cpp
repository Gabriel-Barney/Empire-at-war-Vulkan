// alamo_render FrameEffectAlpha (src/frame_fade.hpp, shaders/frame_fade.hlsl, docs/re/FRAME_EFFECTS.md).
#include "frame_fade.hpp"

#include <memory>
#include <vector>

#include "alamo/core/log.hpp"
#include "render_internal.hpp"
#include "frame_fade_ps.h"
#include "frame_fade_vs.h"

namespace alamo::render {

namespace {
struct Push {
    float texel[4];
    float params[4];
};
static_assert(sizeof(Push) == 32);
constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
}  // namespace

bool FrameFadePass::init(rhi::Context& ctx, rhi::Allocator& alloc) {
    shutdown();
    ctx_ = &ctx;
    alloc_ = &alloc;
    samplers_.init(ctx);
    if (!cache_.init(ctx)) return false;
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto ci = rhi::vk_struct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    ci.bindingCount = 1;
    ci.pBindings = &b;
    if (ctx.vkd().vkCreateDescriptorSetLayout(ctx.device(), &ci, nullptr, &set_) != VK_SUCCESS) return false;
    layout_ = rhi::create_pipeline_layout(ctx, {&set_, 1}, sizeof(Push), kPushStages);
    if (!layout_) {
        log::errorf("render", "frame fade pipeline layout could not be created");
        return false;
    }
    return true;
}

void FrameFadePass::shutdown() {
    if (!ctx_) return;
    const rhi::DeviceFns& fn = ctx_->vkd();
    if (pipeline_) fn.vkDestroyPipeline(ctx_->device(), pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    pipeline_format_ = VK_FORMAT_UNDEFINED;
    if (layout_) fn.vkDestroyPipelineLayout(ctx_->device(), layout_, nullptr);
    layout_ = VK_NULL_HANDLE;
    if (set_) fn.vkDestroyDescriptorSetLayout(ctx_->device(), set_, nullptr);
    set_ = VK_NULL_HANDLE;
    prev_.shutdown();
    prev_format_ = VK_FORMAT_UNDEFINED;
    prev_valid_ = false;
    cache_.shutdown(false);
    samplers_.shutdown();
    ctx_ = nullptr;
    alloc_ = nullptr;
}

bool FrameFadePass::ensure_pipeline(VkFormat format) {
    if (pipeline_ && pipeline_format_ == format) return true;
    if (pipeline_) {  // a colour format change happens with the GPU idle (resize / recreated targets)
        ctx_->vkd().vkDestroyPipeline(ctx_->device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    const std::vector<uint32_t> vs = rhi::spirv_from_bytes({g_frame_fade_vs, sizeof(g_frame_fade_vs)});
    const std::vector<uint32_t> ps = rhi::spirv_from_bytes({g_frame_fade_ps, sizeof(g_frame_fade_ps)});
    rhi::GraphicsPipelineDesc d;
    d.vs_spirv = vs;
    d.fs_spirv = ps;
    d.vs_entry = "VSMain";
    d.fs_entry = "PSMain";
    d.raster.cull = VK_CULL_MODE_NONE;
    d.blend.enable = true;  // the quad's diffuse alpha over the frame (0x1b22a0)
    d.blend.src_color = VK_BLEND_FACTOR_SRC_ALPHA;
    d.blend.dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    d.blend.write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    d.color_formats = {&format, 1};
    d.layout = layout_;
    d.name = "render.frame_fade";
    pipeline_ = cache_.create_graphics(d);
    pipeline_format_ = pipeline_ ? format : VK_FORMAT_UNDEFINED;
    if (!pipeline_) log::errorf("render", "frame fade pipeline could not be created");
    return pipeline_ != VK_NULL_HANDLE;
}

bool FrameFadePass::has_capture(VkFormat format, VkExtent2D ext) const {
    return prev_valid_ && prev_.has_color() && prev_format_ == format && prev_.extent().width == ext.width &&
           prev_.extent().height == ext.height;
}

bool FrameFadePass::ensure_prev(VkFormat format, VkExtent2D ext) {
    if (prev_.has_color() && prev_format_ == format && prev_.extent().width == ext.width && prev_.extent().height == ext.height)
        return true;
    if (prev_.has_color()) ctx_->wait_idle();  // resize on the frame thread; the old image may be in flight
    prev_.shutdown();
    prev_valid_ = false;
    rhi::RenderTargetDesc d;
    d.color = rhi::from_vk(format);
    d.name = "render.prev_scene";
    if (d.color == rhi::Format::Undefined || !prev_.init(*alloc_, d, ext)) {
        prev_format_ = VK_FORMAT_UNDEFINED;
        return false;
    }
    prev_format_ = format;
    return true;
}

bool FrameFadePass::record(rhi::Frame& frame, VkCommandBuffer cmd, VkImage color, VkImageView view, VkFormat format,
                           VkExtent2D extent, rhi::ImageState state, rhi::ImageState after, float alpha, bool capture) {
    if (!ctx_ || !color || !view || extent.width == 0 || extent.height == 0) return false;
    const rhi::DeviceFns& fn = ctx_->vkd();
    bool ok = true;
    rhi::ImageState cur = state;
    if (alpha > 0.0f && has_capture(format, extent)) {
        const VkDescriptorSet set = ensure_pipeline(format) ? frame.descriptors(0).allocate(set_) : VK_NULL_HANDLE;
        const VkSampler point = samplers_.get(rhi::SamplerKey::point_clamp());
        if (set && point) {
            const VkDescriptorImageInfo ii{point, prev_.color().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet wr = rhi::vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            wr.dstSet = set;
            wr.dstBinding = 0;
            wr.descriptorCount = 1;
            wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr.pImageInfo = &ii;
            fn.vkUpdateDescriptorSets(ctx_->device(), 1, &wr, 0, nullptr);
            if (cur != rhi::ImageState::ColorAttachment)
                rhi::cmd_barrier(fn, cmd, rhi::transition(color, cur, rhi::ImageState::ColorAttachment));
            cur = rhi::ImageState::ColorAttachment;
            auto ca = rhi::vk_struct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
            ca.imageView = view;
            ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // blended over the frame
            ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            auto ri = rhi::vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
            ri.renderArea = {{0, 0}, extent};
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &ca;
            fn.vkCmdBeginRendering(cmd, &ri);
            VkViewport vp{};
            vp.width = static_cast<float>(extent.width);
            vp.height = static_cast<float>(extent.height);
            vp.maxDepth = 1.0f;
            fn.vkCmdSetViewport(cmd, 0, 1, &vp);
            const VkRect2D sc{{0, 0}, extent};
            fn.vkCmdSetScissor(cmd, 0, 1, &sc);
            Push pc{};
            pc.texel[0] = 1.0f / static_cast<float>(extent.width);
            pc.texel[1] = 1.0f / static_cast<float>(extent.height);
            pc.texel[2] = static_cast<float>(extent.width);
            pc.texel[3] = static_cast<float>(extent.height);
            pc.params[0] = alpha;
            fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
            fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
            fn.vkCmdPushConstants(cmd, layout_, kPushStages, 0, sizeof pc, &pc);
            fn.vkCmdDraw(cmd, 3, 1, 0, 0);
            fn.vkCmdEndRendering(cmd);
        } else {
            ok = false;
        }
    }
    if (capture) {  // 0x181c20: the frame -> PrevSceneTexture
        if (ensure_prev(format, extent)) {
            rhi::CopyImage src;
            src.image = color;
            src.extent = extent;
            src.format = format;
            src.before = cur;
            src.after = after;
            prev_.copy_from(fn, cmd, src, VK_FILTER_NEAREST, rhi::ImageState::ShaderRead);
            prev_valid_ = true;
            return ok;
        }
        ok = false;
    }
    if (cur != after) rhi::cmd_barrier(fn, cmd, rhi::transition(color, cur, after));
    return ok;
}

// ------------------------------------------------------------------------------------------- Renderer hook

bool RendererCore::apply_frame_fade(rhi::Frame& frame, VkCommandBuffer cmd, VkImage color, VkImageView view, VkFormat format,
                                    VkExtent2D extent, rhi::ImageState state, rhi::ImageState after, float alpha, bool capture) {
    if (!(alpha > 0.0f) && !capture) return true;  // nothing to do
    if (!ctx_) return false;
    if (!frame_fade_) {
        frame_fade_ = std::make_unique<FrameFadePass>();
        if (!frame_fade_->init(*ctx_, *alloc_)) {
            log::errorf("render", "frame fade could not be initialised; frames are drawn without it");
            frame_fade_.reset();
            return false;
        }
    }
    return frame_fade_->record(frame, cmd, color, view, format, extent, state, after, alpha, capture);
}

}  // namespace alamo::render
