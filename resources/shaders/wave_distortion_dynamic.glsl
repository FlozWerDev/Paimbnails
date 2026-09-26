#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;

void main() {
    float strength = u_intensity * 0.02;
    vec2 uv = v_texCoord;
    
    float cursorDist = length(v_texCoord - u_cursor);
    float ripple = sin(cursorDist * 30.0 - u_time * 4.0) * strength * (1.0 - smoothstep(0.0, 0.7, cursorDist));
    
    uv.x += sin(uv.y * 15.0 + u_time * 2.0) * strength * 0.6;
    uv.y += cos(uv.x * 12.0 + u_time * 1.7) * strength * 0.5;
    
    vec2 rippleDir = normalize(v_texCoord - u_cursor + 0.001);
    uv += rippleDir * ripple;
    
    vec4 color = texture2D(u_texture, uv);
    float caustic = sin(uv.x * 30.0 + u_time * 4.0) * sin(uv.y * 30.0 + u_time * 3.0);
    caustic = smoothstep(0.3, 1.0, caustic) * strength * 2.0;
    color.rgb += vec3(0.1, 0.3, 0.5) * caustic;
    
    color.rgb = mix(color.rgb, color.rgb * vec3(0.9, 0.95, 1.1), u_intensity * 0.05);
    
    gl_FragColor = color * v_fragmentColor;
}
