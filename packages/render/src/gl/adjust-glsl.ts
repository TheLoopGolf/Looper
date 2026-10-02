import { KERNEL } from '../shader-common';

/** GLSL port of the adjustment kernels in core/raster/adjustments.ts (applyKernel). */
export const ADJUST_KERNELS_GLSL = /* glsl */ `
vec3 rgbToHsl(vec3 c) {
  float mx = max(c.r, max(c.g, c.b));
  float mn = min(c.r, min(c.g, c.b));
  float l = (mx + mn) / 2.0;
  if (mx == mn) return vec3(0.0, 0.0, l);
  float d = mx - mn;
  float s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
  float h;
  if (mx == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
  else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
  else h = (c.r - c.g) / d + 4.0;
  return vec3(h * 60.0, s, l);
}
float hueToRgb(float p, float q, float t) {
  if (t < 0.0) t += 1.0;
  if (t > 1.0) t -= 1.0;
  if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
  if (t < 0.5) return q;
  if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  return p;
}
vec3 hslToRgb(vec3 hsl) {
  if (hsl.y <= 0.0) return vec3(hsl.z);
  float q = hsl.z < 0.5 ? hsl.z * (1.0 + hsl.y) : hsl.z + hsl.y - hsl.z * hsl.y;
  float p = 2.0 * hsl.z - q;
  float hh = mod(mod(hsl.x, 360.0) + 360.0, 360.0) / 360.0;
  return vec3(hueToRgb(p, q, hh + 1.0 / 3.0), hueToRgb(p, q, hh), hueToRgb(p, q, hh - 1.0 / 3.0));
}
vec3 applyKernel(int k, vec4 P[4], vec3 c0, vec3 x, vec3 rgb8) {
  if (k == ${KERNEL.hueSat}) {
    vec3 hsl = rgbToHsl(x);
    if (P[0].w > 0.5) { hsl.x = P[0].x < 0.0 ? P[0].x + 360.0 : P[0].x; hsl.y = (P[0].y + 1.0) / 2.0; }
    else { hsl.x += P[0].x; hsl.y = clamp(hsl.y * (1.0 + P[0].y), 0.0, 1.0); }
    hsl.z = P[0].z < 0.0 ? hsl.z * (1.0 + P[0].z) : hsl.z + (1.0 - hsl.z) * P[0].z;
    return hslToRgb(hsl);
  }
  if (k == ${KERNEL.vibrance}) {
    float l = 0.299 * x.r + 0.587 * x.g + 0.114 * x.b;
    float s = max(x.r, max(x.g, x.b)) - min(x.r, min(x.g, x.b));
    float f = (1.0 + P[0].x * (1.0 - s)) * (1.0 + P[0].y);
    return clamp(l + (x - l) * f, 0.0, 1.0);
  }
  if (k == ${KERNEL.preserveLum}) return setLum(x, lumF(c0));
  if (k == ${KERNEL.blackWhite}) {
    float w0 = P[0].x, w1 = P[0].y, w2 = P[0].z, w3 = P[0].w, w4 = P[1].x, w5 = P[1].y;
    float r = x.r, g = x.g, b = x.b, gray;
    if (r >= g && r >= b) gray = g >= b ? b + (g - b) * w1 + (r - g) * w0 : g + (b - g) * w5 + (r - b) * w0;
    else if (g >= r && g >= b) gray = r >= b ? b + (r - b) * w1 + (g - r) * w2 : r + (b - r) * w3 + (g - b) * w2;
    else gray = r >= g ? g + (r - g) * w5 + (b - r) * w4 : r + (g - r) * w3 + (b - g) * w4;
    gray = clamp(gray, 0.0, 1.0);
    float amt = P[2].w;
    if (amt <= 0.0) return vec3(gray);
    vec3 t = setLum(P[2].xyz, gray);
    return vec3(gray) + (t - vec3(gray)) * amt;
  }
  if (k == ${KERNEL.photoFilter}) {
    vec3 f = x * (1.0 - P[0].w + P[0].w * P[0].xyz);
    return P[1].x > 0.5 ? setLum(f, lumF(x)) : f;
  }
  if (k == ${KERNEL.threshold}) {
    return 299.0 * rgb8.r + 587.0 * rgb8.g + 114.0 * rgb8.b >= P[0].x * 1000.0 ? vec3(1.0) : vec3(0.0);
  }
  return x;
}`;
