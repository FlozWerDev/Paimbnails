precision mediump float;

varying vec2 v_texCoord;

uniform sampler2D u_textureY;
uniform sampler2D u_textureCb;
uniform sampler2D u_textureCr;

void main() {
    float y  = texture2D(u_textureY,  v_texCoord).r;
    float cb = texture2D(u_textureCb, v_texCoord).r - 0.5;
    float cr = texture2D(u_textureCr, v_texCoord).r - 0.5;

    // BT.601 conversion
    float r = y + 1.402 * cr;
    float g = y - 0.34414 * cb - 0.71414 * cr;
    float b = y + 1.772 * cb;

    gl_FragColor = vec4(r, g, b, 1.0);
}
