// Liquid Glass plate. a_texCoord carries the plate-local position in node
// units (the quad reaches past the shape for the shadow). It keeps high
// precision where the GPU has it: the bezel and the 1 px edge need it.
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
#define GLASS_P highp
#else
#define GLASS_P mediump
#endif
#else
#define GLASS_P
#endif

attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texCoord;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
#else
varying vec4 v_fragmentColor;
#endif
varying GLASS_P vec2 v_local;

void main()
{
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_local = a_texCoord;
}
