import contextlib
import gzip
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec=importlib.util.spec_from_file_location('coverage_report',Path(__file__).resolve().parents[1]/'tools/coverage_report.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class CoverageTests(unittest.TestCase):
    def test_union_inventory_and_xml(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for folder in ['include','src','firmware/cm4','tools/probes']:(root/folder).mkdir(parents=True)
            for file,text in {'include/a.hpp':'int f();\nint g();','src/a.cpp':'template class X<Y>;','tools/a.py':'print(1)','firmware/cm4/park.s':'wfe','tools/probes/a.cpp':'fixture'}.items():(root/file).write_text(text)
            lcov=root/'a.info';lcov.write_text(f'SF:{root}/include/a.hpp\nDA:1,0\nDA:2,1\nend_of_record\nSF:/external/a.hpp\nDA:1,1\n')
            gcov=root/'a.gz';gcov.write_bytes(gzip.compress(json.dumps({'files':[{'file':str(root/'include/a.hpp'),'lines':[{'line_number':1,'count':3}]}]}).encode()))
            py=root/'py.json';py.write_text(json.dumps({'files':{'tools/a.py':{'executed_lines':[1],'missing_lines':[]}}}))
            asm=root/'asm.json';asm.write_text(json.dumps({'firmware/cm4/park.s':{'total':9,'covered':9}}))
            with contextlib.redirect_stdout(io.StringIO()):module.main(['--root',str(root),'--lcov',str(lcov),'--gcov',str(gcov),'--python',str(py),'--assembly',str(asm),'--output',str(root/'out')])
            report=json.loads((root/'out/summary.json').read_text());self.assertEqual(report['percent'],100);self.assertTrue(report['passed']);self.assertNotIn('tools/probes/a.cpp',report['files'])
            self.assertEqual(ET.parse(root/'out/sonar.xml').find('file/lineToCover').attrib['covered'],'true')
    def test_unmeasured_and_exactly_80_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for folder in ['include','src','firmware','tools']:(root/folder).mkdir()
            (root/'src/a.cpp').write_text('int f(){return 1;}');(root/'tools/a.py').write_text('print(1)')
            lines={'src/a.cpp':{i:i<5 for i in range(1,6)},'tools/a.py':{1:True}}
            result=module.report(root,lines,{})
            self.assertFalse(result['passed']);self.assertEqual(result['groups']['c-cpp']['percent'],80)
            (root/'include/missing.hpp').write_text('int absent();')
            self.assertIn('include/missing.hpp',module.report(root,lines,{})['unmeasured'])
    def test_missing_assembly_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'firmware').mkdir();(root/'firmware/a.s').write_text('wfe')
            self.assertFalse(module.report(root,{}, {})['passed'])
            self.assertIn('firmware/a.s',module.report(root,{}, {})['unmeasured'])
