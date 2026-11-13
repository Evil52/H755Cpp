from pathlib import Path
import argparse
import hashlib
import json
import subprocess

parser = argparse.ArgumentParser()
project = Path(__file__).resolve().parents[1]
parser.add_argument('--toolchain', type=Path, default=project / '.tools/arm-gcc-16.2.0-cm7')
parser.add_argument('--output', type=Path, default=project / 'build/toolchain-checks')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
compiler = args.toolchain / 'bin/arm-none-eabi-g++'
commands = []

def run(arguments, input_text=None):
    result = subprocess.run([str(a) for a in arguments], input=input_text, text=True, capture_output=True)
    commands.append({'command': [str(a) for a in arguments], 'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
    if result.returncode:
        (args.output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        raise RuntimeError(result.stderr or result.stdout)
    return result.stdout

version = run([compiler, '-dumpfullversion']).strip()
target = run([compiler, '-dumpmachine']).strip()
if version != '16.2.0' or target != 'arm-none-eabi':
    raise RuntimeError(f'Unexpected compiler: {version} {target}')

flags = ['-std=c++26', '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-fcontracts', '-fcontract-evaluation-semantic=enforce', '-D_GLIBCXX_ASSERTIONS=1', '-fno-exceptions', '-fno-rtti']
preprocessor = run([compiler, *flags, '-dM', '-E', '-x', 'c++', '-'], '#include <version>\n#include <contracts>\n#include <span>\n')
wanted = {'__cplusplus', '__cpp_contracts', '__cpp_lib_contracts', '__cpp_lib_replaceable_contract_violation_handler', '__cpp_lib_hardened_span', '__cpp_lib_hardened_array', '__GLIBCXX__', '_GLIBCXX_RELEASE', '_GLIBCXX_ASSERTIONS', '__ARM_ARCH', '__ARM_PCS_VFP'}
macros = {}
for line in preprocessor.splitlines():
    fields = line.split(maxsplit=2)
    if len(fields) == 3 and fields[1] in wanted:
        macros[fields[1]] = fields[2]

object_file = args.output / 'arm_cpp26.o'
run([compiler, *flags, '-O0', '-ffreestanding', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-c', project / 'tools/probes/arm_cpp26.cpp', '-o', object_file])
undefined = run([args.toolchain / 'bin/arm-none-eabi-nm', '-C', '-u', object_file])
attributes = run([args.toolchain / 'bin/arm-none-eabi-readelf', '-A', object_file])
link_source = args.output / 'link_only.cpp'
link_source.write_text('int positive(int value) pre(value > 0) { return value; }\nint main() { volatile int value = 1; return positive(value) - 1; }\n')
elf = args.output / 'link_only.elf'
run([compiler, *flags, '-O2', link_source, '--specs=nosys.specs', '-lstdc++exp', '-o', elf])
header = run([args.toolchain / 'bin/arm-none-eabi-readelf', '-h', elf])
if 'ARM' not in header:
    raise RuntimeError('Linked output is not an ARM ELF')

report = {
    'compiler_version': version,
    'target': target,
    'compiler_sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
    'runtime_profile': 'Cortex-M7 Thumb, FPv5-D16, hard-float; single runtime variant',
    'feature_macros': macros,
    'native_contracts_compile': True,
    'native_contracts_link': True,
    'object_undefined_symbols': undefined.splitlines(),
    'arm_attributes': attributes,
    'library_assertion_reference': '__glibcxx_assert_fail' in undefined,
    'target_runtime_execution': 'not run',
    'hardware_fault_action': 'not verified',
    'link_only_image': 'compiler/runtime linkage fixture; not STM32 firmware, never flash',
    'm4_runtime': 'not provided by this single-variant runtime; requires separate verified build'
}
(args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
(args.output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
print(json.dumps(report, indent=2))
