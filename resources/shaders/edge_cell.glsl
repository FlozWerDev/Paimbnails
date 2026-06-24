#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    
    float kernel[9];
    kernel[0] = -1.0; kernel[1] = -1.0; kernel[2] = -1.0;
    kernel[3] = -1.0; kernel[4] = 8.0; kernel[5] = -1.0;
    kernel[6] = -1.0; kernel[7] = -1.0; kernel[8] = -1.0;
    
    vec4 sum = vec4(0.0);
    int index = 0;
    for (float y = -1.0; y <= 1.0; y++) {
        for (float x = -1.0; x <= 1.0; x++) {
            sum += texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel) * kernel[index];
            index++;
        }
    }
    
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
}
