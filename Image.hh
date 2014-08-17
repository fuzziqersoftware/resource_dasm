#ifndef __IMAGE_HH
#define __IMAGE_HH

#include <map>
#include <stdint.h>

// an Image represents a drawing canvas. this class is fairly simple; it
// supports reading/writing individual pixels, drawing lines, and saving the
// image as a PPM or Windows BMP file.
class Image {
private:
  int width;
  int height;
  uint8_t* data;

  void _Load(FILE* f);

public:
  Image() = delete;
  Image(int x, int y, bool c = true);
  Image(const Image&);
  Image(FILE* f);
  Image(const char* filename);
  ~Image();

  const Image& operator=(const Image& other);

  inline int Width() const { return width; }
  inline int Height() const { return height; }

  enum ImageFormat {
    GrayscalePPM = 0,
    ColorPPM = 1,
    WindowsBitmap = 2,
  };

  static const char* MimeTypeForFormat(ImageFormat format);

  int Save(FILE* f, ImageFormat format) const;
  int Save(const char* filename, ImageFormat format) const;

  // read/write functions
  void Clear(uint8_t r, uint8_t g, uint8_t b);
  int ReadPixel(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) const;
  int WritePixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);

  // drawing functions
  void DrawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b);
  void DrawHorizontalLine(int x1, int x2, int y, uint8_t r, uint8_t g, uint8_t b);
  void DrawVerticalLine(int x, int y1, int y2, uint8_t r, uint8_t g, uint8_t b);
  void DrawText(int x, int y, int* width, int* height, uint8_t r, uint8_t g,
      uint8_t b, uint8_t br, uint8_t bg, uint8_t bb, uint8_t ba, const char* fmt, ...);
  void Blit(const Image& source, int x, int y, int w, int h, int sx, int sy);
  void MaskBlit(const Image& source, int x, int y, int w, int h, int sx, int sy, uint8_t r, uint8_t g, uint8_t b);
  void FillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
};

#endif // __IMAGE_HH
