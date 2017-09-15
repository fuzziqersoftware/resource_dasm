#include "audio_codecs.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <vector>

using namespace std;



/* This decoder is based on the MACE decoder in libavcodec/ffmpeg.
 * See original decoder and license information at
 * https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/mace.c
 *
 * See the above URL for license information as well.
 */

static const int16_t MACEtable1[] = {-13, 8, 76, 222, 222, 76, 8, -13};

static const int16_t MACEtable3[] = {-18, 140, 140, -18};

static const int16_t MACEtable2[][4] = {
    {    37,    116,    206,    330}, {    39,    121,    216,    346},
    {    41,    127,    225,    361}, {    42,    132,    235,    377},
    {    44,    137,    245,    392}, {    46,    144,    256,    410},
    {    48,    150,    267,    428}, {    51,    157,    280,    449},
    {    53,    165,    293,    470}, {    55,    172,    306,    490},
    {    58,    179,    319,    511}, {    60,    187,    333,    534},
    {    63,    195,    348,    557}, {    66,    205,    364,    583},
    {    69,    214,    380,    609}, {    72,    223,    396,    635},
    {    75,    233,    414,    663}, {    79,    244,    433,    694},
    {    82,    254,    453,    725}, {    86,    265,    472,    756},
    {    90,    278,    495,    792}, {    94,    290,    516,    826},
    {    98,    303,    538,    862}, {   102,    316,    562,    901},
    {   107,    331,    588,    942}, {   112,    345,    614,    983},
    {   117,    361,    641,   1027}, {   122,    377,    670,   1074},
    {   127,    394,    701,   1123}, {   133,    411,    732,   1172},
    {   139,    430,    764,   1224}, {   145,    449,    799,   1280},
    {   152,    469,    835,   1337}, {   159,    490,    872,   1397},
    {   166,    512,    911,   1459}, {   173,    535,    951,   1523},
    {   181,    558,    993,   1590}, {   189,    584,   1038,   1663},
    {   197,    610,   1085,   1738}, {   206,    637,   1133,   1815},
    {   215,    665,   1183,   1895}, {   225,    695,   1237,   1980},
    {   235,    726,   1291,   2068}, {   246,    759,   1349,   2161},
    {   257,    792,   1409,   2257}, {   268,    828,   1472,   2357},
    {   280,    865,   1538,   2463}, {   293,    903,   1606,   2572},
    {   306,    944,   1678,   2688}, {   319,    986,   1753,   2807},
    {   334,   1030,   1832,   2933}, {   349,   1076,   1914,   3065},
    {   364,   1124,   1999,   3202}, {   380,   1174,   2088,   3344},
    {   398,   1227,   2182,   3494}, {   415,   1281,   2278,   3649},
    {   434,   1339,   2380,   3811}, {   453,   1398,   2486,   3982},
    {   473,   1461,   2598,   4160}, {   495,   1526,   2714,   4346},
    {   517,   1594,   2835,   4540}, {   540,   1665,   2961,   4741},
    {   564,   1740,   3093,   4953}, {   589,   1818,   3232,   5175},
    {   615,   1898,   3375,   5405}, {   643,   1984,   3527,   5647},
    {   671,   2072,   3683,   5898}, {   701,   2164,   3848,   6161},
    {   733,   2261,   4020,   6438}, {   766,   2362,   4199,   6724},
    {   800,   2467,   4386,   7024}, {   836,   2578,   4583,   7339},
    {   873,   2692,   4786,   7664}, {   912,   2813,   5001,   8008},
    {   952,   2938,   5223,   8364}, {   995,   3070,   5457,   8739},
    {  1039,   3207,   5701,   9129}, {  1086,   3350,   5956,   9537},
    {  1134,   3499,   6220,   9960}, {  1185,   3655,   6497,  10404},
    {  1238,   3818,   6788,  10869}, {  1293,   3989,   7091,  11355},
    {  1351,   4166,   7407,  11861}, {  1411,   4352,   7738,  12390},
    {  1474,   4547,   8084,  12946}, {  1540,   4750,   8444,  13522},
    {  1609,   4962,   8821,  14126}, {  1680,   5183,   9215,  14756},
    {  1756,   5415,   9626,  15415}, {  1834,   5657,  10057,  16104},
    {  1916,   5909,  10505,  16822}, {  2001,   6173,  10975,  17574},
    {  2091,   6448,  11463,  18356}, {  2184,   6736,  11974,  19175},
    {  2282,   7037,  12510,  20032}, {  2383,   7351,  13068,  20926},
    {  2490,   7679,  13652,  21861}, {  2601,   8021,  14260,  22834},
    {  2717,   8380,  14897,  23854}, {  2838,   8753,  15561,  24918},
    {  2965,   9144,  16256,  26031}, {  3097,   9553,  16982,  27193},
    {  3236,   9979,  17740,  28407}, {  3380,  10424,  18532,  29675},
    {  3531,  10890,  19359,  31000}, {  3688,  11375,  20222,  32382},
    {  3853,  11883,  21125,  32767}, {  4025,  12414,  22069,  32767},
    {  4205,  12967,  23053,  32767}, {  4392,  13546,  24082,  32767},
    {  4589,  14151,  25157,  32767}, {  4793,  14783,  26280,  32767},
    {  5007,  15442,  27452,  32767}, {  5231,  16132,  28678,  32767},
    {  5464,  16851,  29957,  32767}, {  5708,  17603,  31294,  32767},
    {  5963,  18389,  32691,  32767}, {  6229,  19210,  32767,  32767},
    {  6507,  20067,  32767,  32767}, {  6797,  20963,  32767,  32767},
    {  7101,  21899,  32767,  32767}, {  7418,  22876,  32767,  32767},
    {  7749,  23897,  32767,  32767}, {  8095,  24964,  32767,  32767},
    {  8456,  26078,  32767,  32767}, {  8833,  27242,  32767,  32767},
    {  9228,  28457,  32767,  32767}, {  9639,  29727,  32767,  32767}
};

static const int16_t MACEtable4[][2] = {
    {    64,    216}, {    67,    226}, {    70,    236}, {    74,    246},
    {    77,    257}, {    80,    268}, {    84,    280}, {    88,    294},
    {    92,    307}, {    96,    321}, {   100,    334}, {   104,    350},
    {   109,    365}, {   114,    382}, {   119,    399}, {   124,    416},
    {   130,    434}, {   136,    454}, {   142,    475}, {   148,    495},
    {   155,    519}, {   162,    541}, {   169,    564}, {   176,    590},
    {   185,    617}, {   193,    644}, {   201,    673}, {   210,    703},
    {   220,    735}, {   230,    767}, {   240,    801}, {   251,    838},
    {   262,    876}, {   274,    914}, {   286,    955}, {   299,    997},
    {   312,   1041}, {   326,   1089}, {   341,   1138}, {   356,   1188},
    {   372,   1241}, {   388,   1297}, {   406,   1354}, {   424,   1415},
    {   443,   1478}, {   462,   1544}, {   483,   1613}, {   505,   1684},
    {   527,   1760}, {   551,   1838}, {   576,   1921}, {   601,   2007},
    {   628,   2097}, {   656,   2190}, {   686,   2288}, {   716,   2389},
    {   748,   2496}, {   781,   2607}, {   816,   2724}, {   853,   2846},
    {   891,   2973}, {   930,   3104}, {   972,   3243}, {  1016,   3389},
    {  1061,   3539}, {  1108,   3698}, {  1158,   3862}, {  1209,   4035},
    {  1264,   4216}, {  1320,   4403}, {  1379,   4599}, {  1441,   4806},
    {  1505,   5019}, {  1572,   5244}, {  1642,   5477}, {  1715,   5722},
    {  1792,   5978}, {  1872,   6245}, {  1955,   6522}, {  2043,   6813},
    {  2134,   7118}, {  2229,   7436}, {  2329,   7767}, {  2432,   8114},
    {  2541,   8477}, {  2655,   8854}, {  2773,   9250}, {  2897,   9663},
    {  3026,  10094}, {  3162,  10546}, {  3303,  11016}, {  3450,  11508},
    {  3604,  12020}, {  3765,  12556}, {  3933,  13118}, {  4108,  13703},
    {  4292,  14315}, {  4483,  14953}, {  4683,  15621}, {  4892,  16318},
    {  5111,  17046}, {  5339,  17807}, {  5577,  18602}, {  5826,  19433},
    {  6086,  20300}, {  6358,  21205}, {  6642,  22152}, {  6938,  23141},
    {  7248,  24173}, {  7571,  25252}, {  7909,  26380}, {  8262,  27557},
    {  8631,  28786}, {  9016,  30072}, {  9419,  31413}, {  9839,  32767},
    { 10278,  32767}, { 10737,  32767}, { 11216,  32767}, { 11717,  32767},
    { 12240,  32767}, { 12786,  32767}, { 13356,  32767}, { 13953,  32767},
    { 14576,  32767}, { 15226,  32767}, { 15906,  32767}, { 16615,  32767}
};

static const struct {
  const int16_t* table1;
  const int16_t* table2;
  int stride;
} tables[] = {
  {MACEtable1, &MACEtable2[0][0], 4},
  {MACEtable3, &MACEtable4[0][0], 2},
  {MACEtable1, &MACEtable2[0][0], 4},
};

struct ChannelData {
  int16_t index;
  int16_t factor;
  int16_t prev2;
  int16_t previous;
  int16_t level;
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

vector<int16_t> decode_mace(const uint8_t* data, size_t size, bool stereo,
    bool is_mace3) {

  size_t num_channels = stereo ? 2 : 1;
  ChannelData channel_data[num_channels];
  vector<int16_t> result_data(size * (is_mace3 ? 3 : 6));

  size_t bytes_per_frame = (is_mace3 ? 2 : 1) * num_channels;
  size_t output_offset = 0;
  for (size_t input_offset = 0; input_offset < size;) {
    if (input_offset + bytes_per_frame > size) {
      throw runtime_error("odd number of bytes remaining");
    }

    for (size_t which_channel = 0; which_channel < num_channels; which_channel++) {
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



struct ima4_packet {
  uint16_t header;
  uint8_t data[32];

  uint16_t predictor() const {
    return this->header & 0xFF80;
  }

  uint8_t step_index() const {
    return this->header & 0x007F;
  }
};

vector<int16_t> decode_ima4(const uint8_t* data, size_t size, bool stereo) {
  static const int16_t index_table[16] = {
      -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
  static const int16_t step_table[89] = {
          7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
         19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
         50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
        130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
        337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
        876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
       2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
       5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
      15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

  if (size % (stereo ? 68 : 34)) {
    throw runtime_error("ima4 data size must be a multiple of 34 bytes");
  }
  vector<int16_t> result_data((size * 64) / 34);

  struct ChannelState {
    int32_t predictor;
    int32_t step_index;
    int32_t step;
  } channel_state[2];

  {
    const ima4_packet* base_packet = reinterpret_cast<const ima4_packet*>(data);
    channel_state[0].predictor = base_packet->predictor();
    channel_state[0].step_index = base_packet->step_index();
    channel_state[0].step = step_table[channel_state[0].step_index];
  }
  if (stereo) {
    const ima4_packet* base_packet = reinterpret_cast<const ima4_packet*>(data + 34);
    channel_state[1].predictor = base_packet->predictor();
    channel_state[1].step_index = base_packet->step_index();
    channel_state[1].step = step_table[channel_state[1].step_index];
  }

  for (size_t packet_offset = 0; packet_offset < size; packet_offset += 34) {
    const ima4_packet* packet = reinterpret_cast<const ima4_packet*>(
        data + packet_offset);
    size_t packet_index = packet_offset / 34;
    auto& channel = channel_state[stereo ? (packet_index & 1) : 0];

    // interleave stereo samples appropriately
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