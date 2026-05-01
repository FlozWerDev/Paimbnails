precision highp float;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texelSize;

void main() {
    vec4 sum = vec4(0.0);
    sum += texture2D(u_texture, v_texCoord + vec2(-1.0, -1.0) * u_texelSize);
    sum += texture2D(u_texture, v_texCoord + vec2( 1.0, -1.0) * u_texelSize);
    sum += texture2D(u_texture, v_texCoord + vec2(-1.0,  1.0) * u_texelSize);
    sum += texture2D(u_texture, v_texCoord + vec2( 1.0,  1.0) * u_texelSize);
    gl_FragColor = sum * 0.25;
}
