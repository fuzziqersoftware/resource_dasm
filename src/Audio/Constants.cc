#include "Constants.hh"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <format>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <vector>

#include "../AudioCodecs.hh"

using namespace std;

namespace ResourceDASM {
namespace Audio {

uint8_t note_for_name(const char* name) {
  char letter = 0;
  bool sharp = false;

  if ((name[0] == 'A') || (name[0] == 'a') ||
      (name[0] == 'B') || (name[0] == 'b') ||
      (name[0] == 'C') || (name[0] == 'c') ||
      (name[0] == 'D') || (name[0] == 'd') ||
      (name[0] == 'E') || (name[0] == 'e') ||
      (name[0] == 'F') || (name[0] == 'f') ||
      (name[0] == 'G') || (name[0] == 'g')) {
    letter = toupper(name[0]);
  } else {
    throw out_of_range("note does not exist");
  }

  if (name[1] == '#') {
    // B and E don't have sharps
    if ((letter == 'B') || (letter == 'E')) {
      throw out_of_range("note does not have a sharp");
    }
    sharp = true;

  } else if (name[1] == 'b') {
    // C and F don't have flats
    if ((letter == 'C') || (letter == 'F')) {
      throw out_of_range("note does not have a flat");
    }

    sharp = true;
    letter = (letter == 'A') ? 'G' : (letter - 1);
  }

  char* endptr = NULL;
  long octave = strtol(&name[1 + sharp], &endptr, 10);
  if ((octave == 0) && (errno == EINVAL)) {
    throw out_of_range("no octave given");
  }

  if (octave < 0) {
    throw out_of_range("note out of range");
  }
  if (octave > 10) {
    throw out_of_range("note out of range");
  }

  uint8_t note;
  switch (letter) {
    case 'C':
      note = 0;
      break;
    case 'D':
      note = 2;
      break;
    case 'E':
      note = 4;
      break;
    case 'F':
      note = 5;
      break;
    case 'G':
      note = 7;
      break;
    case 'A':
      note = 9;
      break;
    case 'B':
      note = 11;
      break;
    default:
      throw logic_error("letter is invalid");
  }

  if (sharp) {
    note += 1;
  }
  note += (12 * octave);

  if (note > 0x7F) {
    throw out_of_range("note out of range");
  }
  return note;
}

const char* name_for_note(uint8_t note) {
  if (note >= 0x80) {
    return "invalid-note";
  }

  static const char* name_table[0x80] = {
      // clang-format off
      "C0", "C#0", "D0", "D#0", "E0", "F0", "F#0", "G0", "G#0", "A0", "A#0", "B0",
      "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1",
      "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
      "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
      "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
      "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5",
      "C6", "C#6", "D6", "D#6", "E6", "F6", "F#6", "G6", "G#6", "A6", "A#6", "B6",
      "C7", "C#7", "D7", "D#7", "E7", "F7", "F#7", "G7", "G#7", "A7", "A#7", "B7",
      "C8", "C#8", "D8", "D#8", "E8", "F8", "F#8", "G8", "G#8", "A8", "A#8", "B8",
      "C9", "C#9", "D9", "D#9", "E9", "F9", "F#9", "G9", "G#9", "A9", "A#9", "B9",
      "C10", "C#10", "D10", "D#10", "E10", "F10", "F#10", "G10",
      // clang-format on
  };
  return name_table[note];
}

double frequency_for_note(uint8_t note) {
  static const double freq_table[0x80] = {
      // clang-format off
      // C0             C#0               D0                D#0
      8.1757989156,     8.6619572180,     9.1770239974,     9.7227182413,
      10.3008611535,    10.9133822323,    11.5623257097,    12.2498573744,
      12.9782717994,    13.7500000000,    14.5676175474,    15.4338531643,
      // C1             C#1               D1                D#1
      16.3515978313,    17.3239144361,    18.3540479948,    19.4454364826,
      20.6017223071,    21.8267644646,    23.1246514195,    24.4997147489,
      25.9565435987,    27.5000000000,    29.1352350949,    30.8677063285,
      // C2             C#2               D2                D#2
      32.7031956626,    34.6478288721,    36.7080959897,    38.8908729653,
      41.2034446141,    43.6535289291,    46.2493028390,    48.9994294977,
      51.9130871975,    55.0000000000,    58.2704701898,    61.7354126570,
      // C3             C#3               D3                D#3
      65.4063913251,    69.2956577442,    73.4161919794,    77.7817459305,
      82.4068892282,    87.3070578583,    92.4986056779,    97.9988589954,
      103.8261743950,   110.0000000000,   116.5409403795,   123.4708253140,
      // C4             C#4               D4                D#4
      130.8127826503,   138.5913154884,   146.8323839587,   155.5634918610,
      164.8137784564,   174.6141157165,   184.9972113558,   195.9977179909,
      207.6523487900,   220.0000000000,   233.0818807590,   246.9416506281,
      // C5             C#5               D5                D#5
      261.6255653006,   277.1826309769,   293.6647679174,   311.1269837221,
      329.6275569129,   349.2282314330,   369.9944227116,   391.9954359817,
      415.3046975799,   440.0000000000,   466.1637615181,   493.8833012561,
      // C6             C#6               D6                D#6
      523.2511306012,   554.3652619537,   587.3295358348,   622.2539674442,
      659.2551138257,   698.4564628660,   739.9888454233,   783.9908719635,
      830.6093951599,   880.0000000000,   932.3275230362,   987.7666025122,
      // C7             C#7               D7                D#7
      1046.5022612024,  1108.7305239075,  1174.6590716696,  1244.5079348883,
      1318.5102276515,  1396.9129257320,  1479.9776908465,  1567.9817439270,
      1661.2187903198,  1760.0000000000,  1864.6550460724,  1975.5332050245,
      // C8             C#8               D8                D#8
      2093.0045224048,  2217.4610478150,  2349.3181433393,  2489.0158697766,
      2637.0204553030,  2793.8258514640,  2959.9553816931,  3135.9634878540,
      3322.4375806396,  3520.0000000000,  3729.3100921447,  3951.0664100490,
      // C9             C#9               D9                D#9
      4186.009044809,   4434.922095630,   4698.636286678,   4978.031739553,
      5274.040910605,   5587.651702928,   5919.910763386,   6271.926975708,
      6644.875161279,   7040.000000000,   7458.620234756,   7902.132834658,
      // C10            C#10              D10               D#10
      8372.0180896192,  8869.8441912599,  9397.2725733570,  9956.0634791066,
      10548.0818212118, 11175.3034058561, 11839.8215267723, 12543.8539514160,
      // clang-format on
  };
  if (note >= 0x80) {
    throw invalid_argument("note does not exist");
  }
  return freq_table[note];
}

} // namespace Audio
} // namespace ResourceDASM
