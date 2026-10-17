stb_image 2.30, upstream commit 013ac3beddff3dbffafd5177e7972067cd2b5083
https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h
SHA-256 594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
Dual public domain / MIT license included at end.
Local change: optional STBI_JPEG_OUTPUT_ROW callback converts to a thumbnail one RGB row at a time, avoiding the full-resolution RGB buffer. Coefficient decoding, upsampling, colors and original license are unchanged. The recorded hash above identifies the upstream source before this documented patch.
Only JPEG and PNG enabled by cover_image.c; no floating-point/HDR/stdio/SIMD.
