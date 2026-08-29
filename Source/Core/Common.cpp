#include "Common.h"
#include "FileSystem.h"
#include <cstdio>

namespace painful {

Mat4 Mat4::InvertAffine(const Mat4& x) {
    const float* m = x.m;
    double a = m[0], b = m[1], c = m[2];
    double d = m[4], e = m[5], f = m[6];
    double g = m[8], h = m[9], i = m[10];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det > -1e-12 && det < 1e-12) det = 1e-12;
    double id = 1.0 / det;
    Mat4 r;
    r.m[0]  = static_cast<float>((e * i - f * h) * id);
    r.m[1]  = static_cast<float>((c * h - b * i) * id);
    r.m[2]  = static_cast<float>((b * f - c * e) * id);
    r.m[3]  = 0.f;
    r.m[4]  = static_cast<float>((f * g - d * i) * id);
    r.m[5]  = static_cast<float>((a * i - c * g) * id);
    r.m[6]  = static_cast<float>((c * d - a * f) * id);
    r.m[7]  = 0.f;
    r.m[8]  = static_cast<float>((d * h - e * g) * id);
    r.m[9]  = static_cast<float>((b * g - a * h) * id);
    r.m[10] = static_cast<float>((a * e - b * d) * id);
    r.m[11] = 0.f;
    double tx = m[12], ty = m[13], tz = m[14];
    r.m[12] = static_cast<float>(-(tx * r.m[0] + ty * r.m[4] + tz * r.m[8]));
    r.m[13] = static_cast<float>(-(tx * r.m[1] + ty * r.m[5] + tz * r.m[9]));
    r.m[14] = static_cast<float>(-(tx * r.m[2] + ty * r.m[6] + tz * r.m[10]));
    r.m[15] = 1.f;
    return r;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    // Mounted .pak archives shadow loose files, matching the original
    // engine's mount order; anything they don't serve is read from disk.
    if (FileSystem::Get().ReadPakFile(path, out)) return true;

    FILE* fp = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) return false;
#else
    fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
#endif
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (n < 0) { std::fclose(fp); return false; }
    out.resize(static_cast<size_t>(n));
    size_t rd = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    return rd == out.size();
}

} // namespace painful
