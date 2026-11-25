// Integer voting preserves CPU alpha thresholds and first-color tie breaking.
ByteAddressBuffer pixels : register(t0);
ByteAddressBuffer removed : register(t1);
ByteAddressBuffer lookup : register(t2);
RWStructuredBuffer<int> cells : register(u0);
cbuffer Parameters : register(b0) {
    uint width, height, sourceWidth, sourceHeight;
    uint stride, alphaThreshold, colors, reserved;
};
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID) {
    if (thread.x >= width || thread.y >= height) return;
    uint x0 = thread.x * sourceWidth / width;
    uint y0 = thread.y * sourceHeight / height;
    uint x1 = min(sourceWidth, max(x0 + 1, ((thread.x + 1) * sourceWidth + width - 1) / width));
    uint y1 = min(sourceHeight, max(y0 + 1, ((thread.y + 1) * sourceHeight + height - 1) / height));
    uint votes[64];
    for (uint color = 0; color < colors; ++color) votes[color] = 0;
    uint alphaSum = 0, samples = 0;
    for (uint y = y0; y < y1; y += stride) for (uint x = x0; x < x1; x += stride) {
        uint position = y * sourceWidth + x;
        ++samples;
        if (((removed.Load(position & ~3u) >> ((position & 3) * 8)) & 255) != 0) continue;
        uint pixel = pixels.Load(position * 4);
        uint alpha = pixel >> 24;
        alphaSum += alpha;
        if (alpha < alphaThreshold) continue;
        uint key = ((pixel >> 3) & 31) << 10 | ((pixel >> 11) & 31) << 5 | ((pixel >> 19) & 31);
        uint packed = lookup.Load((key & ~1u) * 2);
        uint label = (packed >> ((key & 1) * 16)) & 65535;
        votes[label] += alpha;
    }
    int winner = -1;
    uint best = 0;
    if (samples != 0 && alphaSum / samples >= alphaThreshold) {
        for (uint color = 0; color < colors; ++color) {
            if (votes[color] > best) { best = votes[color]; winner = int(color); }
        }
    }
    cells[thread.y * width + thread.x] = winner;
}
