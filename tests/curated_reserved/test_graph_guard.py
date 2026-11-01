#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 the sco-core contributors
"""Exercise the curated target checks against real configure-time mutations."""
import argparse
import pathlib
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--generator', required=True)
    parser.add_argument('--platform', default='')
    args = parser.parse_args()
    cases = {
        'hook': ('target_link_libraries(test_curated_reserved_host PRIVATE sco_hook)', 'changed LINK_LIBRARIES'),
        'source': ('target_sources(curated_reserved_module PRIVATE tests/test_runtime.cpp)', 'changed SOURCES'),
        'link': ('target_link_options(curated_reserved_module PRIVATE "unexpected-link-option")', 'changed LINK_OPTIONS'),
    }
    with tempfile.TemporaryDirectory(prefix='sco-curated-graph-') as temp:
        root = pathlib.Path(temp)
        for name, (mutation, expected) in cases.items():
            script = root / (name + '.cmake')
            script.write_text('function(sco_test_inject)\n  ' + mutation +
                              '\nendfunction()\ncmake_language(DEFER CALL sco_test_inject)\n', encoding='utf-8')
            command = [args.cmake, '-S', args.source, '-B', str(root / name), '-G', args.generator,
                       '-DSCO_GAME_SC=OFF', '-DSCO_BUILD_TESTS=ON',
                       '-DCMAKE_PROJECT_INCLUDE=' + str(script)]
            if args.platform:
                command.extend(['-A', args.platform])
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, errors='replace')
            if result.returncode == 0 or expected not in result.stdout:
                raise RuntimeError(name + ' was not rejected for the expected reason:\n' + result.stdout)
            print('PASS:', name, expected)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
