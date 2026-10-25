#version 440

// One direction of a separable Gaussian blur (9 taps, as 5 bilinear fetches).
// NeonBloom runs it twice per glow level: horizontally, then vertically, on a downscaled texture.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // Distance between the taps in texture coordinates, along the blur direction
    vec2 texelStep;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    // Weights and offsets of a 9 tap Gaussian folded into bilinear fetches
    const float w0 = 0.2270270270;
    const float w1 = 0.3162162162;
    const float w2 = 0.0702702703;
    const float o1 = 1.3846153846;
    const float o2 = 3.2307692308;

    vec4 sum = texture(source, qt_TexCoord0) * w0;
    sum += texture(source, qt_TexCoord0 + texelStep * o1) * w1;
    sum += texture(source, qt_TexCoord0 - texelStep * o1) * w1;
    sum += texture(source, qt_TexCoord0 + texelStep * o2) * w2;
    sum += texture(source, qt_TexCoord0 - texelStep * o2) * w2;
    fragColor = sum * qt_Opacity;
}
