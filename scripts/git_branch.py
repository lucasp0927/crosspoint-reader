"""
PlatformIO pre-build script: inject git branch and short SHA into the build.

Two macros, because the version string has consumers that must not change:
CROSSPOINT_VERSION is compared against the OTA feed (OtaUpdater.cpp), reported
by the web API, and sent as the HTTP User-Agent.

  CROSSPOINT_VERSION   dev builds only, e.g. 1.1.0-dev-feat-kosync-xpath-05c6cf8
                       Release envs set this in the ini and are left alone, so
                       OTA update detection keeps working.
  CROSSPOINT_BUILD_ID  every other env, e.g. -feat-kosync-05c6cf8 (leading
                       separator included so it concatenates onto the version
                       literal). Empty for the default env, whose version
                       already carries the branch and SHA. Shown on the boot
                       splash so a device can be traced back to a build.
"""

import configparser
import os
import subprocess
import sys


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def run_git_value(project_dir, args, label):
    try:
        value = subprocess.check_output(
            ['git', *args],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()
        # Strip characters that would break a C string literal
        return ''.join(c for c in value if c not in '"\\')
    except FileNotFoundError:
        warn(f'git not found on PATH; {label} suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(
            f'git command failed (exit {e.returncode}): '
            f'{e.stderr.strip()}; {label} suffix will be "unknown"'
        )
        return 'unknown'
    except OSError as e:
        warn(
            f'OS error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'
    except Exception as e:  # pylint: disable=broad-exception-caught
        warn(
            f'Unexpected error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'


def get_git_branch(project_dir):
    branch = run_git_value(
        project_dir, ['rev-parse', '--abbrev-ref', 'HEAD'], 'branch'
    )
    # Detached HEAD has no branch name.
    if branch == 'HEAD':
        return 'detached'
    return branch


def get_git_short_sha(project_dir):
    return run_git_value(
        project_dir, ['rev-parse', '--short', 'HEAD'], 'short SHA'
    )


def get_base_version(project_dir):
    ini_path = os.path.join(project_dir, 'platformio.ini')
    if not os.path.isfile(ini_path):
        warn(f'platformio.ini not found at {ini_path}; base version will be "0.0.0"')
        return '0.0.0'
    config = configparser.ConfigParser()
    config.read(ini_path, encoding='utf-8')
    if not config.has_option('crosspoint', 'version'):
        warn('No [crosspoint] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return config.get('crosspoint', 'version')


# The splash line is centred at the panel width, so an unbounded branch name
# (e.g. fix/handle-crashes-on-very-large-epub-chapters-#2256) would run off
# both edges. Keep the head, which is the part that identifies the work.
BRANCH_DISPLAY_MAX = 24


def truncate_branch(branch):
    if len(branch) <= BRANCH_DISPLAY_MAX:
        return branch
    return branch[:BRANCH_DISPLAY_MAX - 1] + '~'


def inject_version(env):
    project_dir = env['PROJECT_DIR']
    branch = get_git_branch(project_dir)
    short_sha = get_git_short_sha(project_dir)

    if env['PIOENV'] in ('default', 'sticky'):
        # Dev builds fold the branch and SHA into the version itself, so the
        # build id would only repeat it on the splash.
        base_version = get_base_version(project_dir)
        version_string = f'{base_version}-dev-{branch}-{short_sha}'
        env.Append(CPPDEFINES=[('CROSSPOINT_VERSION', f'\\"{version_string}\\"')])
        env.Append(CPPDEFINES=[('CROSSPOINT_BUILD_ID', '\\"\\"')])
        print(f'CrossPoint build version: {version_string}')
        return

    # Release-style envs keep the CROSSPOINT_VERSION set in the ini so OTA
    # comparison stays exact; the provenance rides alongside it instead.
    build_id = f'-{truncate_branch(branch)}-{short_sha}'
    env.Append(CPPDEFINES=[('CROSSPOINT_BUILD_ID', f'\\"{build_id}\\"')])
    print(f'CrossPoint build id: {build_id}')


# PlatformIO/SCons entry point — Import and env are SCons builtins injected at runtime.
# When run directly with Python (e.g. for validation), a lightweight fake env is used
# so the git/version logic can be exercised without a full build.
try:
    Import('env')           # noqa: F821  # type: ignore[name-defined]
    inject_version(env)     # noqa: F821  # type: ignore[name-defined]
except NameError:
    class _Env(dict):
        def Append(self, **_): pass

    _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    inject_version(_Env({'PIOENV': 'default', 'PROJECT_DIR': _project_dir}))
