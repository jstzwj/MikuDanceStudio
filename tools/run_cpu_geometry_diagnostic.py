"""Verify the live CPU deformation boundary using the existing PMM GUI workflow."""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import hashlib

LIVE_PHASES = {
    'loaded_edited': ('v1', '0'), 'seek1': ('v1', '0'), 'seek5': ('v1', '0'),
    'playing': ('v1', '1'), 'stopped_before_save': ('v1', '0'),
    'v2_loaded_edited': ('v2', '0'), 'v2_seek': ('v2', '0'),
    'v2_playing': ('v2', '1'), 'v2_stopped': ('v2', '0'),
}

def live_history_exact(groups):
    """A phase label alone is not evidence that a playing frame was captured."""
    seen = {}
    owners = {}
    for group in groups.values():
        if not group:
            return False
        phase = group[0]['phase']
        if phase not in LIVE_PHASES or phase in seen:
            return False
        version, playing = LIVE_PHASES[phase]
        process = group[0]['process']
        if any(r['phase'] != phase or r['pmmVersion'] != version or
               r['playing'] != playing or r['process'] != process or
               r['status'] != 'PASS' or int(r['differentPixels']) != 0 for r in group):
            return False
        lifecycle = [r for r in group if r['family'] == 'lifecycle']
        prefixes = [r for r in group if r['family'] != 'lifecycle']
        draws = int(group[0]['draws'])
        if draws <= 0 or len(lifecycle) != 1 or lifecycle[0]['backend'] != 'both' or \
                [(int(r['boundary']), r['backend']) for r in prefixes] != \
                [(n, b) for n in range(draws + 1) for b in ('dx11', 'dx12')]:
            return False
        if version in owners and owners[version] != process:
            return False
        owners[version] = process
        seen[phase] = True
    return set(seen) == set(LIVE_PHASES) and owners['v1'] != owners['v2']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('gui', 'application', 'fixture', 'output'):
        parser.add_argument('--'+name, required=True, type=Path)
    parser.add_argument('--model', default='Dammy_Bone.pmd')
    parser.add_argument('--material', type=int, default=10)
    parser.add_argument('--materials',help='ordered comma-separated independent material subset, e.g. 12,13,14')
    parser.add_argument('--translucent',action='store_true',help='set material 13 alpha to 0.5 in the temporary GUI copy only')
    parser.add_argument('--switch-toon',action='store_true',help='set material 12 shared Toon to slot 2 in the temporary GUI copy only')
    parser.add_argument('--upload', action='store_true')
    parser.add_argument('--play', action='store_true', help='exercise the actual play/stop controls as well as frame stepping')
    parser.add_argument('--preview', action='store_true', help='compare DX11/DX12 isolated live CPU batch; diagnostic build required')
    parser.add_argument('--inputs', choices=('audit','direct'), help='generate CPU material/camera/light inputs; direct requires an already accepted audit')
    parser.add_argument('--accepted-audit',type=Path,help='successful audit directory, required for direct mode')
    parser.add_argument('--lifecycle',action='store_true',help='resize and replace both backends during the real CPU batch workflow')
    parser.add_argument('--history',action='store_true',help='verify a native DX9 prefix and append the same-frame CPU subset in both backends; single sample only')
    parser.add_argument('--history-export',action='store_true',help='save immutable native prefix archives per process for independent reductions')
    parser.add_argument('--ground',action='store_true',help='enable ground only in the temporary PMM fixture (history diagnostic)')
    parser.add_argument('--projected-shadows',action='store_true',help='enable projected ground shadows only in the temporary PMM fixture')
    parser.add_argument('--cpu-prefix', choices=('ground','ground-shadow','ground-shadow-model','full-scene'),
                        help='replace these preceding draw families with audited CPU producers')
    parser.add_argument('--mip-prepare',choices=('box','dx9'),help='explicit CPU-box or independent DX9 compatibility Mip preparation')
    parser.add_argument('--alpha12',action='store_true',help='default-off fixed12 Alpha comparison candidate on application diagnostic DX11/DX12 devices')
    parser.add_argument('--texture-fma',action='store_true',help='default-off ordered fused texture-coordinate transform on measured diagnostic DX11/DX12 devices')
    parser.add_argument('--specular-fma',action='store_true',help='default-off prepared camera matrix and fused local-viewer half vector on measured DX11/DX12 devices')
    parser.add_argument('--live-history',action='store_true',help='bounded acknowledged full CPU scene captures during v1/v2 seek, play, edit and save/reopen')
    parser.add_argument('--screen-source',choices=('scene','empty-background'),default='scene',help='explicit temporary GUI screen-source selection; dynamic native screen capture remains unsupported')
    args = parser.parse_args()
    materials=[int(n) for n in args.materials.split(',')] if args.materials else [args.material]
    if len(materials)!=len(set(materials)) or materials!=sorted(materials) or any(n<0 for n in materials) or (args.materials and (len(materials)<2 or not args.inputs)):
        raise ValueError('materials must be an increasing subset of at least two indices, with --inputs')
    if args.history and (not args.materials or materials!=list(range(materials[0],materials[-1]+1))):
        raise ValueError('--history requires a contiguous CPU material subset')
    if args.ground and not args.history:raise ValueError('--ground requires --history')
    if args.history_export and not args.history:raise ValueError('--history-export requires --history')
    if args.cpu_prefix and not args.history:raise ValueError('--cpu-prefix requires --history')
    if args.mip_prepare and args.cpu_prefix not in ('ground-shadow-model','full-scene'):raise ValueError('--mip-prepare requires --cpu-prefix ground-shadow-model')
    if args.projected_shadows and not args.ground:raise ValueError('--projected-shadows requires --ground')
    if args.alpha12 and not args.preview:raise ValueError('--alpha12 requires --preview')
    if args.texture_fma and not args.preview:raise ValueError('--texture-fma requires --preview')
    if args.specular_fma and not args.preview:raise ValueError('--specular-fma requires --preview')
    if args.live_history and not (args.cpu_prefix=='full-scene' and args.lifecycle and args.play):raise ValueError('--live-history requires --cpu-prefix full-scene --lifecycle --play')
    identity=dict(applicationSha256=hashlib.sha256(args.application.read_bytes()).hexdigest(),
                  fixtureSha256=hashlib.sha256(args.fixture.read_bytes()).hexdigest(),guiSha256=hashlib.sha256(args.gui.read_bytes()).hexdigest(),model=args.model,material=materials[0],materials=materials,translucent=args.translucent,switchToon=args.switch_toon,cpuInputContract=4,sceneHistory=args.history,groundTemporaryFixture=args.ground)
    identity['cpuPrefixFamilies']=args.cpu_prefix
    identity['projectedShadowsTemporaryFixture']=args.projected_shadows
    identity['mipPreparation']=args.mip_prepare
    identity['diagnosticAlpha12']=args.alpha12
    identity['diagnosticTextureFma']=args.texture_fma
    identity['diagnosticSpecularFma']=args.specular_fma
    identity['liveHistory']=args.live_history
    identity['liveReportContract']=1 if args.live_history else None
    identity['screenSource']=args.screen_source
    if args.inputs=='direct':
        if not args.accepted_audit:
            raise ValueError('direct mode requires --accepted-audit')
        accepted=json.loads((args.accepted_audit/'run.json').read_text())
        if accepted.get('status')!='PASS' or not accepted.get('inputsExact') or any(accepted.get(k)!=v for k,v in identity.items()):
            raise ValueError('accepted audit must match this application, fixture, model, material and input contract')
    output = args.output.resolve()
    if output.exists():
        raise ValueError('choose a fresh evidence directory')
    output.mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('MDS_RHI_')}
    env.update(MDS_PMM_GUI_FIXTURE=str(args.fixture.resolve()), MDS_PMM_RELOCATE_MODEL=args.model,
               MDS_RHI_CPU_GEOMETRY='1', MDS_RHI_CPU_GEOMETRY_UPLOAD='1' if args.upload else '0',
               MDS_RHI_CPU_GEOMETRY_DIR=str(output))
    if args.play:
        env['MDS_RHI_CPU_PLAY']='1'
    if args.translucent:env['MDS_RHI_HISTORY_TRANSLUCENT']='1'
    if args.switch_toon:env['MDS_RHI_CPU_SWITCH_TOON']='1'
    if args.lifecycle:
        if not args.preview:raise ValueError('--lifecycle requires --preview')
        env['MDS_RHI_CPU_LIFECYCLE']='1'
    if args.preview:
        env.update(MDS_RHI_CPU_PREVIEW='1', MDS_RHI_GUI_MODE='model_toon', MDS_RHI_GUI_MATERIAL=str(materials[0]))
    if args.materials:env['MDS_RHI_CPU_MATERIALS']=','.join(map(str,materials))
    if args.inputs:
        if not args.preview:
            raise ValueError('--inputs requires --preview')
        env.update(MDS_RHI_CPU_INPUTS='1',MDS_RHI_CPU_INPUTS_AUDIT='1' if args.inputs=='audit' else '0',
                   MDS_RHI_CPU_INPUTS_ACCEPTED='1' if args.inputs=='direct' else '0')
    if args.history:
        env.update(MDS_RHI_CPU_SCENE_HISTORY='1',MDS_RHI_HISTORY_REPLAY='1',MDS_RHI_PORTABLE_REPLAY='1',
                   MDS_RHI_REPLAY_SINGLE_SAMPLE_CONTROL='1',MDS_RHI_HISTORY_TRACE='1',
                   MDS_RHI_PORTABLE_MIP_CAPTURE='1',MDS_RHI_PORTABLE_SHADOW_CAPTURE='1')
    if args.ground:env['MDS_RHI_GUI_GROUND']='1'
    if args.projected_shadows:env['MDS_RHI_GUI_PROJECTED_SHADOWS']='1'
    if args.history_export:env['MDS_RHI_PORTABLE_PREFIX_EXPORT']='1'
    if args.cpu_prefix:env['MDS_RHI_CPU_PREFIX']=str({'ground':1,'ground-shadow':3,'ground-shadow-model':7,'full-scene':31}[args.cpu_prefix])
    if args.cpu_prefix=='full-scene':env['MDS_RHI_CPU_FULL_SCENE']='1'
    if args.mip_prepare:env['MDS_RHI_CPU_MIP_PREPARE']=args.mip_prepare
    if args.alpha12:env['MDS_RHI_DIAGNOSTIC_ALPHA12']='1'
    if args.texture_fma:env['MDS_RHI_DIAGNOSTIC_TEXTURE_FMA']='1'
    if args.specular_fma:env['MDS_RHI_DIAGNOSTIC_SPECULAR_FMA']='1'
    if args.live_history:env['MDS_RHI_CPU_LIVE_HISTORY']='1'
    if args.screen_source=='empty-background':env['MDS_RHI_CPU_EMPTY_BACKGROUND_SCREEN']='1'
    with (output/'gui.log').open('w', encoding='utf-8') as log:
        try:
            gui = subprocess.run([str(args.gui.resolve()), str(args.application.resolve())],
                                 env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600 if args.live_history else 180)
            exit_code = gui.returncode
        except subprocess.TimeoutExpired:
            # The GUI driver's job closes its owned application on termination.
            exit_code = 124
    report = dict(guiExit=exit_code, mode='upload' if args.upload else 'compare', applicationBackend='dx9')
    report.update(identity)
    def evidence(name):
        rows=[]
        for file in sorted(output.glob(name+'*.csv')):
            with file.open(newline='') as stream:rows.extend(csv.DictReader(stream))
        return rows
    file = output/'cpu_geometry.csv'
    rows = list(csv.DictReader(file.open(newline=''))) if file.is_file() else []
    differences = sum(int(row['mainDifferentBytes'])+int(row['edgeDifferentBytes']) for row in rows)
    compares = sum(row['mode'] == 'independent_compare' for row in rows)
    uploads = sum(row['mode'] == 'upload' for row in rows)
    cpu_sources=sum(row['mode']=='cpu_source' for row in rows)
    frames = sorted(set(int(row['frame']) for row in rows))
    slots = sorted(set(int(row['slot']) for row in rows if int(row['slot']) >= 0))
    ids = sorted(set(int(row['id']) for row in rows))
    playback = sum(row.get('playing')=='1' for row in rows)
    geometry_passed=cpu_sources==len(rows) if args.inputs=='direct' else compares and (uploads if args.upload else not uploads)
    passed = exit_code == 0 and len(rows) > 10 and geometry_passed and not differences and len(frames) > 1
    if args.play:
        passed = passed and playback>0
    if args.preview:
        preview_rows = evidence('cpu_preview.')
        covered = sum(int(row['coveredPixels']) for row in preview_rows)
        preview_passed = len(preview_rows)>10 and covered>0 and all(row['status']=='PASS' for row in preview_rows) and len(set(row['frame'] for row in preview_rows))>1
        passed = passed and preview_passed
        report.update(previewSamples=len(preview_rows), previewExact=preview_passed, previewCoveredPixels=covered,
                      previewEmptySamples=sum(int(row['coveredPixels'])==0 for row in preview_rows))
        if args.alpha12 or args.texture_fma or args.specular_fma or args.live_history:
            devices=evidence('cpu_preview_devices')
            policy_exact=bool(devices) and all(r['success']=='1' and r.get('diagnosticAlpha12')==str(int(args.alpha12)) and r.get('diagnosticTextureFma')==str(int(args.texture_fma)) and r.get('diagnosticSpecularFma')==str(int(args.specular_fma)) for r in devices)
            passed=passed and policy_exact
            report.update(devicePolicyChecks=len(devices),devicePolicyExact=policy_exact)
    if args.materials:
        ordered=evidence('cpu_material_order.')
        groups={}
        for row in ordered:groups.setdefault((row['process'],row['model'],row['submission']),[]).append(row)
        order_exact=bool(groups) and all([int(r['material']) for r in group]==materials and [int(r['order']) for r in group]==list(range(len(materials))) and all(r['status']=='PASS' for r in group) for group in groups.values())
        per_process={}
        for row in preview_rows:per_process.setdefault(row['process'],[]).append(row)
        boundaries=bool(preview_rows) and all([int(r['material']) for r in rows[i:i+len(materials)]]==materials for rows in per_process.values() for i in range(0,len(rows),len(materials)))
        boundaries=boundaries and len(preview_rows)==len(groups)*len(materials)
        passed=passed and order_exact and boundaries
        report.update(modelSubmissions=len(groups),materialOrderExact=order_exact,prefixOrderExact=boundaries)
    if args.inputs=='audit':
        input_rows=evidence('cpu_inputs.')
        exact=len(input_rows)>10 and all(row['status']=='PASS' for row in input_rows) and len(set(row['frame'] for row in input_rows))>1
        exact=exact and set(int(r['material']) for r in input_rows)==set(materials)
        if args.materials:exact=exact and len(input_rows)==len(groups)*len(materials)
        passed=passed and exact
        report.update(inputChecks=len(input_rows),inputsExact=exact,inputMode=args.inputs)
    elif args.inputs:
        report.update(inputMode=args.inputs)
        native_inputs=evidence('cpu_inputs.')
        direct_exact=bool(rows) and not native_inputs and all(r['mode']=='cpu_source' for r in rows)
        report.update(directCpuInputsExact=direct_exact,nativeInputAuditChecks=len(native_inputs))
        passed=passed and direct_exact
    if args.lifecycle:
        lifecycle_file=output/'cpu_preview_lifecycle.csv'
        lifecycle_rows=list(csv.DictReader(lifecycle_file.open(newline=''))) if lifecycle_file.is_file() else []
        expected_lifecycle={(o,b) for o in ('resize','replace') for b in ('dx11','dx12')}
        # A larger suffix can also reach the resize probe after Save As/reopen.
        # Require at least one complete lifecycle plus every additional probe,
        # with each operation recorded as the ordered DX11/DX12 pair.
        lifecycle_passed=len(lifecycle_rows)>=4 and len(lifecycle_rows)%2==0 and all(row['status']=='PASS' for row in lifecycle_rows)
        lifecycle_passed=lifecycle_passed and all(r.get('diagnosticAlpha12')==str(int(args.alpha12)) and r.get('diagnosticTextureFma')==str(int(args.texture_fma)) and r.get('diagnosticSpecularFma')==str(int(args.specular_fma)) for r in lifecycle_rows)
        lifecycle_passed=lifecycle_passed and {(r['operation'],r['backend']) for r in lifecycle_rows}==expected_lifecycle
        lifecycle_passed=lifecycle_passed and all(a['operation']==b['operation'] and a['backend']=='dx11' and b['backend']=='dx12'
            for a,b in zip(lifecycle_rows[::2],lifecycle_rows[1::2]))
        passed=passed and lifecycle_passed
        report.update(lifecycleChecks=len(lifecycle_rows),lifecycleExact=lifecycle_passed)
    if args.history and args.cpu_prefix!='full-scene':
        history_rows=evidence('cpu_scene_history.')
        history_groups={}
        for row in history_rows:history_groups.setdefault((row['process'],row['frame']),[]).append(row)
        def history_group_exact(group):
            draws=int(group[0]['prefixDraws'])
            expected=[('prefix',str(n),str(materials[0]),b) for n in range(draws+1) for b in ('dx11','dx12')]
            expected += [('material',str(n),str(m),b) for n,m in enumerate(materials,1) for b in ('dx11','dx12')]
            return draws>0 and [(r['phase'],r['boundary'],r['material'],r['backend']) for r in group]==expected and all(r['status']=='PASS' and int(r['differentPixels'])==0 and int(r['prefixDraws'])==draws for r in group)
        history_exact=bool(history_groups) and all(history_group_exact(group) for group in history_groups.values())
        passed=passed and history_exact
        report.update(sceneHistoryChecks=len(history_rows),sceneHistoryExact=history_exact,
                      nativePrefixDraws=sorted(set(int(r['prefixDraws']) for r in history_rows)),productionActivation=False)
    if args.cpu_prefix=='full-scene':
        full_rows=evidence('cpu_full_scene.')
        full_groups={}
        for row in full_rows:full_groups.setdefault((row['process'],row.get('checkpoint',row['frame'])),[]).append(row)
        def full_exact(group):
            draws=int(group[0]['draws'])
            prefixes=[r for r in group if r['family']!='lifecycle']
            return draws>0 and [(int(r['boundary']),r['backend']) for r in prefixes]==[(n,b) for n in range(draws+1) for b in ('dx11','dx12')] and all(r['status']=='PASS' and int(r['differentPixels'])==0 and int(r['draws'])==draws for r in group)
        exact=bool(full_groups) and all(full_exact(g) for g in full_groups.values())
        exact=exact and all(r.get('diagnosticAlpha12')==str(int(args.alpha12)) and r.get('diagnosticTextureFma')==str(int(args.texture_fma)) and r.get('diagnosticSpecularFma')==str(int(args.specular_fma)) for r in full_rows)
        imports=evidence('cpu_accessory_import.')
        imported=bool(imports) and all(r['status']=='PASS' and r['origin']=='independent_d3dx_system_memory' and all(int(r[k])>0 for k in ('vertices','subsets','materials','geometryId')) for r in imports)
        passed=passed and exact and imported
        report.update(accessoryImportChecks=len(imports),accessoryImportExact=imported)
        report.update(fullSceneChecks=len(full_rows),fullSceneExact=exact,fullSceneDraws=sorted({int(r['draws']) for r in full_rows}),productionActivation=False)
        if args.live_history:
            phases={r['phase'] for r in full_rows}
            live_exact=live_history_exact(full_groups)
            ledger_files=list(output.glob('cpu_full_resources.*.csv'))
            ledgers_exact=len(ledger_files)==len(full_groups) and bool(full_groups)
            for (process, checkpoint) in full_groups:
                path=output/f'cpu_full_resources.{process}.{checkpoint}.csv'
                ledger=list(csv.DictReader(path.open(newline=''))) if path.is_file() else []
                keys={(r['id'],r['version']) for r in ledger}
                ledgers_exact=ledgers_exact and bool(ledger) and len(keys)==len(ledger) and all(
                    int(r['id'])>0 and int(r['version'])>0 and int(r['bytes'])>0 and int(r['hash'])>0 and
                    (int(r['levels'])>0 if r['kind']=='2' else int(r['levels'])==0) for r in ledger)
            live_exact=live_exact and ledgers_exact
            passed=passed and live_exact
            contracts=[]
            for path in output.glob('cpu_history_failure.*.contract.csv'):
                with path.open(newline='',encoding='utf-8') as source:
                    contracts.extend(csv.DictReader(source))
            precision_only=bool(contracts) and any(r.get('field')=='classification' and r.get('value')=='portable_contract_exact_backend_precision' for r in contracts)
            input_contract_failure=any(r.get('field')=='classification' and r.get('value')=='portable_contract_field_difference' for r in contracts)
            report.update(liveCheckpoints=len(full_groups),livePhases=sorted(phases),liveHistoryExact=live_exact,pmmVersions=sorted({r['pmmVersion'] for r in full_rows}),
                          resourceLedgerChecks=len(ledger_files),resourceLedgersExact=ledgers_exact,
                          backendPrecisionOnly=precision_only,inputContractFailure=input_contract_failure)
    if args.cpu_prefix:
        cpu_rows=evidence('cpu_prefix.')
        wanted={'ground_lines','ground_plane'}
        if args.projected_shadows and 'shadow' in args.cpu_prefix:wanted.add('projected_model_shadow')
        if args.cpu_prefix in ('ground-shadow-model','full-scene'):wanted.add('model_material')
        if args.cpu_prefix=='full-scene':
            wanted.update(('accessory_material','model_outline'))
            if args.projected_shadows:wanted.add('projected_model_shadow')
        cpu_exact=bool(cpu_rows) and all(r['status']=='PASS' and r['geometrySource']==r['stateSource']=='cpu' for r in cpu_rows) and wanted.issubset({r['family'] for r in cpu_rows})
        if args.cpu_prefix in ('ground-shadow-model','full-scene'):
            for filename in output.glob('cpu_prefix.*.csv'):
                with filename.open(newline='',encoding='utf-8') as source:
                    process_rows=list(csv.DictReader(source))
                cpu_exact=cpu_exact and [int(r['material']) for r in process_rows if r['family']=='model_material']==list(range(17 if args.cpu_prefix=='full-scene' else materials[0]))
        passed=passed and cpu_exact
        report.update(cpuPrefixChecks=len(cpu_rows),cpuPrefixExact=cpu_exact,cpuPrefixCoverage=sorted({r['family'] for r in cpu_rows}))
    if args.mip_prepare:
        mip_rows=evidence('mip_preparation.')
        mip_exact=bool(mip_rows) and all(r['origin']=='independent_cpu_source' and r['policy']==args.mip_prepare and
            all(int(r[k])>0 for k in ('sourceRevision','providerRevision','preparedRevision','decodedRevision','width','height','levels')) for r in mip_rows)
        versions={}
        for row in mip_rows:
            key=(Path(row['asset']).name,row['sourceRevision'],row['providerRevision'])
            value=(row['preparedRevision'],row['levels'],row['width'],row['height'])
            mip_exact=mip_exact and (key not in versions or versions[key]==value)
            versions[key]=value
        report.update(mipPreparationChecks=len(mip_rows),mipPreparationExact=mip_exact)
        passed=passed and mip_exact
    fixture_preserved=hashlib.sha256(args.fixture.read_bytes()).hexdigest()==identity['fixtureSha256']
    passed=passed and fixture_preserved
    report.update(fixturePreserved=fixture_preserved,status='PASS' if passed else 'UNAVAILABLE' if exit_code == 77 else 'FAIL',
                  samples=len(rows), independentCompares=compares, cpuUploads=uploads,
                  cpuSourceSamples=cpu_sources,
                  differentBytes=differences, playbackSamples=playback, frames=frames, slots=slots, geometryResourceIds=len(ids))
    (output/'run.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
    return 0 if passed else exit_code or 1

if __name__ == '__main__':
    raise SystemExit(main())
