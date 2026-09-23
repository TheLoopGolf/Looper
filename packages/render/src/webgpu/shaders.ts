import { MODE } from '../shader-common';

/** WGSL port of blend.ts / gl/shaders.ts. Golden tests compare all three. */
const BLEND_FUNCS = /* wgsl */ `
fn screenF(b: f32, s: f32) -> f32 { return b + s - b * s; }
fn colorBurnF(b: f32, s: f32) -> f32 { if (b >= 1.0) { return 1.0; } if (s <= 0.0) { return 0.0; } return 1.0 - min(1.0, (1.0 - b) / s); }
fn colorDodgeF(b: f32, s: f32) -> f32 { if (b <= 0.0) { return 0.0; } if (s >= 1.0) { return 1.0; } return min(1.0, b / (1.0 - s)); }
fn hardLightF(b: f32, s: f32) -> f32 { if (s <= 0.5) { return b * 2.0 * s; } return screenF(b, 2.0 * s - 1.0); }
fn softLightF(b: f32, s: f32) -> f32 {
  if (s <= 0.5) { return b - (1.0 - 2.0 * s) * b * (1.0 - b); }
  var d = sqrt(b);
  if (b <= 0.25) { d = ((16.0 * b - 12.0) * b + 4.0) * b; }
  return b + (2.0 * s - 1.0) * (d - b);
}
fn sepF(m: u32, b: f32, s: f32) -> f32 {
  switch (m) {
    case ${MODE.darken}u: { return min(b, s); }
    case ${MODE.multiply}u: { return b * s; }
    case ${MODE.colorBurn}u: { return colorBurnF(b, s); }
    case ${MODE.linearBurn}u: { return max(0.0, b + s - 1.0); }
    case ${MODE.lighten}u: { return max(b, s); }
    case ${MODE.screen}u: { return screenF(b, s); }
    case ${MODE.colorDodge}u: { return colorDodgeF(b, s); }
    case ${MODE.linearDodge}u: { return min(1.0, b + s); }
    case ${MODE.overlay}u: { return hardLightF(s, b); }
    case ${MODE.softLight}u: { return softLightF(b, s); }
    case ${MODE.hardLight}u: { return hardLightF(b, s); }
    case ${MODE.vividLight}u: { if (s <= 0.5) { return colorBurnF(b, 2.0 * s); } return colorDodgeF(b, 2.0 * s - 1.0); }
    case ${MODE.linearLight}u: { return clamp(b + 2.0 * s - 1.0, 0.0, 1.0); }
    case ${MODE.pinLight}u: { if (s <= 0.5) { return min(b, 2.0 * s); } return max(b, 2.0 * s - 1.0); }
    case ${MODE.hardMix}u: { return select(0.0, 1.0, round(b * 255.0) + round(s * 255.0) >= 255.0); }
    case ${MODE.difference}u: { return abs(b - s); }
    case ${MODE.exclusion}u: { return b + s - 2.0 * b * s; }
    case ${MODE.subtract}u: { return max(0.0, b - s); }
    case ${MODE.divide}u: { if (s <= 0.0) { return select(1.0, 0.0, b <= 0.0); } return min(1.0, b / s); }
    default: { return s; }
  }
}
fn lumF(c: vec3f) -> f32 { return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b; }
fn clipColor(cin: vec3f) -> vec3f {
  var c = cin;
  let l = lumF(c);
  let n = min(c.r, min(c.g, c.b));
  let x = max(c.r, max(c.g, c.b));
  if (n < 0.0) { c = l + (c - l) * l / (l - n); }
  if (x > 1.0) { c = l + (c - l) * (1.0 - l) / (x - l); }
  return c;
}
fn setLum(c: vec3f, l: f32) -> vec3f { return clipColor(c + (l - lumF(c))); }
fn satF(c: vec3f) -> f32 { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
fn setSat(c: vec3f, s: f32) -> vec3f {
  let n = min(c.r, min(c.g, c.b));
  let x = max(c.r, max(c.g, c.b));
  if (x <= n) { return vec3f(0.0); }
  return (c - n) * s / (x - n);
}
fn lum8(c: vec3f) -> f32 { let q = round(c * 255.0); return 30.0 * q.r + 59.0 * q.g + 11.0 * q.b; }
fn blendRGB(m: u32, b: vec3f, s: vec3f) -> vec3f {
  switch (m) {
    case ${MODE.hue}u: { return setLum(setSat(s, satF(b)), lumF(b)); }
    case ${MODE.saturation}u: { return setLum(setSat(b, satF(s)), lumF(b)); }
    case ${MODE.color}u: { return setLum(s, lumF(b)); }
    case ${MODE.luminosity}u: { return setLum(b, lumF(s)); }
    case ${MODE.darkerColor}u: { return select(b, s, lum8(s) < lum8(b)); }
    case ${MODE.lighterColor}u: { return select(b, s, lum8(s) > lum8(b)); }
    default: { return vec3f(sepF(m, b.r, s.r), sepF(m, b.g, s.g), sepF(m, b.b, s.b)); }
  }
}`;

const TILE_VS = /* wgsl */ `
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
  return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}`;

/** Tile ops: binding 0 = backdrop, 1 = source, 2 = uniforms { mode, opacity }. */
export const TILE_OPS_WGSL = /* wgsl */ `
struct Params { mode: u32, opacity: f32, validMax: vec2u };
@group(0) @binding(0) var backdrop: texture_2d<f32>;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var<uniform> params: Params;
${TILE_VS}
${BLEND_FUNCS}
@fragment fn blend(@builtin(position) pos: vec4f) -> @location(0) vec4f {
  let p = vec2i(pos.xy);
  let bd = textureLoad(backdrop, p, 0);
  let sr = textureLoad(src, p, 0);
  let as_ = sr.a * params.opacity;
  if (as_ <= 0.0) { return bd; }
  let ab = bd.a;
  let bl = blendRGB(params.mode, bd.rgb, sr.rgb);
  let csp = (1.0 - ab) * sr.rgb + ab * bl;
  let ao = as_ + ab * (1.0 - as_);
  return vec4f(clamp((as_ * csp + ab * bd.rgb * (1.0 - as_)) / ao, vec3f(0.0), vec3f(1.0)), ao);
}
@fragment fn mixOp(@builtin(position) pos: vec4f) -> @location(0) vec4f {
  let p = vec2i(pos.xy);
  let a = textureLoad(backdrop, p, 0);
  let b = textureLoad(src, p, 0);
  let o = params.opacity;
  let ao = a.a * (1.0 - o) + b.a * o;
  let pc = a.rgb * a.a * (1.0 - o) + b.rgb * b.a * o;
  if (ao <= 0.0) { return vec4f(0.0); }
  return vec4f(clamp(pc / ao, vec3f(0.0), vec3f(1.0)), ao);
}
@fragment fn copyOp(@builtin(position) pos: vec4f) -> @location(0) vec4f {
  return textureLoad(src, vec2i(pos.xy), 0);
}
@fragment fn premultiply(@builtin(position) pos: vec4f) -> @location(0) vec4f {
  // Replicate edge texels beyond the document so filtering doesn't fade the border.
  let c = textureLoad(src, vec2i(min(vec2u(pos.xy), params.validMax)), 0);
  return vec4f(c.rgb * c.a, c.a);
}`;

/** Box-filter downsample of the previous mip level (display tiles are premultiplied, so this is correct). */
export const MIP_WGSL = /* wgsl */ `
@group(0) @binding(0) var prev: texture_2d<f32>;
${TILE_VS}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
  let p = vec2i(pos.xy) * 2;
  return (textureLoad(prev, p, 0) + textureLoad(prev, p + vec2i(1, 0), 0) + textureLoad(prev, p + vec2i(0, 1), 0) + textureLoad(prev, p + vec2i(1, 1), 0)) * 0.25;
}`;

/** Screen quads: tiles (textured) and the checkerboard behind the document. */
export const PRESENT_WGSL = /* wgsl */ `
struct Quad { rect: vec4f, viewport: vec2f, uvScale: vec2f, light: vec4f, dark: vec4f, checkerSize: f32 };
@group(0) @binding(0) var<uniform> q: Quad;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
struct VOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f };
@vertex fn vs(@builtin(vertex_index) i: u32) -> VOut {
  var C = array<vec2f, 6>(vec2f(0, 0), vec2f(1, 0), vec2f(0, 1), vec2f(0, 1), vec2f(1, 0), vec2f(1, 1));
  let c = C[i];
  let px = q.rect.xy + c * q.rect.zw;
  var o: VOut;
  o.pos = vec4f(px.x / q.viewport.x * 2.0 - 1.0, 1.0 - px.y / q.viewport.y * 2.0, 0.0, 1.0);
  o.uv = c * q.uvScale;
  return o;
}
@fragment fn tile(v: VOut) -> @location(0) vec4f { return textureSample(tex, samp, v.uv); }
@fragment fn checker(v: VOut) -> @location(0) vec4f {
  let c = floor(v.pos.xy / q.checkerSize);
  return select(q.dark, q.light, (c.x + c.y) % 2.0 < 1.0);
}`;
