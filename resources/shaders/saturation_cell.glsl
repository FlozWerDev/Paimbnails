#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity; // saturation: 1.0 = normal
uniform float u_brightness; // brightness: 1.0 = normal

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    
    // saturation
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 grayColor = vec3(gray);
    vec3 saturated = mix(grayColor, color.rgb, u_intensity);
    
    // brightness
    vec3 finalRGB = saturated * u_brightness;
    
    gl_FragColor = vec4(finalRGB, color.a);
}
