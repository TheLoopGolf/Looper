//! Pixel kernels shared by the browser (wasm32) and desktop (native) builds.
//!
//! Kernels are pure and deterministic so golden-image tests stay stable.
//! The wasm build exposes a tiny C ABI (`alloc`/`dealloc` + kernels over
//! linear memory) so no bindgen toolchain is required. M1 only needs the
//! mip/thumbnail downsampler; filters arrive in M2.

pub const TILE_SIZE: usize = 256;

/// Halves a premultiplied RGBA8 image with a 2×2 box filter (odd edges clamp).
pub fn downsample_2x(src: &[u8], width: usize, height: usize) -> (Vec<u8>, usize, usize) {
    assert_eq!(src.len(), width * height * 4, "buffer size mismatch");
    let ow = (width / 2).max(1);
    let oh = (height / 2).max(1);
    let mut out = vec![0u8; ow * oh * 4];
    for y in 0..oh {
        let y0 = (y * 2).min(height - 1);
        let y1 = (y * 2 + 1).min(height - 1);
        for x in 0..ow {
            let x0 = (x * 2).min(width - 1);
            let x1 = (x * 2 + 1).min(width - 1);
            for c in 0..4 {
                let sum = src[(y0 * width + x0) * 4 + c] as u32
                    + src[(y0 * width + x1) * 4 + c] as u32
                    + src[(y1 * width + x0) * 4 + c] as u32
                    + src[(y1 * width + x1) * 4 + c] as u32;
                out[(y * ow + x) * 4 + c] = ((sum + 2) / 4) as u8;
            }
        }
    }
    (out, ow, oh)
}

/// Converts straight-alpha RGBA8 to premultiplied in place (round-to-nearest).
pub fn premultiply(pixels: &mut [u8]) {
    for px in pixels.chunks_exact_mut(4) {
        let a = px[3] as u32;
        for c in &mut px[..3] {
            *c = ((*c as u32 * a + 127) / 255) as u8;
        }
    }
}

/// Converts premultiplied RGBA8 to straight alpha in place.
pub fn unpremultiply(pixels: &mut [u8]) {
    for px in pixels.chunks_exact_mut(4) {
        let a = px[3] as u32;
        if a == 0 {
            px[..3].fill(0);
            continue;
        }
        for c in &mut px[..3] {
            *c = ((*c as u32 * 255 + a / 2) / a).min(255) as u8;
        }
    }
}

#[cfg(target_arch = "wasm32")]
mod wasm_abi {
    use std::alloc::{alloc as sys_alloc, dealloc as sys_dealloc, Layout};

    #[no_mangle]
    pub extern "C" fn alloc(len: usize) -> *mut u8 {
        unsafe { sys_alloc(Layout::from_size_align(len.max(1), 8).unwrap()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn dealloc(ptr: *mut u8, len: usize) {
        sys_dealloc(ptr, Layout::from_size_align(len.max(1), 8).unwrap())
    }

    #[no_mangle]
    pub unsafe extern "C" fn premultiply(ptr: *mut u8, len: usize) {
        super::premultiply(std::slice::from_raw_parts_mut(ptr, len))
    }

    #[no_mangle]
    pub unsafe extern "C" fn unpremultiply(ptr: *mut u8, len: usize) {
        super::unpremultiply(std::slice::from_raw_parts_mut(ptr, len))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn downsample_averages_blocks() {
        let src: Vec<u8> = [[0, 0, 0, 255], [4, 8, 12, 255], [8, 16, 24, 255], [12, 24, 36, 255]].concat();
        let (out, w, h) = downsample_2x(&src, 2, 2);
        assert_eq!((w, h), (1, 1));
        assert_eq!(out, vec![6, 12, 18, 255]);
    }

    #[test]
    fn downsample_handles_odd_sizes() {
        let src = vec![100u8; 3 * 3 * 4];
        let (out, w, h) = downsample_2x(&src, 3, 3);
        assert_eq!((w, h), (1, 1));
        assert!(out.iter().all(|&v| v == 100));
    }

    #[test]
    fn premultiply_roundtrip_is_stable_for_opaque_and_clears_transparent() {
        let mut px = vec![200, 100, 50, 255, 200, 100, 50, 128, 9, 9, 9, 0];
        premultiply(&mut px);
        assert_eq!(&px[0..4], &[200, 100, 50, 255]);
        assert_eq!(&px[4..8], &[100, 50, 25, 128]);
        unpremultiply(&mut px);
        assert_eq!(&px[0..4], &[200, 100, 50, 255]);
        assert_eq!(&px[4..8], &[199, 100, 50, 128]);
        assert_eq!(&px[8..12], &[0, 0, 0, 0]);
    }
}
