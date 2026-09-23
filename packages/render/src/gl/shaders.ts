import { MODE } from '../shader-common';

/** Full-tile triangle; tile ops address texels with gl_FragCoord so no varyings are needed. */
export const TILE_VS = /* glsl */ `#version 300 es
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}`;

/** GLSL port of blend.ts. Keep in sync with the CPU reference; golden tests compare them. */
const BLEND_FUNCS = /* glsl */ `
float screenF(float b, float s) { return b + s - b * s; }
float colorBurnF(float b, float s) { return b >= 1.0 ? 1.0 : (s <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - b) / s)); }
float colorDodgeF(float b, float s) { return b <= 0.0 ? 0.0 : (s >= 1.0 ? 1.0 : min(1.0, b / (1.0 - s))); }
float hardLightF(float b, float s) { return s <= 0.5 ? b * 2.0 * s : screenF(b, 2.0 * s - 1.0); }
float softLightF(float b, float s) {
  if (s <= 0.5) return b - (1.0 - 2.0 * s) * b * (1.0 - b);
  float d = b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b);
  return b + (2.0 * s - 1.0) * (d - b);
}
float sepF(int m, float b, float s) {
  switch (m) {
    case ${MODE.darken}: return min(b, s);
    case ${MODE.multiply}: return b * s;
    case ${MODE.colorBurn}: return colorBurnF(b, s);
    case ${MODE.linearBurn}: return max(0.0, b + s - 1.0);
    case ${MODE.lighten}: return max(b, s);
    case ${MODE.screen}: return screenF(b, s);
    case ${MODE.colorDodge}: return colorDodgeF(b, s);
    case ${MODE.linearDodge}: return min(1.0, b + s);
    case ${MODE.overlay}: return hardLightF(s, b);
    case ${MODE.softLight}: return softLightF(b, s);
    case ${MODE.hardLight}: return hardLightF(b, s);
    case ${MODE.vividLight}: return s <= 0.5 ? colorBurnF(b, 2.0 * s) : colorDodgeF(b, 2.0 * s - 1.0);
    case ${MODE.linearLight}: return clamp(b + 2.0 * s - 1.0, 0.0, 1.0);
    case ${MODE.pinLight}: return s <= 0.5 ? min(b, 2.0 * s) : max(b, 2.0 * s - 1.0);
    case ${MODE.hardMix}: return round(b * 255.0) + round(s * 255.0) >= 255.0 ? 1.0 : 0.0;
    case ${MODE.difference}: return abs(b - s);
    case ${MODE.exclusion}: return b + s - 2.0 * b * s;
    case ${MODE.subtract}: return max(0.0, b - s);
    case ${MODE.divide}: return s <= 0.0 ? (b <= 0.0 ? 0.0 : 1.0) : min(1.0, b / s);
    default: return s;
  }
}
float lumF(vec3 c) { return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b; }
vec3 clipColor(vec3 c) {
  float l = lumF(c);
  float n = min(c.r, min(c.g, c.b));
  float x = max(c.r, max(c.g, c.b));
  if (n < 0.0) c = l + (c - l) * l / (l - n);
  if (x > 1.0) c = l + (c - l) * (1.0 - l) / (x - l);
  return c;
}
vec3 setLum(vec3 c, float l) { return clipColor(c + (l - lumF(c))); }
float satF(vec3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
vec3 setSat(vec3 c, float s) {
  float n = min(c.r, min(c.g, c.b));
  float x = max(c.r, max(c.g, c.b));
  return x <= n ? vec3(0.0) : (c - n) * s / (x - n);
}
float lum8(vec3 c) { vec3 q = round(c * 255.0); return 30.0 * q.r + 59.0 * q.g + 11.0 * q.b; }
vec3 blendRGB(int m, vec3 b, vec3 s) {
  switch (m) {
    case ${MODE.hue}: return setLum(setSat(s, satF(b)), lumF(b));
    case ${MODE.saturation}: return setLum(setSat(b, satF(s)), lumF(b));
    case ${MODE.color}: return setLum(s, lumF(b));
    case ${MODE.luminosity}: return setLum(b, lumF(s));
    case ${MODE.darkerColor}: return lum8(s) < lum8(b) ? s : b;
    case ${MODE.lighterColor}: return lum8(s) > lum8(b) ? s : b;
    default: return vec3(sepF(m, b.r, s.r), sepF(m, b.g, s.g), sepF(m, b.b, s.b));
  }
}`;

export const BLEND_FS = /* glsl */ `#version 300 es
precision highp float;
precision highp int;
uniform sampler2D u_backdrop;
uniform sampler2D u_src;
uniform int u_mode;
uniform float u_opacity;
out vec4 o;
${BLEND_FUNCS}
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec4 bd = texelFetch(u_backdrop, p, 0);
  vec4 sr = texelFetch(u_src, p, 0);
  float as = sr.a * u_opacity;
  if (as <= 0.0) { o = bd; return; }
  float ab = bd.a;
  vec3 bl = blendRGB(u_mode, bd.rgb, sr.rgb);
  vec3 csp = (1.0 - ab) * sr.rgb + ab * bl;
  float ao = as + ab * (1.0 - as);
  o = vec4(clamp((as * csp + ab * bd.rgb * (1.0 - as)) / ao, 0.0, 1.0), ao);
}`;

export const MIX_FS = /* glsl */ `#version 300 es
precision highp float;
uniform sampler2D u_backdrop;
uniform sampler2D u_src;
uniform float u_opacity;
out vec4 o;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec4 a = texelFetch(u_backdrop, p, 0);
  vec4 b = texelFetch(u_src, p, 0);
  float ao = a.a * (1.0 - u_opacity) + b.a * u_opacity;
  vec3 pc = a.rgb * a.a * (1.0 - u_opacity) + b.rgb * b.a * u_opacity;
  o = ao > 0.0 ? vec4(clamp(pc / ao, 0.0, 1.0), ao) : vec4(0.0);
}`;

/**
 * Copies a straight-alpha tile into the premultiplied display tile (which is
 * then mipmapped). Texels beyond the document edge (partial edge tiles)
 * replicate the last valid row/column so filtering doesn't fade the border.
 */
export const PREMULTIPLY_FS = /* glsl */ `#version 300 es
precision highp float;
uniform sampler2D u_src;
uniform ivec2 u_validMax;
out vec4 o;
void main() {
  vec4 c = texelFetch(u_src, min(ivec2(gl_FragCoord.xy), u_validMax), 0);
  o = vec4(c.rgb * c.a, c.a);
}`;

export const COPY_FS = /* glsl */ `#version 300 es
precision highp float;
uniform sampler2D u_src;
out vec4 o;
void main() { o = texelFetch(u_src, ivec2(gl_FragCoord.xy), 0); }`;

/** Screen-space quad; u_rect is in device pixels (x, y, w, h), u_uv the texture sub-rect. */
export const QUAD_VS = /* glsl */ `#version 300 es
uniform vec4 u_rect;
uniform vec2 u_viewport;
uniform vec2 u_uvScale;
out vec2 v_uv;
const vec2 C[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
void main() {
  vec2 c = C[gl_VertexID];
  vec2 px = u_rect.xy + c * u_rect.zw;
  v_uv = c * u_uvScale;
  gl_Position = vec4(px.x / u_viewport.x * 2.0 - 1.0, 1.0 - px.y / u_viewport.y * 2.0, 0.0, 1.0);
}`;

export const PRESENT_FS = /* glsl */ `#version 300 es
precision highp float;
uniform sampler2D u_tex;
in vec2 v_uv;
out vec4 o;
void main() { o = texture(u_tex, v_uv); }`;

export const CHECKER_FS = /* glsl */ `#version 300 es
precision highp float;
uniform vec3 u_light;
uniform vec3 u_dark;
uniform float u_size;
out vec4 o;
void main() {
  vec2 c = floor(gl_FragCoord.xy / u_size);
  o = vec4(mod(c.x + c.y, 2.0) < 1.0 ? u_light : u_dark, 1.0);
}`;
