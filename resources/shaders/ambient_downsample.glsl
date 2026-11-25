// Dynamic Ambient background downsample shader.
// Converts framebuffer samples into clean linear-ish RGBA for CPU centroid analysis.
#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;
varying vec4 v_fragmentColor;
uniform sampler2D u_texture;

void main() {
    vec4 c = texture2D(u_texture, v_texCoord);
    gl_FragColor = vec4(clamp(c.rgb, 0.0, 1.0), 1.0);
}
