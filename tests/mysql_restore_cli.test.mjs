import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import { runRestoreCli } from './verify_mysql_restore.mjs';

function fakeChild({ closeAfterKill = null, normalClose = null } = {}) {
    const child = new EventEmitter();
    child.pid = 99123; // 纯EventEmitter标记，没有真实进程，不对系统PID发信号。
    child.stdin = new PassThrough();
    child.stdout = new PassThrough();
    child.stderr = new PassThrough();
    child.kills = 0;
    child.unrefs = 0;
    child.events = [];
    child.kill = () => {
        child.kills += 1;
        child.events.push('kill');
        if (closeAfterKill !== null) setTimeout(() => {
            child.events.push('close');
            child.emit('exit', 1);
            child.emit('close', 1);
        }, closeAfterKill);
        return true;
    };
    child.unref = () => { child.unrefs += 1; };
    if (normalClose !== null) setTimeout(() => {
        child.events.push('close');
        child.emit('exit', 0);
        child.emit('close', 0);
    }, normalClose);
    return child;
}

test('import入口不执行主流程；正常CLI必须等close后返回', async () => {
    const child = fakeChild({ normalClose: 15 });
    await runRestoreCli('fixture-mysql.exe', [], async () => {}, { spawnProcess: () => child, timeoutMs: 200 });
    assert.deepEqual(child.events, ['close']);
    assert.equal(child.kills, 0);
});

test('pipeline失败先kill且await close，之后才能交给调用方收尾', async () => {
    const child = fakeChild({ closeAfterKill: 15 });
    await assert.rejects(runRestoreCli('fixture-dump.exe', [], async () => { throw new Error('fixture ENOSPC'); }, {
        spawnProcess: () => child, reapTimeoutMs: 100
    }), (error) => {
        child.events.push('rejected');
        assert.equal(error.code, 'CLI_FAILED');
        assert.equal(error.exitConfirmed, true);
        assert.deepEqual(child.events, ['kill', 'close', 'rejected']);
        return true;
    });
    assert.ok(child.stdin.destroyed && child.stdout.destroyed && child.stderr.destroyed);
});

test('kill不发close则有界未确认，携带仅自己PID/path，调用方不得DROP', async () => {
    const child = fakeChild();
    const reported = [];
    const started = performance.now();
    await assert.rejects(runRestoreCli('fixture-restore.exe', ['never-log-arguments'], async () => { throw new Error('fixture EPIPE'); }, {
        spawnProcess: () => child, reapTimeoutMs: 25, onUnconfirmed: (info) => reported.push(info)
    }), (error) => {
        assert.equal(error.code, 'CLI_EXIT_UNCONFIRMED');
        assert.equal(error.exitConfirmed, false);
        return true;
    });
    assert.ok(performance.now() - started < 500);
    assert.equal(child.unrefs, 1);
    assert.deepEqual(reported, [{ pid: 99123, executable: 'fixture-restore.exe' }]);
    assert.ok(child.stdin.destroyed && child.stdout.destroyed && child.stderr.destroyed);
});

test('晚到close在预算内也必须等到，不将kill调用当退出证据', async () => {
    const child = fakeChild({ closeAfterKill: 40 });
    const started = performance.now();
    await assert.rejects(runRestoreCli('fixture-restore.exe', [], async () => { throw new Error('fixture pipeline'); }, {
        spawnProcess: () => child, reapTimeoutMs: 150
    }), (error) => error.exitConfirmed === true);
    assert.ok(performance.now() - started >= 30);
    assert.deepEqual(child.events, ['kill', 'close']);
});

test('kill抛错也不能跳过未确认退出守卫', async () => {
    const child = fakeChild();
    child.kill = () => { throw new Error('fixture EPERM'); };
    const reported = [];
    await assert.rejects(runRestoreCli('fixture-restore.exe', [], async () => { throw new Error('fixture EPIPE'); }, {
        spawnProcess: () => child, reapTimeoutMs: 20, onUnconfirmed: (info) => reported.push(info)
    }), (error) => error.code === 'CLI_EXIT_UNCONFIRMED' && error.exitConfirmed === false);
    assert.equal(reported.length, 1);
});

test('挂起CLI总时限后kill/close回收，挂起transfer不会无限等待', async () => {
    const child = fakeChild({ closeAfterKill: 5 });
    await assert.rejects(runRestoreCli('fixture-restore.exe', [], async () => new Promise(() => {}), {
        spawnProcess: () => child, timeoutMs: 20, reapTimeoutMs: 100
    }), (error) => error.code === 'CLI_TIMEOUT' && error.exitConfirmed === true);
    assert.deepEqual(child.events, ['kill', 'close']);
});
