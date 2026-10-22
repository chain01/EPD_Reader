"""Run reader host checks (Python 3, gcc and g++ must be on PATH)."""
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    commands = [[sys.executable, str(path)] for path in sorted((root / 'tools').glob('test_*.py'))]
    commands.append([sys.executable, '-m', 'unittest', 'discover', '-s',
                     str(root / 'tools/flash_gui'), '-p', 'test_*.py', '-v'])
    failed = []
    for command in commands:
        print('RUN ' + ' '.join(command[1:]), flush=True)
        result = subprocess.run(command, cwd=root)
        if result.returncode:
            failed.append(command[1:])
    print(f'{len(commands) - len(failed)}/{len(commands)} test commands passed', flush=True)
    for command in failed:
        print('FAILED ' + ' '.join(command))
    return bool(failed)


if __name__ == '__main__':
    sys.exit(main())
