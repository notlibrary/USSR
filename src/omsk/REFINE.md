# Omsk — refinement ideas

**Today:** image thumbnails in pure USSR. It reads PNG (every colour type
and bit depth, not interlaced) and sequential Huffman JPEG (greyscale and
YCbCr, 1x/2x/4x sampling, restart intervals). It writes a baseline JPEG
at quality 85 fitted into a square box. It uses scaled IDCT decoding
(1/8, 1/4, 1/2), row-by-row PNG inflate, base64 both ways and a pixel
limit. A 2048x1536 JPEG takes about 4 s and a PNG of that size about
12 s.

## Formats in
- Interlaced PNG (Adam7)
- Progressive JPEG (browsers and phones produce it often)
- CMYK and YCCK JPEG (Adobe), with an APP14 marker check
- EXIF orientation: rotate or flip the thumbnail by the orientation tag
- GIF (first frame, or all frames for animation), BMP, WebP lossless and
  lossy, QOI, PPM/PGM (Petersburg's image widget takes P6)
- ICC profiles: at least sRGB conversion for Adobe RGB / Display P3
  images
- Gamma (`gAMA`) and `sRGB` chunks in PNG

## Formats out
- PNG output (lossless thumbnails for screenshots and diagrams), with a
  deflate encoder
- WebP or AVIF output (smaller thumbnails)
- PPM P6 output, so Petersburg can show Omsk images directly
- Choice of JPEG quality and chroma subsampling (4:2:0 for photos)
- Progressive JPEG output
- Optimised Huffman tables (a two-pass encode) for smaller files

## Operations
- General resize to any width/height, not only a square box: fit, fill
  (crop), stretch
- Better filters: bilinear, bicubic, Lanczos, alongside the 4x4 box
  average
- Crop, rotate 90/180/270, flip
- Smart crop (centre of interest by edge density) for square thumbnails
- Sharpen after downscaling
- Greyscale, brightness/contrast, and a blur preview (blurhash or a
  low-quality placeholder)
- Perceptual hash (aHash/dHash/pHash) for Novgorod's duplicate detection
- Dominant colour extraction (for placeholders)
- Alpha kept in PNG thumbnails, with a choice of background colour
- Watermark overlay
- Metadata reading (EXIF date and camera), plus stripping on re-encode

## Speed
- Integer-only IDCT (AAN) and fast colour conversion tables
- 1/8 decode used more aggressively, plus DC-only previews
- Paeth filter fast path (the README notes Paeth rows cost the most)
- Inflate with larger lookup tables
- A cache of thumbnails by input hash
- Background worker tasks for Novgorod, so a slow decode never holds a
  request

## Safety
- Limits on decompression ratio and on chunk count, as well as pixel
  count
- A fuzz corpus (truncated, malformed, hostile files) run in CI
- A time budget per image with a clean abort

## API
- `omsk_decode(img): bytes` to a pixel buffer struct, `omsk_encode(b):
  img fmt opts`, and `omsk_resize`
- Streaming decode by rows for callers that only need statistics
- A CLI: `omsk.su thumb|info|convert|hash in out [options]`

## Tests
- Pixel-exact comparisons with a reference decoder for every PNG test
  suite image (PngSuite)
- JPEG conformance images (the IJG test set)
- Benchmarks tracked per release

## Beyond thumbnails
- Animated output: a few frames of a GIF or video as an animated
  thumbnail
- SVG rasterising of a safe subset (paths, shapes, text), so vector
  uploads get previews
- PDF first-page preview and text-file previews rendered with Pobeda
- TIFF, ICO, TGA and PSD (flattened) input
- HEIC/AVIF input, at least dimensions and a refusal with a clear
  reason
- 16-bit and HDR handling: tone-map to 8 bits with a stated rule
- Image composition: paste one image onto another (sprites, contact
  sheets of a thread's images for Novgorod's gallery)
- Text drawing onto images with a TrueType font (captions, placeholders
  for missing files)
- Charts and QR codes generated as images
- Colour quantisation and dithering (palette PNG or GIF output)
- Histogram, mean colour and "is it mostly blank" checks

## Quality and control
- Chroma upsampling by interpolation (the README notes Omsk repeats
  chroma where others smooth it)
- Gamma-correct (linear light) resizing
- Dithering when reducing 16-bit to 8-bit
- JPEG quality chosen by a target file size
- Lossless JPEG transforms: rotate or crop without re-encoding
- Keep or copy selected metadata (copyright, orientation) on request
- Progress callback and cancel for long decodes
- A second API result with timings for each stage, to guide tuning
