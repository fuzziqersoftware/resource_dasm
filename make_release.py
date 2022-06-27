#!/usr/bin/env python3

import subprocess
import sys

# Note: if you're not me, don't expect this to work.

def main():
  subprocess.check_call(['rm', '-rf', 'release'])
  subprocess.check_call(['mkdir', 'release'])

  executables = [
    'ferazel_render',
    'flashback_decomp',
    'gamma_zee_render',
    'harry_render',
    'hypercard_dasm',
    'infotron_render',
    'lemmings_render',
    'm68kexec',
    'macski_decomp',
    'mshines_render',
    'realmz_dasm',
    'render_bits',
    'render_sprite',
    'resource_dasm',
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
