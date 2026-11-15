import contextlib
import hashlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools import sonar_local


class SonarTargetHeadersTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.project = Path(self.directory.name)
        self.toolchain = self.project / 'arm-gcc-16.2.0-cm7'
        self.compiler = self.toolchain / 'bin/arm-none-eabi-g++'
        self.compiler.parent.mkdir(parents=True)
        self.compiler.touch()
        manifest = self.project / 'docs/toolchain-validation.json'
        manifest.parent.mkdir(parents=True)
        manifest.write_text(json.dumps({
            'compiler_sha256': hashlib.sha256(self.compiler.read_bytes()).hexdigest()}))
        config = self.project / 'build/cm7-ethernet-coverage/CMakeFiles/3.31.6/CMakeCXXCompiler.cmake'
        config.parent.mkdir(parents=True)
        config.write_text(f'set(CMAKE_CXX_COMPILER "{self.compiler}")\n')
        for path in (
            'arm-none-eabi/include/c++/16.2.0',
            'arm-none-eabi/include/c++/16.2.0/arm-none-eabi',
            'arm-none-eabi/include/c++/16.2.0/backward',
            'lib/gcc/arm-none-eabi/16.2.0/include',
            'lib/gcc/arm-none-eabi/16.2.0/include-fixed',
            'arm-none-eabi/include',
        ):
            (self.toolchain / path).mkdir(parents=True, exist_ok=True)

    @patch.object(sonar_local.subprocess, 'check_output', return_value='16.2.0\n')
    def test_uses_compiler_from_target_build(self, check_output):
        self.assertEqual(sonar_local.target_toolchain(self.project), self.toolchain)
        check_output.assert_called_once_with([self.compiler, '-dumpfullversion'], text=True)

    @patch.object(sonar_local.subprocess, 'check_output', return_value='16.1.0\n')
    def test_rejects_wrong_compiler_version(self, _):
        with self.assertRaisesRegex(RuntimeError, 'pinned GCC 16.2'):
            sonar_local.target_toolchain(self.project)

    def test_rejects_changed_compiler_bytes(self):
        self.compiler.write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'differs from the pinned toolchain'):
            sonar_local.target_toolchain(self.project)

    @patch.object(sonar_local.subprocess, 'check_output', return_value='16.2.0\n')
    def test_rejects_missing_standard_headers(self, _):
        (self.toolchain / 'lib/gcc/arm-none-eabi/16.2.0/include-fixed').rmdir()
        with self.assertRaisesRegex(RuntimeError, 'include tree is incomplete'):
            sonar_local.target_toolchain(self.project)

    def test_scanner_mounts_target_headers_read_only(self):
        truststore = self.project / 'build/sonar-trust/truststore.p12'
        truststore.parent.mkdir(parents=True)
        truststore.touch()
        command = sonar_local.scanner_command(
            self.project, 'sonarsource/sonar-scanner-cli@sha256:abc',
            self.project / 'build/sonar', self.toolchain)
        self.assertIn(
            f'type=bind,src={self.toolchain},dst=/opt/h755-arm,readonly', command)

    def test_include_resolution_errors_fail_scan(self):
        output = self.project / 'build/sonar'
        output.mkdir(parents=True)
        result = SimpleNamespace(
            stdout='WARN Preprocessor: 2 include directive error(s)\n', returncode=0)
        args = SimpleNamespace(scanner_image='sonarsource/sonar-scanner-cli@sha256:abc')
        with patch.object(sonar_local, 'target_toolchain', return_value=self.toolchain), \
             patch.object(sonar_local, 'scanner_command', return_value=['scanner']), \
             patch.object(sonar_local.subprocess, 'run', return_value=result), \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, 'could not resolve target headers'):
                sonar_local.scan(self.project, args, 'test-token', output)


if __name__ == '__main__':
    unittest.main()
