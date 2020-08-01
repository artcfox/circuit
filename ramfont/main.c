#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <png.h>
#include <math.h>

int decode_png(char* pngfile, uint8_t* buffer, size_t bufferlen, uint32_t* w, uint32_t* h)
{
  printf("Opening: \"%s\"\n", pngfile);

  // decode PNG file
  FILE *fp = fopen(pngfile, "rb");
  if (!fp) {
    fprintf(stderr, "Error: Unable to open \"%s\"\n", pngfile);
    return -1;
  }

  unsigned char header[8]; // NUM_SIG_BYTES = 8
  fread(header, 1, sizeof(header), fp);
  if (png_sig_cmp(header, 0, sizeof(header))) {
    fprintf(stderr, "'%s' is not a png file\n", pngfile);
    fclose(fp);
    return -1;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                               (png_voidp)NULL, NULL, NULL);
  if (!png_ptr) {
    fprintf(stderr, "png_create_read_struct() failed\n");
    fclose(fp);
    return -1;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    fprintf(stderr, "png_create_info_struct() failed\n");
    png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    fclose(fp);
    return -1;
  }

  png_infop end_info = png_create_info_struct(png_ptr);
  if (!end_info) {
    fprintf(stderr, "png_create_info_struct() failed\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    fclose(fp);
    return -1;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    fprintf(stderr, "longjmp() called\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
    fclose(fp);
    return -1;
  }

  png_init_io(png_ptr, fp);
  png_set_sig_bytes(png_ptr, sizeof(header));
  png_read_info(png_ptr, info_ptr);

  png_uint_32 width, height;
  int bit_depth, color_type, interlace_method, compression_method,
    filter_method;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
               &interlace_method, &compression_method, &filter_method);

  /* do any transformations necessary here */
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png_ptr);

  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png_ptr);

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png_ptr);

  if (bit_depth == 16)
    png_set_strip_16(png_ptr);

  png_color_16 black_bg = {0, 0, 0, 0, 0};
  png_set_background(png_ptr, &black_bg, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1);

  png_read_update_info(png_ptr, info_ptr);
  /* end transformations */

  png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);
  png_uint_32 pixel_size = rowbytes / width;

  if (height > PNG_UINT_32_MAX / sizeof(png_byte))
    png_error(png_ptr, "Image is too tall to process in memory");
  if (width > PNG_UINT_32_MAX / pixel_size)
    png_error(png_ptr, "Image is too wide to process in memory");

  png_bytep data = (png_bytep)png_malloc(png_ptr, height * rowbytes);
  png_bytepp row_pointers = (png_bytepp)png_malloc(png_ptr, height * sizeof(png_bytep));

  for (png_uint_32 i = 0; i < height; ++i)
    row_pointers[i] = &data[i * rowbytes];

  png_set_rows(png_ptr, info_ptr, row_pointers);
  png_read_image(png_ptr, row_pointers);

  // data now contains the byte data in the format RGB
  int retval = 0;

  if (bufferlen < height * rowbytes) {
    fprintf(stderr, "The buffer passed to decode_png is not large enough for '%s'.\n", pngfile);
    retval = -1;
  } else {
    memcpy(buffer, data, height * rowbytes);
    *w = width;
    *h = height;
  }

  png_free(png_ptr, row_pointers);
  png_free(png_ptr, data);

  png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
  fclose(fp);

  return retval;
}

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} __attribute__ ((packed)) PIXEL_DATA;

bool isWhite(const PIXEL_DATA* p)
{
  return ((p->r == 255) &&
          (p->g == 255) &&
          (p->b == 255));
}

#define BUF_SIZE (1024 * 1024)

int main(int argc, char *argv[]) {
  uint8_t* buf = malloc(BUF_SIZE);
  
  uint32_t width = 0;
  uint32_t height = 0;
  decode_png(argv[1], buf, BUF_SIZE, &width, &height);

  if (height != 8) {
    printf("Sorry, the input image must be 8 pixels tall\n");
    return -1;
  }

  uint8_t font[width];
  memset(&font, 0, width);
  size_t pos = 0;
  
  // Loop over every tile
  for (uint32_t i = 0; i < width / 8; i++) {
      for (uint32_t h = 0; h < height; h++) {
	for (uint32_t w = 0; w < 8; w++) {
	  PIXEL_DATA pixel = {
	    *(buf + h * width * 3 + (i * 8 + w) * 3 + 0),
	    *(buf + h * width * 3 + (i * 8 + w) * 3 + 1),
	    *(buf + h * width * 3 + (i * 8 + w) * 3 + 2),
	  };
	  if (isWhite(&pixel)) {
	    printf("#");
	    font[pos] |= (1 << w);
	  } else {
	    printf(" ");
	  }
	}
	printf("\n");
	pos++;
      }
      printf("\n");
  }

  printf("Copy and paste the following array into your Uzebox game:\n\n");
  
  printf("const uint8_t myramfont[] PROGMEM = {\n  ");
  for (size_t i = 0; i < width; i++) {
    printf("0x%02x, ", font[i]);
    if ((i + 1) % 8 == 0) {
      printf("\n");
      if (i != width - 1)
	printf("  ");
    }
  }
  printf("};\n\n");

  free(buf);
  return 0;
}
