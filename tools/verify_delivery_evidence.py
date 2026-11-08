#!/usr/bin/env python3
"""Reproduce the published delivery archive checks without extraction or network. MIT."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
import zipfile


DEFAULT_EVIDENCE = Path(__file__).resolve().parents[1] / 'doc/evidence/presentation-deploy-2026-11-08'
SHA256 = re.compile(r'[0-9a-f]{64}')
HEAD = re.compile(r'[0-9a-f]{40}')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def object_json(data, label):
    result = json.loads(data)
    require(isinstance(result, dict), label + ' must be a JSON object')
    return result


def member_name(value):
    require(isinstance(value, str) and value and '\\' not in value and
            not any(part in ('', '.', '..') for part in value.split('/')) and
            not value.startswith('/') and ':' not in value and '\x00' not in value,
            'Invalid archive-relative member name: ' + repr(value))
    return value


def hash_map(value, label):
    require(isinstance(value, dict) and value, label + ' must be a nonempty hash map')
    for name, hexadecimal in value.items():
        member_name(name)
        require(isinstance(hexadecimal, str) and SHA256.fullmatch(hexadecimal),
                label + ' contains an invalid SHA-256')
    return value


def log_references(summary, name):
    """Retain every origin; the original direct-command subset stays explicit."""
    references = []

    def visit(value, pointer):
        if isinstance(value, dict):
            if 'log' in value and 'sha256' in value:
                log = member_name(value['log'])
                hexadecimal = value['sha256']
                require(isinstance(hexadecimal, str) and SHA256.fullmatch(hexadecimal),
                        name + pointer + ' has an invalid log SHA-256')
                member = str(PurePosixPath(name).parent / log)
                origin = 'direct-command' if re.fullmatch(r'/commands/[0-9]+', pointer) else 'nested-reference'
                references.append((member, hexadecimal,
                                   dict(summary=name, json_pointer=pointer, origin=origin)))
            for key, child in value.items():
                escaped = key.replace('~', '~0').replace('/', '~1')
                visit(child, pointer + '/' + escaped)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, pointer + '/' + str(index))

    visit(summary, '')
    return references


def verify_table(archive, summary_name, summary, expected):
    before = hash_map(summary.get('exported_sha256_before'), summary_name + ' exported hashes')
    require(before == summary.get('exported_sha256_after') == summary.get('relocated_sha256'),
            summary_name + ' recorded export/relocation maps differ')
    compiled = summary.get('compiled_table', {})
    require(compiled.get('cxx17') is True and compiled.get('assets') == summary.get('assets') == expected['assets'] and
            compiled.get('bytes') == summary.get('bytes') == expected['bytes'] and
            expected.get('all_payload_hashes_verified') is True,
            summary_name + ' table totals differ from the published evidence')
    log = str(PurePosixPath(summary_name).parent / 'table-run.log')
    rows, total, paths = archive.read(log).decode('utf8').splitlines(), 0, set()
    for row in rows:
        columns = row.split('\t')
        require(len(columns) == 4, log + ' has a malformed table row')
        path, mime, length, hexadecimal = columns
        require(path.startswith('/') and mime and re.fullmatch(r'0|[1-9][0-9]*', length),
                log + ' has an invalid path, MIME type or length')
        name = member_name(path[1:])
        require(name not in paths and re.fullmatch(r'(?:[0-9a-f]{2})*', hexadecimal),
                log + ' has a duplicate path or invalid payload hex')
        payload = bytes.fromhex(hexadecimal)
        require(len(payload) == int(length) and digest(payload) == before.get(name),
                log + ' payload length/hash differs: ' + name)
        paths.add(name)
        total += len(payload)
    require(paths == set(before) - {'assets.json', 'telemetry_web_assets.hpp'} and
            len(rows) == expected['assets'] and total == expected['bytes'],
            log + ' reconstructed table count, bytes or paths differ')
    return dict(summary=summary_name, assets=len(rows), bytes=total, payload_hashes_verified=True,
                recorded_mime_types_present=True)


def verify(archive_path, evidence_dir, result):
    github = evidence_dir / 'github'
    verified = object_json((github / 'VERIFIED-ARTIFACT.json').read_bytes(), 'Verification report')
    metadata = object_json((github / 'ARTIFACT-METADATA.json').read_bytes(), 'Artifact metadata')
    coverage = object_json((github / 'LOG-COVERAGE.json').read_bytes(), 'Log coverage')
    run = metadata.get('workflow_run', {})
    head, expected_hash = verified.get('head'), verified.get('zip_sha256')
    require(verified.get('passed') is True and verified.get('hardware') is False and
            isinstance(head, str) and HEAD.fullmatch(head) and
            isinstance(expected_hash, str) and SHA256.fullmatch(expected_hash),
            'Verification report has invalid identity or status')
    require(metadata.get('digest') == 'sha256:' + expected_hash and
            metadata.get('id') == verified.get('artifact') and run.get('id') == verified.get('run') and
            run.get('head_sha') == head,
            'Artifact metadata digest, ID, run or head differs from the verification report')
    require(archive_path.stat().st_size == metadata.get('size_in_bytes'), 'Archive byte size differs from artifact metadata')
    actual_hash = file_digest(archive_path)
    require(actual_hash == expected_hash, 'Archive SHA-256 differs from published metadata')
    result.update(artifact=verified['artifact'], run=verified['run'], head=head, zip_sha256=actual_hash)

    # Nothing above opens the ZIP or reads any compressed member.
    result['phase'] = 'archive_members'
    with zipfile.ZipFile(archive_path) as archive:
        names = [item.filename for item in archive.infolist()]
        require(len(names) == len(set(names)), 'Archive contains duplicate member names')
        for name in names:
            member_name(name.rstrip('/'))
        require(len(names) == verified.get('zip_members_crc_verified'), 'Archive member count differs from the report')
        require(archive.testzip() is None, 'Archive CRC verification failed')
        result['zip_members_crc_verified'] = len(names)

        result['phase'] = 'summaries'
        records, references = {}, []
        for item in verified['summaries']:
            name = member_name(item['name'])
            require(name not in records, 'Duplicate summary in the verification report')
            payload = archive.read(name)
            require(digest(payload) == item['sha256'], 'Summary SHA-256 differs: ' + name)
            summary = object_json(payload, name)
            require(summary.get('passed') is True and summary.get('hardware') is False and
                    summary.get('checks') == item.get('checks'), name + ' status/check count differs')
            for field in ('error', 'errors', 'warnings', 'asset_errors', 'native_stderr'):
                require(not summary.get(field), name + ' reports ' + field)
            if 'native_exit' in summary:
                require(summary['native_exit'] == 0, name + ' native process did not exit successfully')
            for before, after in (('exported_sha256_before', 'exported_sha256_after'),
                                  ('assets_sha256_before', 'assets_sha256_after'),
                                  ('source_sha256_before', 'source_sha256_after')):
                if before in summary:
                    require(summary[before] == summary.get(after), name + ' recorded before/after maps differ')
            if 'relocated_sha256' in summary:
                require(summary['relocated_sha256'] == summary.get('exported_sha256_before'),
                        name + ' recorded relocation map differs')
            records[name] = summary
            references.extend(log_references(summary, name))
        result['summaries_verified'] = len(records)

        result['phase'] = 'command_logs'
        logs = {}
        for member, hexadecimal, origin in references:
            entry = logs.setdefault(member, dict(member=member, sha256=hexadecimal,
                                                 present=member in names, origins=[]))
            require(entry['sha256'] == hexadecimal, 'Conflicting log hash references: ' + member)
            entry['origins'].append(origin)
        for entry in logs.values():
            entry['origins'].sort(key=lambda row: (row['summary'], row['json_pointer']))
            if entry['present']:
                require(digest(archive.read(entry['member'])) == entry['sha256'],
                        'Command log SHA-256 differs: ' + entry['member'])
        direct = [row for row in references if row[2]['origin'] == 'direct-command']
        require(len(direct) == verified['log_hashes_verified'] and all(row[0] in names for row in direct),
                'Original direct command-log coverage differs from the report')
        counts = dict(original_direct_command_logs=len(direct), available_unique_members=sum(row['present'] for row in logs.values()),
                      referenced_unique_members=len(logs), missing_unique_members=sum(not row['present'] for row in logs.values()),
                      total_references=len(references))
        require(coverage.get('format') == 'telemetry-ux-archive-log-coverage-v1' and
                coverage.get('artifact') == dict(id=verified['artifact'], run=verified['run'], head_sha=head, zip_sha256=actual_hash) and
                coverage.get('counts') == counts and
                coverage.get('logs') == [logs[name] for name in sorted(logs)],
                'Recursive log coverage differs from the published available/missing inventory')
        result['log_coverage'] = counts
        result['missing_log_members'] = [name for name in sorted(logs) if not logs[name]['present']]

        result['phase'] = 'compiled_tables'
        expected_tables = {member_name(row['summary']): row for row in verified['tables']}
        require(len(expected_tables) == len(verified['tables']) and
                set(expected_tables) == {name for name, row in records.items() if 'compiled_table' in row},
                'Published compiled table coverage differs from the summaries')
        result['tables'] = [verify_table(archive, name, records[name], expected_tables[name]) for name in sorted(expected_tables)]
        result['compiled_tables_verified'] = len(result['tables'])
        result['table_payloads_verified'] = sum(row['assets'] for row in result['tables'])
        result['table_payload_bytes_verified'] = sum(row['bytes'] for row in result['tables'])
    result['historical_source_claims'] = dict(rechecked=False,
        reason='Archive-only verification does not read a Git checkout or repeat the historical source comparisons.',
        git_source_files_verified=verified.get('git_source_files_verified'),
        creator_title_substitutions_verified=verified.get('creator_title_substitutions_verified'),
        known_fixture_files_verified=verified.get('known_fixture_files_verified'))
    result.update(passed=True, phase='complete')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path, help='Downloaded installed-web-configurator ZIP')
    parser.add_argument('--evidence-dir', type=Path, default=DEFAULT_EVIDENCE,
                        help='Published evidence directory containing github/*.json')
    args = parser.parse_args()
    result = dict(passed=False, scope='archive-only', extraction=False, network=False, hardware=False, phase='archive_identity')
    try:
        verify(args.archive, args.evidence_dir, result)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, RuntimeError, zipfile.BadZipFile) as error:
        result['error'] = str(error)
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
