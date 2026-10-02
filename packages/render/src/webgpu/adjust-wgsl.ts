import { KERNEL } from '../shader-common';

/** WGSL port of the adjustment kernels in core/raster/adjustments.ts (applyKernel). Requires setLum/lumF. */
export const ADJUST_KERNELS_WGSL = /* wgsl */ `
fn rgbToHsl(c: vec3f) -> vec3f {
  let mx = max(c.r, max(c.g, c.b));
  let mn = min(c.r, min(c.g, c.b));
  let l = (mx + mn) / 2.0;
  if (mx == mn) { return vec3f(0.0, 0.0, l); }
  let d = mx - mn;
  var s = d / (mx + mn);
  if (l > 0.5) { s = d / (2.0 - mx - mn); }
  var h: f32;
  if (mx == c.r) { h = (c.g - c.b) / d + select(0.0, 6.0, c.g < c.b); }
  else if (mx == c.g) { h = (c.b - c.r) / d + 2.0; }
  else { h = (c.r - c.g) / d + 4.0; }
  return vec3f(h * 60.0, s, l);
}
fn hueToRgb(p: f32, q: f32, tin: f32) -> f32 {
  var t = tin;
  if (t < 0.0) { t += 1.0; }
  if (t > 1.0) { t -= 1.0; }
  if (t < 1.0 / 6.0) { return p + (q - p) * 6.0 * t; }
  if (t < 0.5) { return q; }
  if (t < 2.0 / 3.0) { return p + (q - p) * (2.0 / 3.0 - t) * 6.0; }
  return p;
}
fn fmod(a: f32, b: f32) -> f32 { return a - b * floor(a / b); }
fn hslToRgb(hsl: vec3f) -> vec3f {
  if (hsl.y <= 0.0) { return vec3f(hsl.z); }
  var q = hsl.z + hsl.y - hsl.z * hsl.y;
  if (hsl.z < 0.5) { q = hsl.z * (1.0 + hsl.y); }
  let p = 2.0 * hsl.z - q;
  let hh = fmod(fmod(hsl.x, 360.0) + 360.0, 360.0) / 360.0;
  return vec3f(hueToRgb(p, q, hh + 1.0 / 3.0), hueToRgb(p, q, hh), hueToRgb(p, q, hh - 1.0 / 3.0));
}
fn applyKernel(k: u32, P: array<vec4f, 4>, c0: vec3f, x: vec3f, rgb8: vec3f) -> vec3f {
  if (k == ${KERNEL.hueSat}u) {
    var hsl = rgbToHsl(x);
    if (P[0].w > 0.5) { hsl.x = select(P[0].x, P[0].x + 360.0, P[0].x < 0.0); hsl.y = (P[0].y + 1.0) / 2.0; }
    else { hsl.x += P[0].x; hsl.y = clamp(hsl.y * (1.0 + P[0].y), 0.0, 1.0); }
    hsl.z = select(hsl.z + (1.0 - hsl.z) * P[0].z, hsl.z * (1.0 + P[0].z), P[0].z < 0.0);
    return hslToRgb(hsl);
  }
  if (k == ${KERNEL.vibrance}u) {
    let l = 0.299 * x.r + 0.587 * x.g + 0.114 * x.b;
    let s = max(x.r, max(x.g, x.b)) - min(x.r, min(x.g, x.b));
    let f = (1.0 + P[0].x * (1.0 - s)) * (1.0 + P[0].y);
    return clamp(l + (x - l) * f, vec3f(0.0), vec3f(1.0));
  }
  if (k == ${KERNEL.preserveLum}u) { return setLum(x, lumF(c0)); }
  if (k == ${KERNEL.blackWhite}u) {
    let w0 = P[0].x; let w1 = P[0].y; let w2 = P[0].z; let w3 = P[0].w; let w4 = P[1].x; let w5 = P[1].y;
    let r = x.r; let g = x.g; let b = x.b;
    var gray: f32;
    if (r >= g && r >= b) { gray = select(g + (b - g) * w5 + (r - b) * w0, b + (g - b) * w1 + (r - g) * w0, g >= b); }
    else if (g >= r && g >= b) { gray = select(r + (b - r) * w3 + (g - b) * w2, b + (r - b) * w1 + (g - r) * w2, r >= b); }
    else { gray = select(r + (g - r) * w3 + (b - g) * w4, g + (r - g) * w5 + (b - r) * w4, r >= g); }
    gray = clamp(gray, 0.0, 1.0);
    let amt = P[2].w;
    if (amt <= 0.0) { return vec3f(gray); }
    let t = setLum(P[2].xyz, gray);
    return vec3f(gray) + (t - vec3f(gray)) * amt;
  }
  if (k == ${KERNEL.photoFilter}u) {
    let f = x * (1.0 - P[0].w + P[0].w * P[0].xyz);
    if (P[1].x > 0.5) { return setLum(f, lumF(x)); }
    return f;
  }
  if (k == ${KERNEL.threshold}u) {
    return select(vec3f(0.0), vec3f(1.0), 299.0 * rgb8.r + 587.0 * rgb8.g + 114.0 * rgb8.b >= P[0].x * 1000.0);
  }
  return x;
}`;
