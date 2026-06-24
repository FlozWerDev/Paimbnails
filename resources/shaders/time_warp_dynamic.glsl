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
    
    // Time dilation: areas near cursor warp differently
    float warpZone = smoothstep(0.4, 0.0, dist);
    
    // Radial stretch/compress pulsing
    float breathe = sin(u_time * 2.0 + dist * 10.0) * 0.02 * u_intensity * 0.1;
    float stretch = 1.0 + breathe * warpZone * (1.0 + u_click * 2.0);
    
    uv = u_cursor + delta * stretch;
    
    // Temporal echo: blend with slightly offset sample
    float echoOffset = 0.01 * u_intensity * 0.1 * warpZone;
    vec2 echoUV = uv + vec2(
        sin(u_time * 3.0) * echoOffset,
        cos(u_time * 3.0) * echoOffset
    );
    
    vec4 color = texture2D(u_texture, uv);
    vec4 echo = texture2D(u_texture, echoUV);
    
    // Blend echo on click
    color.rgb = mix(color.rgb, echo.rgb, warpZone * u_click * 0.4);
    
    // Clock-like radial lines
    float angle = atan(delta.y, delta.x);
    float clock = abs(sin(angle * 6.0 + u_time * 1.0));
    float clockMask = smoothstep(0.15, 0.05, dist) * smoothstep(0.02, 0.05, dist);
    color.rgb += vec3(0.6, 0.8, 1.0) * pow(clock, 8.0) * clockMask * u_intensity * 0.08 * u_click;
    
    // Desaturation in warp zone (time slowing)
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(color.rgb, vec3(gray), warpZone * 0.2 * u_click);
    
    gl_FragColor = color * v_fragmentColor;
}
