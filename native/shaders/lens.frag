#version 300 es
// hyprlens: a magnifier drawn by the compositor, centred on the pointer.
//
// Drawn over a square box centred on the pointer, so the lens centre is always
// uv (0.5, 0.5). `tex` is a copy of the frame rendered so far. Because the lens
// is drawn by the compositor, it is never part of what it magnifies.
//
// Sampling happens in framebuffer pixel space (gl_FragCoord), which stays
// correct under any monitor rotation or flip: this fragment's offset from the
// box centre is recovered from the screen-space derivatives of v_texcoord.
precision highp float;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

uniform vec2  boxSize;    // size of the drawn box, physical px
uniform vec2  pointerOff; // pointer minus lens centre, physical px (nonzero when the lens is held on screen)
uniform float radius;     // lens half-size, physical px
uniform float zoom;       // magnification
uniform float roundness;  // 1 circle, 0 square (corners keep a small radius)
uniform float cornerPx;   // corner radius of the square lens, physical px
uniform float rimPx;      // rim thickness, physical px
uniform vec3  accent;    // theme accent colour
uniform float openAmt;   // 0 solid dot .. 1 fully open
uniform int   style;     // outline: 0 ring, 1 none, 2 glow, 3 double
uniform float pxScale;   // monitor scale, so glow and gap sizes stay logical
uniform float alpha;      // 0..1, for fading in and out
uniform sampler2D tex;    // copy of the frame so far
uniform vec2  fbSize;     // its size in pixels

float roundedBoxSDF(vec2 p, vec2 halfSize, float r) {
    r = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Signed distance to the lens outline, blending square -> circle.
float lensSDF(vec2 d) {
    float r = mix(cornerPx, radius, roundness);
    return roundedBoxSDF(d, vec2(radius), r);
}

void main() {
    // Offset from the lens centre, screen orientation, physical px.
    vec2 d = (v_texcoord - 0.5) * boxSize;

    // The same offset in framebuffer pixels, whatever the monitor transform.
    mat2 duv     = mat2(dFdx(v_texcoord), dFdy(v_texcoord));
    vec2 fbOff   = inverse(duv) * (v_texcoord - 0.5);
    vec2 fbCentre = gl_FragCoord.xy - fbOff;

    float sdf  = lensSDF(d);
    float aa   = 1.0;
    float body = 1.0 - smoothstep(-aa, aa, sdf);     // inside the lens
    if (body <= 0.0 && sdf > rimPx + 28.0 * pxScale) discard;

    // Magnified view.
    vec2 fbShift = inverse(duv) * (pointerOff / boxSize);
    vec2 src = fbCentre + fbShift + fbOff / zoom;
    vec3 view = texture(tex, clamp(src / fbSize, vec2(0.0), vec2(1.0))).rgb;

    // Rim: the theme's accent colour, a solid ring just inside the edge.
    float ring = 1.0 - smoothstep(-aa, aa, abs(sdf + rimPx * 0.5) - rimPx * 0.5);

    // Double: a second, thin ring just inside the first, once the lens is open.
    float inner = 0.0;
    if (style == 3) {
        float gap = 3.0 * pxScale, thin = max(1.0, pxScale);
        inner = (1.0 - smoothstep(-aa, aa, abs(sdf + rimPx + gap + thin * 0.5) - thin * 0.5)) * openAmt;
    }

    // Glow: the outline colour bleeding outward from the rim.
    float glow = 0.0;
    if (style == 2)
        glow = exp(-max(sdf, 0.0) / (7.0 * pxScale)) * 0.6 * openAmt * (1.0 - body);

    // Soft shadow outside the lens.
    float shadow = (1.0 - smoothstep(0.0, 6.0 * pxScale, max(sdf, 0.0))) * (style == 2 ? 0.2 : 0.35);

    vec3  col = view;
    // Crosshair at the magnified pointer spot, only while the lens is held off
    // the pointer (the real cursor isn't over it then).
    float held = smoothstep(4.0, 24.0, length(pointerOff));
    float arm  = 7.0, th = 1.0;
    float cx = (1.0 - step(th, abs(d.x))) * (1.0 - step(arm, abs(d.y)));
    float cy = (1.0 - step(th, abs(d.y))) * (1.0 - step(arm, abs(d.x)));
    float cross = max(cx, cy) * body * held * openAmt;
    col = mix(col, accent, cross * 0.9);
    float a = body;
    col = mix(col, accent, ring);
    col = mix(col, accent, inner * 0.9);
    a   = max(a, max(ring, inner));
    // glow and shadow only where nothing else is drawn
    vec4  outc = vec4(col * a, a);                    // premultiplied
    float ga   = glow * (1.0 - a);
    outc += vec4(accent * ga, ga);
    float sa = shadow * (1.0 - outc.a);
    outc.a  += sa;
    fragColor = outc * alpha;
}
