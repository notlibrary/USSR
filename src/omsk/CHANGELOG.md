# Changelog

## 0.1.0
- First version: thumbnails in pure USSR. PNG (all colour types and bit
  depths, not interlaced) and sequential Huffman JPEG (greyscale, YCbCr,
  any 1x/2x/4x sampling, restart intervals) in; a baseline JPEG fitted
  into a square box out. Scaled JPEG decoding at 1/8, 1/4 and 1/2,
  row-by-row PNG inflate, base64 both ways, a pixel limit.
- Novgorod's thumbnails: the server makes them with Omsk into
  `thumbs/N.svg` instead of trusting the browser's.
