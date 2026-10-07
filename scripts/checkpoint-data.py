"""Create consistent local SQLite snapshots without changing source databases."""
from datetime import datetime, timezone, timedelta
from contextlib import closing
from pathlib import Path
import json
import os
import sqlite3
import sys


def main():
    """Back up each existing database using SQLite's online backup API."""
    project = Path(sys.argv[1]).resolve()
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d-%H%M%S-%f')
    destination = project / '.recovery' / 'databases' / stamp
    sources = sorted(p for p in (project / 'data').glob('*')
                     if p.is_file() and p.suffix.lower() in {'.sqlite', '.sqlite3', '.db'})
    if not sources:
        print('No runtime database found; no snapshot created.')
        return
    destination.mkdir(parents=True, exist_ok=False)
    results = []
    for source in sources:
        target = destination / source.name
        partial = target.with_name(target.name + '.partial')
        # Read-only source plus online backup handles active writers/WAL coherently.
        with closing(sqlite3.connect(source.as_uri() + '?mode=ro', uri=True, timeout=5)) as original:
            with closing(sqlite3.connect(partial)) as snapshot:
                original.backup(snapshot, pages=128, sleep=0.05)
                rows = snapshot.execute('PRAGMA quick_check').fetchall()
                if rows != [('ok',)]:
                    raise RuntimeError(f'Backup validation failed for {source.name}: {rows}')
        os.replace(partial, target)
        results.append({'source': str(source), 'snapshot': str(target), 'quick_check': 'ok'})
    (destination / 'manifest.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'snapshot_directory': str(destination), 'databases': results}, ensure_ascii=False))


if __name__ == '__main__':
    main()
