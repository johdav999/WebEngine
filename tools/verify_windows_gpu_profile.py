"""Verify real direct GPU query results, trace bounds and profiling-off pixels."""
import json, math, sys
from pathlib import Path

CASES = ['above', 'underwater', 'split', 'resized', 'recovered', 'no-bloom', 'scene-copy']

def verify(directory):
    directory = Path(directory)
    result = json.loads((directory / 'result.json').read_text())
    assert result['passed']
    direct, disabled = result['backend'] == 'd3d12', result['disabled']
    for case in CASES:
        report = json.loads((directory / (case + '.json')).read_text())
        trace = json.loads((directory / (case + '-trace.json')).read_text())
        gpu = report['gpu']
        if disabled:
            assert gpu['state'] == 'disabled' and gpu['totalSamples'] == 0
            assert gpu['totalMs'] is None and gpu['ageMs'] is None
        else:
            assert gpu['state'] == 'enabled' and gpu['totalSamples'] > 0
            assert gpu['totalMs']['mean'] > 0 and gpu['ageMs'] >= 0
        if not direct:
            assert trace['traceEvents'] == []
            continue
        profile = report['gpuProfile']
        assert profile['state'] == ('disabled' if disabled else 'enabled')
        assert profile['frequencyHz'] > 0 and profile['invalidSamples'] == 0
        state = json.loads((directory / (case + '-state.json')).read_text())
        assert state['validation'] is True
        if disabled:
            assert profile['readbackBytes'] == 0 and profile['latest'] is None
            assert profile['frameMs'] is None and trace['traceEvents'] == []
            continue
        assert profile['readbackBytes'] == 3 * 2048 * 8
        sample = profile['latest']
        events = sample['events']
        assert 0 < len(events) <= 1024 and sample['droppedSpans'] == 0
        assert events[0]['name'] == 'Frame' and events[0]['parent'] == -1
        assert events[0]['durationMs'] > 0
        assert gpu['totalMs']['mean'] <= profile['frameMs']['mean'] + 1e-4
        for i, event in enumerate(events):
            assert all(math.isfinite(event[k]) and event[k] >= 0 for k in ['startMs', 'durationMs', 'selfMs', 'drawCalls', 'triangles'])
            assert event['selfMs'] <= event['durationMs'] + 1e-5
            if i:
                assert 0 <= event['parent'] < i
                parent = events[event['parent']]
                assert event['startMs'] >= parent['startMs'] - 1e-5
                assert event['startMs'] + event['durationMs'] <= parent['startMs'] + parent['durationMs'] + 1e-5
        assert abs(sum(e['selfMs'] for e in events) - events[0]['durationMs']) < len(events) * 2e-6
        names = {e['name'] for e in events}
        assert {'View', 'Shadow map', 'Sky', 'Opaque', 'Transparent / FX', 'Water snapshot', 'GUI'} <= names
        assert next(e for e in events if e['name'] == 'GUI')['drawCalls'] > 0
        if case in ['above', 'split', 'resized', 'recovered']:
            assert {'Reflection', 'Water surface', 'MSAA resolve', 'Bloom', 'Composition'} <= names
        if case == 'underwater':
            assert 'Underwater' in names
        if case == 'split':
            assert {e['view'] for e in events if e['name'] == 'View'} == {0, 1}
        if case == 'resized':
            assert (sample['width'], sample['height']) == (703, 503)
        if case == 'no-bloom':
            assert 'Bloom' not in names and 'Composition' in names
        if case == 'scene-copy':
            assert 'Scene copy' in names and not {'Bloom', 'Composition', 'MSAA resolve'} & names
        assert trace['clockDomain'].startswith('GPU')
        roots = [e for e in trace['traceEvents'] if e['name'] == 'Frame']
        assert 0 < len(roots) <= 120 and roots[-1]['args']['frame'] == sample['frame']
        for event in trace['traceEvents']:
            assert event['ph'] == 'X' and event['cat'] == 'GPU'
            assert math.isfinite(event['ts']) and event['ts'] >= 0 and event['dur'] >= 0
            assert event['args']['epoch'] == profile['epoch']
    if direct and not disabled:
        profile = json.loads((directory / 'bounded.json').read_text())['gpuProfile']
        trace = json.loads((directory / 'bounded-trace.json').read_text())['traceEvents']
        assert profile['totalSamples'] >= 125 and profile['windowSamples'] == 120
        roots = [e for e in trace if e['name'] == 'Frame']
        assert len(roots) == 120 and roots[0]['ts'] == 0
        assert all(a['ts'] <= b['ts'] and a['args']['frame'] < b['args']['frame'] for a, b in zip(roots, roots[1:]))
        capture_trace = json.loads((directory / 'sampled-capture-trace.json').read_text())['traceEvents']
        assert any(e['name'] == 'Framebuffer capture' for e in capture_trace)
    cpu = json.loads((directory / 'cpu-trace.json').read_text())['traceEvents']
    if direct:
        assert any(e['name'] == 'Direct D3D12 render' for e in cpu)
    return result

if __name__ == '__main__':
    first = Path(sys.argv[1]); result = verify(first)
    if len(sys.argv) > 2:
        second = Path(sys.argv[2]); other = verify(second)
        assert result['backend'] == other['backend'] and result['disabled'] != other['disabled']
        for case in CASES:
            assert (first / (case + '.ppm')).read_bytes() == (second / (case + '.ppm')).read_bytes(), f'Profiling changed {case} pixels'
    print(f"GPU profiling evidence verified: {first}")
