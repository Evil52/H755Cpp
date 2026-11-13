import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SemgrepReportingTests(unittest.TestCase):
    def run_report(self, report):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            path.write_text(json.dumps(report))
            return subprocess.run([sys.executable, str(ROOT / 'tools/check_semgrep_result.py'), str(path)], capture_output=True).returncode

    def valid_report(self):
        return {'paths': {'scanned': ['include/h755/ethernet/mdio.hpp', 'src/ethernet/cmsis_mdio.cpp', 'include/h755/ethernet/lan8742.hpp', 'firmware/cm7/main.cpp', 'firmware/cm7/coverage.cpp', 'firmware/cm7/board.cpp', 'firmware/cm7/fault.cpp', 'firmware/cm7/processor.cpp', 'src/diagnostics.cpp', 'src/timebase.cpp', 'include/h755/application/controller.hpp']}, 'results': [], 'errors': []}

    def test_valid_source_coverage_passes(self):
        self.assertEqual(self.run_report(self.valid_report()), 0)

    def test_empty_scan_fails(self):
        self.assertNotEqual(self.run_report({'paths': {'scanned': []}, 'results': [], 'errors': []}), 0)

    def test_missing_driver_fails(self):
        report = self.valid_report()
        report['paths']['scanned'].remove('src/ethernet/cmsis_mdio.cpp')
        self.assertNotEqual(self.run_report(report), 0)

    def test_parser_error_fails(self):
        report = self.valid_report()
        report['errors'] = [{'type': 'ParseError'}]
        self.assertNotEqual(self.run_report(report), 0)

    def test_finding_fails(self):
        report = self.valid_report()
        report['results'] = [{'check_id': 'h755-no-heap-allocation'}]
        self.assertNotEqual(self.run_report(report), 0)
