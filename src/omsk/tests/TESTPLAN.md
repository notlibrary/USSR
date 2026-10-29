# Omsk test plan

Automated (also through russia.su in `tests/run_all.sh`, about 30 s each):

- `ussr tests/omsk_test.su`: small JPEGs made by Pillow, base64 in the
  test: 4:2:0, greyscale, 4:4:4 with a restart marker after every block,
  thumbnails at full size and in an 8x8 box. Prints each size and the
  CRC-32 of the JPEG Omsk wrote, and reads that JPEG back with Omsk. A
  progressive JPEG is refused with its reason.
- `ussr tests/omsk_png_test.su`: RGBA PNG, 4-bit palette with
  transparency, 16-bit greyscale, the same way; `omsk_fit` for wide and
  tall pictures; base64 both ways and refusals; text that is not an image;
  the `omsk_max_pixels` limit.
- `ussr tests/novgorod_test.su`: a post with a fake image is refused, a
  post with a real JPEG gets its size from Omsk (not from the form) and a
  thumbnail in `thumbs/`, which goes away with the thread.

By hand, against Pillow (what was done for 0.1.0): PNG at every colour type
and depth decodes to exactly Pillow's pixels; JPEG 4:4:4 and greyscale to
within 1 level; 4:2:0 and 4:2:2 differ only where Pillow smooths colour
edges (it interpolates chroma, Omsk repeats it). Thumbnails of 640x480,
1700x1100 and 2048x1536 pictures match Pillow's box-filtered resize by
eye; every JPEG Omsk writes opens in Pillow.
