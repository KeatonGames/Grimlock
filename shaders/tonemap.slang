[[vk::binding(0, 0)]] Sampler2D scene;
struct Settings { float exposure; };
[[vk::push_constant]] ConstantBuffer<Settings> settings;
struct Output { float4 position : SV_Position; [[vk::location(0)]] float2 uv : TEXCOORD0; };
[shader("vertex")]
Output vertMain(uint index : SV_VulkanVertexID) {
    Output output;
    output.uv = float2((index << 1) & 2, index & 2);
    output.position = float4(output.uv * 2 - 1, 0, 1);
    return output;
}
[shader("fragment")]
float4 fragMain(Output input) : SV_Target0 {
    float3 color = min(max(scene.SampleLevel(input.uv, 0).rgb, 0) * settings.exposure, 65504);
    // Narkowicz filmic fit; this is not a complete ACES color-management pipeline.
    color = saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
    return float4(color, 1); // The sRGB attachment performs the sole output encoding.
}
