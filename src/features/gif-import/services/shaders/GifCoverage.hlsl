struct Shape {
    float x, y, width, height;
    float cosine, sine, skewTangent, skewCosine;
    uint kind;
};
StructuredBuffer<Shape> shapes : register(t0);
StructuredBuffer<uint2> tiles : register(t1);
StructuredBuffer<uint> indices : register(t2);
RWStructuredBuffer<uint> coverage : register(u0);
cbuffer Parameters : register(b0) {
    uint width, height, tilesWide, protectTransparency;
};

uint sampleShape(Shape form, float2 position) {
    if (form.width <= 0 || form.height <= 0 || abs(form.skewCosine) < 0.000001) return 0;
    precise float dx = position.x - form.x;
    precise float dy = position.y - form.y;
    precise float rotatedX = dx * form.cosine + dy * form.sine;
    precise float rotatedY = -dx * form.sine + dy * form.cosine;
    precise float localX = rotatedX + form.skewTangent * rotatedY;
    precise float localY = rotatedY / form.skewCosine;
    if (form.kind == 2) {
        precise float nx = localX / (form.width * 0.5);
        precise float ny = localY / (form.height * 0.5);
        precise float radiusSquared = nx * nx + ny * ny;
        if (radiusSquared > 1) return 0;
        // Match the native circle fringe used by CPU opaque-coverage checks.
        return radiusSquared < 0.99 * 0.99 ? 3 : 1;
    }
    if (form.kind == 3 || form.kind == 4) {
        precise float u = localX / form.width + 0.5;
        precise float v = localY / form.height + 0.5;
        if (u < 0 || v < 0 || u > 1 || v > 1 || u + v > 1) return 0;
        return localX / form.width + localY / form.height < -(1.5 / 120.0) ? 3 : 1;
    }
    return abs(localX) <= form.width * 0.5 && abs(localY) <= form.height * 0.5 ? 3 : 0;
}

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID) {
    if (thread.x >= width || thread.y >= height) return;
    uint2 tile = tiles[(thread.y / 8) * tilesWide + thread.x / 8];
    uint filled = 0, opaque = 0, centers = 0;
    for (uint entry = tile.x; entry < tile.y; ++entry) {
        Shape form = shapes[indices[entry]];
        [unroll] for (uint sy = 0; sy < 4; ++sy) {
            [unroll] for (uint sx = 0; sx < 4; ++sx) {
                uint bit = 1u << (sy * 4 + sx);
                if ((opaque & bit) != 0) continue;
                uint sample = sampleShape(form, float2(thread.xy) + (float2(sx, sy) + 0.5) * 0.25);
                if (sample & 1) filled |= bit;
                if (sample & 2) opaque |= bit;
            }
        }
        if (protectTransparency) {
            [unroll] for (uint cy = 0; cy < 3; ++cy) {
                [unroll] for (uint cx = 0; cx < 3; ++cx) {
                    uint bit = 1u << (cy * 3 + cx);
                    if ((centers & bit) == 0 && sampleShape(form,
                        float2(thread.xy) + float2(0.4375 + cx * 0.0625, 0.4375 + cy * 0.0625))) centers |= bit;
                }
            }
        }
        if (opaque == 65535 && (!protectTransparency || centers == 511)) break;
    }
    coverage[thread.y * width + thread.x] = countbits(filled) | (countbits(opaque) << 5) | (centers << 10);
}
