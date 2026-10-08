import test from 'node:test';
import assert from 'node:assert/strict';
import { annotationFingerprint, validateAnnotationCreate, validateAnnotationUpdate, validateAnnotationQuery, validateUuid } from '../src/annotations.mjs';

const fixture = () => ({ requestId: '86EE66F3-BE48-4EBE-B33B-0F719C9B4B7F', equipmentId: '中文 A ',
    partId: '同名/a ', text: "工艺 <b>原文</b>'", author: '创建者甲' });

test('批注精确scope、规范UUID、原始payload幂等指纹与可选设备级关联', () => {
    const value = validateAnnotationCreate(fixture());
    assert.equal(value.requestId, fixture().requestId.toLowerCase());
    assert.equal(value.equipmentId, fixture().equipmentId);
    assert.equal(value.partId, fixture().partId);
    assert.equal(annotationFingerprint(value), annotationFingerprint(validateAnnotationCreate(fixture())));
    assert.notEqual(annotationFingerprint(value), annotationFingerprint({ ...value, text: '修改内容' }));
    assert.equal(validateAnnotationCreate({ ...fixture(), partId: undefined }).partId, null);
    assert.equal(validateUuid(value.requestId), value.requestId);
});

test('批注文本2000/作者80 UTF16边界、Unicode、修订整数与不可变输入校验', () => {
    assert.equal(validateAnnotationCreate({ ...fixture(), text: '😀'.repeat(1000), author: '😀'.repeat(40) }).text.length, 2000);
    for (const change of [{ requestId: 'bad' }, { equipmentId: ' ' }, { partId: '' }, { text: ' ' },
        { text: 'x'.repeat(2001) }, { text: '\uD800' }, { author: 'x'.repeat(81) }, { author: 1 }])
        assert.throws(() => validateAnnotationCreate({ ...fixture(), ...change }));
    const update = { text: '修改', editor: '编辑者', expectedRevision: 2147483646 };
    assert.deepEqual(validateAnnotationUpdate(update), update);
    for (const revision of [0, -1, 1.5, '1', 2147483647, NaN])
        assert.throws(() => validateAnnotationUpdate({ ...update, expectedRevision: revision }));
    assert.throws(() => validateAnnotationUpdate({ ...update, editor: ' ' }));
    for (const value of [null, [], 'text']) assert.throws(() => validateAnnotationCreate(value));
});

test('批注查询严格编码、必填scope、重复/未知拒绝，精确加号尾空格保持', () => {
    assert.deepEqual(validateAnnotationQuery('equipmentId=.&partId=' + encodeURIComponent('a/+ ')), { equipmentId: '.', partId: 'a/+ ' });
    assert.deepEqual(validateAnnotationQuery('equipmentId=..'), { equipmentId: '..', partId: null });
    for (const query of ['', 'partId=a', 'equipmentId=', 'equipmentId=%FF', 'equipmentId=%ZZ',
        'equipmentId=a&equipmentId=b', 'equipmentId=a&partId=', 'equipmentId=a&unknown=b'])
        assert.throws(() => validateAnnotationQuery(query));
});
