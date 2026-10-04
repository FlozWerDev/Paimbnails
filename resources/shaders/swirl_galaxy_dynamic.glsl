// spiral galaxy: pixels rotate by distance-dependent angle with glowing arms
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
    vec2 delta = v_texCoord - u_cursor;
    float dist = length(delta);
    float t = mod(u_time, 628.318);

    float swirl = (u_intensity * 0.5 + u_click * 1.5) * smoothstep(0.7, 0.0, dist);
    float a = swirl / (dist + 0.15) + t * 0.3;
    float s = sin(a);
    float c = cos(a);
    vec2 rot = vec2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
    vec4 color = texture2D(u_texture, clamp(u_cursor + rot, 0.0, 1.0));

    float ang = atan(rot.y, rot.x);
    float arm = sin(ang * 2.0 + dist * 18.0 - t * 1.5) * 0.5 + 0.5;
    float arms = pow(arm, 3.0) * smoothstep(0.6, 0.05, dist);
    color.rgb += mix(vec3(0.5, 0.4, 1.0), vec3(1.0, 0.7, 0.9), arm) * arms * u_intensity * 0.1;

    color.rgb += vec3(1.0, 0.95, 0.8) * smoothstep(0.05, 0.0, dist) * 0.4;

    gl_FragColor = color * v_fragmentColor;
}
