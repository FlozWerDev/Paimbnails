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
    vec4 color = texture2D(u_texture, v_texCoord);
    
    vec4 sum = vec4(0.0);
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, -1.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(-1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 0.0) * onePixel) * 5.0;
    sum += texture2D(u_texture, v_texCoord + vec2(1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 1.0) * onePixel) * -1.0;
    
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
}
