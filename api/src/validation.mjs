export const maximumBodyBytes = 5 * 1024 * 1024;
export const maximumParts = 10000;

/** 可直接传给调用方的业务错误；不承载数据库内部信息。 */
export class ApiError extends Error {
    constructor(status, code, message) {
        super(message);
        this.status = status;
        this.code = code;
    }
}

const invalid = (message) => { throw new ApiError(400, 'VALIDATION_ERROR', message); };

/** 保留身份原文；拒绝MySQL无法无损存储的不成对UTF-16代理字符。 */
export function validateText(value, path, id = false) {
    if (typeof value !== 'string' || value.trim().length === 0)
        invalid(`${path} 必须是非空白字符串。`);
    if (!value.isWellFormed()) invalid(`${path} 含无效 Unicode 字符。`);
    if (id && value.length > 128) invalid(`${path} 超过128个UTF-16单元。`);
    return value;
}

function object(value, path) {
    if (value === null || typeof value !== 'object' || Array.isArray(value))
        invalid(`${path} 必须是对象。`);
}

function numbers(value, count, positive, path) {
    if (!Array.isArray(value) || value.length !== count)
        invalid(`${path} 必须包含${count}个数值。`);
    if (!value.every((item) => typeof item === 'number' && Number.isFinite(item)
        && (!positive || item > 0))) invalid(`${path} 必须是${positive ? '正' : ''}有限数值。`);
    return [...value];
}

/** 完整验证并复制可发布数据；忽略额外字段，失败不写数据库。 */
export function validateEquipment(value) {
    object(value, '设备');
    const equipmentId = validateText(value.equipmentId, 'equipmentId', true);
    const equipmentName = validateText(value.equipmentName, 'equipmentName');
    if (!Array.isArray(value.parts)) invalid('parts 必须是数组。');
    if (value.parts.length > maximumParts)
        throw new ApiError(413, 'TOO_MANY_PARTS', 'parts 超过10000个零件。');
    const ids = new Set();
    const parts = value.parts.map((part, index) => {
        const path = `parts[${index}]`;
        object(part, path);
        const id = validateText(part.id, `${path}.id`, true);
        if (ids.has(id)) invalid(`${path}.id 重复。`);
        ids.add(id);
        const name = validateText(part.name, `${path}.name`);
        const material = validateText(part.material, `${path}.material`);
        if (part.shape !== 'box' && part.shape !== 'cylinder')
            invalid(`${path}.shape 只允许box或cylinder。`);
        return { id, name, material, shape: part.shape,
            dimensions: numbers(part.dimensions, part.shape === 'box' ? 3 : 2, true, `${path}.dimensions`),
            position: numbers(part.position, 3, false, `${path}.position`) };
    });
    return { equipmentId, equipmentName, parts };
}
