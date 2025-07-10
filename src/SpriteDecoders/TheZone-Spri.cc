#include "Decoders.hh"

#include <string.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "../Emulators/M68KEmulator.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {

struct SpriHeader {
  // All sprites are square, so both width and height are equal to side here.
  be_uint16_t side;
  // For some reason, they also store the area, even though this value is always
  // equal to side * side.
  be_uint16_t area;
  // The TMPL says that for mask_type, 0 = mask and 1 = 68k executable code, but
  // this appears not to be the case. Every sprite in the file has 0 here, and
  // all of them contain executable code.
  uint8_t mask_type;
  uint8_t unused;
  // Variable-length fields:
  // uint8_t sprite_data[area]
  // uint8_t blitter_code[...EOF]
} __attribute__((packed));

ImageRGBA8888N decode_Spri(const string& spri_data, const vector<ColorTableEntry>& clut) {
  StringReader r(spri_data);

  const auto& header = r.get<SpriHeader>();
  if (header.area != header.side * header.side) {
    throw runtime_error("sprite is not square");
  }
  string data = r.read(header.area);
  string code = r.read(r.size() - r.where());

  // To render these sprites with accurate transparency, we have to actually
  // execute the code they contain. Fortunately, the code's interface is fairly
  // simple (and is described below). In its original mode of operation, these
  // code snippets would be writing directly to the screen buffer, so pixels in
  // the sprite that aren't copied to the screen buffer should be considered as
  // transparent in the sprite data. Since sprites may contain any valid byte,
  // we need a way to find out which pixels were written in the output after the
  // code returns - so, we call it twice: once with the actual sprite data as
  // input (so it renders normally) and once with all FF bytes as input, so we
  // can tell which bytes it actually affects in the output buffer. Then we use
  // that output as the alpha mask, and combine it with the color data from the
  // first pass to produce a sprite with correct transparency.

  auto mem = make_shared<MemoryContext>();

  // Memory map:
  // 10000000 - output color data
  // 20000000 - output alpha data
  // 40000000 - input color data (original sprite data)
  // 50000000 - input alpha data ("\xFF" * size of sprite data)
  // 80000000 - stack (4KB)
  // C0000000 - renderer code
  // F0000000 - wrapper code (entry point is here)

  // Set up the output regions
  uint32_t output_color_addr = 0x10000000;
  mem->allocate_at(output_color_addr, header.area);
  mem->memset(output_color_addr, 0, header.area);
  uint32_t output_alpha_addr = 0x20000000;
  mem->allocate_at(output_alpha_addr, header.area);
  mem->memset(output_alpha_addr, 0, header.area);

  // Set up the input regions
  uint32_t input_color_addr = 0x40000000;
  mem->allocate_at(input_color_addr, header.area);
  mem->memcpy(input_color_addr, data.data(), header.area);
  uint32_t input_alpha_addr = 0x50000000;
  mem->allocate_at(input_alpha_addr, header.area);
  mem->memset(input_alpha_addr, 0xFF, header.area);

  // Set up the stack
  const uint32_t stack_size = 0x1000;
  uint32_t stack_addr = 0x80000000;
  mem->allocate_at(stack_addr, stack_size);
  mem->memset(stack_addr, 0x00, stack_size);

  // Set up the code region
  uint32_t code_addr = 0xC0000000;
  mem->allocate_at(code_addr, code.size());
  mem->memcpy(code_addr, code.data(), code.size());

  // The sprite renderer code expects the following stack at entry time:
  // [A7+00] return addr
  // [A7+04] input row_bytes
  // [A7+08] output row_bytes
  // [A7+0C] input buffer addr
  // [A7+10] output buffer addr

  // Write a short bit of 68K code to call the sprite renderer twice.
  StringWriter wrapper_code_w;
  // pea.l [output_color_buffer]
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(output_color_addr);
  // pea.l [input_color_buffer]
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(input_color_addr);
  // push.l row_bytes
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(header.side);
  // push.l row_bytes
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(header.side);
  // jsr [code_addr]
  wrapper_code_w.put_u16b(0x4EB9);
  wrapper_code_w.put_u32b(code_addr);
  // adda.w A7, 0x10
  wrapper_code_w.put_u32b(0xDEFC0010);
  // pea.l [output_alpha_buffer]
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(output_alpha_addr);
  // pea.l [input_alpha_buffer]
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(input_alpha_addr);
  // push.l row_bytes
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(header.side);
  // push.l row_bytes
  wrapper_code_w.put_u16b(0x4879);
  wrapper_code_w.put_u32b(header.side);
  // jsr [code_addr]
  wrapper_code_w.put_u16b(0x4EB9);
  wrapper_code_w.put_u32b(code_addr);
  // reset (this terminates emulation cleanly)
  wrapper_code_w.put_u16b(0x4E70);

  // Set up the wrapper code region
  const string& wrapper_code = wrapper_code_w.str();
  uint32_t wrapper_code_addr = 0xF0000000;
  mem->allocate_at(wrapper_code_addr, wrapper_code.size());
  mem->memcpy(wrapper_code_addr, wrapper_code.data(), wrapper_code.size());

  // Set up registers
  M68KEmulator emu(mem);
  auto& regs = emu.registers();
  regs.a[7] = stack_addr + stack_size;
  regs.pc = wrapper_code_addr;

  // Run the renderer
  emu.execute();

  // The sprite renderer code has executed, giving us two buffers: one with the
  // sprite's (indexed) color data, and another with the alpha channel. Convert
  // these to an Image and return it.
  const uint8_t* output_color = mem->at<const uint8_t>(output_color_addr, header.area);
  const uint8_t* output_alpha = mem->at<const uint8_t>(output_alpha_addr, header.area);
  ImageRGBA8888N ret(header.side, header.side);
  for (size_t y = 0; y < header.side; y++) {
    for (size_t x = 0; x < header.side; x++) {
      size_t z = (y * header.side) + x;
      ret.write(x, y, clut.at(output_color[z]).c.rgba8888(output_alpha[z]));
    }
  }

  return ret;
}

} // namespace ResourceDASM
