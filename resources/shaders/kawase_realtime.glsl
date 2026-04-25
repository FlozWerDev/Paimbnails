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
    float blurAmount = u_intensity * 4.0 + 1.5;

    vec2 hp = (blurAmount * 0.5) * texelSize;
    vec2 off = blurAmount * texelSize;

    // inner ring: Dual Kawase core (weight 4 center + 4 diagonals)
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;
    color += texture2D(u_texture, v_texCoord - hp).rgb;
    color += texture2D(u_texture, v_texCoord + hp).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(hp.x, -hp.y)).rgb;
    color += texture2D(u_texture, v_texCoord - vec2(hp.x, -hp.y)).rgb;

    // cardinal ring (weight 2 each)
    color += texture2D(u_texture, v_texCoord + vec2(-off.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2( off.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, -off.y)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0,  off.y)).rgb * 2.0;

    // outer diagonal ring (weight 1 each)
    color += texture2D(u_texture, v_texCoord + off).rgb;
    color += texture2D(u_texture, v_texCoord - off).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(off.x, -off.y)).rgb;
    color += texture2D(u_texture, v_texCoord - vec2(off.x, -off.y)).rgb;

    gl_FragColor = vec4(color / 24.0, 1.0) * v_fragmentColor;
}
