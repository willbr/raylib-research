#version 330

in vec2 fragTexCoord;
out vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 resolution;

void main() {
    vec2 uv = fragTexCoord;
    vec4 color = texture2D(texture0, uv);

    // Simple blur effect to simulate wet paint
    vec4 blur = vec4(0.0);
    float blurSize = 1.0 / resolution.x; // Adjust blur radius
    int samples = 5;
    float total = 0.0;

    for (int x = -samples; x <= samples; x++) {
        for (int y = -samples; y <= samples; y++) {
            vec2 offset = vec2(float(x), float(y)) * blurSize;
            blur += texture2D(texture0, uv + offset);
            total += 1.0;
        }
    }
    blur /= total;

    // Blend original color with blurred version for a soft paint look
    fragColor = mix(color, blur, 0.5);
}
