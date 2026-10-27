"""Shared development policy and bounded headless process runner."""
import argparse
from pathlib import Path
import subprocess
import time
import tomllib
import psutil

ROOT=Path(__file__).resolve().parents[1]

def policy(path=ROOT/'config/development.toml'):
    cfg=tomllib.loads(path.read_text(encoding='utf-8'))
    d=cfg['desktop']
    if not d['windowed_only'] or any(d[k] for k in (
        'fullscreen_allowed','maximize_on_start','always_on_top','capture_mouse_on_start')):
        raise ValueError('Desktop policy forbids fullscreen, maximization, topmost and initial capture')
    if not 1 <= d['fps_limit'] <= 60 or not 1 <= d['width'] <= 1920 or not 1 <= d['height'] <= 1080:
        raise ValueError('Invalid development window limits')
    a=cfg['analysis']
    if not 1 <= a['jobs'] <= 4 or a['memory_limit_mb'] <= 0 or a['timeout_seconds'] <= 0:
        raise ValueError('Invalid analysis limits')
    return cfg

def run(command, log):
    cfg=policy()['analysis']
    if (ROOT/command[0]).is_file(): command[0]=str((ROOT/command[0]).resolve())
    log.parent.mkdir(parents=True,exist_ok=True)
    with log.open('wb') as out:
        proc=subprocess.Popen(command,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT)
        owner=psutil.Process(proc.pid)
        start=time.monotonic()
        reason=None
        while proc.poll() is None:
            try:
                tree=[owner,*owner.children(recursive=True)]
                rss=sum(p.memory_info().rss for p in tree if p.is_running())
                if rss > cfg['memory_limit_mb']*1024*1024: reason='memory limit'
                if time.monotonic()-start > cfg['timeout_seconds']: reason='timeout'
                if reason:
                    for p in reversed(tree):
                        try: p.kill()
                        except psutil.NoSuchProcess: pass
                    proc.wait()
                    out.write(f'\nMP2R stopped owned process tree: {reason}\n'.encode())
                    return 124
            except psutil.NoSuchProcess:
                pass
            time.sleep(0.2)
        return proc.returncode

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--log',type=Path,required=True)
    p.add_argument('command',nargs=argparse.REMAINDER)
    a=p.parse_args()
    command=a.command[1:] if a.command[:1]==['--'] else a.command
    if not command: p.error('command required')
    raise SystemExit(run(command,a.log))
