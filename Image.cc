#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cfloat>
#include <stdexcept>
#include <map>

#include <math.h>

#include "Image.hh"
#include "ImageTextFont.hh"

using namespace std;

struct WindowsBitmapFileHeader {
  uint16_t magic;
  uint32_t file_size;
  uint16_t reserved[2];
  uint32_t data_offset;
} __attribute__((packed));

struct WindowsBitmapInfoHeader {
  uint32_t header_size;
  int32_t width;
  int32_t height;
  uint16_t num_planes;
  uint16_t bit_depth;
  uint32_t compression;
  uint32_t image_size;
  int32_t x_pixels_per_meter;
  int32_t y_pixels_per_meter;
  uint32_t num_used_colors;
  uint32_t num_important_colors;
} __attribute__((packed));

struct WindowsBitmapHeader {
  WindowsBitmapFileHeader file_header;
  WindowsBitmapInfoHeader info_header;
} __attribute__((packed));



void Image::_Load(FILE* f) {
  char sig[2];

  // read signature. this will tell us what kind of file it is
  fread(sig, 2, 1, f);

  // find out what kind of image it is
  ImageFormat format;
  if (sig[0] == 'P' && sig[1] == '5') {
    format = GrayscalePPM;
  } else if (sig[0] == 'P' && sig[1] == '6') {
    format = ColorPPM;
  } else if (sig[0] == 'B' && sig[1] == 'M') {
    format = WindowsBitmap;
  }

  if (format == GrayscalePPM || format == ColorPPM) {
    unsigned char color_max = 0;
    fscanf(f, "%d", &this->width);
    fscanf(f, "%d", &this->height);
    fscanf(f, "%hhu", &color_max);
    fgetc(f); // skip the newline
    this->data = new uint8_t[width * height * 3];
    fread(this->data, width * height * (format == ColorPPM ? 3 : 1), 1, f);

    // expand grayscale data into color data if necessary
    if (format == GrayscalePPM) {
      for (int y = this->height - 1; y >= 0; y--) {
        for (int x = this->width - 1; x >= 0; x--) {
          this->data[(y * this->width + x) * 3 + 0] = this->data[y * this->width + x];
          this->data[(y * this->width + x) * 3 + 1] = this->data[y * this->width + x];
          this->data[(y * this->width + x) * 3 + 2] = this->data[y * this->width + x];
        }
      }
    }

  } else if (format == WindowsBitmap) {
    WindowsBitmapHeader header;
    fseek(f, 0, SEEK_SET);
    fread(&header, sizeof(header), 1, f);
    if (header.file_header.magic != 0x4D42) {
      fclose(f);
      throw runtime_error("bad signature in bitmap file");
    }
    if (header.info_header.bit_depth != 24) {
      fclose(f);
      throw runtime_error("can only load 24-bit bitmaps");
    }
    if (header.info_header.num_planes != 1) {
      fclose(f);
      throw runtime_error("can only load 1-plane bitmaps");
    }
    if (header.info_header.compression != 0) {
      fclose(f);
      throw runtime_error("can only load uncompressed bitmaps");
    }

    fseek(f, header.file_header.data_offset, SEEK_SET);
    this->width = header.info_header.width;
    this->height = header.info_header.height;
    this->data = new uint8_t[this->width * this->height * 3];

    int row_padding_bytes = (4 - ((this->width * 3) % 4)) % 4;
    uint8_t row_padding_data[4] = {0, 0, 0, 0};

    uint8_t* row_data = new uint8_t[this->width * 3];
    for (int y = this->height - 1; y >= 0; y--) {
      fread(row_data, this->width * 3, 1, f);
      for (int x = 0; x < this->width * 3; x += 3) {
        this->data[y * this->width * 3 + x + 2] = row_data[x];
        this->data[y * this->width * 3 + x + 1] = row_data[x + 1];
        this->data[y * this->width * 3 + x] = row_data[x + 2];
      }
      if (row_padding_bytes)
        fread(row_padding_data, row_padding_bytes, 1, f);
    }
    delete[] row_data;

  // unknown type
  } else
    throw runtime_error("unknown file type");
}



// creates a new Image with the specified dimensions, filled with black
Image::Image(int x, int y, bool c) {
  this->width = x;
  this->height = y;
  this->data = new uint8_t[this->width * this->height * 3];
  memset(this->data, 0, this->width * this->height * 3 * sizeof(uint8_t));
}

// creates a copy of an existing Image
Image::Image(const Image& im) {
  this->width = im.width;
  this->height = im.height;
  this->data = new uint8_t[this->width * this->height * 3];
  memcpy(this->data, im.data, this->width * this->height * 3 * sizeof(uint8_t));
}

// copies an existing Image
const Image& Image::operator=(const Image& im) {
  this->width = im.width;
  this->height = im.height;
  this->data = new uint8_t[this->width * this->height * 3];
  memcpy(this->data, im.data, this->width * this->height * 3 * sizeof(uint8_t));
  return *this;
}

// loads an image from the hard drive
Image::Image(FILE* f) {
  this->_Load(f);
}

// loads an image from the hard drive
Image::Image(const char* filename) {

  // woo, c-style i/o
  FILE* f;
  if (filename) {
    f = fopen(filename, "rb");
    if (!f)
      throw runtime_error("can\'t open file");
  } else
    f = stdin;

  try {
    this->_Load(f);
    if (filename)
      fclose(f);
  } catch (runtime_error& e) {
    if (filename)
      fclose(f);
    throw;
  }
}

// frees the Image
Image::~Image() {
  delete[] data;
}

const char* Image::MimeTypeForFormat(ImageFormat format) {
  switch (format) {
    case GrayscalePPM:
    case ColorPPM:
      return "image/x-portable-pixmap";
    case WindowsBitmap:
      return "image/bmp";
    default:
      return "text/plain";
  }
}

// save the image to an already-open file
int Image::Save(FILE* f, Image::ImageFormat format) const {

  switch (format) {
    case GrayscalePPM:
      throw runtime_error("can\'t save grayscale ppm files");

    case ColorPPM:
      fprintf(f, "P6 %d %d 255\n", width, height);
      fwrite(this->data, this->width * this->height * 3, 1, f);
      break;

    case WindowsBitmap: {

      int row_padding_bytes = (4 - ((this->width * 3) % 4)) % 4;
      uint8_t row_padding_data[4] = {0, 0, 0, 0};

      WindowsBitmapHeader header;
      header.file_header.magic = 0x4D42;
      header.file_header.file_size = sizeof(WindowsBitmapHeader) + (this->width * this->height * 3) + (row_padding_bytes * this->height);
      header.file_header.reserved[0] = 0;
      header.file_header.reserved[1] = 0;
      header.file_header.data_offset = sizeof(WindowsBitmapHeader);
      header.info_header.header_size = sizeof(WindowsBitmapInfoHeader);
      header.info_header.width = this->width;
      header.info_header.height = this->height;
      header.info_header.num_planes = 1;
      header.info_header.bit_depth = 24;
      header.info_header.compression = 0; // BI_RGB
      header.info_header.image_size = 0; // ok for uncompressed formats
      header.info_header.x_pixels_per_meter = 0x00000B12;
      header.info_header.y_pixels_per_meter = 0x00000B12;
      header.info_header.num_used_colors = 0;
      header.info_header.num_important_colors = 0;
      fwrite(&header, sizeof(header), 1, f);

      uint8_t* row_data = new uint8_t[this->width * 3];
      for (int y = this->height - 1; y >= 0; y--) {
        for (int x = 0; x < this->width * 3; x += 3) {
          row_data[x] = this->data[y * this->width * 3 + x + 2];
          row_data[x + 1] = this->data[y * this->width * 3 + x + 1];
          row_data[x + 2] = this->data[y * this->width * 3 + x];
        }
        fwrite(row_data, this->width * 3, 1, f);
        if (row_padding_bytes)
          fwrite(row_padding_data, row_padding_bytes, 1, f);
      }
      delete[] row_data;

      break; }

    default:
      throw runtime_error("unknown file format in Save()");
  }

  return 0;
}

// saves the Image as a PPM (P6) file. if NULL is given, writes to stdout.
// if saveFloat is true, saves float data instead of converting to unsigned
// chars.
int Image::Save(const char* filename, Image::ImageFormat format) const {

  // woo, c-style i/o
  FILE* f;
  if (filename) {
    f = fopen(filename, "wb");
    if (!f) return (-1);
  } else
    f = stdout;

  // now we have a FILE*; save it
  try {
    Save(f, format);
    if (filename)
      fclose(f);
  } catch (runtime_error& e) {
    if (filename)
      fclose(f);
    throw;
  }

  return 0;
}

// fill the entire image with this color
void Image::Clear(uint8_t r, uint8_t g, uint8_t b) {
  for (int x = 0; x < this->width * this->height; x++) {
    this->data[x * 3 + 0] = r;
    this->data[x * 3 + 1] = g;
    this->data[x * 3 + 2] = b;
  }
}

// read the specified pixel's rgb values (returns 0 on success)
int Image::ReadPixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) const {

  // check coordinates and expand if necessary
  if (x < 0 || y < 0 || x >= this->width || y >= this->height) {
    throw runtime_error("out of bounds");
  }

  // read multiple channels
  int index = (y * this->width + x) * 3;
  if (r)
    *r = this->data[index];
  if (g)
    *g = this->data[index + 1];
  if (b)
    *b = this->data[index + 2];

  return 0;
}

// write the specified pixel's rgb values (returns 0 on success)
int Image::WritePixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {

  // check coordinates
  if (x < 0 || y < 0 || x >= this->width || y >= this->height)
    return 1;

  // write channels
  int index = (y * this->width + x) * 3;
  this->data[index] = r;
  this->data[index + 1] = g;
  this->data[index + 2] = b;
  return 0;
}

// use the Bresenham algorithm to draw a line between the specified points
void Image::DrawLine(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g,
    uint8_t b) {

  // if both endpoints are outside the image, don't bother
  if ((x0 < 0 || x0 >= width || y0 < 0 || y0 >= height) &&
      (x1 < 0 || x1 >= width || y1 < 0 || y1 >= height))
    return;

  // line is too steep? then we step along y rather than x
  bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) {
    // switch x and y
    y0 ^= x0;
    x0 ^= y0;
    y0 ^= x0;
    y1 ^= x1;
    x1 ^= y1;
    y1 ^= x1;
  }

  // line is backward? then switch the points
  if (x0 > x1) {
    // switch the two points
    x1 ^= x0;
    x0 ^= x1;
    x1 ^= x0;
    y1 ^= y0;
    y0 ^= y1;
    y1 ^= y0;
  }

  // initialize variables for stepping along the line
  int dx = x1 - x0;
  int dy = abs(y1 - y0);
  double error = 0;
  double derror = (double)dy / (double)dx;
  int ystep = (y0 < y1) ? 1 : -1;
  int y = y0;

  // now walk along the line
  for (int x = x0; x <= x1; x++) {
    if (steep) {
      if (WritePixel(y, x, r, g, b))
        return;
    } else {
      if (WritePixel(x, y, r, g, b))
        return;
    }
    error += derror;

    // have we passed the center of this row? then move to the next row
    if (error >= 0.5) {
      y += ystep;
      error -= 1.0;
    }
  }
}

void Image::DrawHorizontalLine(int x1, int x2, int y, uint8_t r, uint8_t g, uint8_t b) {
  for (int x = x1; x <= x2; x++)
    if (WritePixel(x, y, r, g, b))
      break;
}

void Image::DrawVerticalLine(int x, int y1, int y2, uint8_t r, uint8_t g, uint8_t b) {
  for (int y = y1; y <= y2; y++)
    if (WritePixel(x, y, r, g, b))
      break;
}

void Image::DrawText(int x, int y, int* width, int* height, uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t bg, uint8_t bb, uint8_t ba, const char* fmt, ...) {

  char* buffer;
  va_list va;
  va_start(va, fmt);
  vasprintf(&buffer, fmt, va);
  va_end(va);
  if (!buffer)
    throw bad_alloc();

  int max_x_pos = 0;
  int x_pos = x, y_pos = y;
  for (int z = 0; buffer[z]; z++) {
    uint8_t ch = buffer[z];
    if (ch == '\r')
      continue;
    if (ch == '\n') {
      if (ba)
        this->FillRect(x_pos - 1, y_pos - 1, 1, 9, br, bg, bb, ba);
      y_pos += 8;
      x_pos = x;
      if (x_pos > max_x_pos)
        max_x_pos = x_pos;
      continue;
    }

    if (ch < 0x20 || ch > 0x7F)
      ch = 0x7F;
    ch -= 0x20;

    if (ba)
      this->FillRect(x_pos - 1, y_pos - 1, 7, 9, br, bg, bb, ba);
    for (int yy = 0; yy < 7; yy++) {
      for (int xx = 0; xx < 5; xx++) {
        if (!font[ch][yy * 5 + xx])
          continue;
        this->WritePixel(x_pos + xx, y_pos + yy, r, g, b);
      }
    }

    x_pos += 6;
  }

  if (width)
    *width = (x_pos > max_x_pos ? x_pos : max_x_pos) - x;
  if (height)
    *height = y_pos + 7 - y;

  free(buffer);
}

void Image::Blit(const Image& source, int x, int y, int w, int h, int sx,
    int sy) {

  if (w < 0)
    w = source.Width();
  if (h < 0)
    h = source.Height();

  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      uint8_t r, g, b;
      source.ReadPixel(sx + xx, sy + yy, &r, &g, &b);
      WritePixel(x + xx, y + yy, r, g, b);
    }
  }
}

void Image::MaskBlit(const Image& source, int x, int y, int w, int h, int sx,
    int sy, uint8_t r, uint8_t g, uint8_t b) {

  if (w < 0)
    w = source.Width();
  if (h < 0)
    h = source.Height();

  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      uint8_t _r, _g, _b;
      source.ReadPixel(sx + xx, sy + yy, &_r, &_g, &_b);
      if (r != _r || g != _g || b != _b)
        WritePixel(x + xx, y + yy, _r, _g, _b);
    }
  }
}

void Image::FillRect(int x, int y, int w, int h, uint8_t r, uint8_t g,
    uint8_t b, uint8_t alpha) {

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > Width())
    w = Width() - x;
  if (y + h > Height())
    h = Height() - y;

  if (alpha == 0xFF) {
    for (int yy = 0; yy < h; yy++)
      for (int xx = 0; xx < w; xx++)
        WritePixel(x + xx, y + yy, r, g, b);
  } else {
    for (int yy = 0; yy < h; yy++) {
      for (int xx = 0; xx < w; xx++) {
        uint8_t _r = 0, _g = 0, _b = 0;
        ReadPixel(x + xx, y + yy, &_r, &_g, &_b);
        _r = (alpha * (uint32_t)r + (0xFF - alpha) * (uint32_t)_r) / 0xFF;
        _g = (alpha * (uint32_t)g + (0xFF - alpha) * (uint32_t)_g) / 0xFF;
        _b = (alpha * (uint32_t)b + (0xFF - alpha) * (uint32_t)_b) / 0xFF;
        WritePixel(x + xx, y + yy, _r, _g, _b);
      }
    }
  }
}
