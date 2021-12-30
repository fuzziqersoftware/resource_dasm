#!/usr/bin/env python3

import subprocess
import sys

# Note: if you're not me, don't expect this to work.

def main():
  subprocess.check_call(['rm', '-rf', 'release'])
  subprocess.check_call(['mkdir', 'release'])

  executables = [
    'resource_dasm',
    'dc_dasm',
    'ferazel_render',
    'hypercard_dasm',
    'infotron_render',
    'mohawk_dasm',
    'gamma_zee_render',
    'mshines_render',
    'sc2k_render',
    'step_on_it_render',
    'realmz_dasm',
    'bt_render',
    'harry_render',
  ]

  for executable in executables:
    subprocess.check_call(['cp', executable, 'release/' + executable])
    subprocess.check_call(['codesign', '--force', '--verify', '--verbose',
        '--sign', 'Developer ID Application: Martin Michelsen',
        'release/' + executable])

  subprocess.check_call(['cp', 'README.md', 'release/README.md'])

  subprocess.check_call(['zip', '-r', 'release.zip', 'release'])


if __name__ == '__main__':
  sys.exit(main())
