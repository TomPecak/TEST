#version 440

// Neon composite of NeonBloom: the sharp scene plus its glow, added like light.
//
// Adding (not blending) keeps black black: where the scene and the glow are 0, the result is 0,
// so on an OLED screen only the neon and its halo light up.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // Strength of the tight halo and of the wide aura
    float nearStrength;
    float wideStrength;
    // Extra saturation of the glow: neon halos are more saturated than the tube itself
    float saturation;
    // Shift of red and blue of the tight halo, in pixels (chromatic aberration)
    float aberration;
    // 0..1: how much the CRT scanlines darken the lit pixels
    float scanlines;
    // 0..1: master switch of the neon (power on flicker)
    float power;
    vec2 resolution;
};

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D glowNear;
layout(binding = 3) uniform sampler2D glowWide;

void main()
{
    vec2 uv = qt_TexCoord0;
    vec4 scene = texture(source, uv);

    vec2 shift = vec2(aberration / resolution.x, 0.0);
    vec3 nearGlow = vec3(texture(glowNear, uv + shift).r,
                         texture(glowNear, uv).g,
                         texture(glowNear, uv - shift).b);
    vec3 wideGlow = texture(glowWide, uv).rgb;

    vec3 glow = nearGlow * nearStrength + wideGlow * wideStrength;
    float luma = dot(glow, vec3(0.299, 0.587, 0.114));
    glow = max(mix(vec3(luma), glow, saturation), 0.0);

    vec3 color = scene.rgb * mix(0.35, 1.0, power) + glow * power;

    // Scanlines: every other screen row a bit darker; black stays black (it is multiplied)
    float row = 0.5 + 0.5 * cos(uv.y * resolution.y * 3.14159265);
    color *= 1.0 - scanlines * 0.22 * row;

    fragColor = vec4(min(color, vec3(1.0)), 1.0) * qt_Opacity;
}
