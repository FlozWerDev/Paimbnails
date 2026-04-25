#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float radius = u_intensity * 4.0 + 0.5;
    vec2 step = texelSize * radius;

    // Gaussian-weighted 9x9 sampling with fractional offsets for linear filtering
    // Center
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 0.1621;

    // Primary cross (high weight)
    color += texture2D(u_texture, v_texCoord + vec2(step.x, 0.0)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord - vec2(step.x, 0.0)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, step.y)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, step.y)).rgb * 0.1408;

    // Near diagonals
    color += texture2D(u_texture, v_texCoord + vec2(step.x, step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x, step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(step.x, -step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x, -step.y) * 0.7071).rgb * 0.0911;

    // Far cross
    vec2 step2 = step * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(step2.x, 0.0)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord - vec2(step2.x, 0.0)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, step2.y)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, step2.y)).rgb * 0.0215;

    // Far diagonals
    color += texture2D(u_texture, v_texCoord + step2 * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord + vec2(-step2.x, step2.y) * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord + vec2(step2.x, -step2.y) * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord - step2 * 0.7071).rgb * 0.0108;

    // Intermediate points (linear filtering)
    vec2 step15 = step * 1.5;
    color += texture2D(u_texture, v_texCoord + vec2(step15.x, step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step15.x, step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step15.x, -step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step15.x, -step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step.x * 0.5, step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x * 0.5, step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step.x * 0.5, -step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x * 0.5, -step15.y)).rgb * 0.0156;

    gl_FragColor = vec4(color, 1.0) * v_fragmentColor;
}
