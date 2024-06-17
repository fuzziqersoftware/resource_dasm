#include "AudioCodecs.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <stdexcept>
#include <vector>

using namespace std;

/* This decoder is based on the MACE decoder in libavcodec/ffmpeg.
 * See original decoder and license information at
 * https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/mace.c
 */

static const int16_t mace_table_1[] = {
    -0x0D, 0x08, 0x4C, 0xDE, 0xDE, 0x4C, 0x08, -0x0D};

static const int16_t mace_table_3[] = {-0x12, 0x8C, 0x8C, -0x12};

static const int16_t mace_table_2[][4] = {
    // clang-format off
    {0x0025, 0x0074, 0x00CE, 0x014A}, {0x0027, 0x0079, 0x00D8, 0x015A},
    {0x0029, 0x007F, 0x00E1, 0x0169}, {0x002A, 0x0084, 0x00EB, 0x0179},
    {0x002C, 0x0089, 0x00F5, 0x0188}, {0x002E, 0x0090, 0x0100, 0x019A},
    {0x0030, 0x0096, 0x010B, 0x01AC}, {0x0033, 0x009D, 0x0118, 0x01C1},
    {0x0035, 0x00A5, 0x0125, 0x01D6}, {0x0037, 0x00AC, 0x0132, 0x01EA},
    {0x003A, 0x00B3, 0x013F, 0x01FF}, {0x003C, 0x00BB, 0x014D, 0x0216},
    {0x003F, 0x00C3, 0x015C, 0x022D}, {0x0042, 0x00CD, 0x016C, 0x0247},
    {0x0045, 0x00D6, 0x017C, 0x0261}, {0x0048, 0x00DF, 0x018C, 0x027B},
    {0x004B, 0x00E9, 0x019E, 0x0297}, {0x004F, 0x00F4, 0x01B1, 0x02B6},
    {0x0052, 0x00FE, 0x01C5, 0x02D5}, {0x0056, 0x0109, 0x01D8, 0x02F4},
    {0x005A, 0x0116, 0x01EF, 0x0318}, {0x005E, 0x0122, 0x0204, 0x033A},
    {0x0062, 0x012F, 0x021A, 0x035E}, {0x0066, 0x013C, 0x0232, 0x0385},
    {0x006B, 0x014B, 0x024C, 0x03AE}, {0x0070, 0x0159, 0x0266, 0x03D7},
    {0x0075, 0x0169, 0x0281, 0x0403}, {0x007A, 0x0179, 0x029E, 0x0432},
    {0x007F, 0x018A, 0x02BD, 0x0463}, {0x0085, 0x019B, 0x02DC, 0x0494},
    {0x008B, 0x01AE, 0x02FC, 0x04C8}, {0x0091, 0x01C1, 0x031F, 0x0500},
    {0x0098, 0x01D5, 0x0343, 0x0539}, {0x009F, 0x01EA, 0x0368, 0x0575},
    {0x00A6, 0x0200, 0x038F, 0x05B3}, {0x00AD, 0x0217, 0x03B7, 0x05F3},
    {0x00B5, 0x022E, 0x03E1, 0x0636}, {0x00BD, 0x0248, 0x040E, 0x067F},
    {0x00C5, 0x0262, 0x043D, 0x06CA}, {0x00CE, 0x027D, 0x046D, 0x0717},
    {0x00D7, 0x0299, 0x049F, 0x0767}, {0x00E1, 0x02B7, 0x04D5, 0x07BC},
    {0x00EB, 0x02D6, 0x050B, 0x0814}, {0x00F6, 0x02F7, 0x0545, 0x0871},
    {0x0101, 0x0318, 0x0581, 0x08D1}, {0x010C, 0x033C, 0x05C0, 0x0935},
    {0x0118, 0x0361, 0x0602, 0x099F}, {0x0125, 0x0387, 0x0646, 0x0A0C},
    {0x0132, 0x03B0, 0x068E, 0x0A80}, {0x013F, 0x03DA, 0x06D9, 0x0AF7},
    {0x014E, 0x0406, 0x0728, 0x0B75}, {0x015D, 0x0434, 0x077A, 0x0BF9},
    {0x016C, 0x0464, 0x07CF, 0x0C82}, {0x017C, 0x0496, 0x0828, 0x0D10},
    {0x018E, 0x04CB, 0x0886, 0x0DA6}, {0x019F, 0x0501, 0x08E6, 0x0E41},
    {0x01B2, 0x053B, 0x094C, 0x0EE3}, {0x01C5, 0x0576, 0x09B6, 0x0F8E},
    {0x01D9, 0x05B5, 0x0A26, 0x1040}, {0x01EF, 0x05F6, 0x0A9A, 0x10FA},
    {0x0205, 0x063A, 0x0B13, 0x11BC}, {0x021C, 0x0681, 0x0B91, 0x1285},
    {0x0234, 0x06CC, 0x0C15, 0x1359}, {0x024D, 0x071A, 0x0CA0, 0x1437},
    {0x0267, 0x076A, 0x0D2F, 0x151D}, {0x0283, 0x07C0, 0x0DC7, 0x160F},
    {0x029F, 0x0818, 0x0E63, 0x170A}, {0x02BD, 0x0874, 0x0F08, 0x1811},
    {0x02DD, 0x08D5, 0x0FB4, 0x1926}, {0x02FE, 0x093A, 0x1067, 0x1A44},
    {0x0320, 0x09A3, 0x1122, 0x1B70}, {0x0344, 0x0A12, 0x11E7, 0x1CAB},
    {0x0369, 0x0A84, 0x12B2, 0x1DF0}, {0x0390, 0x0AFD, 0x1389, 0x1F48},
    {0x03B8, 0x0B7A, 0x1467, 0x20AC}, {0x03E3, 0x0BFE, 0x1551, 0x2223},
    {0x040F, 0x0C87, 0x1645, 0x23A9}, {0x043E, 0x0D16, 0x1744, 0x2541},
    {0x046E, 0x0DAB, 0x184C, 0x26E8}, {0x04A1, 0x0E47, 0x1961, 0x28A4},
    {0x04D6, 0x0EEA, 0x1A84, 0x2A75}, {0x050D, 0x0F95, 0x1BB3, 0x2C5B},
    {0x0547, 0x1046, 0x1CEF, 0x2E55}, {0x0583, 0x1100, 0x1E3A, 0x3066},
    {0x05C2, 0x11C3, 0x1F94, 0x3292}, {0x0604, 0x128E, 0x20FC, 0x34D2},
    {0x0649, 0x1362, 0x2275, 0x372E}, {0x0690, 0x143F, 0x23FF, 0x39A4},
    {0x06DC, 0x1527, 0x259A, 0x3C37}, {0x072A, 0x1619, 0x2749, 0x3EE8},
    {0x077C, 0x1715, 0x2909, 0x41B6}, {0x07D1, 0x181D, 0x2ADF, 0x44A6},
    {0x082B, 0x1930, 0x2CC7, 0x47B4}, {0x0888, 0x1A50, 0x2EC6, 0x4AE7},
    {0x08EA, 0x1B7D, 0x30DE, 0x4E40}, {0x094F, 0x1CB7, 0x330C, 0x51BE},
    {0x09BA, 0x1DFF, 0x3554, 0x5565}, {0x0A29, 0x1F55, 0x37B4, 0x5932},
    {0x0A9D, 0x20BC, 0x3A31, 0x5D2E}, {0x0B16, 0x2231, 0x3CC9, 0x6156},
    {0x0B95, 0x23B8, 0x3F80, 0x65AF}, {0x0C19, 0x2551, 0x4256, 0x6A39},
    {0x0CA4, 0x26FB, 0x454C, 0x6EF7}, {0x0D34, 0x28B8, 0x4864, 0x73EB},
    {0x0DCB, 0x2A8A, 0x4B9F, 0x7918}, {0x0E68, 0x2C6F, 0x4EFE, 0x7E7E},
    {0x0F0D, 0x2E6B, 0x5285, 0x7FFF}, {0x0FB9, 0x307E, 0x5635, 0x7FFF},
    {0x106D, 0x32A7, 0x5A0D, 0x7FFF}, {0x1128, 0x34EA, 0x5E12, 0x7FFF},
    {0x11ED, 0x3747, 0x6245, 0x7FFF}, {0x12B9, 0x39BF, 0x66A8, 0x7FFF},
    {0x138F, 0x3C52, 0x6B3C, 0x7FFF}, {0x146F, 0x3F04, 0x7006, 0x7FFF},
    {0x1558, 0x41D3, 0x7505, 0x7FFF}, {0x164C, 0x44C3, 0x7A3E, 0x7FFF},
    {0x174B, 0x47D5, 0x7FB3, 0x7FFF}, {0x1855, 0x4B0A, 0x7FFF, 0x7FFF},
    {0x196B, 0x4E63, 0x7FFF, 0x7FFF}, {0x1A8D, 0x51E3, 0x7FFF, 0x7FFF},
    {0x1BBD, 0x558B, 0x7FFF, 0x7FFF}, {0x1CFA, 0x595C, 0x7FFF, 0x7FFF},
    {0x1E45, 0x5D59, 0x7FFF, 0x7FFF}, {0x1F9F, 0x6184, 0x7FFF, 0x7FFF},
    {0x2108, 0x65DE, 0x7FFF, 0x7FFF}, {0x2281, 0x6A6A, 0x7FFF, 0x7FFF},
    {0x240C, 0x6F29, 0x7FFF, 0x7FFF}, {0x25A7, 0x741F, 0x7FFF, 0x7FFF},
    // clang-format on
};

static const int16_t mace_table_4[][2] = {
    // clang-format off
    {0x0040, 0x00D8}, {0x0043, 0x00E2}, {0x0046, 0x00EC}, {0x004A, 0x00F6},
    {0x004D, 0x0101}, {0x0050, 0x010C}, {0x0054, 0x0118}, {0x0058, 0x0126},
    {0x005C, 0x0133}, {0x0060, 0x0141}, {0x0064, 0x014E}, {0x0068, 0x015E},
    {0x006D, 0x016D}, {0x0072, 0x017E}, {0x0077, 0x018F}, {0x007C, 0x01A0},
    {0x0082, 0x01B2}, {0x0088, 0x01C6}, {0x008E, 0x01DB}, {0x0094, 0x01EF},
    {0x009B, 0x0207}, {0x00A2, 0x021D}, {0x00A9, 0x0234}, {0x00B0, 0x024E},
    {0x00B9, 0x0269}, {0x00C1, 0x0284}, {0x00C9, 0x02A1}, {0x00D2, 0x02BF},
    {0x00DC, 0x02DF}, {0x00E6, 0x02FF}, {0x00F0, 0x0321}, {0x00FB, 0x0346},
    {0x0106, 0x036C}, {0x0112, 0x0392}, {0x011E, 0x03BB}, {0x012B, 0x03E5},
    {0x0138, 0x0411}, {0x0146, 0x0441}, {0x0155, 0x0472}, {0x0164, 0x04A4},
    {0x0174, 0x04D9}, {0x0184, 0x0511}, {0x0196, 0x054A}, {0x01A8, 0x0587},
    {0x01BB, 0x05C6}, {0x01CE, 0x0608}, {0x01E3, 0x064D}, {0x01F9, 0x0694},
    {0x020F, 0x06E0}, {0x0227, 0x072E}, {0x0240, 0x0781}, {0x0259, 0x07D7},
    {0x0274, 0x0831}, {0x0290, 0x088E}, {0x02AE, 0x08F0}, {0x02CC, 0x0955},
    {0x02EC, 0x09C0}, {0x030D, 0x0A2F}, {0x0330, 0x0AA4}, {0x0355, 0x0B1E},
    {0x037B, 0x0B9D}, {0x03A2, 0x0C20}, {0x03CC, 0x0CAB}, {0x03F8, 0x0D3D},
    {0x0425, 0x0DD3}, {0x0454, 0x0E72}, {0x0486, 0x0F16}, {0x04B9, 0x0FC3},
    {0x04F0, 0x1078}, {0x0528, 0x1133}, {0x0563, 0x11F7}, {0x05A1, 0x12C6},
    {0x05E1, 0x139B}, {0x0624, 0x147C}, {0x066A, 0x1565}, {0x06B3, 0x165A},
    {0x0700, 0x175A}, {0x0750, 0x1865}, {0x07A3, 0x197A}, {0x07FB, 0x1A9D},
    {0x0856, 0x1BCE}, {0x08B5, 0x1D0C}, {0x0919, 0x1E57}, {0x0980, 0x1FB2},
    {0x09ED, 0x211D}, {0x0A5F, 0x2296}, {0x0AD5, 0x2422}, {0x0B51, 0x25BF},
    {0x0BD2, 0x276E}, {0x0C5A, 0x2932}, {0x0CE7, 0x2B08}, {0x0D7A, 0x2CF4},
    {0x0E14, 0x2EF4}, {0x0EB5, 0x310C}, {0x0F5D, 0x333E}, {0x100C, 0x3587},
    {0x10C4, 0x37EB}, {0x1183, 0x3A69}, {0x124B, 0x3D05}, {0x131C, 0x3FBE},
    {0x13F7, 0x4296}, {0x14DB, 0x458F}, {0x15C9, 0x48AA}, {0x16C2, 0x4BE9},
    {0x17C6, 0x4F4C}, {0x18D6, 0x52D5}, {0x19F2, 0x5688}, {0x1B1A, 0x5A65},
    {0x1C50, 0x5E6D}, {0x1D93, 0x62A4}, {0x1EE5, 0x670C}, {0x2046, 0x6BA5},
    {0x21B7, 0x7072}, {0x2338, 0x7578}, {0x24CB, 0x7AB5}, {0x266F, 0x7FFF},
    {0x2826, 0x7FFF}, {0x29F1, 0x7FFF}, {0x2BD0, 0x7FFF}, {0x2DC5, 0x7FFF},
    {0x2FD0, 0x7FFF}, {0x31F2, 0x7FFF}, {0x342C, 0x7FFF}, {0x3681, 0x7FFF},
    {0x38F0, 0x7FFF}, {0x3B7A, 0x7FFF}, {0x3E22, 0x7FFF}, {0x40E7, 0x7FFF},
    // clang-format on
};

static const struct {
  const int16_t* table1;
  const int16_t* table2;
  int stride;
} tables[] = {
    {mace_table_1, &mace_table_2[0][0], 4},
    {mace_table_3, &mace_table_4[0][0], 2},
    {mace_table_1, &mace_table_2[0][0], 4},
};

struct ChannelData {
  int16_t index = 0;
  int16_t factor = 0;
  int16_t prev2 = 0;
  int16_t previous = 0;
  int16_t level = 0;
};

static int16_t clip_int16(int32_t x) {
  if (x > 0x7FFF) {
    return 0x7FFF;
  }
  if (x < -0x8000) {
    return -0x7FFF;
  }
  return x;
}

static int16_t read_table(ChannelData& channel, uint8_t value, size_t table_index) {
  int16_t current;

  size_t entry_index = ((channel.index & 0x7F0) >> 4) * tables[table_index].stride;
  if (value < tables[table_index].stride) {
    entry_index += value;
    current = tables[table_index].table2[entry_index];
  } else {
    entry_index += 2 * tables[table_index].stride - value - 1;
    current = -1 - tables[table_index].table2[entry_index];
  }

  if ((channel.index += tables[table_index].table1[value] - (channel.index >> 5)) < 0) {
    channel.index = 0;
  }

  return current;
}

vector<le_int16_t> decode_mace(
    const void* vdata, size_t size, bool stereo, bool is_mace3) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  vector<ChannelData> channel_data(stereo ? 2 : 1);
  vector<le_int16_t> result_data(size * (is_mace3 ? 3 : 6));

  size_t bytes_per_frame = (is_mace3 ? 2 : 1) * channel_data.size();
  size_t output_offset = 0;
  for (size_t input_offset = 0; input_offset < size;) {
    if (input_offset + bytes_per_frame > size) {
      throw runtime_error("odd number of bytes remaining");
    }

    for (size_t which_channel = 0; which_channel < channel_data.size(); which_channel++) {
      ChannelData& channel = channel_data[which_channel];

      if (is_mace3) {
        for (size_t k = 0; k < 2; k++) {
          uint8_t value = data[input_offset++];
          uint8_t values[3] = {static_cast<uint8_t>(value & 7),
              static_cast<uint8_t>((value >> 3) & 3),
              static_cast<uint8_t>(value >> 5)};

          for (size_t l = 0; l < 3; l++) {
            int16_t current = read_table(channel_data[which_channel], values[l], l);

            int16_t sample = clip_int16(current + channel.level);
            result_data[output_offset++] = sample;
            channel.level = sample - (sample >> 3);
          }
        }

      } else {
        uint8_t value = data[input_offset++];

        uint8_t values[3] = {static_cast<uint8_t>(value >> 5),
            static_cast<uint8_t>((value >> 3) & 3),
            static_cast<uint8_t>(value & 7)};
        for (size_t l = 0; l < 3; l++) {

          int16_t current = read_table(channel, values[l], l);

          if ((channel.previous ^ current) >= 0) {
            if (channel.factor + 506 > 32767) {
              channel.factor = 32767;
            } else {
              channel.factor += 506;
            }
          } else {
            if (channel.factor - 314 < -32768) {
              channel.factor = -32767;
            } else {
              channel.factor -= 314;
            }
          }

          current = clip_int16(current + channel.level);

          channel.level = (current * channel.factor) >> 15;
          current >>= 1;

          result_data[output_offset++] = channel.previous + channel.prev2 -
              ((channel.prev2 - current) >> 2);
          result_data[output_offset++] = channel.previous + current +
              ((channel.prev2 - current) >> 2);

          channel.prev2 = channel.previous;
          channel.previous = current;
        }
      }
    }
  }

  return result_data;
}

struct IMA4Packet {
  be_uint16_t header;
  uint8_t data[32];

  uint16_t predictor() const {
    // Note: the lack of a shift here is not a bug - these 9 bits actually do
    // store the high bits of the predictor
    return this->header & 0xFF80;
  }

  uint8_t step_index() const {
    return this->header & 0x007F;
  }
};

vector<le_int16_t> decode_ima4(const void* vdata, size_t size, bool stereo) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  static const int16_t index_table[16] = {
      -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
  static const int16_t step_table[89] = {
      7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
      19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
      50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
      130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
      337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
      876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
      2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
      5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
      15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

  if (size % (stereo ? 68 : 34)) {
    throw runtime_error("ima4 data size must be a multiple of 34 bytes");
  }
  vector<le_int16_t> result_data((size * 64) / 34);

  struct ChannelState {
    int32_t predictor;
    int32_t step_index;
    int32_t step;
  } channel_state[2];

  {
    const IMA4Packet* base_packet = reinterpret_cast<const IMA4Packet*>(data);
    channel_state[0].predictor = base_packet->predictor();
    channel_state[0].step_index = base_packet->step_index();
    channel_state[0].step = step_table[channel_state[0].step_index];
  }
  if (stereo) {
    const IMA4Packet* base_packet = reinterpret_cast<const IMA4Packet*>(data + 34);
    channel_state[1].predictor = base_packet->predictor();
    channel_state[1].step_index = base_packet->step_index();
    channel_state[1].step = step_table[channel_state[1].step_index];
  }

  for (size_t packet_offset = 0; packet_offset < size; packet_offset += 34) {
    const IMA4Packet* packet = reinterpret_cast<const IMA4Packet*>(
        data + packet_offset);
    size_t packet_index = packet_offset / 34;
    auto& channel = channel_state[stereo ? (packet_index & 1) : 0];

    // Interleave stereo samples appropriately
    size_t output_offset;
    size_t output_step = stereo ? 2 : 1;
    if (stereo) {
      output_offset = (packet_index & ~1) * 64 + (packet_index & 1);
    } else {
      output_offset = packet_index * 64;
    }

    for (size_t x = 0; x < 32; x++) {
      uint8_t value = packet->data[x];
      for (size_t y = 0; y < 2; y++) {
        uint8_t nybble = value & 0x0F;
        value >>= 4;

        int32_t diff = 0;
        if (nybble & 4) {
          diff += channel.step;
        }
        if (nybble & 2) {
          diff += channel.step >> 1;
        }
        if (nybble & 1) {
          diff += channel.step >> 2;
        }
        diff += channel.step >> 3;
        if (nybble & 8) {
          diff = -diff;
        }

        channel.predictor += diff;

        if (channel.predictor > 0x7FFF) {
          channel.predictor = 0x7FFF;
        } else if (channel.predictor < -0x8000) {
          channel.predictor = -0x8000;
        }

        result_data[output_offset] = channel.predictor;
        output_offset += output_step;

        channel.step_index += index_table[nybble];
        if (channel.step_index < 0) {
          channel.step_index = 0;
        } else if (channel.step_index > 88) {
          channel.step_index = 88;
        }
        channel.step = step_table[channel.step_index];
      }
    }
  }

  return result_data;
}

vector<le_int16_t> decode_alaw(const void* vdata, size_t size) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  vector<le_int16_t> ret(size);
  for (size_t x = 0; x < size; x++) {
    int8_t sample = static_cast<int8_t>(data[x]) ^ 0x55;
    int8_t sign = (sample & 0x80) ? -1 : 1;

    if (sign == -1) {
      sample &= 0x7F;
    }

    uint8_t shift = ((sample & 0xF0) >> 4) + 4;
    if (shift == 4) {
      ret[x] = sign * ((sample << 1) | 1);
    } else {
      ret[x] = sign * ((1 << shift) | ((sample & 0x0F) << (shift - 4)) | (1 << (shift - 5)));
    }
  }
  return ret;
}

vector<le_int16_t> decode_ulaw(const void* vdata, size_t size) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);

  static const uint16_t ULAW_BIAS = 33;

  vector<le_int16_t> ret(size);
  for (size_t x = 0; x < size; x++) {
    int8_t sample = ~static_cast<int8_t>(data[x]);

    int8_t sign = (sample & 0x80) ? -1 : 1;
    if (sign == -1) {
      sample &= 0x7F;
    }
    uint8_t shift = ((sample & 0xF0) >> 4) + 5;
    ret[x] = sign * ((1 << shift) | ((sample & 0x0F) << (shift - 4)) | (1 << (shift - 5))) - ULAW_BIAS;
  }
  return ret;
}
