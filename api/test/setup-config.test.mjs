import test from 'node:test';
import assert from 'node:assert/strict';
import { matchesExpectedCore, isClientConfig } from '../tools/setup-mysql.mjs';

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
