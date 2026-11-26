// alamo_render FrameEffectAlpha (render/src/frame_fade.cpp, docs/re/FRAME_EFFECTS.md): one full-screen triangle that
// draws the captured previous scene over the frame with alpha Params.x (0x1b22a0 puts (1 - t / length) x 255 in the
// quad's diffuse alpha), blended src-alpha / inverse-src-alpha.
struct FadePush {
    float4 Texel;   // 1 / width, 1 / height, width, height
    float4 Params;  // x alpha
};
[[vk::push_constant]] FadePush PC;

[[vk::combinedImageSampler]][[vk::binding(0, 0)]] Texture2D<float4> PrevTex;
[[vk::combinedImageSampler]][[vk::binding(0, 0)]] SamplerState PrevSmp;

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    const float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    const float4 c = PrevTex.SampleLevel(PrevSmp, pos.xy * PC.Texel.xy, 0);
    return float4(c.rgb, saturate(PC.Params.x));
}
