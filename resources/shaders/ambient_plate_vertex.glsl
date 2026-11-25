// Dynamic Ambient plate vertex shader.
// Computes normalized host-local coordinates v_local in [0, 1] across a
// single sprite or an entire 9-slice batch.
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
#define AMB_P highp
#else
#define AMB_P mediump
#endif
#else
#define AMB_P
#endif

attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texCoord;

uniform vec2 u_origin;
uniform vec2 u_size;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif
varying AMB_P vec2 v_local;

void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
    v_local = (a_position.xy - u_origin) / max(u_size, vec2(1.0));
}
