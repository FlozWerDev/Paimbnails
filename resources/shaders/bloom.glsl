#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec2 px = u_intensity * 2.0 / u_screenSize;
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 bloom = vec3(0.0);
    // 9-tap cross pattern: center + 4 cardinal + 4 diagonal
    vec3 s0 = texture2D(u_texture, v_texCoord + vec2(-px.x, 0.0)).rgb;
    vec3 s1 = texture2D(u_texture, v_texCoord + vec2( px.x, 0.0)).rgb;
    vec3 s2 = texture2D(u_texture, v_texCoord + vec2(0.0, -px.y)).rgb;
    vec3 s3 = texture2D(u_texture, v_texCoord + vec2(0.0,  px.y)).rgb;
    vec3 s4 = texture2D(u_texture, v_texCoord + vec2(-px.x, -px.y)).rgb;
    vec3 s5 = texture2D(u_texture, v_texCoord + vec2( px.x, -px.y)).rgb;
    vec3 s6 = texture2D(u_texture, v_texCoord + vec2(-px.x,  px.y)).rgb;
    vec3 s7 = texture2D(u_texture, v_texCoord + vec2( px.x,  px.y)).rgb;
    // extract bright parts from each tap
    float t = 0.75;
    bloom += max(s0 - t, 0.0) + max(s1 - t, 0.0) + max(s2 - t, 0.0) + max(s3 - t, 0.0);
    bloom += (max(s4 - t, 0.0) + max(s5 - t, 0.0) + max(s6 - t, 0.0) + max(s7 - t, 0.0)) * 0.7;
    bloom += max(color.rgb - t, 0.0);
    bloom *= u_intensity * 0.15;
    gl_FragColor = vec4(color.rgb + bloom, color.a) * v_fragmentColor;
}
