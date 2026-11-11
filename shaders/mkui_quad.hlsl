// MkUI reference shader for mkui::Quad (mkui/Ui.h), the same math as the MealKit engine's UI pass.
// One instance = one Quad, 6 vertices via SV_VertexID (no vertex buffer): draw(6, quadCount). No depth, no culling.
// Blend: premultiplied alpha (One, OneMinusSrcAlpha). Colors are sRGB bytes used as is (write to a non-sRGB view).
// Compile: dxc -T vs_6_0 -E VSMain / -T ps_6_0 -E PSMain (add -spirv for Vulkan; bindings below carry [[vk::binding]]).
// The host binds the Quad array, the two atlases (mkui::AtlasKind, kAtlasSize square) and a linear clamp sampler.

struct Quad // 112 bytes
{
    float4 rect;     // x, y, w, h (pixels, box before rotation)
    float4 clip;     // x0, y0, x1, y1 (pixels, nothing is drawn outside)
    float4 radii;    // top left, top right, bottom right, bottom left (pixels)
    float4 uv;       // atlas u0, v0, u1, v1
    float4 gradient; // linear: start xy, end xy / radial: center xy, radius xy (box 0..1 coordinates)
    uint color0;     // fill (gradient start), RGBA8 straight alpha
    uint color1;     // gradient end / image brightness
    uint strokeColor;
    uint flags;      // 0-3 kind (0 shape, 1 image, 2 text, 3 shadow), 4-5 fill (0 solid, 1 linear, 2 radial)
    float strokeWidth;
    float rotation;  // radians (around the box center)
    float softness;  // shadow blur (pixels)
    float sdfScale;  // text: screen pixels per atlas value unit
};

cbuffer Frame : register(b0)
{
    float2 screenSize; // back buffer pixels
    uint quadOffset;   // first Quad of this draw in the buffer
    uint pad;
};
[[vk::binding(1)]] StructuredBuffer<Quad> quads : register(t0);
[[vk::binding(2)]] Texture2D<float> glyphAtlas : register(t1);    // R8 SDF
[[vk::binding(3)]] Texture2D<float4> imageAtlas : register(t2);   // RGBA8
[[vk::binding(4)]] SamplerState linearClamp : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 local : LOCAL; // pixels relative to box center (before rotation)
    nointerpolation uint quad : QUAD;
};

float4 unpack(uint c)
{
    return float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24) / 255.0;
}

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    Quad q = quads[quadOffset + instanceId];
    const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 0), float2(1, 1), float2(0, 1) };
    float2 c = corners[vertexId];
    float margin = q.softness * 2.0 + 1.0; // half pixel outside the border + shadow blur
    float2 half = q.rect.zw * 0.5;
    float2 local = lerp(-half - margin, half + margin, c);
    float s = sin(q.rotation), co = cos(q.rotation);
    float2 rotated = float2(local.x * co - local.y * s, local.x * s + local.y * co);
    float2 pixel = q.rect.xy + half + rotated;
    VSOutput o;
    o.position = float4(pixel.x / screenSize.x * 2.0 - 1.0, 1.0 - pixel.y / screenSize.y * 2.0, 0.0, 1.0);
    o.local = local;
    o.quad = quadOffset + instanceId;
    return o;
}

// Signed distance of a rounded rectangle with per-corner radii (negative inside)
float roundBox(float2 p, float2 half, float4 r)
{
    float radius = p.x > 0.0 ? (p.y > 0.0 ? r.z : r.y) : (p.y > 0.0 ? r.w : r.x);
    radius = min(radius, min(half.x, half.y));
    float2 d = abs(p) - half + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

float4 PSMain(VSOutput input) : SV_Target
{
    Quad q = quads[input.quad];
    float2 screen = input.position.xy;
    if (screen.x < q.clip.x || screen.y < q.clip.y || screen.x >= q.clip.z || screen.y >= q.clip.w) discard;

    uint kind = q.flags & 0xF;
    float2 half = q.rect.zw * 0.5;
    float2 box01 = (input.local + half) / max(q.rect.zw, 1e-3);
    float d = roundBox(input.local, half, q.radii);
    float coverage = saturate(0.5 - d);

    if (kind == 3) // shadow
    {
        float blur = max(q.softness, 0.5);
        float a = 1.0 - smoothstep(-blur, blur, d);
        float4 c = unpack(q.color0);
        return float4(c.rgb, 1.0) * (c.a * a);
    }
    if (kind == 2) // text (SDF, 0.5 = edge)
    {
        float2 uv = lerp(q.uv.xy, q.uv.zw, box01);
        float s = glyphAtlas.SampleLevel(linearClamp, uv, 0);
        float a = saturate((s - 0.5) * q.sdfScale + 0.5);
        float4 c = unpack(q.color0);
        return float4(c.rgb, 1.0) * (c.a * a);
    }

    float4 fill;
    if (kind == 1) // image (multiplied by color)
    {
        float2 uv = lerp(q.uv.xy, q.uv.zw, box01);
        fill = imageAtlas.SampleLevel(linearClamp, uv, 0) * unpack(q.color0);
        float4 bright = unpack(q.color1); // brightness: toward white or black by its alpha
        fill.rgb = lerp(fill.rgb, bright.rgb, bright.a);
    }
    else
    {
        uint paint = (q.flags >> 4) & 0x3;
        float4 c0 = unpack(q.color0);
        fill = c0;
        if (paint == 1)
        {
            float2 a = q.gradient.xy, b = q.gradient.zw;
            float2 ab = b - a;
            float t = saturate(dot(box01 - a, ab) / max(dot(ab, ab), 1e-6));
            fill = lerp(c0, unpack(q.color1), t);
        }
        else if (paint == 2)
        {
            float t = saturate(length((box01 - q.gradient.xy) / max(q.gradient.zw, 1e-4)));
            fill = lerp(c0, unpack(q.color1), t);
        }
    }

    // Inner border: from the outer edge to strokeWidth inward
    float inner = q.strokeWidth > 0.0 ? saturate(0.5 - (d + q.strokeWidth)) : coverage;
    float strokeCoverage = coverage - inner;
    float4 stroke = unpack(q.strokeColor);
    float3 rgb = fill.rgb * fill.a * inner + stroke.rgb * stroke.a * strokeCoverage;
    float alpha = fill.a * inner + stroke.a * strokeCoverage;
    return float4(rgb, alpha);
}
