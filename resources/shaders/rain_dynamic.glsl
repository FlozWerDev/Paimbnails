#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor; // normalized 0..1 cursor/touch position

float rHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float rainLayer(vec2 uv, float speed, float density, float layer) {
    vec2 r = uv * vec2(density, 1.0);
    r.y += u_time * speed + rHash(vec2(floor(r.x), layer)) * 100.0;
    float drop = smoothstep(0.0, 0.02, fract(r.y * 0.1) - 0.97);
    float mask = smoothstep(0.45, 0.5, abs(fract(r.x) - 0.5));
    return drop * (1.0 - mask);
}

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.15;
    
    // Rain intensity varies with distance from cursor (splash effect)
    float cursorDist = length(v_texCoord - u_cursor);
    float splash = 1.0 + (1.0 - smoothstep(0.0, 0.35, cursorDist)) * 0.8;
    
    // Wind direction influenced by cursor horizontal position
    float wind = (u_cursor.x - 0.5) * 0.02 * u_intensity;
    vec2 windUV = v_texCoord + vec2(wind, 0.0);
    
    // 3 layers with cursor-influenced intensity
    float rain = rainLayer(windUV, 4.0, 80.0, 0.0)
               + rainLayer(windUV, 6.0, 120.0, 1.0) * 0.75
               + rainLayer(windUV, 8.0, 160.0, 2.0) * 0.5;
    color.rgb += vec3(0.7, 0.8, 1.0) * rain * str * str * splash;
    color.rgb *= 1.0 - str * 0.2;
    gl_FragColor = color * v_fragmentColor;
}
