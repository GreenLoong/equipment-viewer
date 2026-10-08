import test from 'node:test';
import assert from 'node:assert/strict';
import { matchesExpectedCore, isClientConfig } from '../tools/setup-mysql.mjs';
import { preparePrivateOptionFile } from '../tools/backup-mysql.mjs';

test('setup核心核对保留扩展字段与key顺序，拒绝身份/秘密变更', () => {
    const expected = { host: '127.0.0.1', port: 8765, token: 'fixture-token', mysql: {
        host: '127.0.0.1', port: 3310, user: 'ev_app', password: 'fixture-password', database: 'ev_test', connectionLimit: 4 } };
    const current = { mysql: { ...expected.mysql, ssl: false }, token: expected.token, port: 8765, host: '127.0.0.1',
        operationTimeoutMs: 15000, closeTimeoutMs: 1000, redis: { port: 6379 } };
    assert.equal(matchesExpectedCore(current, expected), true);
    assert.equal(current.operationTimeoutMs, 15000);
    assert.equal(matchesExpectedCore({ ...current, mysql: { ...current.mysql, database: 'ev_shared' } }, expected), false);
    assert.equal(matchesExpectedCore({ ...current, token: 'changed' }, expected), false);
});

test('clientDTO严格仅host/port/token，不含MySQL或其他秘密', () => {
    const client = { host: '127.0.0.1', port: 8765, token: 'fixture-token' };
    assert.equal(isClientConfig(client, client), true);
    assert.equal(isClientConfig({ ...client, mysql: {} }, client), false);
    assert.equal(isClientConfig({ ...client, password: 'fixture-password' }, client), false);
});

test('临时选项open成功后写入失败也关闭并删除本次文件，不制造真实ENOSPC', async () => {
    const calls = [];
    await assert.rejects(preparePrivateOptionFile('fixture.local.ini', 'fixture contents', {
        openFile: async (_filename, flags, mode) => {
            assert.equal(flags, 'wx');
            assert.equal(mode, 0o600);
            calls.push('open');
            return { writeFile: async () => { calls.push('write'); throw Object.assign(new Error('simulated write failure'), { code: 'ENOSPC' }); },
                close: async () => { calls.push('close'); } };
        }, unlinkFile: async () => { calls.push('unlink'); },
        onCleanupFailure: () => assert.fail('收尾替身不应失败')
    }), { code: 'ENOSPC' });
    assert.deepEqual(calls, ['open', 'write', 'close', 'unlink']);
});

test('临时选项open EEXIST未取得所有权，不unlink已有文件', async () => {
    let unlinked = false;
    await assert.rejects(preparePrivateOptionFile('existing.local.ini', 'fixture contents', {
        openFile: async () => { throw Object.assign(new Error('simulated existing file'), { code: 'EEXIST' }); },
        unlinkFile: async () => { unlinked = true; }, onCleanupFailure: () => assert.fail('未拥有文件无需收尾')
    }), { code: 'EEXIST' });
    assert.equal(unlinked, false);
});

test('临时选项正常写完先close，使用结束后unlink；重复cleanup不再删除', async () => {
    const calls = [];
    const cleanup = await preparePrivateOptionFile('normal.local.ini', 'fixture contents', {
        openFile: async () => ({ writeFile: async () => { calls.push('write'); }, close: async () => { calls.push('close'); } }),
        unlinkFile: async () => { calls.push('unlink'); }, onCleanupFailure: () => assert.fail('正常收尾不应失败')
    });
    assert.deepEqual(calls, ['write', 'close']);
    await cleanup();
    await cleanup();
    assert.deepEqual(calls, ['write', 'close', 'unlink']);
});
