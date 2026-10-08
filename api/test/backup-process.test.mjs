import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import { pipeDumpUntilClosed } from '../tools/backup-mysql.mjs';

function childFixture() {
    const child = new EventEmitter();
    child.stdout = new PassThrough(); child.stderr = new PassThrough(); child.kills = 0;
    child.unref = () => {};
    return child;
}

test('dump管道失败先kill再等待真实close，不能早于退出返回', async () => {
    const child = childFixture(); let confirmed = false;
    child.kill = () => { child.kills += 1; setTimeout(() => { confirmed = true; child.emit('close', 1); }, 10); return true; };
    await assert.rejects(pipeDumpUntilClosed(child, new PassThrough(), {
        runPipeline: async () => { throw new Error('controlled pipe failure'); }, operationTimeoutMs: 1000, terminateGraceMs: 100
    }), { code: 'DUMP_FAILED' });
    assert.equal(child.kills, 1); assert.equal(confirmed, true);
});

test('dump kill无close有界失败，不误报已收尾，不等无限管道', async () => {
    const child = childFixture(); child.kill = () => { child.kills += 1; return true; };
    const started = Date.now();
    await assert.rejects(pipeDumpUntilClosed(child, new PassThrough(), {
        runPipeline: () => new Promise(() => {}), operationTimeoutMs: 20, terminateGraceMs: 20
    }), { code: 'DUMP_EXIT_UNCONFIRMED' });
    assert.ok(Date.now() - started < 500);
    assert.equal(child.kills, 1);
    assert.equal(child.stdout.destroyed, true); assert.equal(child.stderr.destroyed, true);
    child.emit('close', 1);
});

test('dump正常pipeline完成仍等待close，确认0后才成功，不kill', async () => {
    const child = childFixture(); let confirmed = false;
    child.kill = () => { child.kills += 1; return true; };
    const output = new PassThrough(); output.resume();
    const operation = pipeDumpUntilClosed(child, output, { operationTimeoutMs: 1000 });
    child.stdout.end('controlled SQL bytes');
    setTimeout(() => { confirmed = true; child.emit('close', 0); }, 10);
    await operation;
    assert.equal(confirmed, true); assert.equal(child.kills, 0);
});

test('dump kill抛EPERM仍销毁3管道/有界wait，并明确退出未确认', async () => {
    const child = childFixture();
    child.kill = () => { throw Object.assign(new Error('controlled kill failure'), { code: 'EPERM' }); };
    const output = new PassThrough();
    const started = Date.now();
    await assert.rejects(pipeDumpUntilClosed(child, output, { runPipeline: async () => { throw new Error('controlled pipe failure'); },
        operationTimeoutMs: 1000, terminateGraceMs: 20 }), { code: 'DUMP_EXIT_UNCONFIRMED' });
    assert.ok(Date.now() - started < 500);
    assert.equal(child.stdout.destroyed, true); assert.equal(child.stderr.destroyed, true); assert.equal(output.destroyed, true);
    child.emit('close', 1);
});
