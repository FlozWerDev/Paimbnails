// YUV→RGBA blit shader — renders YUV planes to an RGBA FBO.
// Used by VideoPlayer::resolveToRGBA() to eliminate the CPU-side
// SIMD conversion when callers need an RGBA texture (e.g. background
// video rendered via plain CCSprite without the YUV shader).
//
// This is functionally identical to yuv_fragment.glsl but outputs to
// an FBO instead of the screen, producing a reusable RGBA CCTexture2D.
#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;

uniform sampler2D u_textureY;
uniform sampler2D u_textureCb;
uniform sampler2D u_textureCr;
uniform float u_colorSpace; // 0.0 = BT.601, 1.0 = BT.709

void main() {
    // Video-range: Y [16,235] → [0,1], Cb/Cr [16,240] → [-0.5,0.5]
    float y  = (texture2D(u_textureY,  v_texCoord).r - 0.0625) * 1.1644;
    float cb = texture2D(u_textureCb, v_texCoord).r - 0.5;
    float cr = texture2D(u_textureCr, v_texCoord).r - 0.5;

    // BT.601 coefficients
    float r601 = y + 1.596 * cr;
    float g601 = y - 0.392 * cb - 0.813 * cr;
    float b601 = y + 2.017 * cb;

    // BT.709 coefficients
    float r709 = y + 1.793 * cr;
    float g709 = y - 0.213 * cb - 0.533 * cr;
    float b709 = y + 2.112 * cb;

    // Select based on uniform (branchless mix)
    float r = mix(r601, r709, u_colorSpace);
    float g = mix(g601, g709, u_colorSpace);
    float b = mix(b601, b709, u_colorSpace);

    gl_FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
