static const float PI = 3.14159265359;
struct CameraData { float4x4 view; float4x4 projection; float4 position; };
struct DrawData {
    float4x4 model;
    float4 normalColumn0; // W = sign of model determinant.
    float4 normalColumn1; // W = selection highlight; XYZ still stores the normal matrix.
    float4 normalColumn2;
    float4 baseColor;
};
struct PointLight { float4 positionRange; float4 colorIntensity; };
struct SpotLight { float4 positionRange; float4 colorIntensity; float4 directionInner; float4 outer; };
struct LightData {
    float4 directionIntensity;
    float4 directionalColor;
    float4 environment; // intensity, yaw, max specular LOD
    uint4 counts;
    PointLight points[8];
    SpotLight spots[8];
};
struct TextureCoordinates { float4 row0; float4 row1; };
struct MaterialData {
    float4 baseColor;
    float4 emissiveRoughness;
    float4 factors; // metallic, normal scale, occlusion strength, alpha cutoff
    float4 state; // alpha mode, double-sided
    float4 specularColorIor;
    float4 extensions;
    TextureCoordinates coordinates[10];
};
[[vk::binding(0, 0)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 0)]] ConstantBuffer<LightData> lights;
[[vk::binding(2, 0)]] SamplerCube irradianceMap;
[[vk::binding(3, 0)]] SamplerCube specularMap;
[[vk::binding(4, 0)]] Sampler2D brdfMap;
[[vk::push_constant]] ConstantBuffer<DrawData> draw;
[[vk::binding(0, 1)]] Sampler2D baseColorMap;
[[vk::binding(1, 1)]] ConstantBuffer<MaterialData> material;
[[vk::binding(2, 1)]] Sampler2D metallicRoughnessMap;
[[vk::binding(3, 1)]] Sampler2D normalMap;
[[vk::binding(4, 1)]] Sampler2D occlusionMap;
[[vk::binding(5, 1)]] Sampler2D emissiveMap;
[[vk::binding(6, 1)]] Sampler2D specularWeightMap;
[[vk::binding(7, 1)]] Sampler2D specularColorMap;
[[vk::binding(8, 1)]] Sampler2D clearcoatMap;
[[vk::binding(9, 1)]] Sampler2D clearcoatRoughnessMap;
[[vk::binding(10, 1)]] Sampler2D clearcoatNormalMap;
struct DeformDraw { uint4 offsets; uint4 counts; };
struct MorphDelta { float4 position; float4 normal; float4 tangent; };
[[vk::binding(0, 2)]] StructuredBuffer<DeformDraw> deformationDraws;
[[vk::binding(1, 2)]] StructuredBuffer<float4x4> jointMatrices;
[[vk::binding(2, 2)]] StructuredBuffer<float> morphWeights;
[[vk::binding(3, 2)]] StructuredBuffer<MorphDelta> morphDeltas;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float2 uv1 : TEXCOORD1;
    [[vk::location(4)]] float4 tangent : TANGENT;
    [[vk::location(5)]] uint4 joints0 : BLENDINDICES0;
    [[vk::location(6)]] uint4 joints1 : BLENDINDICES1;
    [[vk::location(7)]] float4 weights0 : BLENDWEIGHT0;
    [[vk::location(8)]] float4 weights1 : BLENDWEIGHT1;
    [[vk::location(9)]] uint morphIndex : TEXCOORD2;
#ifdef GRID_INSTANCES
    [[vk::location(10)]] float3 instancePosition : TEXCOORD3;
    [[vk::location(11)]] float3 instanceScale : TEXCOORD4;
#endif
};
struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldNormal : NORMAL;
    [[vk::location(1)]] float3 worldPosition : POSITION;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float2 uv1 : TEXCOORD1;
    [[vk::location(4)]] float4 tangent : TANGENT;
};
[shader("vertex")]
VertexOutput vertMain(VertexInput input, uint instance : SV_VulkanInstanceID) {
    VertexOutput output;
#ifdef GRID_INSTANCES
    input.position = input.position * input.instanceScale + input.instancePosition;
    input.normal /= input.instanceScale;
#else
    DeformDraw deform = deformationDraws[instance];
    for (uint target = 0; target < deform.counts.y; ++target) {
        float weight = morphWeights[deform.offsets.y + target];
        MorphDelta delta = morphDeltas[target * deform.counts.z + input.morphIndex];
        input.position += delta.position.xyz * weight;
        input.normal += delta.normal.xyz * weight;
        input.tangent.xyz += delta.tangent.xyz * weight;
    }
    if (deform.counts.x > 0) {
        float4x4 skin = float4x4(0);
        for (uint lane=0; lane<4; ++lane) {
            skin += jointMatrices[deform.offsets.x + input.joints0[lane]] * input.weights0[lane];
            skin += jointMatrices[deform.offsets.x + input.joints1[lane]] * input.weights1[lane];
        }
        input.position = mul(skin,float4(input.position,1)).xyz;
        float3 c0 = float3(skin[0][0],skin[1][0],skin[2][0]);
        float3 c1 = float3(skin[0][1],skin[1][1],skin[2][1]);
        float3 c2 = float3(skin[0][2],skin[1][2],skin[2][2]);
        float determinant = dot(c0,cross(c1,c2));
        if (abs(determinant)>1e-10) {
            input.normal = (cross(c1,c2)*input.normal.x + cross(c2,c0)*input.normal.y + cross(c0,c1)*input.normal.z)/determinant;
        } // Singular blended skin keeps its geometric normal instead of emitting NaNs.
        input.tangent.xyz = mul((float3x3)skin,input.tangent.xyz);
        input.tangent.w *= determinant < 0 ? -1 : 1;
    }
#endif
    float4 world = mul(draw.model, float4(input.position, 1));
    output.position = mul(camera.projection, mul(camera.view, world));
    output.worldPosition = world.xyz;
    output.worldNormal = draw.normalColumn0.xyz * input.normal.x
                       + draw.normalColumn1.xyz * input.normal.y
                       + draw.normalColumn2.xyz * input.normal.z;
    output.tangent = float4(mul((float3x3)draw.model, input.tangent.xyz), input.tangent.w * draw.normalColumn0.w);
    output.uv = input.uv; output.uv1 = input.uv1;
    return output;
}
float2 coordinates(VertexOutput input, uint slot) {
    TextureCoordinates transform = material.coordinates[slot];
    float2 uv = transform.row0.w > 0.5 ? input.uv1 : input.uv;
    return float2(dot(transform.row0.xyz, float3(uv, 1)), dot(transform.row1.xyz, float3(uv, 1)));
}
float3 safeNormalize(float3 value, float3 fallback) {
    float length2 = dot(value, value);
    return length2 > 1e-12 ? value * rsqrt(length2) : fallback;
}
float smithG1(float cosine, float alphaSquared) {
    return 2 * cosine / max(cosine + sqrt(alphaSquared + (1 - alphaSquared) * cosine * cosine), 1e-6);
}
float3 evaluateLight(float3 n, float3 v, float3 l, float3 radiance, float3 albedo, float metallic, float roughness, float3 f0, float3 f90) {
    float nl = saturate(dot(n, l)), nv = max(saturate(dot(n, v)), 1e-4);
    float3 h = safeNormalize(l + v, n);
    float nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    float alpha = roughness * roughness, alphaSquared = alpha * alpha;
    float denominator = nh * nh * (alphaSquared - 1) + 1;
    float distribution = alphaSquared / max(PI * denominator * denominator, 1e-12);
    float geometry = smithG1(nv, alphaSquared) * smithG1(nl, alphaSquared);
    float3 f = f0 + (f90 - f0) * pow(1 - vh, 5);
    float3 specular = distribution * geometry * f / max(4 * nv * nl, 1e-5);
    float3 diffuse = (1 - max(f.x, max(f.y, f.z))) * (1 - metallic) * albedo / PI;
    return (diffuse + specular) * radiance * nl;
}
float3 environmentDirection(float3 direction) {
    float s, c; sincos(lights.environment.y, s, c);
    return float3(c * direction.x + s * direction.z, direction.y, -s * direction.x + c * direction.z);
}
float3 selectionHighlight(float3 color, VertexOutput input) {
    if (draw.normalColumn1.w < 0.5) return color;
    // Inner silhouette rim, not an expanded hull: existing depth/alpha tests retain occlusion.
    float3 normal = safeNormalize(input.worldNormal, float3(0,1,0));
    float3 view = safeNormalize(camera.position.xyz - input.worldPosition, normal);
    float rim = pow(1 - saturate(abs(dot(normal,view))), 3);
    float strength = 0.10 + 0.50 * smoothstep(0.12, 0.8, rim);
    // A bounded cyan overlay both lifts dark surfaces and contrasts with bright ones.
    return lerp(color, float3(0.08, 1.8, 2.6), strength);
}
[shader("fragment")]
float4 fragMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target0 {
    float4 base = baseColorMap.Sample(coordinates(input, 0)) * material.baseColor * draw.baseColor;
    if (material.state.x > 0.5 && material.state.x < 1.5 && base.a < material.factors.w) discard;
    if (material.state.x < 1.5) base.a = 1;
    if (material.state.z > 0.5) return float4(selectionHighlight(base.rgb,input), base.a);
    float2 packed = metallicRoughnessMap.Sample(coordinates(input, 1)).gb;
    float roughness = clamp(material.emissiveRoughness.w * packed.x, 0.045, 1);
    float metallic = saturate(material.factors.x * packed.y);
    float3 n = safeNormalize(input.worldNormal, float3(0, 1, 0));
    float3 coatNormal = n;
    if (material.coordinates[9].row1.w > 0.5) {
        float2 uv = coordinates(input, 9);
        float3 px = ddx(input.worldPosition), py = ddy(input.worldPosition);
        float2 ux = ddx(uv), uy = ddy(uv);
        float determinant = ux.x * uy.y - ux.y * uy.x;
        if (abs(determinant) > 1e-10) {
            float3 ct = (px * uy.y - py * ux.y) / determinant;
            float3 cb = (py * ux.x - px * uy.x) / determinant;
            ct = safeNormalize(ct - n * dot(n, ct), float3(1,0,0));
            cb = cross(n, ct) * (dot(cross(n, ct), cb) < 0 ? -1 : 1);
            float3 mapped = clearcoatNormalMap.Sample(uv).xyz * 2 - 1;
            mapped.xy *= material.state.w;
            coatNormal = safeNormalize(ct * mapped.x + cb * mapped.y + n * mapped.z, n);
        }
    }
    float3 t = input.tangent.xyz - n * dot(n, input.tangent.xyz);
    if (material.coordinates[2].row1.w > 0.5 && abs(input.tangent.w) > 0.5 && dot(t, t) > 1e-12) {
        t = normalize(t);
        float3 b = cross(n, t) * (input.tangent.w < 0 ? -1 : 1);
        float3 mapped = normalMap.Sample(coordinates(input, 2)).xyz * 2 - 1;
        mapped.xy *= material.factors.y;
        mapped = safeNormalize(mapped, float3(0, 0, 1));
        n = safeNormalize(t * mapped.x + b * mapped.y + n * mapped.z, n);
    }
    if (material.state.y > 0.5 && !frontFacing) { n = -n; coatNormal = -coatNormal; }
    float3 v = safeNormalize(camera.position.xyz - input.worldPosition, n);
    float specularWeight = material.extensions.z * specularWeightMap.Sample(coordinates(input, 5)).a;
    float3 specularColor = material.specularColorIor.rgb * specularColorMap.Sample(coordinates(input, 6)).rgb;
    float ior = material.specularColorIor.w;
    float reflectance = ior == 0 ? 1 : pow((ior - 1) / (ior + 1), 2);
    float3 f0 = lerp(min(reflectance * specularColor, float3(1)) * specularWeight, base.rgb, metallic);
    float3 f90 = lerp(float3(specularWeight), float3(1), metallic);
    float coat = material.extensions.x * clearcoatMap.Sample(coordinates(input, 7)).r;
    float coatRoughness = clamp(material.extensions.y * clearcoatRoughnessMap.Sample(coordinates(input, 8)).g, 0.045, 1);
    float coatFresnel = coat * (0.04 + 0.96 * pow(1 - saturate(dot(coatNormal, v)), 5));
    float3 color = evaluateLight(n, v, -lights.directionIntensity.xyz,
        lights.directionalColor.rgb * lights.directionIntensity.w, base.rgb, metallic, roughness, f0, f90) * (1 - coatFresnel);
    color += evaluateLight(coatNormal, v, -lights.directionIntensity.xyz, lights.directionalColor.rgb * lights.directionIntensity.w,
        float3(1), 1, coatRoughness, float3(1), float3(1)) * coatFresnel;
    for (uint i = 0; i < min(lights.counts.x, 8u); ++i) {
        PointLight light = lights.points[i];
        float3 delta = light.positionRange.xyz - input.worldPosition;
        float distance2 = max(dot(delta, delta), 1e-4);
        float attenuation = 1 / distance2;
        if (light.positionRange.w > 0) {
            float cutoff = saturate(1 - pow(sqrt(distance2) / light.positionRange.w, 4));
            attenuation *= cutoff * cutoff;
        }
        color += evaluateLight(n, v, delta * rsqrt(distance2), light.colorIntensity.rgb * light.colorIntensity.w * attenuation,
            base.rgb, metallic, roughness, f0, f90) * (1 - coatFresnel);
        color += evaluateLight(coatNormal, v, delta * rsqrt(distance2), light.colorIntensity.rgb * light.colorIntensity.w * attenuation,
            float3(1), 1, coatRoughness, float3(1), float3(1)) * coatFresnel;
    }
    for (uint i=0; i<min(lights.counts.y,8u); ++i) {
        SpotLight light=lights.spots[i]; float3 delta=light.positionRange.xyz-input.worldPosition;
        float distance2=max(dot(delta,delta),1e-4); float3 l=delta*rsqrt(distance2);
        float angular=saturate((dot(-l,light.directionInner.xyz)-light.outer.x)/max(light.directionInner.w-light.outer.x,1e-6));
        float attenuation=angular*angular/distance2;
        if (light.positionRange.w>0) { float cutoff=saturate(1-pow(sqrt(distance2)/light.positionRange.w,4)); attenuation*=cutoff*cutoff; }
        float3 radiance=light.colorIntensity.rgb*light.colorIntensity.w*attenuation;
        color+=evaluateLight(n,v,l,radiance,base.rgb,metallic,roughness,f0,f90)*(1-coatFresnel);
        color+=evaluateLight(coatNormal,v,l,radiance,float3(1),1,coatRoughness,float3(1),float3(1))*coatFresnel;
    }
    float nv = saturate(dot(n, v));
    float3 f = f0 + (max((1 - roughness) * f90, f0) - f0) * pow(1 - nv, 5);
    float3 diffuseIbl = (1 - max(f.x, max(f.y, f.z))) * (1 - metallic) * base.rgb
        * irradianceMap.SampleLevel(environmentDirection(n), 0).rgb / PI;
    float2 brdf = brdfMap.SampleLevel(float2(nv, roughness), 0).rg;
    float3 specularIbl = specularMap.SampleLevel(environmentDirection(reflect(-v, n)), roughness * lights.environment.z).rgb
        * (f0 * brdf.x + f90 * brdf.y);
    float ao = lerp(1.0, occlusionMap.Sample(coordinates(input, 3)).r, material.factors.z);
    float coatNv = saturate(dot(coatNormal, v));
    float2 coatBrdf = brdfMap.SampleLevel(float2(coatNv, coatRoughness), 0).rg;
    float3 coatIbl = specularMap.SampleLevel(environmentDirection(reflect(-v, coatNormal)), coatRoughness * lights.environment.z).rgb
        * (coatBrdf.x + coatBrdf.y) * coatFresnel;
    color += ((diffuseIbl + specularIbl) * (1 - coatFresnel) + coatIbl) * lights.environment.x * ao;
    color += emissiveMap.Sample(coordinates(input, 4)).rgb * material.emissiveRoughness.rgb * material.extensions.w * (1 - coatFresnel);
    return float4(clamp(selectionHighlight(color,input), 0, 65504), base.a); // Finite RGBA16F; tone mapping happens after blending.
}
