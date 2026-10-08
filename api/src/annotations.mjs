import { createHash } from 'node:crypto';
import { ApiError, validateText } from './validation.mjs';

const invalid = (message) => { throw new ApiError(400, 'VALIDATION_ERROR', message); };
function object(value) {
    if (value === null || typeof value !== 'object' || Array.isArray(value)) invalid('批注必须是JSON对象。');
}
function limitedText(value, field, maximum) {
    validateText(value, field);
    if (value.length > maximum) invalid(`${field} 超过${maximum}个UTF-16单元。`);
    return value;
}

/** UUID统一小写，规范形状校验；不使用显示名作为身份。 */
export function validateUuid(value) {
    if (typeof value !== 'string' || !/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i.test(value))
        invalid('requestId/id 必须是UUID。');
    return value.toLowerCase();
}

/** 创建请求的语义规范化，保留原文身份及内容；缺省partId为设备级批注。 */
export function validateAnnotationCreate(value) {
    object(value);
    return { requestId: validateUuid(value.requestId), equipmentId: validateText(value.equipmentId, 'equipmentId', true),
        partId: value.partId === undefined || value.partId === null ? null : validateText(value.partId, 'partId', true),
        text: limitedText(value.text, 'text', 2000), author: limitedText(value.author, 'author', 80) };
}

/** 修改必须显式提交期望revision，保留创建者/关联身份/创建时间。 */
export function validateAnnotationUpdate(value) {
    object(value);
    if (!Number.isInteger(value.expectedRevision) || value.expectedRevision < 1 || value.expectedRevision > 2147483646)
        invalid('expectedRevision 必须是1..2147483646的整数。');
    return { text: limitedText(value.text, 'text', 2000), editor: limitedText(value.editor, 'editor', 80),
        expectedRevision: value.expectedRevision };
}

/** 幂等指纹基于已校验的原始创建业务字段；后续PUT不改变指纹。 */
export function annotationFingerprint(value) {
    return createHash('sha256').update(JSON.stringify({ equipmentId: value.equipmentId, partId: value.partId,
        text: value.text, author: value.author })).digest('hex');
}

/** 严格解码批注scope，未知/重复/无效编码参数一律拒绝。 */
export function validateAnnotationQuery(query) {
    const parameters = new Map();
    for (const entry of query.split('&')) {
        if (!entry) continue;
        let key;
        let value;
        try {
            const equals = entry.indexOf('=');
            key = decodeURIComponent((equals < 0 ? entry : entry.slice(0, equals)).replace(/\+/g, ' '));
            value = decodeURIComponent((equals < 0 ? '' : entry.slice(equals + 1)).replace(/\+/g, ' '));
        } catch { invalid('批注查询参数URL编码无效。'); }
        if (!['equipmentId', 'partId'].includes(key) || parameters.has(key)) invalid('批注查询参数无效或重复。');
        parameters.set(key, value);
    }
    return { equipmentId: validateText(parameters.get('equipmentId'), 'equipmentId', true),
        partId: parameters.has('partId') ? validateText(parameters.get('partId'), 'partId', true) : null };
}
