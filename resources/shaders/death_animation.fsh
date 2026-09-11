#ifdef GL_ES
precision mediump float;
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif
uniform float u_progress;
uniform float u_style;
uniform vec3 u_tint;
const float PI = 3.14159265;
float band(float d, float width) { return 1.0-smoothstep(width, width+0.018, abs(d)); }
float hash(float n) { return fract(sin(n*12.9898)*437.5453); }
void main() {
    vec2 p = v_texCoord*2.0-1.0;
    float t = clamp(u_progress,0.0,1.0);
    float e = 1.0-pow(1.0-t,3.0);
    float r = length(p);
    float a = atan(p.y,p.x);
    float radius = 0.08+0.68*e;
    float light = 0.0;
    vec3 tint = u_tint;
    if (u_style < 0.5) {
        light = band(r-radius,0.018)*0.8 + exp(-r*12.0)*(1.0-t)*2.0;
        light += pow(max(0.0,cos(a*10.0+t*2.0)),24.0)*exp(-abs(r-radius*0.65)*9.0);
    } else if (u_style < 1.5) {
        light = band(r-radius*(0.65+0.22*sin(a*3.0+r*12.0-t*9.0)),0.022);
        tint = mix(tint,vec3(0.65,0.2,1.0),0.6);
    } else if (u_style < 2.5) {
        float hex = cos(mod(a+PI/6.0,PI/3.0)-PI/6.0)*r;
        light = band(hex-radius*0.8,0.014)+0.5*band(hex-radius*0.5,0.01);
        tint = 0.55+0.45*cos(vec3(0.0,2.1,4.2)+a+t*3.0);
    } else if (u_style < 3.5) {
        light = band(r-radius,0.014)+0.6*band(r-radius*0.73,0.012)+0.3*band(r-radius*0.46,0.01);
    } else if (u_style < 4.5) {
        tint = vec3(1.0,0.35+0.35*(1.0-t),0.08);
        for (int i=0;i<12;i++) {
            float n=float(i); float angle=n*2.4;
            vec2 q=p-vec2(cos(angle),sin(angle))*e*(0.25+0.5*hash(n+1.0))-vec2(0.0,t*t*0.18);
            light += exp(-dot(q,q)*1800.0)*(0.5+hash(n+3.0));
        }
    } else if (u_style < 5.5) {
        tint=vec3(0.45,0.85,1.0);
        light=pow(max(0.0,cos(a*6.0)),36.0)*(1.0-smoothstep(radius-0.08,radius,r));
        light+=band(r-radius*0.6,0.012)*pow(max(0.0,cos(a*6.0)),4.0);
    } else if (u_style < 6.5) {
        vec2 q=p; q.x+=sin(floor(q.y*18.0)*7.0+floor(t*16.0))*0.12*t;
        light=band(max(abs(q.x),abs(q.y))-radius*0.7,0.025)*step(0.3,hash(floor(q.y*24.0)+floor(t*12.0)));
        tint=mix(vec3(0.1,1.0,0.9),vec3(1.0,0.15,0.55),step(0.0,q.x));
    } else if (u_style < 7.5) {
        tint=vec3(1.0,0.65,0.15);
        light=band(r-radius*(0.8+0.1*sin(a*9.0-t*7.0)),0.035);
        light+=exp(-abs(p.y)*65.0-abs(p.x)*3.0)*(1.0-t);
    } else if (u_style < 8.5) {
        light=band(r-radius*(0.55+0.3*cos(a*5.0-t*2.0)),0.02);
        light+=0.5*band(r-radius*(0.4+0.2*cos(a*5.0+t*2.0)),0.012);
        tint=vec3(1.0,0.4,0.75);
    } else if (u_style < 9.5) {
        for(int i=0;i<3;i++) {
            float angle=float(i)*PI/3.0+t*2.0;
            vec2 q=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*p;
            light+=band(length(q*vec2(1.0,3.0))-radius,0.022)*0.65;
        }
    } else if (u_style < 10.5) {
        vec2 q=mat2(0.8,-0.6,0.6,0.8)*p;
        light=band(abs(q.x)-0.06*sin(q.y*18.0+t*8.0)*(1.0-t),0.018)*(1.0-smoothstep(radius*0.6,radius,abs(q.y)));
        light+=0.4*band(length(q*vec2(2.8,1.0))-radius,0.016);
        tint=vec3(0.6,0.3,1.0);
    } else {
        for(int i=0;i<16;i++) {
            float n=float(i); float angle=n*2.4+t*(hash(n+2.0)-0.5);
            vec2 q=p-vec2(cos(angle),sin(angle))*e*(0.25+hash(n+1.0)*0.5);
            light+=exp(-length(q)*90.0)*(0.6+0.4*sin(t*12.0+n));
        }
        tint=mix(tint,vec3(0.7,0.65,1.0),0.6);
    }
    float fade=(1.0-smoothstep(0.45,1.0,t))*smoothstep(0.0,0.045,t);
    float alpha=clamp(light*fade,0.0,0.85)*(1.0-smoothstep(0.86,0.98,r));
    gl_FragColor=vec4(tint,alpha)*v_fragmentColor;
}
