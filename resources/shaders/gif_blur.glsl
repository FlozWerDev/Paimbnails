// Separable, alpha-weighted Gaussian for the image-to-objects source.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texel;
void main() {
    vec4 a = texture2D(u_texture, v_texCoord - 2.0 * u_texel);
    vec4 b = texture2D(u_texture, v_texCoord - u_texel);
    vec4 c = texture2D(u_texture, v_texCoord);
    vec4 d = texture2D(u_texture, v_texCoord + u_texel);
    vec4 e = texture2D(u_texture, v_texCoord + 2.0 * u_texel);
    float alpha = a.a + 4.0*b.a + 6.0*c.a + 4.0*d.a + e.a;
    vec3 rgb = a.rgb*a.a + 4.0*b.rgb*b.a + 6.0*c.rgb*c.a + 4.0*d.rgb*d.a + e.rgb*e.a;
    gl_FragColor = vec4(alpha > 0.0 ? rgb / alpha : vec3(0.0), alpha / 16.0);
}
