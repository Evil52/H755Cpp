import argparse
import gzip
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def lcov_records(path):
    filename=None
    for record in path.read_text().splitlines():
        if record.startswith('SF:'): filename=record[3:]
        elif record.startswith('DA:') and filename:
            number,count,*_=record[3:].split(',')
            yield filename,int(number),int(count)>0


def gcc_records(path):
    for item in json.loads(gzip.decompress(path.read_bytes()))['files']:
        for line in item['lines']: yield item['file'],line['line_number'],line['count']>0


def python_records(path):
    for filename,item in json.loads(path.read_text())['files'].items():
        for number in item['executed_lines']: yield filename,number,True
        for number in item['missing_lines']: yield filename,number,False


def add_record(root, lines, record):
    filename,number,hit=record
    path=Path(filename)
    try: path=(path if path.is_absolute() else root/path).resolve().relative_to(root)
    except ValueError: return
    table=lines.setdefault(path.as_posix(),{})
    table[number]=table.get(number,False) or hit


def collect(root, lcov_paths, gcov_paths, python_path):
    lines={}
    for path in lcov_paths:
        for record in lcov_records(path): add_record(root,lines,record)
    for path in gcov_paths:
        for record in gcc_records(path): add_record(root,lines,record)
    for record in python_records(python_path): add_record(root,lines,record)
    return lines


def inventory(root):
    for directory in ['include','src','firmware','tools']:
        for path in sorted((root/directory).rglob('*')):
            if path.suffix in {'.hpp','.h','.cpp','.c','.py','.s'} and 'probes' not in path.parts:
                yield path


def measure_source(root,path,lines,assembly,declarations=None):
    name=path.relative_to(root).as_posix()
    if path.suffix=='.s':
        measured=assembly.get(name,{})
        complete=measured.get('covered',0)==measured.get('total',-1) and measured.get('total',0)>0
        return {'kind':'assembly-instructions',**measured},complete
    measured=lines.get(name,{})
    body=[s.strip() for s in path.read_text().splitlines() if s.strip() and not s.lstrip().startswith('#')]
    instantiation_only=bool(body) and all(s.startswith('template class ') and s.endswith(';') for s in body)
    reviewed=(declarations or {}).get(name,{})
    declaration_only=reviewed.get('sha256')==hashlib.sha256(path.read_bytes()).hexdigest() and bool(reviewed.get('reason'))
    result={'kind':'python' if path.suffix=='.py' else 'c-cpp','covered':sum(measured.values()),'total':len(measured)}
    if declaration_only and not measured: result['zero_executable_reason']=reviewed['reason']
    return result,bool(measured) or instantiation_only or declaration_only


def language_total(files,kind):
    subset=[f for f in files.values() if f['kind']==kind]
    total=sum(f['total'] for f in subset);covered=sum(f['covered'] for f in subset)
    return {'covered':covered,'total':total,'percent':100*covered/total if total else 0}


def report(root, lines, assembly, declarations=None):
    files={};missing=[]
    for path in inventory(root):
        name=path.relative_to(root).as_posix()
        measured,complete=measure_source(root,path,lines,assembly,declarations)
        files[name]=measured
        if not complete: missing.append(name)
    groups={kind:language_total(files,kind) for kind in ['c-cpp','python']}
    total=sum(g['total'] for g in groups.values());covered=sum(g['covered'] for g in groups.values())
    return {'files':files,'groups':groups,'covered':covered,'total':total,'percent':100*covered/total if total else 0,'unmeasured':missing,
            'passed':not missing and total>0 and all(g['percent']>80 for g in groups.values())}


def write_reports(output,result,lines):
    output.mkdir(parents=True,exist_ok=True)
    (output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    tree=ET.Element('coverage',version='1')
    for name,item in result['files'].items():
        if item['kind']=='assembly-instructions': continue
        file=ET.SubElement(tree,'file',path=name)
        for number,hit in sorted(lines.get(name,{}).items()):
            ET.SubElement(file,'lineToCover',lineNumber=str(number),covered=str(hit).lower())
    ET.ElementTree(tree).write(output/'sonar.xml',encoding='utf-8',xml_declaration=True)


def main(argv=None):
    parser=argparse.ArgumentParser(description='Strict >80% line coverage across owned runtime C/C++ and Python; assembly checked separately')
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--lcov',type=Path,nargs='+',required=True)
    parser.add_argument('--gcov',type=Path,nargs='+',required=True)
    parser.add_argument('--python',type=Path,required=True)
    parser.add_argument('--assembly',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--declarations',type=Path)
    args=parser.parse_args(argv)
    root=args.root.resolve()
    lines=collect(root,args.lcov,args.gcov,args.python)
    declarations=json.loads(args.declarations.read_text()) if args.declarations else {}
    result=report(root,lines,json.loads(args.assembly.read_text()),declarations)
    write_reports(args.output,result,lines)
    print(json.dumps(result,indent=2))
    if not result['passed']: raise SystemExit('Coverage gate failed: every runtime language must exceed 80%; no unmeasured owned source allowed')


if __name__=='__main__': main()
