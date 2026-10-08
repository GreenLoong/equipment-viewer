import test from 'node:test';
import assert from 'node:assert/strict';
import { ApiError, validateEquipment, validateText } from '../src/validation.mjs';

export function sample(id = '设备A') {
    return { equipmentId: id, equipmentName: '中文设备', parts: [
        { id: '零件1', name: '同名', material: "钢'材", shape: 'box', dimensions: [1, 2, 3], position: [0, -2, 3] },
        { id: '零件2', name: '同名', material: '铝', shape: 'cylinder', dimensions: [2, 10], position: [1, 2, 3] }
    ] };
}

test('校验保留精确身份、同名、全部几何和顺序，忽略额外字段且复制输入', () => {
    const input = sample('A ');
    input.extra = true;
    input.parts[0].extra = true;
    const result = validateEquipment(input);
    assert.equal(result.equipmentId, 'A ');
    assert.equal(result.extra, undefined);
    assert.equal(result.parts[0].extra, undefined);
    assert.deepEqual(result.parts.map((part) => part.name), ['同名', '同名']);
    input.parts[0].dimensions[0] = 500;
    assert.equal(result.parts[0].dimensions[0], 1);
    assert.deepEqual(validateEquipment({ ...sample(), parts: [] }).parts, []);
});

test('严格类型、必填值、几何和重复ID校验', () => {
    for (const mutate of [
        (value) => { value.equipmentId = ' \n'; },
        (value) => { value.equipmentName = 2; },
        (value) => { value.parts = {}; },
        (value) => { value.parts[0] = []; },
        (value) => { value.parts[0].id = value.parts[1].id; },
        (value) => { value.parts[0].material = null; },
        (value) => { value.parts[0].shape = 'sphere'; },
        (value) => { value.parts[0].dimensions = [1, 2]; },
        (value) => { value.parts[1].dimensions = [1, 2, 3]; },
        (value) => { value.parts[0].dimensions[0] = 0; },
        (value) => { value.parts[0].position[0] = '2'; },
        (value) => { value.parts[0].position[0] = Infinity; },
        (value) => { value.parts[0].name = '\uD800'; }
    ]) {
        const value = sample();
        mutate(value);
        assert.throws(() => validateEquipment(value), (error) => error instanceof ApiError && error.status === 400);
    }
    for (const value of [null, [], 1, 'text']) assert.throws(() => validateEquipment(value));
});

test('ID以UTF-16单元限长，代理字符配对，大小写与尾空格区分', () => {
    assert.equal(validateText('😀'.repeat(64), 'id', true).length, 128);
    assert.throws(() => validateText('😀'.repeat(65), 'id', true));
    assert.throws(() => validateText('x'.repeat(129), 'id', true));
    assert.throws(() => validateText('\uDC00', 'id', true));
    const value = sample();
    value.parts.push({ ...value.parts[0], id: '零件1 ' }, { ...value.parts[0], id: 'a' }, { ...value.parts[0], id: 'A' });
    assert.equal(validateEquipment(value).parts.length, 5);
});

test('10000零件边界接受，10001拒绝413', () => {
    const value = sample();
    value.parts = Array.from({ length: 10000 }, (_, index) => ({ ...sample().parts[0], id: `P${index}` }));
    assert.equal(validateEquipment(value).parts.length, 10000);
    value.parts.push({ ...value.parts[0], id: 'P10000' });
    assert.throws(() => validateEquipment(value), (error) => error.status === 413);
});
