# Changelog

## 0.2.0
Refine pass 1 from REFINE.md.
- `image.su`: whole images as btrees (`w`, `h`, flat `rgb`), with
  `omsk_decode` (PNG, JPEG, QOI, 24/32-bit BMP, PPM/PGM in ASCII and
  binary, any maxval) and `omsk_decode_fit` (the fast scaled decode);
  `omsk_sniff` and `omsk_refusal` name GIF, WebP, TIFF, ICO, HEIC and
  AVIF and say why they are refused.
- `omsk_save(img fmt opts)`: JPEG with a quality option, lossless PNG
  (stored deflate, CRC and Adler checked by Omsk's own reader), QOI,
  BMP, PPM and PGM. `omsk_jpeg_quality(q)` rescales the IJG tables for
  `omsk_encode` and `omsk_thumb` too.
- Operations: `omsk_resize` (stretch, fit, fill with centre crop; nearest,
  box, bilinear), `omsk_crop`, `omsk_rotate` (90/180/270), `omsk_flip`,
  `omsk_smart_square` (crop around the most edges), `omsk_grey`,
  `omsk_adjust` (brightness, contrast), `omsk_blur`, `omsk_sharpen`
  (unsharp mask), `omsk_quantize` (levels, Floyd-Steinberg).
- Analysis: `omsk_ahash`, `omsk_dhash`, `omsk_hamming` (duplicate
  detection for Novgorod), `omsk_mean_color`, `omsk_dominant_color`,
  `omsk_histogram`, `omsk_is_blank`.
- Composition: `omsk_paste` with opacity (watermarks) and
  `omsk_contact_sheet`.
- EXIF: `omsk_exif` (orientation, date, make, model), `omsk_orient` and
  `omsk_thumb_oriented`, which turns phone photos the right way up.
- `omsk_thumb_cached`: thumbnails kept on disk by the SHA-256 of the
  input.
- `cli.su`: `info`, `thumb`, `convert` (by the output extension, with a
  JPEG quality) and `hash`.
- `tests/omsk_image_test.su`: 53 checks, under ussr and russia.su.

## 0.1.0
- First version: thumbnails in pure USSR. PNG (all colour types and bit
  depths, not interlaced) and sequential Huffman JPEG (greyscale, YCbCr,
  any 1x/2x/4x sampling, restart intervals) in; a baseline JPEG fitted
  into a square box out. Scaled JPEG decoding at 1/8, 1/4 and 1/2,
  row-by-row PNG inflate, base64 both ways, a pixel limit.
- Novgorod's thumbnails: the server makes them with Omsk into
  `thumbs/N.svg` instead of trusting the browser's.
