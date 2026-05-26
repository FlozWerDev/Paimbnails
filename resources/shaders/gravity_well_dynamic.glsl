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
    
    // Gravitational lensing effect
    float mass = u_intensity * 0.03 * (1.0 + u_click * 2.0);
    float bend = mass / (dist * dist + 0.01);
    bend *= smoothstep(0.8, 0.0, dist);
    
    // Spiral infall
    float spiralAngle = bend * 0.5 + u_time * 0.5;
    float s = sin(spiralAngle);
    float c = cos(spiralAngle);
    vec2 spiralDelta = vec2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
    
    // Pull toward center
    uv = u_cursor + spiralDelta * (1.0 - bend * 0.3);
    
    vec4 color = texture2D(u_texture, uv);
    
    // Event horizon darkening
    float horizon = smoothstep(0.05, 0.0, dist) * u_click;
    color.rgb *= 1.0 - horizon * 0.8;
    
    // Accretion disk glow
    float ring = smoothstep(0.08, 0.06, abs(dist - 0.07)) * u_click;
    color.rgb += vec3(1.0, 0.6, 0.2) * ring * u_intensity * 0.15;
    
    // Gravitational redshift
    float redshift = bend * 0.3;
    color.r += redshift * 0.1;
    color.b -= redshift * 0.05;
    
    gl_FragColor = color * v_fragmentColor;
}
