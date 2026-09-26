// half-downsample for object tracing; alpha-weighted average avoids dark fringes.

#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texel;

void main() {
    vec2 base = v_texCoord - u_texel * 0.5;
    vec4 s0 = texture2D(u_texture, base);
    vec4 s1 = texture2D(u_texture, base + vec2(u_texel.x, 0.0));
    vec4 s2 = texture2D(u_texture, base + vec2(0.0, u_texel.y));
    vec4 s3 = texture2D(u_texture, base + u_texel);

    float weight = s0.a + s1.a + s2.a + s3.a;
    vec3 color = s0.rgb * s0.a + s1.rgb * s1.a + s2.rgb * s2.a + s3.rgb * s3.a;
    gl_FragColor = vec4(weight > 0.0 ? color / weight : vec3(0.0), weight * 0.25);
}
