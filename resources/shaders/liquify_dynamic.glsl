#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;
uniform float u_click;

void main() {
    vec2 uv = v_texCoord;
    vec2 delta = uv - u_cursor;
    float dist = length(delta);
    
    // Liquid distortion radius
    float radius = 0.25 + u_click * 0.15;
    float falloff = smoothstep(radius, 0.0, dist);
    
    // Swirling liquid motion
    float angle = falloff * u_intensity * 0.4 * sin(u_time * 1.5);
    float s = sin(angle);
    float c = cos(angle);
    vec2 rotDelta = vec2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
    
    // Bulge effect on click
    float bulge = 1.0 + falloff * u_click * 0.3;
    rotDelta *= bulge;
    
    // Ripple waves
    float ripple = sin(dist * 30.0 - u_time * 4.0) * 0.005 * u_intensity * 0.1 * falloff;
    
    uv = u_cursor + rotDelta + normalize(delta + 0.001) * ripple;
    
    vec4 color = texture2D(u_texture, uv);
    
    // Iridescent sheen
    float sheen = falloff * 0.15 * u_intensity * 0.1;
    color.rgb += vec3(
        sin(dist * 20.0 + u_time) * 0.5 + 0.5,
        sin(dist * 20.0 + u_time + 2.0) * 0.5 + 0.5,
        sin(dist * 20.0 + u_time + 4.0) * 0.5 + 0.5
    ) * sheen;
    
    gl_FragColor = color * v_fragmentColor;
}
