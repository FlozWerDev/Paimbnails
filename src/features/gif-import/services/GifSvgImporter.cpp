#include "GifSvgImporter.hpp"

#include "ColorSpace.hpp"
#include "GifMotionPlanner.hpp"
#include "GifParallel.hpp"
#include "GifShapeRaster.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace paimon::gifimport {

namespace {

constexpr double kPiD = 3.14159265358979323846;

struct Pt {
    double x = 0.0;
    double y = 0.0;
};

Pt operator+(Pt a, Pt b) { return {a.x + b.x, a.y + b.y}; }
Pt operator-(Pt a, Pt b) { return {a.x - b.x, a.y - b.y}; }
Pt operator*(Pt a, double s) { return {a.x * s, a.y * s}; }

double ptDistSq(Pt a, Pt b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}
double ptDist(Pt a, Pt b) { return std::hypot(a.x - b.x, a.y - b.y); }
double ptCross(Pt a, Pt b) { return a.x * b.y - a.y * b.x; }
double ptDot(Pt a, Pt b) { return a.x * b.x + a.y * b.y; }

struct Mat2D {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;

    Pt apply(Pt p) const {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }

    Mat2D mul(Mat2D const& o) const {
        return {
            a * o.a + c * o.b,
            b * o.a + d * o.b,
            a * o.c + c * o.d,
            b * o.c + d * o.d,
            a * o.e + c * o.f + e,
            b * o.e + d * o.f + f
        };
    }
};

struct Seg {
    bool cubic = false;
    Pt p0{}, c1{}, c2{}, p1{};
};

Seg reverseSeg(Seg const& s) {
    if (!s.cubic) return {false, s.p1, s.p1, s.p0, s.p0};
    return {true, s.p1, s.c2, s.c1, s.p0};
}

double segChordLen(Seg const& s) {
    return ptDist(s.p0, s.p1);
}

using Ring = std::vector<Seg>;

struct ParsedShape {
    std::vector<Ring> rings;
    Color color{};
    float opacity = 1.f;
    bool evenOdd = false;
    // Direct parametric hints for layered SVGs (rectangles / circles / ellipses).
    bool isDirectEllipse = false;
    Pt ellipseCenter{};
    double ellipseRx = 0.0;
    double ellipseRy = 0.0;
    double ellipseAngleRad = 0.0;
    bool isDirectRect = false;
    std::array<Pt, 4> rectCorners{};
};

// -------------------- String / Color / Transform Parsing --------------------

std::string trim(std::string_view sv) {
    std::size_t s = 0;
    while (s < sv.size() && std::isspace(static_cast<unsigned char>(sv[s]))) ++s;
    std::size_t e = sv.size();
    while (e > s && std::isspace(static_cast<unsigned char>(sv[e - 1]))) --e;
    return std::string(sv.substr(s, e - s));
}

std::string toLower(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

double parseLength(std::string_view sv, double fallback = 0.0) {
    std::string s = trim(sv);
    if (s.empty()) return fallback;
    char* end = nullptr;
    double val = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return fallback;
    std::string unit = toLower(trim(std::string_view(end)));
    if (unit == "pt") val *= 1.3333333333;
    else if (unit == "pc") val *= 16.0;
    else if (unit == "mm") val *= 3.7795275591;
    else if (unit == "cm") val *= 37.795275591;
    else if (unit == "in") val *= 96.0;
    return val;
}

std::vector<double> parseNumbers(std::string_view sv) {
    std::vector<double> out;
    char const* p = sv.data();
    char const* end = sv.data() + sv.size();
    while (p < end) {
        while (p < end && !(std::isdigit(static_cast<unsigned char>(*p)) ||
                            *p == '-' || *p == '+' || *p == '.')) {
            ++p;
        }
        if (p >= end) break;
        char* next = nullptr;
        double v = std::strtod(p, &next);
        if (next == p) {
            ++p;
            continue;
        }
        out.push_back(v);
        p = next;
    }
    return out;
}

std::optional<Color> parseSvgColor(std::string_view raw, bool& isNone) {
    isNone = false;
    std::string s = toLower(trim(raw));
    if (s.empty()) return std::nullopt;
    if (s == "none" || s == "transparent") {
        isNone = true;
        return std::nullopt;
    }
    if (s[0] == '#') {
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return -1;
        };
        if (s.size() == 4) {
            int r = hexVal(s[1]), g = hexVal(s[2]), b = hexVal(s[3]);
            if (r >= 0 && g >= 0 && b >= 0) {
                return Color{static_cast<std::uint8_t>(r * 17),
                             static_cast<std::uint8_t>(g * 17),
                             static_cast<std::uint8_t>(b * 17)};
            }
        } else if (s.size() == 7 || s.size() == 9) {
            int r1 = hexVal(s[1]), r2 = hexVal(s[2]);
            int g1 = hexVal(s[3]), g2 = hexVal(s[4]);
            int b1 = hexVal(s[5]), b2 = hexVal(s[6]);
            if (r1 >= 0 && r2 >= 0 && g1 >= 0 && g2 >= 0 && b1 >= 0 && b2 >= 0) {
                return Color{static_cast<std::uint8_t>((r1 << 4) | r2),
                             static_cast<std::uint8_t>((g1 << 4) | g2),
                             static_cast<std::uint8_t>((b1 << 4) | b2)};
            }
        }
        return std::nullopt;
    }
    if (s.rfind("rgb(", 0) == 0 || s.rfind("rgba(", 0) == 0) {
        auto nums = parseNumbers(s);
        if (nums.size() >= 3) {
            return Color{
                static_cast<std::uint8_t>(std::clamp(std::lround(nums[0]), 0l, 255l)),
                static_cast<std::uint8_t>(std::clamp(std::lround(nums[1]), 0l, 255l)),
                static_cast<std::uint8_t>(std::clamp(std::lround(nums[2]), 0l, 255l))
            };
        }
        return std::nullopt;
    }
    static std::unordered_map<std::string, Color> const kNamed{
        {"black", {0, 0, 0}}, {"white", {255, 255, 255}}, {"red", {255, 0, 0}},
        {"green", {0, 128, 0}}, {"lime", {0, 255, 0}}, {"blue", {0, 0, 255}},
        {"yellow", {255, 255, 0}}, {"cyan", {0, 255, 255}}, {"magenta", {255, 0, 255}},
        {"gray", {128, 128, 128}}, {"grey", {128, 128, 128}}, {"silver", {192, 192, 192}},
        {"orange", {255, 165, 0}}, {"purple", {128, 0, 128}}, {"pink", {255, 192, 203}},
        {"brown", {165, 42, 42}}, {"navy", {0, 0, 128}}, {"teal", {0, 128, 128}},
        {"maroon", {128, 0, 0}}, {"olive", {128, 128, 0}}, {"gold", {255, 215, 0}},
    };
    auto it = kNamed.find(s);
    if (it != kNamed.end()) return it->second;
    return std::nullopt;
}

Mat2D parseTransform(std::string_view sv) {
    Mat2D result{};
    std::size_t pos = 0;
    while (pos < sv.size()) {
        while (pos < sv.size() && !std::isalpha(static_cast<unsigned char>(sv[pos]))) ++pos;
        if (pos >= sv.size()) break;
        std::size_t nameStart = pos;
        while (pos < sv.size() && std::isalpha(static_cast<unsigned char>(sv[pos]))) ++pos;
        std::string name = toLower(sv.substr(nameStart, pos - nameStart));
        std::size_t open = sv.find('(', pos);
        if (open == std::string_view::npos) break;
        std::size_t close = sv.find(')', open + 1);
        if (close == std::string_view::npos) break;
        auto nums = parseNumbers(sv.substr(open + 1, close - open - 1));
        pos = close + 1;

        Mat2D m{};
        if (name == "matrix" && nums.size() >= 6) {
            m = {nums[0], nums[1], nums[2], nums[3], nums[4], nums[5]};
        } else if (name == "translate" && !nums.empty()) {
            m.e = nums[0];
            m.f = nums.size() >= 2 ? nums[1] : 0.0;
        } else if (name == "scale" && !nums.empty()) {
            m.a = nums[0];
            m.d = nums.size() >= 2 ? nums[1] : nums[0];
        } else if (name == "rotate" && !nums.empty()) {
            double rad = nums[0] * kPiD / 180.0;
            double cs = std::cos(rad), sn = std::sin(rad);
            if (nums.size() >= 3) {
                double cx = nums[1], cy = nums[2];
                Mat2D t1{1, 0, 0, 1, cx, cy};
                Mat2D r{cs, sn, -sn, cs, 0, 0};
                Mat2D t2{1, 0, 0, 1, -cx, -cy};
                m = t1.mul(r).mul(t2);
            } else {
                m = {cs, sn, -sn, cs, 0, 0};
            }
        } else if (name == "skewx" && !nums.empty()) {
            m.c = std::tan(nums[0] * kPiD / 180.0);
        } else if (name == "skewy" && !nums.empty()) {
            m.b = std::tan(nums[0] * kPiD / 180.0);
        }
        result = result.mul(m);
    }
    return result;
}

// Convert SVG elliptical arc to 1..4 cubic Bezier segments.
void appendArcAsCubics(
    Ring& ring, Pt p0, double rx, double ry, double xAxisRotDeg,
    bool largeArc, bool sweep, Pt p1, Mat2D const& xform
) {
    if (ptDist(p0, p1) < 1e-9) return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-9 || ry < 1e-9) {
        ring.push_back({false, xform.apply(p0), {}, {}, xform.apply(p1)});
        return;
    }
    double phi = xAxisRotDeg * kPiD / 180.0;
    double cPhi = std::cos(phi), sPhi = std::sin(phi);
    double dx2 = (p0.x - p1.x) * 0.5;
    double dy2 = (p0.y - p1.y) * 0.5;
    double x1p = cPhi * dx2 + sPhi * dy2;
    double y1p = -sPhi * dx2 + cPhi * dy2;
    double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0) {
        double s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
    }
    double rx2 = rx * rx, ry2 = ry * ry;
    double x1p2 = x1p * x1p, y1p2 = y1p * y1p;
    double num = std::max(0.0, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2);
    double den = rx2 * y1p2 + ry2 * x1p2;
    double coef = (den > 1e-12) ? std::sqrt(num / den) : 0.0;
    if (largeArc == sweep) coef = -coef;
    double cxp = coef * (rx * y1p / ry);
    double cyp = coef * (-ry * x1p / rx);
    double cx = cPhi * cxp - sPhi * cyp + (p0.x + p1.x) * 0.5;
    double cy = sPhi * cxp + cPhi * cyp + (p0.y + p1.y) * 0.5;

    auto vecAngle = [](double ux, double uy, double vx, double vy) {
        double dot = ux * vx + uy * vy;
        double len = std::hypot(ux, uy) * std::hypot(vx, vy);
        double c = std::clamp(len > 1e-12 ? dot / len : 1.0, -1.0, 1.0);
        double ang = std::acos(c);
        return (ux * vy - uy * vx) < 0.0 ? -ang : ang;
    };

    double theta1 = vecAngle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    double dTheta = vecAngle((x1p - cxp) / rx, (y1p - cyp) / ry,
                             (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dTheta > 0.0) dTheta -= 2.0 * kPiD;
    if (sweep && dTheta < 0.0) dTheta += 2.0 * kPiD;

    int segs = std::max(1, static_cast<int>(std::ceil(std::abs(dTheta) / (kPiD * 0.5))));
    double delta = dTheta / segs;
    double alpha = (4.0 / 3.0) * std::tan(delta * 0.25);

    auto evalPt = [&](double t) -> Pt {
        double ct = std::cos(t), st = std::sin(t);
        return {cx + cPhi * rx * ct - sPhi * ry * st,
                cy + sPhi * rx * ct + cPhi * ry * st};
    };
    auto evalTan = [&](double t) -> Pt {
        double ct = std::cos(t), st = std::sin(t);
        return {-cPhi * rx * st - sPhi * ry * ct,
                -sPhi * rx * st + cPhi * ry * ct};
    };

    for (int i = 0; i < segs; ++i) {
        double t0 = theta1 + i * delta;
        double t1 = t0 + delta;
        Pt a = evalPt(t0);
        Pt b = evalPt(t1);
        Pt ta = evalTan(t0);
        Pt tb = evalTan(t1);
        Pt q1 = a + ta * alpha;
        Pt q2 = b - tb * alpha;
        ring.push_back({true, xform.apply(a), xform.apply(q1), xform.apply(q2), xform.apply(b)});
    }
}

std::vector<Ring> parsePathD(std::string_view d, Mat2D const& xform) {
    std::vector<Ring> rings;
    Ring current;

    char const* p = d.data();
    char const* end = d.data() + d.size();
    auto skipSep = [&]() {
        while (p < end && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p;
    };
    auto readNumber = [&](double& val) -> bool {
        skipSep();
        if (p >= end) return false;
        char* next = nullptr;
        val = std::strtod(p, &next);
        if (next == p) return false;
        p = next;
        return true;
    };
    auto readFlag = [&](bool& val) -> bool {
        skipSep();
        if (p >= end || (*p != '0' && *p != '1')) return false;
        val = (*p == '1');
        ++p;
        return true;
    };
    auto flushRing = [&]() {
        if (!current.empty()) {
            rings.push_back(std::move(current));
            current.clear();
        }
    };

    Pt cur{0, 0}, start{0, 0};
    Pt lastCubicCtrl{0, 0}, lastQuadCtrl{0, 0};
    char prevCmd = 0;
    char cmd = 0;

    while (p < end) {
        skipSep();
        if (p >= end) break;
        if (std::isalpha(static_cast<unsigned char>(*p))) {
            cmd = *p++;
        } else if (cmd == 0) {
            break;
        }
        bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
        char uc = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

        if (uc == 'Z') {
            if (ptDist(cur, start) > 1e-6) {
                current.push_back({false, xform.apply(cur), {}, {}, xform.apply(start)});
            }
            cur = start;
            flushRing();
            prevCmd = uc;
            cmd = 0;
            continue;
        }

        if (uc == 'M') {
            double x = 0, y = 0;
            if (!readNumber(x) || !readNumber(y)) break;
            if (!current.empty()) {
                if (ptDist(cur, start) > 1e-6) {
                    current.push_back({false, xform.apply(cur), {}, {}, xform.apply(start)});
                }
                flushRing();
            }
            cur = rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
            start = cur;
            prevCmd = uc;
            cmd = rel ? 'l' : 'L';
            continue;
        }
        if (uc == 'L') {
            double x = 0, y = 0;
            if (!readNumber(x) || !readNumber(y)) break;
            Pt next = rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
            if (ptDist(cur, next) > 1e-6) {
                current.push_back({false, xform.apply(cur), {}, {}, xform.apply(next)});
            }
            cur = next;
            prevCmd = uc;
            continue;
        }
        if (uc == 'H') {
            double x = 0;
            if (!readNumber(x)) break;
            Pt next = rel ? Pt{cur.x + x, cur.y} : Pt{x, cur.y};
            if (ptDist(cur, next) > 1e-6) {
                current.push_back({false, xform.apply(cur), {}, {}, xform.apply(next)});
            }
            cur = next;
            prevCmd = uc;
            continue;
        }
        if (uc == 'V') {
            double y = 0;
            if (!readNumber(y)) break;
            Pt next = rel ? Pt{cur.x, cur.y + y} : Pt{cur.x, y};
            if (ptDist(cur, next) > 1e-6) {
                current.push_back({false, xform.apply(cur), {}, {}, xform.apply(next)});
            }
            cur = next;
            prevCmd = uc;
            continue;
        }
        if (uc == 'C') {
            double x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;
            if (!readNumber(x1) || !readNumber(y1) || !readNumber(x2) ||
                !readNumber(y2) || !readNumber(x) || !readNumber(y)) break;
            Pt c1 = rel ? Pt{cur.x + x1, cur.y + y1} : Pt{x1, y1};
            Pt c2 = rel ? Pt{cur.x + x2, cur.y + y2} : Pt{x2, y2};
            Pt next = rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
            if (ptDist(cur, next) > 1e-6 || ptDist(cur, c1) > 1e-6 || ptDist(cur, c2) > 1e-6) {
                current.push_back({true, xform.apply(cur), xform.apply(c1),
                                   xform.apply(c2), xform.apply(next)});
            }
            lastCubicCtrl = c2;
            cur = next;
            prevCmd = uc;
            continue;
        }
        if (uc == 'S') {
            double x2 = 0, y2 = 0, x = 0, y = 0;
            if (!readNumber(x2) || !readNumber(y2) || !readNumber(x) || !readNumber(y)) break;
            Pt c1 = (prevCmd == 'C' || prevCmd == 'S')
                ? Pt{2.0 * cur.x - lastCubicCtrl.x, 2.0 * cur.y - lastCubicCtrl.y}
                : cur;
            Pt c2 = rel ? Pt{cur.x + x2, cur.y + y2} : Pt{x2, y2};
            Pt next = rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
            current.push_back({true, xform.apply(cur), xform.apply(c1),
                               xform.apply(c2), xform.apply(next)});
            lastCubicCtrl = c2;
            cur = next;
            prevCmd = uc;
            continue;
        }
        if (uc == 'Q') {
            double x1 = 0, y1 = 0, x = 0, y = 0;
            if (!readNumber(x1) || !readNumber(y1) || !readNumber(x) || !readNumber(y)) break;
            Pt q = rel ? Pt{cur.x + x1, cur.y + y1} : Pt{x1, y1};
            Pt next = rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
            Pt c1 = cur + (q - cur) * (2.0 / 3.0);
            Pt c2 = next + (q - next) * (2.0 / 3.0);
            current.push_back({true, xform.apply(cur), xform.apply(c1),
                               xform.apply(c2), xform.apply(next)});
            lastQuadCtrl = q;
            cur = next;
            prevCmd = uc;
            continue;
        }
        if (uc == 'T') {
            double x = 0, y = 0;
            if (!readNumber(x) || !readNumber(y)) break;
            Pt q = (prevCmd == 'Q' || prevCmd == 'T')
                ? Pt{2.0 * cur.x - lastQuadCtrl.x, 2.0 * cur.y - lastQuadCtrl.y}
                : cur;
            Pt next = rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
            Pt c1 = cur + (q - cur) * (2.0 / 3.0);
            Pt c2 = next + (q - next) * (2.0 / 3.0);
            current.push_back({true, xform.apply(cur), xform.apply(c1),
                               xform.apply(c2), xform.apply(next)});
            lastQuadCtrl = q;
            cur = next;
            prevCmd = uc;
            continue;
        }
        if (uc == 'A') {
            double rx = 0, ry = 0, rot = 0, x = 0, y = 0;
            bool large = false, sweep = false;
            if (!readNumber(rx) || !readNumber(ry) || !readNumber(rot) ||
                !readFlag(large) || !readFlag(sweep) ||
                !readNumber(x) || !readNumber(y)) break;
            Pt next = rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
            appendArcAsCubics(current, cur, rx, ry, rot, large, sweep, next, xform);
            cur = next;
            prevCmd = uc;
            continue;
        }
        break;
    }

    if (!current.empty()) {
        if (ptDist(cur, start) > 1e-6) {
            current.push_back({false, xform.apply(cur), {}, {}, xform.apply(start)});
        }
        flushRing();
    }
    return rings;
}

struct StyleState {
    std::optional<Color> fill = Color{0, 0, 0};
    bool fillNone = false;
    float opacity = 1.f;
    float fillOpacity = 1.f;
    bool evenOdd = false;
    bool hidden = false;
    Mat2D xform{};
};

void applyInlineStyle(std::string_view style, std::unordered_map<std::string, std::string>& attrs) {
    std::size_t pos = 0;
    while (pos < style.size()) {
        std::size_t semi = style.find(';', pos);
        if (semi == std::string_view::npos) semi = style.size();
        std::string_view decl = style.substr(pos, semi - pos);
        std::size_t colon = decl.find(':');
        if (colon != std::string_view::npos) {
            std::string k = toLower(trim(decl.substr(0, colon)));
            std::string v = trim(decl.substr(colon + 1));
            if (!k.empty()) attrs[k] = std::move(v);
        }
        pos = semi + 1;
    }
}

StyleState inheritStyle(StyleState parent, std::unordered_map<std::string, std::string> const& attrs) {
    auto it = attrs.find("transform");
    if (it != attrs.end()) parent.xform = parent.xform.mul(parseTransform(it->second));
    if (auto f = attrs.find("fill"); f != attrs.end()) {
        bool none = false;
        auto col = parseSvgColor(f->second, none);
        if (none) {
            parent.fillNone = true;
            parent.fill = std::nullopt;
        } else if (col) {
            parent.fillNone = false;
            parent.fill = *col;
        }
    }
    if (auto op = attrs.find("opacity"); op != attrs.end()) {
        parent.opacity *= static_cast<float>(std::clamp(parseLength(op->second, 1.0), 0.0, 1.0));
    }
    if (auto fop = attrs.find("fill-opacity"); fop != attrs.end()) {
        parent.fillOpacity *= static_cast<float>(std::clamp(parseLength(fop->second, 1.0), 0.0, 1.0));
    }
    if (auto fr = attrs.find("fill-rule"); fr != attrs.end()) {
        parent.evenOdd = (toLower(trim(fr->second)) == "evenodd");
    }
    if (auto disp = attrs.find("display"); disp != attrs.end()) {
        if (toLower(trim(disp->second)) == "none") parent.hidden = true;
    }
    if (auto vis = attrs.find("visibility"); vis != attrs.end()) {
        if (toLower(trim(vis->second)) == "hidden") parent.hidden = true;
    }
    return parent;
}

struct ParsedSvgDoc {
    double vbX = 0.0, vbY = 0.0, vbW = 0.0, vbH = 0.0;
    std::vector<ParsedShape> shapes;
};

ParsedSvgDoc parseSvgDocument(std::string_view xml) {
    ParsedSvgDoc doc;
    std::vector<StyleState> stack(1);
    int skipDepth = 0;
    struct PendingPath {
        std::size_t shapeIdx = 0;
        std::string d;
        Mat2D xform{};
    };
    std::vector<PendingPath> pendingPaths;

    std::size_t pos = 0;
    while (pos < xml.size()) {
        std::size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) break;
        pos = lt + 1;
        if (pos >= xml.size()) break;

        if (xml.substr(lt, 4) == "<!--") {
            std::size_t endC = xml.find("-->", lt + 4);
            pos = (endC == std::string_view::npos) ? xml.size() : endC + 3;
            continue;
        }
        if (xml[pos] == '?' || xml[pos] == '!') {
            std::size_t gt = xml.find('>', pos);
            pos = (gt == std::string_view::npos) ? xml.size() : gt + 1;
            continue;
        }
        bool closing = false;
        if (xml[pos] == '/') {
            closing = true;
            ++pos;
        }
        std::size_t nameStart = pos;
        while (pos < xml.size() && !std::isspace(static_cast<unsigned char>(xml[pos])) &&
               xml[pos] != '>' && xml[pos] != '/') {
            ++pos;
        }
        std::string tag = toLower(xml.substr(nameStart, pos - nameStart));
        if (auto colon = tag.rfind(':'); colon != std::string::npos) {
            tag = tag.substr(colon + 1);
        }

        // Parse attributes until '>'
        std::unordered_map<std::string, std::string> attrs;
        bool selfClosing = false;
        while (pos < xml.size()) {
            while (pos < xml.size() && std::isspace(static_cast<unsigned char>(xml[pos]))) ++pos;
            if (pos >= xml.size()) break;
            if (xml[pos] == '>') {
                ++pos;
                break;
            }
            if (xml[pos] == '/' && pos + 1 < xml.size() && xml[pos + 1] == '>') {
                selfClosing = true;
                pos += 2;
                break;
            }
            std::size_t kStart = pos;
            while (pos < xml.size() && !std::isspace(static_cast<unsigned char>(xml[pos])) &&
                   xml[pos] != '=' && xml[pos] != '>' && xml[pos] != '/') {
                ++pos;
            }
            std::string key = toLower(xml.substr(kStart, pos - kStart));
            while (pos < xml.size() && std::isspace(static_cast<unsigned char>(xml[pos]))) ++pos;
            std::string val;
            if (pos < xml.size() && xml[pos] == '=') {
                ++pos;
                while (pos < xml.size() && std::isspace(static_cast<unsigned char>(xml[pos]))) ++pos;
                if (pos < xml.size() && (xml[pos] == '"' || xml[pos] == '\'')) {
                    char q = xml[pos++];
                    std::size_t vStart = pos;
                    while (pos < xml.size() && xml[pos] != q) ++pos;
                    val = std::string(xml.substr(vStart, pos - vStart));
                    if (pos < xml.size()) ++pos;
                } else {
                    std::size_t vStart = pos;
                    while (pos < xml.size() && !std::isspace(static_cast<unsigned char>(xml[pos])) &&
                           xml[pos] != '>') ++pos;
                    val = std::string(xml.substr(vStart, pos - vStart));
                }
            }
            if (!key.empty()) attrs[key] = std::move(val);
        }

        if (closing) {
            if (tag == "defs" || tag == "clippath" || tag == "mask" ||
                tag == "symbol" || tag == "lineargradient" || tag == "radialgradient" ||
                tag == "pattern" || tag == "style" || tag == "title" || tag == "desc") {
                if (skipDepth > 0) --skipDepth;
            } else if (stack.size() > 1) {
                stack.pop_back();
            }
            continue;
        }

        if (tag == "defs" || tag == "clippath" || tag == "mask" ||
            tag == "symbol" || tag == "lineargradient" || tag == "radialgradient" ||
            tag == "pattern" || tag == "style" || tag == "title" || tag == "desc") {
            if (!selfClosing) ++skipDepth;
            continue;
        }
        if (skipDepth > 0) continue;

        if (auto stIt = attrs.find("style"); stIt != attrs.end()) {
            applyInlineStyle(stIt->second, attrs);
        }

        StyleState state = inheritStyle(stack.back(), attrs);
        if (!selfClosing) stack.push_back(state);

        if (tag == "svg") {
            if (auto vb = attrs.find("viewbox"); vb != attrs.end()) {
                auto nums = parseNumbers(vb->second);
                if (nums.size() >= 4) {
                    doc.vbX = nums[0];
                    doc.vbY = nums[1];
                    doc.vbW = nums[2];
                    doc.vbH = nums[3];
                }
            }
            if (doc.vbW <= 0.0 && attrs.count("width")) doc.vbW = parseLength(attrs["width"]);
            if (doc.vbH <= 0.0 && attrs.count("height")) doc.vbH = parseLength(attrs["height"]);
            continue;
        }

        float const totalOpacity = state.opacity * state.fillOpacity;
        if (state.hidden || state.fillNone || !state.fill || totalOpacity < 0.05f) continue;

        ParsedShape shape;
        shape.color = *state.fill;
        shape.opacity = totalOpacity;
        shape.evenOdd = state.evenOdd;

        if (tag == "path") {
            auto dIt = attrs.find("d");
            if (dIt == attrs.end()) continue;
            std::size_t const slot = doc.shapes.size();
            doc.shapes.push_back(std::move(shape));
            pendingPaths.push_back({slot, std::move(dIt->second), state.xform});
            continue;
        } else if (tag == "rect") {
            double x = attrs.count("x") ? parseLength(attrs["x"]) : 0.0;
            double y = attrs.count("y") ? parseLength(attrs["y"]) : 0.0;
            double w = attrs.count("width") ? parseLength(attrs["width"]) : 0.0;
            double h = attrs.count("height") ? parseLength(attrs["height"]) : 0.0;
            if (w <= 0.0 || h <= 0.0) continue;
            double rx = attrs.count("rx") ? parseLength(attrs["rx"]) : 0.0;
            double ry = attrs.count("ry") ? parseLength(attrs["ry"]) : rx;
            if (rx <= 0.0 && ry > 0.0) rx = ry;
            rx = std::min(rx, w * 0.5);
            ry = std::min(ry, h * 0.5);
            if (rx < 1e-4 && ry < 1e-4) {
                Pt p0 = state.xform.apply({x, y});
                Pt p1 = state.xform.apply({x + w, y});
                Pt p2 = state.xform.apply({x + w, y + h});
                Pt p3 = state.xform.apply({x, y + h});
                Ring ring{{false, p0, {}, {}, p1}, {false, p1, {}, {}, p2},
                          {false, p2, {}, {}, p3}, {false, p3, {}, {}, p0}};
                shape.rings.push_back(std::move(ring));
                shape.isDirectRect = true;
                shape.rectCorners = {p0, p1, p2, p3};
            } else {
                constexpr double k = 0.5522847498;
                Ring ring;
                auto addL = [&](Pt a, Pt b) {
                    ring.push_back({false, state.xform.apply(a), {}, {}, state.xform.apply(b)});
                };
                auto addC = [&](Pt a, Pt c1, Pt c2, Pt b) {
                    ring.push_back({true, state.xform.apply(a), state.xform.apply(c1),
                                    state.xform.apply(c2), state.xform.apply(b)});
                };
                addL({x + rx, y}, {x + w - rx, y});
                addC({x + w - rx, y}, {x + w - rx * (1 - k), y}, {x + w, y + ry * (1 - k)}, {x + w, y + ry});
                addL({x + w, y + ry}, {x + w, y + h - ry});
                addC({x + w, y + h - ry}, {x + w, y + h - ry * (1 - k)}, {x + w - rx * (1 - k), y + h}, {x + w - rx, y + h});
                addL({x + w - rx, y + h}, {x + rx, y + h});
                addC({x + rx, y + h}, {x + rx * (1 - k), y + h}, {x, y + h - ry * (1 - k)}, {x, y + h - ry});
                addL({x, y + h - ry}, {x, y + ry});
                addC({x, y + ry}, {x, y + ry * (1 - k)}, {x + rx * (1 - k), y}, {x + rx, y});
                shape.rings.push_back(std::move(ring));
            }
        } else if (tag == "circle" || tag == "ellipse") {
            double cx = attrs.count("cx") ? parseLength(attrs["cx"]) : 0.0;
            double cy = attrs.count("cy") ? parseLength(attrs["cy"]) : 0.0;
            double rx = (tag == "circle")
                ? (attrs.count("r") ? parseLength(attrs["r"]) : 0.0)
                : (attrs.count("rx") ? parseLength(attrs["rx"]) : 0.0);
            double ry = (tag == "circle")
                ? rx
                : (attrs.count("ry") ? parseLength(attrs["ry"]) : 0.0);
            if (rx <= 0.0 || ry <= 0.0) continue;
            constexpr double k = 0.5522847498;
            Ring ring;
            auto addC = [&](Pt a, Pt c1, Pt c2, Pt b) {
                ring.push_back({true, state.xform.apply(a), state.xform.apply(c1),
                                state.xform.apply(c2), state.xform.apply(b)});
            };
            addC({cx + rx, cy}, {cx + rx, cy + ry * k}, {cx + rx * k, cy + ry}, {cx, cy + ry});
            addC({cx, cy + ry}, {cx - rx * k, cy + ry}, {cx - rx, cy + ry * k}, {cx - rx, cy});
            addC({cx - rx, cy}, {cx - rx, cy - ry * k}, {cx - rx * k, cy - ry}, {cx, cy - ry});
            addC({cx, cy - ry}, {cx + rx * k, cy - ry}, {cx + rx, cy - ry * k}, {cx + rx, cy});
            shape.rings.push_back(std::move(ring));
            // Check if transform preserves orthogonal axes (no shear) for direct ellipse.
            Pt ux{state.xform.a * rx, state.xform.b * rx};
            Pt uy{state.xform.c * ry, state.xform.d * ry};
            double uLen = std::hypot(ux.x, ux.y);
            double vLen = std::hypot(uy.x, uy.y);
            if (uLen > 1e-6 && vLen > 1e-6 && std::abs(ptDot(ux, uy)) / (uLen * vLen) < 1e-3) {
                shape.isDirectEllipse = true;
                shape.ellipseCenter = state.xform.apply({cx, cy});
                shape.ellipseRx = uLen;
                shape.ellipseRy = vLen;
                shape.ellipseAngleRad = std::atan2(ux.y, ux.x);
            }
        } else if (tag == "polygon") {
            auto ptIt = attrs.find("points");
            if (ptIt == attrs.end()) continue;
            auto nums = parseNumbers(ptIt->second);
            if (nums.size() < 6) continue;
            Ring ring;
            std::size_t n = nums.size() / 2;
            for (std::size_t i = 0; i < n; ++i) {
                Pt a = state.xform.apply({nums[i * 2], nums[i * 2 + 1]});
                Pt b = state.xform.apply({nums[((i + 1) % n) * 2], nums[((i + 1) % n) * 2 + 1]});
                if (ptDist(a, b) > 1e-6) ring.push_back({false, a, {}, {}, b});
            }
            if (ring.size() >= 3) shape.rings.push_back(std::move(ring));
        } else {
            continue;
        }

        if (!shape.rings.empty()) doc.shapes.push_back(std::move(shape));
    }

    if (!pendingPaths.empty()) {
        if (pendingPaths.size() >= 16) {
            parallelFor(pendingPaths.size(), [&](std::size_t i) {
                auto const& job = pendingPaths[i];
                doc.shapes[job.shapeIdx].rings = parsePathD(job.d, job.xform);
            });
        } else {
            for (auto const& job : pendingPaths) {
                doc.shapes[job.shapeIdx].rings = parsePathD(job.d, job.xform);
            }
        }
        std::erase_if(doc.shapes, [](ParsedShape const& sh) { return sh.rings.empty(); });
    }

    // Fallback viewBox from geometry bounds if missing.
    if (doc.vbW <= 0.0 || doc.vbH <= 0.0) {
        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
        for (auto const& sh : doc.shapes) {
            for (auto const& r : sh.rings) {
                for (auto const& s : r) {
                    for (Pt pt : {s.p0, s.p1}) {
                        minX = std::min(minX, pt.x);
                        minY = std::min(minY, pt.y);
                        maxX = std::max(maxX, pt.x);
                        maxY = std::max(maxY, pt.y);
                    }
                }
            }
        }
        if (maxX > minX && maxY > minY) {
            doc.vbX = minX;
            doc.vbY = minY;
            doc.vbW = maxX - minX;
            doc.vbH = maxY - minY;
        }
    }
    return doc;
}

// -------------------- Curve Flattening & Polygon Utilities --------------------

void flattenCubicRec(Pt p0, Pt c1, Pt c2, Pt p1, double tolSq, int depth, std::vector<Pt>& out) {
    Pt d = p1 - p0;
    double lenSq = d.x * d.x + d.y * d.y;
    double d1Sq = 0.0, d2Sq = 0.0;
    if (lenSq < 1e-12) {
        d1Sq = ptDot(c1 - p0, c1 - p0);
        d2Sq = ptDot(c2 - p0, c2 - p0);
    } else {
        double cross1 = ptCross(c1 - p0, d);
        double cross2 = ptCross(c2 - p0, d);
        d1Sq = (cross1 * cross1) / lenSq;
        d2Sq = (cross2 * cross2) / lenSq;
    }
    if (depth >= 10 || (d1Sq <= tolSq && d2Sq <= tolSq)) {
        out.push_back(p1);
        return;
    }
    Pt p01 = (p0 + c1) * 0.5;
    Pt p12 = (c1 + c2) * 0.5;
    Pt p23 = (c2 + p1) * 0.5;
    Pt p012 = (p01 + p12) * 0.5;
    Pt p123 = (p12 + p23) * 0.5;
    Pt mid = (p012 + p123) * 0.5;
    flattenCubicRec(p0, p01, p012, mid, tolSq, depth + 1, out);
    flattenCubicRec(mid, p123, p23, p1, tolSq, depth + 1, out);
}

std::vector<Pt> flattenRing(Ring const& ring, double tol) {
    std::vector<Pt> pts;
    if (ring.empty()) return pts;
    double tolSq = tol * tol;
    pts.reserve(ring.size() * 2);
    pts.push_back(ring.front().p0);
    for (auto const& s : ring) {
        if (!s.cubic) {
            pts.push_back(s.p1);
        } else {
            flattenCubicRec(s.p0, s.c1, s.c2, s.p1, tolSq, 0, pts);
        }
    }
    if (pts.size() > 1 && ptDist(pts.front(), pts.back()) < 1e-5) pts.pop_back();
    return pts;
}

double polygonSignedArea(std::vector<Pt> const& poly) {
    double sum = 0.0;
    std::size_t n = poly.size();
    if (n < 3) return 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        auto const& a = poly[i];
        auto const& b = poly[(i + 1) % n];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum * 0.5;
}

bool pointInPolygon(Pt p, std::vector<Pt> const& poly) {
    bool inside = false;
    std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        auto const& a = poly[i];
        auto const& b = poly[j];
        bool cond = ((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-30) + a.x);
        if (cond) inside = !inside;
    }
    return inside;
}

struct BBox {
    double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
    void include(Pt p) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    bool contains(Pt p) const {
        return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY;
    }
};

BBox polyBBox(std::vector<Pt> const& poly) {
    BBox b;
    for (auto const& p : poly) b.include(p);
    return b;
}

// Find a guaranteed interior sample point of a simple polygon (via horizontal scanline midpoint).
Pt interiorSamplePoint(std::vector<Pt> const& poly) {
    if (poly.empty()) return {};
    BBox box = polyBBox(poly);
    // Try 5 horizontal scanlines through the polygon to find a clean interior midpoint.
    for (double frac : {0.5, 0.35, 0.65, 0.2, 0.8}) {
        double y = box.minY + (box.maxY - box.minY) * frac;
        std::vector<double> xs;
        std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            Pt a = poly[i];
            Pt b = poly[(i + 1) % n];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                double t = (y - a.y) / (b.y - a.y);
                xs.push_back(a.x + t * (b.x - a.x));
            }
        }
        std::sort(xs.begin(), xs.end());
        double bestGap = -1.0;
        Pt bestPt{};
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            double gap = xs[i + 1] - xs[i];
            if (gap > bestGap) {
                bestGap = gap;
                bestPt = {(xs[i] + xs[i + 1]) * 0.5, y};
            }
        }
        if (bestGap > 1e-6 && pointInPolygon(bestPt, poly)) return bestPt;
    }
    // Fallback to centroid of first triangle or average vertex.
    Pt sum{};
    for (auto const& p : poly) sum = sum + p;
    return sum * (1.0 / static_cast<double>(poly.size()));
}

void reverseRingInPlace(Ring& ring) {
    std::reverse(ring.begin(), ring.end());
    for (auto& s : ring) s = reverseSeg(s);
}

// Normalize winding of a shape's rings: outer rings positive signed area, holes negative.
// Returns the exact net area (std::abs(sum of normalized signed areas)) so callers avoid
// flattening every ring a second time.
double normalizeShapeWinding(ParsedShape& shape) {
    std::size_t n = shape.rings.size();
    if (n == 0) return 0.0;
    std::vector<std::vector<Pt>> polys(n);
    std::vector<double> areas(n, 0.0);
    std::vector<BBox> boxes(n);
    std::vector<Pt> samples(n);
    for (std::size_t i = 0; i < n; ++i) {
        polys[i] = flattenRing(shape.rings[i], 1.5);
        areas[i] = polygonSignedArea(polys[i]);
        boxes[i] = polyBBox(polys[i]);
        samples[i] = interiorSamplePoint(polys[i]);
    }
    double net = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        int depth = 0;
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j || std::abs(areas[j]) <= std::abs(areas[i])) continue;
            if (!boxes[j].contains(samples[i])) continue;
            if (pointInPolygon(samples[i], polys[j])) ++depth;
        }
        bool wantPositive = (depth % 2 == 0);
        if ((wantPositive && areas[i] < 0.0) || (!wantPositive && areas[i] > 0.0)) {
            reverseRingInPlace(shape.rings[i]);
            areas[i] = -areas[i];
        }
        net += areas[i];
    }
    return std::abs(net);
}

// -------------------- Quantized Topological Keys --------------------

struct PtKey {
    std::int64_t x = 0;
    std::int64_t y = 0;
    bool operator==(PtKey const&) const = default;
    auto operator<=>(PtKey const&) const = default;
};

PtKey quantPt(Pt p) {
    return {static_cast<std::int64_t>(std::llround(p.x * 100.0)),
            static_cast<std::int64_t>(std::llround(p.y * 100.0))};
}

struct PtKeyHash {
    std::size_t operator()(PtKey const& k) const noexcept {
        std::uint64_t a = static_cast<std::uint64_t>(k.x) * 0x9e3779b97f4a7c15ULL;
        std::uint64_t b = static_cast<std::uint64_t>(k.y) * 0xbf58476d1ce4e5b9ULL;
        return static_cast<std::size_t>(a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2)));
    }
};

struct DirSegKey {
    bool cubic = false;
    PtKey p0, c1, c2, p1;
    bool operator==(DirSegKey const&) const = default;
    auto operator<=>(DirSegKey const&) const = default;
};

DirSegKey dirKeyOf(Seg const& s) {
    if (!s.cubic) return {false, quantPt(s.p0), {}, {}, quantPt(s.p1)};
    return {true, quantPt(s.p0), quantPt(s.c1), quantPt(s.c2), quantPt(s.p1)};
}

DirSegKey reverseDirKey(DirSegKey const& k) {
    if (!k.cubic) return {false, k.p1, {}, {}, k.p0};
    return {true, k.p1, k.c2, k.c1, k.p0};
}

struct UndirSegKey {
    DirSegKey a;
    bool operator==(UndirSegKey const&) const = default;
    auto operator<=>(UndirSegKey const&) const = default;
};

UndirSegKey undirKeyOf(Seg const& s) {
    DirSegKey fwd = dirKeyOf(s);
    DirSegKey rev = reverseDirKey(fwd);
    return {fwd < rev ? fwd : rev};
}

struct DirSegHash {
    std::size_t operator()(DirSegKey const& k) const noexcept {
        PtKeyHash ph;
        std::size_t h = static_cast<std::size_t>(k.cubic);
        auto mix = [&](std::size_t v) {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(ph(k.p0));
        mix(ph(k.p1));
        if (k.cubic) {
            mix(ph(k.c1));
            mix(ph(k.c2));
        }
        return h;
    }
};

struct UndirSegHash {
    std::size_t operator()(UndirSegKey const& k) const noexcept {
        return DirSegHash{}(k.a);
    }
};

// -------------------- Perceptual OkLab Palette Quantization --------------------

struct QuantizedPalette {
    std::vector<Color> palette;
    std::vector<int> shapeColor;
    int uniqueInputColors = 0;
};

QuantizedPalette quantizeShapeColors(
    std::vector<ParsedShape> const& shapes,
    std::vector<double> const& areas,
    SvgOptions const& options
) {
    QuantizedPalette out;
    out.shapeColor.assign(shapes.size(), 0);
    if (shapes.empty()) return out;

    struct ColorEntry {
        Color color{};
        OkLab lab{};
        double area = 0.0;
        int count = 0;
    };
    std::unordered_map<std::uint32_t, std::size_t> byRgb;
    std::vector<ColorEntry> unique;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        Color c = shapes[i].color;
        std::uint32_t key = (static_cast<std::uint32_t>(c.r) << 16) |
                            (static_cast<std::uint32_t>(c.g) << 8) | c.b;
        auto it = byRgb.find(key);
        if (it == byRgb.end()) {
            byRgb[key] = unique.size();
            unique.push_back({c, rgbToOkLab(c), std::max(areas[i], 1.0), 1});
        } else {
            unique[it->second].area += std::max(areas[i], 1.0);
            ++unique[it->second].count;
        }
    }
    out.uniqueInputColors = static_cast<int>(unique.size());

    // Step 1: Collapse near-identical colors within minColorDistance (area-priority greedy).
    std::vector<std::size_t> order(unique.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return unique[a].area > unique[b].area;
    });

    struct Cluster {
        OkLab lab{};
        double sumL = 0.0, sumA = 0.0, sumB = 0.0;
        double sumR = 0.0, sumG = 0.0, sumBl = 0.0;
        double weight = 0.0;
        double totalArea = 0.0;
    };
    std::vector<Cluster> clusters;
    for (std::size_t idx : order) {
        auto const& u = unique[idx];
        int best = -1;
        float bestDist = std::numeric_limits<float>::max();
        for (int ci = 0; ci < static_cast<int>(clusters.size()); ++ci) {
            auto const& cl = clusters[static_cast<std::size_t>(ci)];
            // Dark lineart tones (L < 0.44) are far less sensitive to tiny OkLab shifts than
            // bright pastels, and fusing jittered dark lineart eliminates fragmented stroke islands!
            float thresh = (u.lab.L < 0.44f && cl.lab.L < 0.44f)
                ? options.minColorDistance * 1.45f
                : options.minColorDistance;
            float d = oklabDistance(u.lab, cl.lab);
            if (d < thresh && d < bestDist) {
                bestDist = d;
                best = ci;
            }
        }
        double w = std::pow(u.area, 0.55);
        if (best < 0) {
            Cluster c;
            c.lab = u.lab;
            c.sumL = u.lab.L * w;
            c.sumA = u.lab.a * w;
            c.sumB = u.lab.b * w;
            c.sumR = u.color.r * u.area;
            c.sumG = u.color.g * u.area;
            c.sumBl = u.color.b * u.area;
            c.weight = w;
            c.totalArea = u.area;
            clusters.push_back(c);
        } else {
            auto& c = clusters[static_cast<std::size_t>(best)];
            c.sumL += u.lab.L * w;
            c.sumA += u.lab.a * w;
            c.sumB += u.lab.b * w;
            c.sumR += u.color.r * u.area;
            c.sumG += u.color.g * u.area;
            c.sumBl += u.color.b * u.area;
            c.weight += w;
            c.totalArea += u.area;
            c.lab = {static_cast<float>(c.sumL / c.weight),
                     static_cast<float>(c.sumA / c.weight),
                     static_cast<float>(c.sumB / c.weight)};
        }
    }

    // Step 2: If maxColors > 0 and clusters exceed maxColors, weighted farthest-point + k-means in OkLab.
    int const targetK = options.maxColors > 0 ? std::min(options.maxColors, static_cast<int>(clusters.size())) : static_cast<int>(clusters.size());
    std::vector<OkLab> centers;
    if (targetK < static_cast<int>(clusters.size())) {
        // Seed 0 = largest-area cluster (e.g. canvas white).
        centers.push_back(clusters[0].lab);
        while (static_cast<int>(centers.size()) < targetK) {
            int bestIdx = -1;
            double bestScore = -1.0;
            for (int i = 0; i < static_cast<int>(clusters.size()); ++i) {
                float minD = std::numeric_limits<float>::max();
                for (auto const& c : centers) {
                    minD = std::min(minD, oklabDistance(clusters[static_cast<std::size_t>(i)].lab, c));
                }
                // Balance contrast (minD^2) with visual importance (pow(area, 0.35)).
                double score = static_cast<double>(minD) * static_cast<double>(minD) *
                               std::pow(clusters[static_cast<std::size_t>(i)].totalArea, 0.35);
                if (score > bestScore) {
                    bestScore = score;
                    bestIdx = i;
                }
            }
            if (bestIdx < 0) break;
            centers.push_back(clusters[static_cast<std::size_t>(bestIdx)].lab);
        }

        // Lloyd iterations weighted by pow(area, 0.55) on unique input colors.
        std::vector<int> assign(unique.size(), 0);
        for (int iter = 0; iter < 18; ++iter) {
            bool changed = false;
            for (std::size_t i = 0; i < unique.size(); ++i) {
                int best = 0;
                float bestD = std::numeric_limits<float>::max();
                for (int k = 0; k < static_cast<int>(centers.size()); ++k) {
                    float d = oklabDistance(unique[i].lab, centers[static_cast<std::size_t>(k)]);
                    if (d < bestD) {
                        bestD = d;
                        best = k;
                    }
                }
                if (assign[i] != best) {
                    assign[i] = best;
                    changed = true;
                }
            }
            std::vector<double> sL(centers.size(), 0.0), sA(centers.size(), 0.0),
                sB(centers.size(), 0.0), sW(centers.size(), 0.0);
            for (std::size_t i = 0; i < unique.size(); ++i) {
                int k = assign[i];
                double w = std::pow(unique[i].area, 0.55);
                sL[static_cast<std::size_t>(k)] += unique[i].lab.L * w;
                sA[static_cast<std::size_t>(k)] += unique[i].lab.a * w;
                sB[static_cast<std::size_t>(k)] += unique[i].lab.b * w;
                sW[static_cast<std::size_t>(k)] += w;
            }
            for (std::size_t k = 0; k < centers.size(); ++k) {
                if (sW[k] > 0.0) {
                    centers[k] = {static_cast<float>(sL[k] / sW[k]),
                                  static_cast<float>(sA[k] / sW[k]),
                                  static_cast<float>(sB[k] / sW[k])};
                }
            }
            if (!changed) break;
        }
    } else {
        for (auto const& c : clusters) centers.push_back(c.lab);
    }

    // Assign each shape to nearest center, then compute exact area-weighted RGB per used center.
    std::vector<double> sumR(centers.size(), 0.0), sumG(centers.size(), 0.0),
        sumB(centers.size(), 0.0), sumA(centers.size(), 0.0);
    std::vector<int> rawAssign(shapes.size(), 0);
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        OkLab lab = rgbToOkLab(shapes[i].color);
        int best = 0;
        float bestD = std::numeric_limits<float>::max();
        for (int k = 0; k < static_cast<int>(centers.size()); ++k) {
            float d = oklabDistance(lab, centers[static_cast<std::size_t>(k)]);
            if (d < bestD) {
                bestD = d;
                best = k;
            }
        }
        rawAssign[i] = best;
        double a = std::max(areas[i], 1.0);
        sumR[static_cast<std::size_t>(best)] += shapes[i].color.r * a;
        sumG[static_cast<std::size_t>(best)] += shapes[i].color.g * a;
        sumB[static_cast<std::size_t>(best)] += shapes[i].color.b * a;
        sumA[static_cast<std::size_t>(best)] += a;
    }

    // Compact to used colors only.
    std::vector<int> remap(centers.size(), -1);
    for (std::size_t k = 0; k < centers.size(); ++k) {
        if (sumA[k] <= 0.0) continue;
        remap[k] = static_cast<int>(out.palette.size());
        out.palette.push_back({
            static_cast<std::uint8_t>(std::clamp(std::lround(sumR[k] / sumA[k]), 0l, 255l)),
            static_cast<std::uint8_t>(std::clamp(std::lround(sumG[k] / sumA[k]), 0l, 255l)),
            static_cast<std::uint8_t>(std::clamp(std::lround(sumB[k] / sumA[k]), 0l, 255l))
        });
    }
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        out.shapeColor[i] = remap[static_cast<std::size_t>(rawAssign[i])];
    }
    return out;
}

// -------------------- Planar Half-Edge Chaining --------------------

double segStartAngle(Seg const& s) {
    Pt d = s.cubic ? (s.c1 - s.p0) : (s.p1 - s.p0);
    if (std::hypot(d.x, d.y) < 1e-9) d = s.cubic ? (s.c2 - s.p0) : (s.p1 - s.p0);
    if (std::hypot(d.x, d.y) < 1e-9) d = s.p1 - s.p0;
    return std::atan2(d.y, d.x);
}

double segEndAngle(Seg const& s) {
    Pt d = s.cubic ? (s.p1 - s.c2) : (s.p1 - s.p0);
    if (std::hypot(d.x, d.y) < 1e-9) d = s.cubic ? (s.p1 - s.c1) : (s.p1 - s.p0);
    if (std::hypot(d.x, d.y) < 1e-9) d = s.p1 - s.p0;
    return std::atan2(d.y, d.x);
}

std::vector<Ring> chainHalfEdges(std::vector<Seg> const& edges) {
    std::vector<Ring> loops;
    if (edges.empty()) return loops;

    std::unordered_map<PtKey, std::vector<std::size_t>, PtKeyHash> outgoing;
    outgoing.reserve(edges.size());
    for (std::size_t i = 0; i < edges.size(); ++i) {
        outgoing[quantPt(edges[i].p0)].push_back(i);
    }
    std::vector<std::uint8_t> used(edges.size(), 0);

    for (std::size_t startIdx = 0; startIdx < edges.size(); ++startIdx) {
        if (used[startIdx]) continue;
        Ring loop;
        std::size_t cur = startIdx;
        PtKey startKey = quantPt(edges[startIdx].p0);
        while (true) {
            used[cur] = 1;
            loop.push_back(edges[cur]);
            PtKey tip = quantPt(edges[cur].p1);
            if (tip == startKey && !loop.empty()) break;
            auto it = outgoing.find(tip);
            if (it == outgoing.end()) break;

            // Choose unused outgoing edge; if multiple exist (pinch vertex), pick sharpest right turn
            // relative to incoming direction so faces never self-cross.
            double inBack = segEndAngle(edges[cur]) + kPiD;
            std::size_t bestNext = edges.size();
            double bestTurn = 1e30;
            for (std::size_t cand : it->second) {
                if (used[cand]) continue;
                double outA = segStartAngle(edges[cand]);
                double diff = std::fmod(outA - inBack, 2.0 * kPiD);
                if (diff <= 0.0) diff += 2.0 * kPiD;
                if (diff < bestTurn) {
                    bestTurn = diff;
                    bestNext = cand;
                }
            }
            if (bestNext >= edges.size()) break;
            cur = bestNext;
        }
        if (loop.size() >= 2) loops.push_back(std::move(loop));
    }
    return loops;
}

// -------------------- Polyline Simplification (VW + Exact Deviation Guard) --------------------

double pointToSegmentDistSq(Pt p, Pt a, Pt b) {
    Pt d = b - a;
    double lenSq = d.x * d.x + d.y * d.y;
    if (lenSq < 1e-12) {
        Pt diff = p - a;
        return diff.x * diff.x + diff.y * diff.y;
    }
    double t = std::clamp(ptDot(p - a, d) / lenSq, 0.0, 1.0);
    Pt proj = a + d * t;
    Pt diff = p - proj;
    return diff.x * diff.x + diff.y * diff.y;
}

// Visvalingam-Whyatt simplification with strict max-deviation guard against ALL
// original flattened points between prev and next: kills microscopic sliver vertices
// while guaranteeing curves never deviate > maxDev (zero sierras!).
std::vector<Pt> simplifyOpenChainVW(
    std::vector<Pt> const& orig,
    double maxDev,
    double minTriArea,
    double gentleRatio = 0.65
) {
    std::size_t const n = orig.size();
    if (n <= 2) return orig;
    double const maxDevSq = maxDev * maxDev;
    double const gentleSq = maxDevSq * gentleRatio * gentleRatio;

    // Doubly-linked list of active indices into `orig`.
    std::vector<int> prev(n), next(n);
    std::vector<std::uint8_t> alive(n, 1);
    for (std::size_t i = 0; i < n; ++i) {
        prev[i] = static_cast<int>(i) - 1;
        next[i] = (i + 1 < n) ? static_cast<int>(i) + 1 : -1;
    }

    auto spanFits = [&](int a, int b) -> bool {
        Pt pa = orig[static_cast<std::size_t>(a)];
        Pt pb = orig[static_cast<std::size_t>(b)];
        for (int k = a + 1; k < b; ++k) {
            if (pointToSegmentDistSq(orig[static_cast<std::size_t>(k)], pa, pb) > maxDevSq) {
                return false;
            }
        }
        return true;
    };

    auto vertexCost = [&](int i) -> double {
        int p = prev[static_cast<std::size_t>(i)];
        int q = next[static_cast<std::size_t>(i)];
        if (p < 0 || q < 0) return 1e30;
        double area = 0.5 * std::abs(ptCross(
            orig[static_cast<std::size_t>(i)] - orig[static_cast<std::size_t>(p)],
            orig[static_cast<std::size_t>(q)] - orig[static_cast<std::size_t>(p)]));
        double devSq = pointToSegmentDistSq(
            orig[static_cast<std::size_t>(i)],
            orig[static_cast<std::size_t>(p)],
            orig[static_cast<std::size_t>(q)]);
        // Eligible if either (dev <= maxDev AND area <= minTriArea) OR gentle curve (dev <= gentleRatio*maxDev).
        if (devSq > maxDevSq) return 1e30;
        if (area > minTriArea && devSq > gentleSq) return 1e30;
        return area;
    };

    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;
    for (int i = 1; i + 1 < static_cast<int>(n); ++i) {
        double c = vertexCost(i);
        if (c < 1e29) pq.push({c, i});
    }

    while (!pq.empty()) {
        auto [cost, idx] = pq.top();
        pq.pop();
        if (!alive[static_cast<std::size_t>(idx)]) continue;
        int p = prev[static_cast<std::size_t>(idx)];
        int q = next[static_cast<std::size_t>(idx)];
        if (p < 0 || q < 0) continue;
        double curCost = vertexCost(idx);
        if (curCost >= 1e29 || std::abs(curCost - cost) > 1e-6) continue;
        if (!spanFits(p, q)) continue;

        alive[static_cast<std::size_t>(idx)] = 0;
        next[static_cast<std::size_t>(p)] = q;
        prev[static_cast<std::size_t>(q)] = p;
        if (p > 0) {
            double cp = vertexCost(p);
            if (cp < 1e29) pq.push({cp, p});
        }
        if (q + 1 < static_cast<int>(n)) {
            double cq = vertexCost(q);
            if (cq < 1e29) pq.push({cq, q});
        }
    }

    std::vector<Pt> out;
    out.reserve(n);
    for (int i = 0; i >= 0; i = next[static_cast<std::size_t>(i)]) {
        out.push_back(orig[static_cast<std::size_t>(i)]);
    }
    return out;
}

std::vector<Pt> simplifyClosedLoopVW(
    std::vector<Pt> const& pts,
    double maxDev,
    double minTriArea
) {
    if (pts.size() <= 4) return pts;
    std::size_t idxA = 0, idxB = pts.size() / 2;
    double bestD = -1.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        double d = ptDist(pts[0], pts[i]);
        if (d > bestD) { bestD = d; idxA = i; }
    }
    bestD = -1.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        double d = ptDist(pts[idxA], pts[i]);
        if (d > bestD) { bestD = d; idxB = i; }
    }
    if (idxA == idxB) return pts;
    std::vector<Pt> half1, half2;
    for (std::size_t i = idxA;; i = (i + 1) % pts.size()) {
        half1.push_back(pts[i]);
        if (i == idxB) break;
    }
    for (std::size_t i = idxB;; i = (i + 1) % pts.size()) {
        half2.push_back(pts[i]);
        if (i == idxA) break;
    }
    auto s1 = simplifyOpenChainVW(half1, maxDev, minTriArea);
    auto s2 = simplifyOpenChainVW(half2, maxDev, minTriArea);
    std::vector<Pt> out;
    out.reserve(s1.size() + s2.size());
    for (std::size_t i = 0; i + 1 < s1.size(); ++i) out.push_back(s1[i]);
    for (std::size_t i = 0; i + 1 < s2.size(); ++i) out.push_back(s2[i]);
    return out;
}

// Clean consecutive duplicate vertices and exact collinear points (strictly preserving shape).
std::vector<Pt> cleanPolygon(std::vector<Pt> poly, double minEdge = 0.25) {
    double const minEdgeSq = minEdge * minEdge;
    bool changed = true;
    while (changed && poly.size() >= 3) {
        changed = false;
        std::vector<Pt> next;
        next.reserve(poly.size());
        for (std::size_t i = 0; i < poly.size(); ++i) {
            Pt prev = next.empty() ? poly.back() : next.back();
            if (ptDistSq(prev, poly[i]) < minEdgeSq && poly.size() > 3) {
                changed = true;
                continue;
            }
            next.push_back(poly[i]);
        }
        if (next.size() >= 3 && ptDistSq(next.front(), next.back()) < minEdgeSq) {
            next.pop_back();
            changed = true;
        }
        poly = std::move(next);
        if (poly.size() < 3) break;
        // Remove near-collinear intermediate vertices (area change < 0.01) in O(N).
        std::size_t const n = poly.size();
        if (n > 3) {
            std::vector<Pt> compacted;
            compacted.reserve(n);
            std::size_t remaining = n;
            for (std::size_t i = 0; i < n; ++i) {
                Pt a = compacted.empty() ? poly[n - 1] : compacted.back();
                Pt b = poly[i];
                Pt c = poly[(i + 1) % n];
                double acSq = ptDistSq(a, c);
                bool collinear = false;
                if (acSq > 1e-18) {
                    double cross = ptCross(b - a, c - a);
                    collinear = (cross * cross < 0.0009 * acSq) && (ptDot(b - a, c - b) >= 0.0);
                } else {
                    collinear = (ptDistSq(a, b) < 0.0009) && (ptDot(b - a, c - b) >= 0.0);
                }
                if (collinear && remaining > 3) {
                    --remaining;
                    changed = true;
                    continue;
                }
                compacted.push_back(b);
            }
            poly = std::move(compacted);
        }
    }
    return poly;
}

// -------------------- Exact Ellipse / Circle Fitting --------------------

struct FittedEllipse {
    Pt center{};
    double rx = 0.0;
    double ry = 0.0;
    double angleRad = 0.0;
    double maxErr = 1e30;
};

std::optional<FittedEllipse> tryFitEllipse(std::vector<Pt> const& poly, double maxTol) {
    if (poly.size() < 8) return std::nullopt;
    double area = std::abs(polygonSignedArea(poly));
    if (area < 4.0) return std::nullopt;

    // Area-weighted centroid + second moments via triangle fan from poly[0].
    Pt origin = poly[0];
    double m0 = 0.0, mx = 0.0, my = 0.0;
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
        Pt a = poly[i] - origin;
        Pt b = poly[i + 1] - origin;
        double tri = ptCross(a, b) * 0.5;
        m0 += tri;
        mx += tri * (a.x + b.x) / 3.0;
        my += tri * (a.y + b.y) / 3.0;
    }
    if (std::abs(m0) < 1e-6) return std::nullopt;
    Pt center{origin.x + mx / m0, origin.y + my / m0};

    double ixx = 0.0, ixy = 0.0, iyy = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        Pt a = poly[i] - center;
        Pt b = poly[(i + 1) % poly.size()] - center;
        double tri = ptCross(a, b);
        ixx += tri * (a.x * a.x + a.x * b.x + b.x * b.x);
        ixy += tri * (a.x * b.y + 2.0 * a.x * a.y + 2.0 * b.x * b.y + b.x * a.y) * 0.5;
        iyy += tri * (a.y * a.y + a.y * b.y + b.y * b.y);
    }
    double denom = 12.0 * m0;
    double cxx = ixx / denom;
    double cxy = ixy / denom;
    double cyy = iyy / denom;
    double angle = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
    double cs = std::cos(angle), sn = std::sin(angle);

    // Exact covariance eigenvalues of an ellipse are (rx^2 / 4, ry^2 / 4) -> immune to small bumps!
    double trace = cxx + cyy;
    double diff = cxx - cyy;
    double disc = std::sqrt(std::max(0.0, diff * diff + 4.0 * cxy * cxy));
    double lam1 = 0.5 * (trace + disc);
    double lam2 = 0.5 * (trace - disc);
    if (lam1 <= 1e-6 || lam2 <= 1e-6) return std::nullopt;
    // Match eigenvector orientation for angle: rotated covariance along `angle` has u-var = lam1 or lam2.
    double uVar = cxx * cs * cs + 2.0 * cxy * cs * sn + cyy * sn * sn;
    double vVar = cxx * sn * sn - 2.0 * cxy * cs * sn + cyy * cs * cs;
    if (uVar <= 1e-6 || vVar <= 1e-6) return std::nullopt;
    double rx = 2.0 * std::sqrt(uVar);
    double ry = 2.0 * std::sqrt(vVar);
    if (rx < 0.8 || ry < 0.8) return std::nullopt;

    double ellipseArea = kPiD * rx * ry;
    if (std::abs(ellipseArea - area) > std::max(0.08 * ellipseArea, 8.0)) return std::nullopt;

    // Check max and mean geometric radial error on every vertex and edge midpoint.
    double effMaxTol = std::max(maxTol, 0.065 * std::min(rx, ry));
    double maxErr = 0.0;
    double sumErr = 0.0;
    int samples = 0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        Pt pts[2] = {poly[i], (poly[i] + poly[(i + 1) % poly.size()]) * 0.5};
        for (Pt p : pts) {
            Pt d = p - center;
            double u = (d.x * cs + d.y * sn) / rx;
            double v = (-d.x * sn + d.y * cs) / ry;
            double r = std::hypot(u, v);
            if (r < 1e-6) return std::nullopt;
            double gx = u / rx;
            double gy = v / ry;
            double grad = std::hypot(gx, gy);
            double dist = (grad > 1e-9) ? std::abs(r * r - 1.0) / (2.0 * grad) : 1e9;
            maxErr = std::max(maxErr, dist);
            sumErr += dist;
            ++samples;
            if (maxErr > effMaxTol) return std::nullopt;
        }
    }
    if (samples > 0 && (sumErr / samples) > effMaxTol * 0.48) return std::nullopt;
    return FittedEllipse{center, rx, ry, angle, maxErr};
}

// -------------------- Earcut + Hole Bridging + Delaunay Flipping --------------------

struct Tri {
    int a = 0, b = 0, c = 0;
};

bool pointInTri(Pt p, Pt a, Pt b, Pt c) {
    double c1 = ptCross(b - a, p - a);
    double c2 = ptCross(c - b, p - b);
    double c3 = ptCross(a - c, p - c);
    return (c1 >= -1e-9 && c2 >= -1e-9 && c3 >= -1e-9);
}

// Merge holes (negative signed area) into outer ring (positive signed area) using visible bridge cuts.
std::vector<Pt> mergeHolesIntoOuter(
    std::vector<Pt> outer,
    std::vector<std::vector<Pt>> holes
) {
    if (holes.empty()) return outer;
    // Sort holes by rightmost vertex X descending (standard Eberly / Earcut order).
    struct HoleInfo {
        std::vector<Pt> pts;
        std::size_t rightIdx = 0;
        double maxX = -1e30;
    };
    std::vector<HoleInfo> list;
    list.reserve(holes.size());
    for (auto& h : holes) {
        if (h.size() < 3) continue;
        if (polygonSignedArea(h) > 0.0) std::reverse(h.begin(), h.end());
        std::size_t best = 0;
        for (std::size_t i = 1; i < h.size(); ++i) {
            if (h[i].x > h[best].x) best = i;
        }
        double const maxX = h[best].x;
        list.push_back({std::move(h), best, maxX});
    }
    std::sort(list.begin(), list.end(), [](auto const& a, auto const& b) {
        return a.maxX > b.maxX;
    });

    for (auto const& hole : list) {
        Pt M = hole.pts[hole.rightIdx];
        // Cast horizontal ray +X from M to find closest intersection with outer polygon edges.
        double bestX = 1e30;
        std::size_t bestEdge = outer.size();
        for (std::size_t i = 0; i < outer.size(); ++i) {
            Pt a = outer[i];
            Pt b = outer[(i + 1) % outer.size()];
            if ((a.y <= M.y && b.y > M.y) || (b.y <= M.y && a.y > M.y)) {
                double t = (M.y - a.y) / (b.y - a.y);
                double ix = a.x + t * (b.x - a.x);
                if (ix >= M.x - 1e-9 && ix < bestX) {
                    bestX = ix;
                    bestEdge = i;
                }
            }
        }
        if (bestEdge >= outer.size()) continue;
        Pt I{bestX, M.y};
        std::size_t cand = (outer[bestEdge].x >= outer[(bestEdge + 1) % outer.size()].x)
            ? bestEdge : (bestEdge + 1) % outer.size();
        Pt P = outer[cand];

        // Check if any reflex vertex of outer lies inside triangle (M, I, P); pick minimum angle.
        double bestAngle = 1e30;
        double bestDist = 1e30;
        std::size_t bridgeIdx = cand;
        bool hasInside = false;
        Pt triA = M, triB = I, triC = P;
        if (ptCross(triB - triA, triC - triA) < 0.0) std::swap(triB, triC);
        for (std::size_t i = 0; i < outer.size(); ++i) {
            if (i == cand) continue;
            Pt prev = outer[(i + outer.size() - 1) % outer.size()];
            Pt cur = outer[i];
            Pt next = outer[(i + 1) % outer.size()];
            if (ptCross(cur - prev, next - cur) >= 0.0) continue; // not reflex
            if (cur.x < M.x - 1e-9) continue;
            if (!pointInTri(cur, triA, triB, triC)) continue;
            double ang = std::abs(std::atan2(cur.y - M.y, cur.x - M.x));
            double d = ptDist(cur, M);
            if (!hasInside || ang < bestAngle - 1e-9 || (std::abs(ang - bestAngle) <= 1e-9 && d < bestDist)) {
                hasInside = true;
                bestAngle = ang;
                bestDist = d;
                bridgeIdx = i;
            }
        }

        // Splice hole into outer at bridgeIdx <-> hole.rightIdx.
        std::vector<Pt> merged;
        merged.reserve(outer.size() + hole.pts.size() + 2);
        for (std::size_t i = 0; i <= bridgeIdx; ++i) merged.push_back(outer[i]);
        for (std::size_t k = 0; k < hole.pts.size(); ++k) {
            merged.push_back(hole.pts[(hole.rightIdx + k) % hole.pts.size()]);
        }
        merged.push_back(hole.pts[hole.rightIdx]);
        merged.push_back(outer[bridgeIdx]);
        for (std::size_t i = bridgeIdx + 1; i < outer.size(); ++i) merged.push_back(outer[i]);
        outer = std::move(merged);
    }
    return outer;
}

double triMinAngleCos(Pt a, Pt b, Pt c) {
    auto ang = [](Pt p, Pt q, Pt r) {
        Pt u = p - q, v = r - q;
        double den = std::hypot(u.x, u.y) * std::hypot(v.x, v.y);
        if (den < 1e-12) return 0.0;
        return std::acos(std::clamp(ptDot(u, v) / den, -1.0, 1.0));
    };
    return std::min({ang(b, a, c), ang(a, b, c), ang(a, c, b)});
}

// Returns true iff this triangle has all interior angles <= 164 deg (so |skew| <= 74 deg -> 1 object, no split!).
bool triNeedsObtuseSplit(Pt a, Pt b, Pt c) {
    auto ang = [](Pt p, Pt q, Pt r) {
        Pt u = p - q, v = r - q;
        double den = std::hypot(u.x, u.y) * std::hypot(v.x, v.y);
        if (den < 1e-12) return kPiD;
        return std::acos(std::clamp(ptDot(u, v) / den, -1.0, 1.0));
    };
    double a0 = ang(b, a, c), a1 = ang(a, b, c), a2 = ang(a, c, b);
    double best = std::min({std::abs(a0 - kPiD * 0.5), std::abs(a1 - kPiD * 0.5), std::abs(a2 - kPiD * 0.5)});
    return best > (74.0 * kPiD / 180.0);
}

std::vector<Tri> triangulatePolygon(std::vector<Pt> const& pts) {
    std::vector<Tri> tris;
    int const n = static_cast<int>(pts.size());
    if (n < 3) return tris;
    if (n == 3) {
        if (std::abs(ptCross(pts[1] - pts[0], pts[2] - pts[0])) > 1e-6) tris.push_back({0, 1, 2});
        return tris;
    }

    std::vector<int> idx(static_cast<std::size_t>(n));
    std::iota(idx.begin(), idx.end(), 0);
    if (polygonSignedArea(pts) < 0.0) std::reverse(idx.begin(), idx.end());

    // Quality-preferring ear clipping: among valid ears, pick the one with best min-angle.
    int guard = n * n + 16;
    while (idx.size() > 3 && guard-- > 0) {
        std::size_t m = idx.size();
        std::size_t bestPos = m;
        double bestQual = -1.0;

        auto checkEar = [&](std::size_t pos, bool strict) -> bool {
            int ia = idx[(pos + m - 1) % m];
            int ib = idx[pos];
            int ic = idx[(pos + 1) % m];
            Pt a = pts[static_cast<std::size_t>(ia)];
            Pt b = pts[static_cast<std::size_t>(ib)];
            Pt c = pts[static_cast<std::size_t>(ic)];
            double cross = ptCross(b - a, c - a);
            if (strict ? (cross <= 1e-7) : (cross < -1e-7)) return false;
            if (cross <= 1e-9) return true; // degenerate collinear ear
            double minX = std::min({a.x, b.x, c.x}) - 1e-7;
            double maxX = std::max({a.x, b.x, c.x}) + 1e-7;
            double minY = std::min({a.y, b.y, c.y}) - 1e-7;
            double maxY = std::max({a.y, b.y, c.y}) + 1e-7;
            for (std::size_t k = 0; k < m; ++k) {
                int ik = idx[k];
                if (ik == ia || ik == ib || ik == ic) continue;
                Pt p = pts[static_cast<std::size_t>(ik)];
                // Skip duplicate bridge vertices coinciding with triangle corners.
                if (ptDist(p, a) < 1e-6 || ptDist(p, b) < 1e-6 || ptDist(p, c) < 1e-6) continue;
                if (p.x < minX || p.x > maxX || p.y < minY || p.y > maxY) continue;
                if (pointInTri(p, a, b, c)) return false;
            }
            return true;
        };

        // Scan ears; for large polygons check up to 64 evenly distributed candidates to stay fast.
        std::size_t step = m > 96 ? std::max<std::size_t>(1, m / 64) : 1;
        for (std::size_t pass = 0; pass < 2 && bestPos == m; ++pass) {
            std::size_t curStep = pass == 0 ? step : 1;
            for (std::size_t i = 0; i < m; i += curStep) {
                if (!checkEar(i, true)) continue;
                Pt a = pts[static_cast<std::size_t>(idx[(i + m - 1) % m])];
                Pt b = pts[static_cast<std::size_t>(idx[i])];
                Pt c = pts[static_cast<std::size_t>(idx[(i + 1) % m])];
                double q = triMinAngleCos(a, b, c);
                if (q > bestQual) {
                    bestQual = q;
                    bestPos = i;
                }
            }
        }
        // Fallback for slightly self-touching / bridge polygons.
        if (bestPos == m) {
            for (std::size_t i = 0; i < m; ++i) {
                if (checkEar(i, false)) {
                    bestPos = i;
                    break;
                }
            }
        }
        if (bestPos == m) {
            // Last resort: clip sharpest positive corner.
            double bestArea = -1e30;
            for (std::size_t i = 0; i < m; ++i) {
                Pt a = pts[static_cast<std::size_t>(idx[(i + m - 1) % m])];
                Pt b = pts[static_cast<std::size_t>(idx[i])];
                Pt c = pts[static_cast<std::size_t>(idx[(i + 1) % m])];
                double cr = ptCross(b - a, c - a);
                if (cr > bestArea) {
                    bestArea = cr;
                    bestPos = i;
                }
            }
        }
        int ia = idx[(bestPos + m - 1) % m];
        int ib = idx[bestPos];
        int ic = idx[(bestPos + 1) % m];
        if (ptCross(pts[static_cast<std::size_t>(ib)] - pts[static_cast<std::size_t>(ia)],
                    pts[static_cast<std::size_t>(ic)] - pts[static_cast<std::size_t>(ia)]) > 1e-6) {
            tris.push_back({ia, ib, ic});
        }
        idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(bestPos));
    }
    if (idx.size() == 3) {
        if (ptCross(pts[static_cast<std::size_t>(idx[1])] - pts[static_cast<std::size_t>(idx[0])],
                    pts[static_cast<std::size_t>(idx[2])] - pts[static_cast<std::size_t>(idx[0])]) > 1e-6) {
            tris.push_back({idx[0], idx[1], idx[2]});
        }
    }

    // Delaunay-like edge flipping on internal diagonals to eliminate thin slivers.
    auto edgeKey = [](int u, int v) -> std::uint64_t {
        if (u > v) std::swap(u, v);
        return (static_cast<std::uint64_t>(u) << 32) | static_cast<std::uint32_t>(v);
    };
    auto triVerts = [](Tri const& t) -> std::array<int, 3> { return {t.a, t.b, t.c}; };
    auto oppVert = [](Tri const& t, int u, int v) -> int {
        if (t.a != u && t.a != v) return t.a;
        if (t.b != u && t.b != v) return t.b;
        return t.c;
    };

    for (int iter = 0; iter < 6 && tris.size() >= 2; ++iter) {
        std::unordered_map<std::uint64_t, std::pair<int, int>> adj;
        adj.reserve(tris.size() * 3);
        for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti) {
            auto v = triVerts(tris[static_cast<std::size_t>(ti)]);
            for (int e = 0; e < 3; ++e) {
                auto k = edgeKey(v[static_cast<std::size_t>(e)], v[static_cast<std::size_t>((e + 1) % 3)]);
                auto it = adj.find(k);
                if (it == adj.end()) adj[k] = {ti, -1};
                else it->second.second = ti;
            }
        }
        bool flippedAny = false;
        std::vector<std::uint8_t> touched(tris.size(), 0);
        for (auto const& [k, pair] : adj) {
            int t1 = pair.first, t2 = pair.second;
            if (t2 < 0 || touched[static_cast<std::size_t>(t1)] || touched[static_cast<std::size_t>(t2)]) continue;
            int u = static_cast<int>(k >> 32);
            int v = static_cast<int>(k & 0xffffffffu);
            int p = oppVert(tris[static_cast<std::size_t>(t1)], u, v);
            int q = oppVert(tris[static_cast<std::size_t>(t2)], u, v);
            if (p == q) continue;
            // Ensure CCW quad (u, p, v, q) or (u, q, v, p).
            if (ptCross(pts[static_cast<std::size_t>(p)] - pts[static_cast<std::size_t>(u)],
                        pts[static_cast<std::size_t>(v)] - pts[static_cast<std::size_t>(u)]) < 0.0) {
                std::swap(p, q);
            }
            Pt pu = pts[static_cast<std::size_t>(u)];
            Pt pp = pts[static_cast<std::size_t>(p)];
            Pt pv = pts[static_cast<std::size_t>(v)];
            Pt pq = pts[static_cast<std::size_t>(q)];
            // Strictly convex check on quad (u, p, v, q).
            if (ptCross(pp - pu, pv - pp) <= 1e-6 || ptCross(pv - pp, pq - pv) <= 1e-6 ||
                ptCross(pq - pv, pu - pq) <= 1e-6 || ptCross(pu - pq, pp - pu) <= 1e-6) {
                continue;
            }
            double oldMin = std::min(triMinAngleCos(pu, pp, pv), triMinAngleCos(pu, pv, pq));
            double newMin = std::min(triMinAngleCos(pu, pp, pq), triMinAngleCos(pp, pv, pq));
            int oldSplits = (triNeedsObtuseSplit(pu, pp, pv) ? 1 : 0) +
                            (triNeedsObtuseSplit(pu, pv, pq) ? 1 : 0);
            int newSplits = (triNeedsObtuseSplit(pu, pp, pq) ? 1 : 0) +
                            (triNeedsObtuseSplit(pp, pv, pq) ? 1 : 0);
            bool betterSplit = (newSplits < oldSplits && newMin > 0.045);
            bool betterAngle = (newSplits <= oldSplits && newMin > oldMin + 0.02);
            if (betterSplit || betterAngle) {
                tris[static_cast<std::size_t>(t1)] = {u, p, q};
                tris[static_cast<std::size_t>(t2)] = {p, v, q};
                touched[static_cast<std::size_t>(t1)] = 1;
                touched[static_cast<std::size_t>(t2)] = 1;
                flippedAny = true;
            }
        }
        if (!flippedAny) break;
    }

    return tris;
}

// -------------------- Exact Z-Layer Cover Grid (Lossless Under-Layer Expansion) --------------------

bool segmentsProperlyIntersect(Pt a, Pt b, Pt c, Pt d) {
    auto orient = [](Pt p, Pt q, Pt r) { return ptCross(q - p, r - p); };
    double o1 = orient(a, b, c);
    double o2 = orient(a, b, d);
    double o3 = orient(c, d, a);
    double o4 = orient(c, d, b);
    return ((o1 > 1e-6 && o2 < -1e-6) || (o1 < -1e-6 && o2 > 1e-6)) &&
           ((o3 > 1e-6 && o4 < -1e-6) || (o3 < -1e-6 && o4 > 1e-6));
}

struct LayerCoverGrid {
    int w = 0;
    int h = 0;
    double normW = 1000.0;
    double normH = 1000.0;
    double toGridX = 1.0;
    double toGridY = 1.0;
    std::vector<std::int16_t> rawZ;
    std::vector<std::int32_t> rawId;

    bool valid() const { return w > 0 && h > 0 && !rawZ.empty(); }

    void init(double nw, double nh, int maxDim, std::int16_t baseVal) {
        normW = std::max(nw, 1.0);
        normH = std::max(nh, 1.0);
        double scale = static_cast<double>(maxDim) / std::max(normW, normH);
        w = std::max(4, static_cast<int>(std::lround(normW * scale)));
        h = std::max(4, static_cast<int>(std::lround(normH * scale)));
        toGridX = static_cast<double>(w) / normW;
        toGridY = static_cast<double>(h) / normH;
        rawZ.assign(static_cast<std::size_t>(w) * h, baseVal);
        rawId.assign(static_cast<std::size_t>(w) * h, -1);
    }

    struct RasterItem {
        std::vector<Pt> const* outer = nullptr;
        std::vector<std::vector<Pt>> const* holes = nullptr;
        std::int16_t zVal = 0;
        std::int32_t regId = -1;
        int y0 = 0;
        int y1 = -1;
    };

    RasterItem makeRasterItem(
        std::vector<Pt> const* outer,
        std::vector<std::vector<Pt>> const* holes,
        std::int16_t zVal,
        std::int32_t regId
    ) const {
        RasterItem item{outer, holes, zVal, regId, 0, -1};
        if (!outer || outer->size() < 3) return item;
        double minY = 1e30, maxY = -1e30;
        for (auto const& p : *outer) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        item.y0 = std::clamp(static_cast<int>(std::floor(minY * toGridY)), 0, h - 1);
        item.y1 = std::clamp(static_cast<int>(std::ceil(maxY * toGridY)), 0, h - 1);
        return item;
    }

    void rasterizePolygonInRows(
        std::vector<Pt> const& outer,
        std::vector<std::vector<Pt>> const* holes,
        std::int16_t zVal,
        std::int32_t regId,
        int yStart,
        int yEnd,
        std::vector<double>& xs
    ) {
        if (outer.size() < 3 || yStart > yEnd) return;

        auto collectCrossings = [&](std::vector<Pt> const& ring, double yNorm) {
            std::size_t const m = ring.size();
            for (std::size_t i = 0; i < m; ++i) {
                Pt a = ring[i];
                Pt b = ring[(i + 1) % m];
                if ((a.y <= yNorm && b.y > yNorm) || (b.y <= yNorm && a.y > yNorm)) {
                    double t = (yNorm - a.y) / (b.y - a.y);
                    xs.push_back(a.x + t * (b.x - a.x));
                }
            }
        };

        for (int gy = yStart; gy <= yEnd; ++gy) {
            double yNorm = (static_cast<double>(gy) + 0.5) / toGridY;
            xs.clear();
            collectCrossings(outer, yNorm);
            if (holes) {
                for (auto const& hole : *holes) collectCrossings(hole, yNorm);
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());
            std::size_t const rowOff = static_cast<std::size_t>(gy) * w;
            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                int x0 = std::clamp(static_cast<int>(std::floor(xs[k] * toGridX)), 0, w - 1);
                int x1 = std::clamp(static_cast<int>(std::ceil(xs[k + 1] * toGridX)) - 1, 0, w - 1);
                for (int gx = x0; gx <= x1; ++gx) {
                    std::size_t const idx = rowOff + static_cast<std::size_t>(gx);
                    if (zVal >= rawZ[idx]) {
                        rawZ[idx] = zVal;
                        rawId[idx] = regId;
                    }
                }
            }
        }
    }

    void rasterizePolygon(
        std::vector<Pt> const& outer,
        std::vector<std::vector<Pt>> const& holes,
        std::int16_t zVal,
        std::int32_t regId
    ) {
        auto item = makeRasterItem(&outer, holes.empty() ? nullptr : &holes, zVal, regId);
        if (item.y0 > item.y1) return;
        std::vector<double> xs;
        xs.reserve(64);
        rasterizePolygonInRows(outer, item.holes, zVal, regId, item.y0, item.y1, xs);
    }

    // Parallel scanline rasterization over disjoint horizontal row stripes (lock-free, deterministic).
    void rasterizeBatchParallel(std::vector<RasterItem> const& items) {
        if (!valid() || items.empty()) return;
        int const stripeH = 32;
        int const numStripes = (h + stripeH - 1) / stripeH;
        parallelFor(static_cast<std::size_t>(numStripes), [&](std::size_t s) {
            int const r0 = static_cast<int>(s) * stripeH;
            int const r1 = std::min(h - 1, r0 + stripeH - 1);
            std::vector<double> xs;
            xs.reserve(64);
            for (auto const& item : items) {
                if (item.y1 < r0 || item.y0 > r1 || !item.outer) continue;
                int const yStart = std::max(r0, item.y0);
                int const yEnd = std::min(r1, item.y1);
                rasterizePolygonInRows(
                    *item.outer, item.holes, item.zVal, item.regId, yStart, yEnd, xs);
            }
        });
    }

    // Exact per-region safety invariant: a cell is safe to expand onto iff it belongs
    // to THIS exact region (rawId == myId) OR to a STRICTLY HIGHER layer (rawZ > myZ).
    inline bool cellRawSafe(std::size_t idx, std::int16_t myZ, std::int32_t myId) const {
        return rawId[idx] == myId || rawZ[idx] > myZ;
    }

    // 5-point cross margin (~1.0u at 1024x1024): guarantees zero AA bleed across lower-Z
    // boundaries while fitting cleanly inside thin 3.0-4.5u lineart strokes!
    inline bool cellMarginSafe(int gx, int gy, std::int16_t myZ, std::int32_t myId) const {
        if (gx <= 0 || gy <= 0 || gx >= w - 1 || gy >= h - 1) return false;
        std::size_t const row = static_cast<std::size_t>(gy) * w;
        std::size_t const col = static_cast<std::size_t>(gx);
        return cellRawSafe(row + col, myZ, myId) &&
               cellRawSafe(row + col - 1, myZ, myId) &&
               cellRawSafe(row + col + 1, myZ, myId) &&
               cellRawSafe(row - static_cast<std::size_t>(w) + col, myZ, myId) &&
               cellRawSafe(row + static_cast<std::size_t>(w) + col, myZ, myId);
    }

    bool pointRawSafe(Pt p, std::int16_t myZ, std::int32_t myId) const {
        if (p.x < 0.0 || p.y < 0.0 || p.x >= normW || p.y >= normH) return false;
        int gx = std::clamp(static_cast<int>(std::floor(p.x * toGridX)), 0, w - 1);
        int gy = std::clamp(static_cast<int>(std::floor(p.y * toGridY)), 0, h - 1);
        return cellRawSafe(static_cast<std::size_t>(gy) * w + static_cast<std::size_t>(gx), myZ, myId);
    }

    bool pointMarginSafe(Pt p, std::int16_t myZ, std::int32_t myId) const {
        if (p.x < 0.0 || p.y < 0.0 || p.x >= normW || p.y >= normH) return false;
        int gx = std::clamp(static_cast<int>(std::floor(p.x * toGridX)), 0, w - 1);
        int gy = std::clamp(static_cast<int>(std::floor(p.y * toGridY)), 0, h - 1);
        return cellMarginSafe(gx, gy, myZ, myId);
    }

    bool edgeMarginSafe(Pt u, Pt v, std::int16_t myZ, std::int32_t myId, Pt fixed1, Pt fixed2) const {
        constexpr double kNearFixedSq = 2.6 * 2.6;
        double len = ptDist(u, v);
        int steps = std::max(2, static_cast<int>(std::ceil(len * 0.75)));
        for (int s = 0; s <= steps; ++s) {
            double t = static_cast<double>(s) / static_cast<double>(steps);
            Pt p = u + (v - u) * t;
            bool nearFixed = (ptDistSq(p, fixed1) <= kNearFixedSq) || (ptDistSq(p, fixed2) <= kNearFixedSq);
            if (!nearFixed) {
                if (!pointMarginSafe(p, myZ, myId)) return false;
            } else {
                if (!pointRawSafe(p, myZ, myId)) return false;
            }
        }
        return true;
    }

    // Verifies that expanding region (myZ, myId) by triangle (a, b, c) never touches
    // any other region at layer <= myZ or transparent background.
    bool triangleSafe(Pt a, Pt b, Pt c, std::int16_t myZ, std::int32_t myId, Pt fixed1, Pt fixed2) const {
        if (!valid()) return false;
        if (a.x < 0.0 || a.y < 0.0 || a.x > normW || a.y > normH ||
            b.x < 0.0 || b.y < 0.0 || b.x > normW || b.y > normH ||
            c.x < 0.0 || c.y < 0.0 || c.x > normW || c.y > normH) {
            return false;
        }
        if (ptCross(b - a, c - a) < 0.0) std::swap(b, c);
        if (!edgeMarginSafe(a, b, myZ, myId, fixed1, fixed2) ||
            !edgeMarginSafe(b, c, myZ, myId, fixed1, fixed2) ||
            !edgeMarginSafe(c, a, myZ, myId, fixed1, fixed2)) {
            return false;
        }
        constexpr double kNearFixedSq = 2.6 * 2.6;
        int x0 = std::clamp(static_cast<int>(std::floor(std::min({a.x, b.x, c.x}) * toGridX)), 0, w - 1);
        int x1 = std::clamp(static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}) * toGridX)), 0, w - 1);
        int y0 = std::clamp(static_cast<int>(std::floor(std::min({a.y, b.y, c.y}) * toGridY)), 0, h - 1);
        int y1 = std::clamp(static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}) * toGridY)), 0, h - 1);
        for (int gy = y0; gy <= y1; ++gy) {
            double py = (static_cast<double>(gy) + 0.5) / toGridY;
            std::size_t row = static_cast<std::size_t>(gy) * w;
            for (int gx = x0; gx <= x1; ++gx) {
                Pt p{(static_cast<double>(gx) + 0.5) / toGridX, py};
                if (!pointInTri(p, a, b, c)) continue;
                std::size_t idx = row + static_cast<std::size_t>(gx);
                if (!cellRawSafe(idx, myZ, myId)) return false;
                if (ptDistSq(p, fixed1) > kNearFixedSq && ptDistSq(p, fixed2) > kNearFixedSq) {
                    if (!cellMarginSafe(gx, gy, myZ, myId)) return false;
                }
            }
        }
        return true;
    }

    // Verifies that all excess pixels of enclosing parallelogram (c0,c1,c2,c3) outside
    // original quad (q0,q1,q2,q3) stay strictly on (rawId == myId || rawZ > myZ).
    bool quadExcessSafe(
        Pt c0, Pt c1, Pt c2, Pt c3,
        Pt q0, Pt q1, Pt q2, Pt q3,
        std::int16_t myZ,
        std::int32_t myId
    ) const {
        if (!valid()) return false;
        Pt corners[4] = {c0, c1, c2, c3};
        double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
        for (Pt p : corners) {
            if (p.x < 0.0 || p.y < 0.0 || p.x > normW || p.y > normH) return false;
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        auto quadSignedArea2 = [](Pt a, Pt b, Pt c, Pt d) {
            return (a.x * b.y - b.x * a.y) + (b.x * c.y - c.x * b.y) +
                   (c.x * d.y - d.x * c.y) + (d.x * a.y - a.x * d.y);
        };
        if (quadSignedArea2(c0, c1, c2, c3) < 0.0) std::swap(c1, c3);
        if (quadSignedArea2(q0, q1, q2, q3) < 0.0) std::swap(q1, q3);

        auto inConvex4 = [](Pt p, Pt a, Pt b, Pt c, Pt d) {
            return ptCross(b - a, p - a) >= -1e-6 &&
                   ptCross(c - b, p - b) >= -1e-6 &&
                   ptCross(d - c, p - c) >= -1e-6 &&
                   ptCross(a - d, p - d) >= -1e-6;
        };
        auto distToQuadEdge = [&](Pt p) {
            double d = pointToSegmentDistSq(p, q0, q1);
            d = std::min(d, pointToSegmentDistSq(p, q1, q2));
            d = std::min(d, pointToSegmentDistSq(p, q2, q3));
            d = std::min(d, pointToSegmentDistSq(p, q3, q0));
            return d;
        };

        constexpr double kNearVertSq = 2.5 * 2.5;
        Pt qPts[4] = {q0, q1, q2, q3};
        for (int e = 0; e < 4; ++e) {
            Pt u = corners[e], v = corners[(e + 1) % 4];
            int steps = std::max(2, static_cast<int>(std::ceil(ptDist(u, v) * 0.7)));
            for (int s = 0; s <= steps; ++s) {
                Pt p = u + (v - u) * (static_cast<double>(s) / steps);
                if (inConvex4(p, q0, q1, q2, q3)) continue;
                bool nearVertex = false;
                for (Pt qp : qPts) if (ptDistSq(p, qp) <= kNearVertSq) { nearVertex = true; break; }
                if (nearVertex) {
                    if (!pointRawSafe(p, myZ, myId)) return false;
                } else {
                    if (!pointMarginSafe(p, myZ, myId)) return false;
                }
            }
        }

        int x0 = std::clamp(static_cast<int>(std::floor(minX * toGridX)), 0, w - 1);
        int x1 = std::clamp(static_cast<int>(std::ceil(maxX * toGridX)), 0, w - 1);
        int y0 = std::clamp(static_cast<int>(std::floor(minY * toGridY)), 0, h - 1);
        int y1 = std::clamp(static_cast<int>(std::ceil(maxY * toGridY)), 0, h - 1);
        for (int gy = y0; gy <= y1; ++gy) {
            double py = (static_cast<double>(gy) + 0.5) / toGridY;
            std::size_t row = static_cast<std::size_t>(gy) * w;
            for (int gx = x0; gx <= x1; ++gx) {
                Pt p{(static_cast<double>(gx) + 0.5) / toGridX, py};
                if (!inConvex4(p, c0, c1, c2, c3)) continue;
                if (inConvex4(p, q0, q1, q2, q3)) continue;
                std::size_t idx = row + static_cast<std::size_t>(gx);
                if (!cellRawSafe(idx, myZ, myId)) return false;
                if (distToQuadEdge(p) > 4.0) {
                    if (!cellMarginSafe(gx, gy, myZ, myId)) return false;
                }
            }
        }
        return true;
    }

    // Verifies that all excess pixels of a convex candidate polygon `cand` outside `origOuter`
    // stay strictly on (rawId == myId || rawZ > myZ) with safety margin away from `origOuter`.
    bool polygonExcessSafe(
        std::vector<Pt> const& cand,
        std::vector<Pt> const& origOuter,
        std::int16_t myZ,
        std::int32_t myId,
        bool allowTightRawSameId = false
    ) const {
        if (!valid() || cand.size() < 3) return false;
        double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
        for (Pt p : cand) {
            if (p.x < 0.0 || p.y < 0.0 || p.x > normW || p.y > normH) return false;
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        BBox const origBox = polyBBox(origOuter);
        auto inOrig = [&](Pt p) -> bool {
            return origBox.contains(p) && pointInPolygon(p, origOuter);
        };
        auto inConvex = [&](Pt p) -> bool {
            std::size_t const m = cand.size();
            for (std::size_t i = 0; i < m; ++i) {
                Pt a = cand[i], b = cand[(i + 1) % m];
                if (ptCross(b - a, p - a) < -1e-6) return false;
            }
            return true;
        };
        auto nearOrigEdge = [&](Pt p, double threshSq) -> bool {
            std::size_t const m = origOuter.size();
            for (std::size_t i = 0; i < m; ++i) {
                if (pointToSegmentDistSq(p, origOuter[i], origOuter[(i + 1) % m]) <= threshSq) {
                    return true;
                }
            }
            return false;
        };

        // Fast reject along candidate perimeter first.
        for (std::size_t i = 0; i < cand.size(); ++i) {
            Pt u = cand[i], v = cand[(i + 1) % cand.size()];
            int steps = std::max(2, static_cast<int>(std::ceil(ptDist(u, v) * 0.8)));
            for (int s = 0; s <= steps; ++s) {
                Pt p = u + (v - u) * (static_cast<double>(s) / steps);
                if (inOrig(p)) continue;
                if (!pointRawSafe(p, myZ, myId)) return false;
                if (!allowTightRawSameId && !nearOrigEdge(p, 5.5)) {
                    if (!pointMarginSafe(p, myZ, myId)) return false;
                }
            }
        }

        int x0 = std::clamp(static_cast<int>(std::floor(minX * toGridX)), 0, w - 1);
        int x1 = std::clamp(static_cast<int>(std::ceil(maxX * toGridX)), 0, w - 1);
        int y0 = std::clamp(static_cast<int>(std::floor(minY * toGridY)), 0, h - 1);
        int y1 = std::clamp(static_cast<int>(std::ceil(maxY * toGridY)), 0, h - 1);
        for (int gy = y0; gy <= y1; ++gy) {
            double py = (static_cast<double>(gy) + 0.5) / toGridY;
            std::size_t row = static_cast<std::size_t>(gy) * w;
            for (int gx = x0; gx <= x1; ++gx) {
                Pt p{(static_cast<double>(gx) + 0.5) / toGridX, py};
                if (!inConvex(p)) continue;
                if (inOrig(p)) continue;
                std::size_t idx = row + static_cast<std::size_t>(gx);
                if (!cellRawSafe(idx, myZ, myId)) return false;
                if (allowTightRawSameId && rawId[idx] == myId) continue;
                if (!nearOrigEdge(p, 5.5)) {
                    if (!cellMarginSafe(gx, gy, myZ, myId)) return false;
                }
            }
        }
        return true;
    }

    // Verifies that a ribbon Block `box` (enclosing a strip of 2-4 consecutive triangles `stripPoly`)
    // never protrudes more than `maxOuterDev` units onto lower-Z regions, while allowing free
    // longitudinal tucking inside the same region (rawId == myId) or higher-Z cover (rawZ > myZ)!
    bool ribbonBoxFits(
        std::vector<Pt> const& box,
        std::vector<Pt> const& stripHull,
        double maxOuterDev,
        std::int16_t myZ,
        std::int32_t myId
    ) const {
        if (box.size() != 4 || stripHull.size() < 3) return false;
        double const maxDevSq = maxOuterDev * maxOuterDev;
        BBox const hullBox = polyBBox(stripHull);
        auto distSqToStrip = [&](Pt p) -> double {
            if (hullBox.contains(p) && pointInPolygon(p, stripHull)) return 0.0;
            double best = 1e30;
            std::size_t const m = stripHull.size();
            for (std::size_t i = 0; i < m; ++i) {
                best = std::min(best, pointToSegmentDistSq(p, stripHull[i], stripHull[(i + 1) % m]));
            }
            return best;
        };
        for (int e = 0; e < 4; ++e) {
            Pt u = box[static_cast<std::size_t>(e)];
            Pt v = box[static_cast<std::size_t>((e + 1) % 4)];
            int steps = std::max(3, static_cast<int>(std::ceil(ptDist(u, v) * 0.7)));
            for (int s = 0; s <= steps; ++s) {
                Pt p = u + (v - u) * (static_cast<double>(s) / steps);
                if (p.x < -0.5 || p.y < -0.5 || p.x > normW + 0.5 || p.y > normH + 0.5) return false;
                if (distSqToStrip(p) <= maxDevSq) continue;
                if (valid() && pointRawSafe(p, myZ, myId)) continue;
                return false;
            }
        }
        return true;
    }

    // Verifies that an enclosing circle (center, radius) has all excess outside `origOuter`
    // hidden strictly on (rawId == myId || rawZ > myZ).
    bool circleExcessSafe(
        Pt center,
        double radius,
        std::vector<Pt> const& origOuter,
        std::int16_t myZ,
        std::int32_t myId
    ) const {
        if (!valid() || radius < 1.0) return false;
        if (center.x - radius < 0.0 || center.y - radius < 0.0 ||
            center.x + radius > normW || center.y + radius > normH) {
            return false;
        }
        double const rSq = radius * radius;
        double const innerRSq = (radius - 2.0) * (radius - 2.0);
        BBox const origBox = polyBBox(origOuter);
        // Perimeter fast check
        int const perimSteps = std::clamp(static_cast<int>(std::ceil(2.0 * kPiD * radius * 0.7)), 16, 180);
        for (int s = 0; s < perimSteps; ++s) {
            double a = 2.0 * kPiD * s / perimSteps;
            Pt p{center.x + radius * std::cos(a), center.y + radius * std::sin(a)};
            if (origBox.contains(p) && pointInPolygon(p, origOuter)) continue;
            if (!pointRawSafe(p, myZ, myId)) return false;
        }
        int x0 = std::clamp(static_cast<int>(std::floor((center.x - radius) * toGridX)), 0, w - 1);
        int x1 = std::clamp(static_cast<int>(std::ceil((center.x + radius) * toGridX)), 0, w - 1);
        int y0 = std::clamp(static_cast<int>(std::floor((center.y - radius) * toGridY)), 0, h - 1);
        int y1 = std::clamp(static_cast<int>(std::ceil((center.y + radius) * toGridY)), 0, h - 1);
        for (int gy = y0; gy <= y1; ++gy) {
            double py = (static_cast<double>(gy) + 0.5) / toGridY;
            double dy = py - center.y;
            double dySq = dy * dy;
            std::size_t row = static_cast<std::size_t>(gy) * w;
            for (int gx = x0; gx <= x1; ++gx) {
                double px = (static_cast<double>(gx) + 0.5) / toGridX;
                double dx = px - center.x;
                double dSq = dx * dx + dySq;
                if (dSq > rSq) continue;
                Pt p{px, py};
                if (origBox.contains(p) && pointInPolygon(p, origOuter)) continue;
                std::size_t idx = row + static_cast<std::size_t>(gx);
                if (!cellRawSafe(idx, myZ, myId)) return false;
                if (dSq < innerRSq) {
                    if (!cellMarginSafe(gx, gy, myZ, myId)) return false;
                }
            }
        }
        return true;
    }
};

std::vector<Pt> convexHullCCW(std::vector<Pt> pts) {
    if (pts.size() <= 3) {
        if (pts.size() == 3 && polygonSignedArea(pts) < 0.0) std::swap(pts[1], pts[2]);
        return pts;
    }
    std::sort(pts.begin(), pts.end(), [](Pt a, Pt b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    std::vector<Pt> h;
    h.reserve(pts.size() * 2);
    for (Pt p : pts) {
        while (h.size() >= 2 && ptCross(h[h.size() - 1] - h[h.size() - 2], p - h[h.size() - 2]) <= 1e-6) {
            h.pop_back();
        }
        h.push_back(p);
    }
    std::size_t const lower = h.size() + 1;
    for (std::size_t i = pts.size(); i-- > 0;) {
        Pt p = pts[i];
        while (h.size() >= lower && ptCross(h[h.size() - 1] - h[h.size() - 2], p - h[h.size() - 2]) <= 1e-6) {
            h.pop_back();
        }
        h.push_back(p);
    }
    if (!h.empty()) h.pop_back();
    if (polygonSignedArea(h) < 0.0) std::reverse(h.begin(), h.end());
    return h;
}

// Losslessly simplify vertices of `poly` that border strictly higher-Z regions by expanding
// `poly` slightly UNDERNEATH those higher-Z regions (P <= P' <= S => 0.00 dB visual loss!).
void simplifyCoveredPolygon(
    std::vector<Pt>& poly,
    std::vector<std::vector<Pt>> const& holes,
    std::int16_t myZ,
    std::int32_t myId,
    LayerCoverGrid const& coverGrid,
    SvgStats& stats
) {
    if (!coverGrid.valid() || poly.size() < 4) return;
    if (polygonSignedArea(poly) < 0.0) std::reverse(poly.begin(), poly.end());

    auto noSelfHit = [&](Pt t0, Pt t1, Pt t2, std::size_t skipA, std::size_t skipB, std::size_t skipC,
                         Pt newE0a, Pt newE0b, bool hasSecondEdge, Pt newE1a, Pt newE1b) -> bool {
        if (ptCross(t1 - t0, t2 - t0) < 0.0) std::swap(t1, t2);
        std::size_t const n = poly.size();
        for (std::size_t k = 0; k < n; ++k) {
            if (k == skipA || k == skipB || k == skipC) continue;
            if (pointInTri(poly[k], t0, t1, t2)) return false;
        }
        for (auto const& h : holes) {
            for (std::size_t k = 0; k < h.size(); ++k) {
                if (pointInTri(h[k], t0, t1, t2)) return false;
                Pt ha = h[k], hb = h[(k + 1) % h.size()];
                if (segmentsProperlyIntersect(newE0a, newE0b, ha, hb)) return false;
                if (hasSecondEdge && segmentsProperlyIntersect(newE1a, newE1b, ha, hb)) return false;
            }
        }
        for (std::size_t k = 0; k < n; ++k) {
            std::size_t k2 = (k + 1) % n;
            if (k == skipA || k == skipB || k == skipC ||
                k2 == skipA || k2 == skipB || k2 == skipC) continue;
            if (segmentsProperlyIntersect(newE0a, newE0b, poly[k], poly[k2])) return false;
            if (hasSecondEdge && segmentsProperlyIntersect(newE1a, newE1b, poly[k], poly[k2])) return false;
        }
        return true;
    };

    for (int pass = 0; pass < 14 && poly.size() > 3; ++pass) {
        bool changed = false;

        // Move A: Collapse reflex (concave) vertices whose exterior bay is completely covered by Z > myZ.
        for (std::size_t i = 0; i < poly.size() && poly.size() > 3; ) {
            std::size_t const n = poly.size();
            std::size_t const ip = (i + n - 1) % n;
            std::size_t const in = (i + 1) % n;
            Pt a = poly[ip], b = poly[i], c = poly[in];
            double cr = ptCross(b - a, c - b);
            if (cr < -1e-4) {
                if (coverGrid.triangleSafe(a, c, b, myZ, myId, a, c) &&
                    noSelfHit(a, c, b, ip, i, in, a, c, false, {}, {})) {
                    poly.erase(poly.begin() + static_cast<std::ptrdiff_t>(i));
                    ++stats.coverVerticesCollapsed;
                    changed = true;
                    continue;
                }
            }
            ++i;
        }

        // Move B: Collapse pairs of consecutive convex vertices (b, c) into outer tangent corner X
        // whenever triangle (b, X, c) sits completely underneath higher-Z regions!
        for (std::size_t i = 0; i < poly.size() && poly.size() > 4; ) {
            std::size_t const n = poly.size();
            std::size_t const ia = (i + n - 1) % n;
            std::size_t const ib = i;
            std::size_t const ic = (i + 1) % n;
            std::size_t const id = (i + 2) % n;
            Pt a = poly[ia], b = poly[ib], c = poly[ic], d = poly[id];
            if (ptCross(b - a, c - b) > 1e-4 && ptCross(c - b, d - c) > 1e-4) {
                Pt d1 = b - a;
                Pt d2 = c - d;
                double den = ptCross(d1, d2);
                if (std::abs(den) > 1e-5) {
                    double t = ptCross(c - b, d2) / den;
                    double u = ptCross(c - b, d1) / den;
                    if (t > 1e-4 && u > 1e-4) {
                        Pt X = b + d1 * t;
                        if (ptDist(X, b) <= 85.0 && ptDist(X, c) <= 85.0 &&
                            ptCross(X - b, c - b) > 1e-4 &&
                            coverGrid.triangleSafe(b, X, c, myZ, myId, b, c) &&
                            noSelfHit(b, X, c, ib, ic, ia, a, X, true, X, d)) {
                            poly[ib] = X;
                            poly.erase(poly.begin() + static_cast<std::ptrdiff_t>(ic));
                            ++stats.coverVerticesCollapsed;
                            changed = true;
                            continue;
                        }
                    }
                }
            }
            ++i;
        }

        // Move C: Multi-vertex concave bay chord shortcut (v_i -> v_{i+k}, k = 2..7)
        // Collapses wavy/stepped concave chains underneath higher-Z regions in one shot!
        for (std::size_t i = 0; i < poly.size() && poly.size() > 4; ++i) {
            std::size_t const n = poly.size();
            std::size_t const maxK = std::min<std::size_t>(7, n - 3);
            bool collapsed = false;
            for (std::size_t k = maxK; k >= 2; --k) {
                std::size_t const j = (i + k) % n;
                Pt a = poly[i], c = poly[j];
                // Check all intermediate vertices lie on the interior (positive cross) side of a->c
                // so replacing i..j by chord a->c strictly EXPANDS poly (P <= P').
                bool allBehind = true;
                for (std::size_t step = 1; step < k; ++step) {
                    Pt m = poly[(i + step) % n];
                    if (ptCross(c - a, m - a) <= 1e-4) { allBehind = false; break; }
                }
                if (!allBehind) continue;
                // Check edge a->c doesn't intersect any other edge of poly or holes.
                bool edgeCrosses = false;
                for (std::size_t e = 0; e < n && !edgeCrosses; ++e) {
                    std::size_t e2 = (e + 1) % n;
                    if (e == i || e2 == i || e == j || e2 == j) continue;
                    // Skip edges inside the collapsed sub-chain i..j
                    bool inChain = false;
                    for (std::size_t s = 0; s < k; ++s) {
                        if (e == (i + s) % n) { inChain = true; break; }
                    }
                    if (inChain) continue;
                    if (segmentsProperlyIntersect(a, c, poly[e], poly[e2])) edgeCrosses = true;
                }
                if (edgeCrosses) continue;
                for (auto const& h : holes) {
                    for (std::size_t e = 0; e < h.size() && !edgeCrosses; ++e) {
                        if (segmentsProperlyIntersect(a, c, h[e], h[(e + 1) % h.size()])) edgeCrosses = true;
                    }
                    if (edgeCrosses) break;
                }
                if (edgeCrosses) continue;
                // Verify the entire bay polygon (a, c, v_{j-1}, ..., v_{i+1}) is simple & cover-safe!
                std::vector<Pt> bay;
                bay.reserve(k + 1);
                bay.push_back(a);
                bay.push_back(c);
                for (std::size_t step = k - 1; step >= 1; --step) {
                    bay.push_back(poly[(i + step) % n]);
                }
                if (polygonSignedArea(bay) <= 1e-3) continue;
                auto bayTris = triangulatePolygon(bay);
                if (bayTris.empty()) continue;
                bool baySafe = true;
                for (auto const& bt : bayTris) {
                    Pt ta = bay[static_cast<std::size_t>(bt.a)];
                    Pt tb = bay[static_cast<std::size_t>(bt.b)];
                    Pt tc = bay[static_cast<std::size_t>(bt.c)];
                    if (!coverGrid.triangleSafe(ta, tb, tc, myZ, myId, a, c)) {
                        baySafe = false;
                        break;
                    }
                    // Ensure no remaining vertex of poly or holes lies inside this bay triangle.
                    for (std::size_t pIdx = 0; pIdx < n && baySafe; ++pIdx) {
                        bool onChain = false;
                        for (std::size_t s = 0; s <= k; ++s) {
                            if (pIdx == (i + s) % n) { onChain = true; break; }
                        }
                        if (onChain) continue;
                        if (pointInTri(poly[pIdx], ta, tb, tc)) baySafe = false;
                    }
                    for (auto const& h : holes) {
                        for (Pt hp : h) {
                            if (pointInTri(hp, ta, tb, tc)) { baySafe = false; break; }
                        }
                        if (!baySafe) break;
                    }
                    if (!baySafe) break;
                }
                if (!baySafe) continue;

                // Remove the k - 1 intermediate vertices!
                std::vector<Pt> rebuilt;
                rebuilt.reserve(n - (k - 1));
                for (std::size_t pIdx = 0; pIdx < n; ++pIdx) {
                    bool drop = false;
                    for (std::size_t s = 1; s < k; ++s) {
                        if (pIdx == (i + s) % n) { drop = true; break; }
                    }
                    if (!drop) rebuilt.push_back(poly[pIdx]);
                }
                poly = std::move(rebuilt);
                stats.coverVerticesCollapsed += static_cast<int>(k - 1);
                changed = true;
                collapsed = true;
                break;
            }
            if (collapsed) break;
        }

        if (!changed) break;
    }
}

// -------------------- Exact GD Primitive Emission --------------------

double normalizePositiveAngle(double rad) {
    double a = std::fmod(rad, 2.0 * kPiD);
    if (a <= 0.0) a += 2.0 * kPiD;
    return a;
}

void emitSingleTriangle(
    std::vector<Primitive>& out,
    Pt p0, Pt p1, Pt p2,
    double toGrid,
    std::uint16_t color,
    std::int16_t layer
) {
    double cross = ptCross(p1 - p0, p2 - p0);
    if (std::abs(cross) < 1e-4) return;
    if (cross < 0.0) std::swap(p1, p2);

    // Choose the vertex whose interior angle is closest to 90 degrees so |skew| is minimized!
    Pt verts[3] = {p0, p1, p2};
    int bestCorner = 0;
    double bestSkewAbs = 1e30;
    double bestXAngle = 0.0, bestSkew = 0.0;
    for (int i = 0; i < 3; ++i) {
        Pt a = verts[i];
        Pt b = verts[(i + 1) % 3];
        Pt c = verts[(i + 2) % 3];
        double xa = std::atan2(b.y - a.y, b.x - a.x);
        double ya = std::atan2(c.y - a.y, c.x - a.x);
        double delta = normalizePositiveAngle(ya - xa); // strictly in (0, pi) since CCW
        double skewDeg = delta * 180.0 / kPiD - 90.0;
        if (std::abs(skewDeg) < bestSkewAbs) {
            bestSkewAbs = std::abs(skewDeg);
            bestCorner = i;
            bestXAngle = xa;
            bestSkew = skewDeg;
        }
    }

    Pt a = verts[bestCorner];
    Pt b = verts[(bestCorner + 1) % 3];
    Pt c = verts[(bestCorner + 2) % 3];

    // If super obtuse (|skew| > 74 deg), drop altitude onto BC -> 2 right triangles with skew = 0!
    // Skip splitting if the altitude is a sub-pixel needle (< 0.75 units in 1000u space).
    if (bestSkewAbs > 74.0) {
        Pt bc = c - b;
        double lenSq = ptDot(bc, bc);
        if (lenSq > 1e-8) {
            double alt = std::abs(ptCross(b - a, c - a)) / std::sqrt(lenSq);
            if (alt < 0.75) return;
            double t = ptDot(a - b, bc) / lenSq;
            if (t > 0.002 && t < 0.998) {
                Pt h = b + bc * t;
                emitSingleTriangle(out, h, a, b, toGrid, color, layer);
                emitSingleTriangle(out, h, c, a, toGrid, color, layer);
                return;
            }
        }
        // Extreme degenerate needle (altitude practically zero): drop it.
        return;
    }

    double w = ptDist(a, b) * toGrid;
    double h = ptDist(a, c) * toGrid;
    if (w < 1e-4 || h < 1e-4 || std::abs(bestSkew) >= 74.5) return;

    Primitive prim{
        static_cast<float>((b.x + c.x) * 0.5 * toGrid),
        static_cast<float>((b.y + c.y) * 0.5 * toGrid),
        static_cast<float>(w),
        static_cast<float>(h),
        static_cast<float>(bestXAngle * 180.0 / kPiD),
        color,
        PrimitiveKind::Triangle,
        layer,
        0
    };
    prim.skew = static_cast<float>(bestSkew);
    out.push_back(prim);
}

double triangleBestSkewAbs(Pt p0, Pt p1, Pt p2) {
    if (ptCross(p1 - p0, p2 - p0) < 0.0) std::swap(p1, p2);
    Pt verts[3] = {p0, p1, p2};
    double best = 1e30;
    for (int i = 0; i < 3; ++i) {
        Pt a = verts[i], b = verts[(i + 1) % 3], c = verts[(i + 2) % 3];
        double xa = std::atan2(b.y - a.y, b.x - a.x);
        double ya = std::atan2(c.y - a.y, c.x - a.x);
        double delta = normalizePositiveAngle(ya - xa);
        best = std::min(best, std::abs(delta * 180.0 / kPiD - 90.0));
    }
    return best;
}

bool tryEmitParallelogram(
    std::vector<Primitive>& out,
    Pt p0, Pt p1, Pt p2, Pt p3,
    double maxCornerErr,
    double toGrid,
    std::uint16_t color,
    std::int16_t layer,
    double bleed = 0.0,
    LayerCoverGrid const* coverGrid = nullptr,
    std::int16_t coverZ = 0,
    std::int32_t coverId = -1
) {
    // Ensure positive orientation (CCW in Y-down).
    if (polygonSignedArea({p0, p1, p2, p3}) < 0.0) std::swap(p1, p3);
    // Strict convexity.
    if (ptCross(p1 - p0, p2 - p1) <= 1e-5 || ptCross(p2 - p1, p3 - p2) <= 1e-5 ||
        ptCross(p3 - p2, p0 - p3) <= 1e-5 || ptCross(p0 - p3, p1 - p0) <= 1e-5) {
        return false;
    }
    Pt center = (p0 + p1 + p2 + p3) * 0.25;
    Pt u = ((p1 - p0) + (p2 - p3)) * 0.5;
    Pt v = ((p3 - p0) + (p2 - p1)) * 0.5;
    Pt errVec = (p0 + p2) - (p1 + p3);
    double cornerErr = 0.25 * std::hypot(errVec.x, errVec.y);
    bool const needsCoverCheck = (cornerErr > maxCornerErr);
    if (needsCoverCheck && (!coverGrid || !coverGrid->valid() || cornerErr > 22.0)) return false;
    double det = ptCross(u, v);
    if (det <= 1e-5) return false;

    double uLen = std::hypot(u.x, u.y);
    double vLen = std::hypot(v.x, v.y);
    if (uLen < 1e-4 || vLen < 1e-4) return false;

    double xa = std::atan2(u.y, u.x);
    double ya = std::atan2(v.y, v.x);
    double delta = normalizePositiveAngle(ya - xa);
    double skewDeg = delta * 180.0 / kPiD - 90.0;
    if (std::abs(skewDeg) > 72.0) return false;

    // Expand strictly to enclose all 4 vertices in (u, v) basis coordinates -> ZERO inward cracks!
    Pt uHat = u * (1.0 / uLen);
    Pt vHat = v * (1.0 / vLen);
    double basisDet = ptCross(uHat, vHat);
    double maxAbsU = uLen * 0.5;
    double maxAbsV = vLen * 0.5;
    if (std::abs(basisDet) > 1e-4) {
        for (Pt pt : {p0, p1, p2, p3}) {
            Pt d = pt - center;
            double cu = ptCross(d, vHat) / basisDet;
            double cv = ptCross(uHat, d) / basisDet;
            maxAbsU = std::max(maxAbsU, std::abs(cu));
            maxAbsV = std::max(maxAbsV, std::abs(cv));
        }
    }
    bool const tightBloatOk =
        (maxAbsU * 2.0 - uLen) <= maxCornerErr * 2.5 + 0.2 &&
        (maxAbsV * 2.0 - vLen) <= maxCornerErr * 2.5 + 0.2;
    if (needsCoverCheck || !tightBloatOk) {
        if (!coverGrid || !coverGrid->valid()) return false;
        Pt hu = uHat * (maxAbsU + bleed * 0.5);
        Pt hv = vHat * (maxAbsV + bleed * 0.5);
        Pt c0 = center - hu - hv;
        Pt c1 = center + hu - hv;
        Pt c2 = center + hu + hv;
        Pt c3 = center - hu + hv;
        if (!coverGrid->quadExcessSafe(c0, c1, c2, c3, p0, p1, p2, p3, coverZ, coverId)) return false;
    }

    Primitive prim{
        static_cast<float>(center.x * toGrid),
        static_cast<float>(center.y * toGrid),
        static_cast<float>((maxAbsU * 2.0 + bleed) * toGrid),
        static_cast<float>((maxAbsV * 2.0 + bleed) * toGrid),
        static_cast<float>(xa * 180.0 / kPiD),
        color,
        PrimitiveKind::Block,
        layer,
        0
    };
    prim.skew = static_cast<float>(skewDeg);
    out.push_back(prim);
    return true;
}

// Exact half-plane offset of ONLY internal shared triangle edges (outer boundary moves by 0.000!).
void bleedTriangleInternalEdges(
    Pt& a, Pt& b, Pt& c,
    bool intAB, bool intBC, bool intCA,
    double bleed
) {
    if (bleed <= 0.0 || (!intAB && !intBC && !intCA)) return;
    if (ptCross(b - a, c - a) < 0.0) {
        std::swap(b, c);
        std::swap(intAB, intCA);
    }
    struct Line { Pt p; Pt d; };
    auto makeEdge = [&](Pt u, Pt v, bool isInt) -> Line {
        Pt d = v - u;
        double l = std::hypot(d.x, d.y);
        if (l < 1e-9 || !isInt) return {u, d};
        // Outward normal for CCW in Y-down (cross > 0) is (dy/l, -dx/l).
        Pt n{d.y / l, -d.x / l};
        return {u + n * bleed, d};
    };
    auto intersect = [](Line const& l1, Line const& l2, Pt fallback) -> Pt {
        double den = ptCross(l1.d, l2.d);
        if (std::abs(den) < 1e-7) return fallback;
        double t = ptCross(l2.p - l1.p, l2.d) / den;
        Pt hit = l1.p + l1.d * t;
        // Clamp miter spikes on ultra-acute angles.
        if (ptDist(hit, fallback) > 1.2) return fallback;
        return hit;
    };
    Line eAB = makeEdge(a, b, intAB);
    Line eBC = makeEdge(b, c, intBC);
    Line eCA = makeEdge(c, a, intCA);
    Pt na = intersect(eCA, eAB, a);
    Pt nb = intersect(eAB, eBC, b);
    Pt nc = intersect(eBC, eCA, c);
    if (ptCross(nb - na, nc - na) > 1e-4) {
        a = na;
        b = nb;
        c = nc;
    }
}

void emitPolygonPrimitives(
    std::vector<Primitive>& out,
    std::vector<Pt> const& outerIn,
    std::vector<Pt> const& outerHighRes,
    std::vector<std::vector<Pt>> const& holesIn,
    double toGrid,
    std::uint16_t color,
    std::int16_t layer,
    SvgOptions const& options,
    SvgStats& stats,
    LayerCoverGrid const* coverGrid = nullptr,
    std::int16_t coverZ = 0,
    std::int32_t coverId = -1
) {
    // 1. Direct ellipse / circle detection on HIGH-RES unsimplified ring (zero chord sag!).
    if (holesIn.empty() && options.detectEllipses && outerHighRes.size() >= 8) {
        if (auto ell = tryFitEllipse(outerHighRes, options.ellipseTolerance)) {
            Primitive prim{
                static_cast<float>(ell->center.x * toGrid),
                static_cast<float>(ell->center.y * toGrid),
                static_cast<float>(ell->rx * 2.0 * toGrid),
                static_cast<float>(ell->ry * 2.0 * toGrid),
                static_cast<float>(ell->angleRad * 180.0 / kPiD),
                color,
                PrimitiveKind::Circle,
                layer,
                0
            };
            out.push_back(prim);
            ++stats.ellipsesFitted;
            return;
        }
    }

    auto outer = cleanPolygon(outerIn, std::clamp(options.simplifyTolerance * 0.55, 0.35, 1.4));
    if (outer.size() < 3 || std::abs(polygonSignedArea(outer)) < 0.5) return;
    if (polygonSignedArea(outer) < 0.0) std::reverse(outer.begin(), outer.end());

    std::vector<std::vector<Pt>> cleanHoles;
    for (auto const& h : holesIn) {
        auto ch = cleanPolygon(h, std::clamp(options.simplifyTolerance * 0.55, 0.35, 1.4));
        if (ch.size() >= 3 && std::abs(polygonSignedArea(ch)) >= 0.5) {
            cleanHoles.push_back(std::move(ch));
        }
    }

    // 1b. Lossless cover expansion under strictly higher-Z regions (0.00 dB visual loss!).
    if (coverGrid && coverGrid->valid() && outer.size() >= 4) {
        simplifyCoveredPolygon(outer, cleanHoles, coverZ, coverId, *coverGrid, stats);
        outer = cleanPolygon(outer, std::clamp(options.simplifyTolerance * 0.55, 0.35, 1.4));
        if (outer.size() < 3 || std::abs(polygonSignedArea(outer)) < 0.5) return;
        if (polygonSignedArea(outer) < 0.0) std::reverse(outer.begin(), outer.end());
    }

    // 2. Direct single-primitive detection BEFORE triangulation!
    //    2a) Direct 4-vertex parallelogram / rectangle (tight OR cover-verified).
    if (cleanHoles.empty() && outer.size() == 4 && options.mergeParallelograms) {
        if (tryEmitParallelogram(out, outer[0], outer[1], outer[2], outer[3],
                                 options.quadTolerance, toGrid, color, layer,
                                 0.0, coverGrid, coverZ, coverId)) {
            ++stats.parallelogramsMerged;
            return;
        }
    }

    //    2b) Whole-region lossless cover fitting (1 Block OBB, 1 Triangle, or 1 Circle)
    //        for ANY hole-free covered polygon (collapses 4..30 triangles into 1 object at 0.00 dB loss!).
    if (cleanHoles.empty() && outer.size() >= 4 && coverGrid && coverGrid->valid()) {
        auto hull = convexHullCCW(outer);

        // (i) Try whole-region enclosing Block (AABB + minimum-area rotating-calipers OBB).
        if (options.mergeParallelograms && hull.size() >= 3 && hull.size() <= 40) {
            struct ObbCand {
                double area = 1e30;
                double cx = 0, cy = 0, w = 0, h = 0, angle = 0;
                std::vector<Pt> corners;
            };
            std::vector<ObbCand> obbs;
            auto evalAxis = [&](double ux, double uy) {
                double vx = -uy, vy = ux;
                double minU = 1e30, maxU = -1e30, minV = 1e30, maxV = -1e30;
                for (Pt p : hull) {
                    double pu = p.x * ux + p.y * uy;
                    double pv = p.x * vx + p.y * vy;
                    minU = std::min(minU, pu); maxU = std::max(maxU, pu);
                    minV = std::min(minV, pv); maxV = std::max(maxV, pv);
                }
                double bw = maxU - minU, bh = maxV - minV;
                if (bw < 0.2 || bh < 0.2) return;
                double cu = 0.5 * (minU + maxU), cv = 0.5 * (minV + maxV);
                Pt c{cu * ux + cv * vx, cu * uy + cv * vy};
                Pt hu{ux * (bw * 0.5), uy * (bw * 0.5)};
                Pt hv{vx * (bh * 0.5), vy * (bh * 0.5)};
                std::vector<Pt> box{c - hu - hv, c + hu - hv, c + hu + hv, c - hu + hv};
                if (polygonSignedArea(box) < 0.0) std::swap(box[1], box[3]);
                obbs.push_back({bw * bh, c.x, c.y, bw, bh, std::atan2(uy, ux), std::move(box)});
            };
            evalAxis(1.0, 0.0);
            for (std::size_t i = 0; i < hull.size(); ++i) {
                Pt d = hull[(i + 1) % hull.size()] - hull[i];
                double l = std::hypot(d.x, d.y);
                if (l > 1e-4) evalAxis(d.x / l, d.y / l);
            }
            std::sort(obbs.begin(), obbs.end(), [](auto const& a, auto const& b) {
                return a.area < b.area;
            });
            std::size_t const tries = std::min<std::size_t>(obbs.size(), 4);
            for (std::size_t k = 0; k < tries; ++k) {
                auto const& o = obbs[k];
                if (coverGrid->polygonExcessSafe(o.corners, outer, coverZ, coverId)) {
                    out.push_back({
                        static_cast<float>(o.cx * toGrid),
                        static_cast<float>(o.cy * toGrid),
                        static_cast<float>(o.w * toGrid),
                        static_cast<float>(o.h * toGrid),
                        static_cast<float>(o.angle * 180.0 / kPiD),
                        color, PrimitiveKind::Block, layer, 0
                    });
                    ++stats.parallelogramsMerged;
                    return;
                }
            }
        }

        // (ii) Try whole-region enclosing Triangle (extend 3 hull edges for compact covered shapes!).
        if (hull.size() >= 3 && hull.size() <= 10) {
            std::size_t const hn = hull.size();
            auto intersectLines = [](Pt p1, Pt d1, Pt p2, Pt d2, Pt& hit) -> bool {
                double den = ptCross(d1, d2);
                if (std::abs(den) < 1e-6) return false;
                double t = ptCross(p2 - p1, d2) / den;
                hit = p1 + d1 * t;
                return std::isfinite(hit.x) && std::isfinite(hit.y);
            };
            double bestArea = 1e30;
            bool foundTri = false;
            Pt bestT0{}, bestT1{}, bestT2{};
            for (std::size_t i = 0; i < hn; ++i) {
                Pt pi = hull[i], di = hull[(i + 1) % hn] - hull[i];
                for (std::size_t j = i + 1; j < hn; ++j) {
                    Pt pj = hull[j], dj = hull[(j + 1) % hn] - hull[j];
                    Pt v01{};
                    if (!intersectLines(pi, di, pj, dj, v01)) continue;
                    for (std::size_t k = j + 1; k < hn; ++k) {
                        Pt pk = hull[k], dk = hull[(k + 1) % hn] - hull[k];
                        Pt v12{}, v20{};
                        if (!intersectLines(pj, dj, pk, dk, v12)) continue;
                        if (!intersectLines(pk, dk, pi, di, v20)) continue;
                        double area = 0.5 * std::abs(ptCross(v12 - v01, v20 - v01));
                        if (area < 1.0 || area >= bestArea || area > std::abs(polygonSignedArea(outer)) * 4.5) continue;
                        Pt t0 = v01, t1 = v12, t2 = v20;
                        if (ptCross(t1 - t0, t2 - t0) < 0.0) std::swap(t1, t2);
                        // Verify all hull vertices lie inside (t0, t1, t2)
                        bool coversAll = true;
                        for (Pt hp : hull) {
                            if (!pointInTri(hp, t0, t1, t2)) { coversAll = false; break; }
                        }
                        if (!coversAll) continue;
                        if (triangleBestSkewAbs(t0, t1, t2) > 73.5) continue;
                        if (!coverGrid->polygonExcessSafe({t0, t1, t2}, outer, coverZ, coverId)) continue;
                        bestArea = area;
                        bestT0 = t0; bestT1 = t1; bestT2 = t2;
                        foundTri = true;
                    }
                }
            }
            if (foundTri) {
                emitSingleTriangle(out, bestT0, bestT1, bestT2, toGrid, color, layer);
                ++stats.triPairsCollapsed;
                return;
            }
        }

        // (iii) Partial-arc covered Circle fitting (e.g. cheek blush, iris behind eyelid, round buttons!)
        if (options.detectEllipses && outerHighRes.size() >= 10 && outer.size() >= 6) {
            std::vector<Pt> exposed;
            std::size_t const m = outerHighRes.size();
            for (std::size_t i = 0; i < m; ++i) {
                Pt prev = outerHighRes[(i + m - 1) % m];
                Pt cur = outerHighRes[i];
                Pt next = outerHighRes[(i + 1) % m];
                Pt tang = next - prev;
                double tl = std::hypot(tang.x, tang.y);
                if (tl < 1e-6) continue;
                Pt outN{tang.y / tl, -tang.x / tl}; // outward normal for CCW in Y-down
                if (!coverGrid->pointRawSafe(cur + outN * 2.0, coverZ, coverId)) {
                    exposed.push_back(cur);
                }
            }
            if (exposed.size() >= 6 && exposed.size() * 10 < m * 9) {
                // Kasa algebraic circle fit on exposed arc points (fast closed-form 3x3 linear system).
                double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sz = 0, sxz = 0, syz = 0;
                double nExp = static_cast<double>(exposed.size());
                for (Pt p : exposed) {
                    double z = p.x * p.x + p.y * p.y;
                    sx += p.x; sy += p.y;
                    sxx += p.x * p.x; syy += p.y * p.y; sxy += p.x * p.y;
                    sz += z; sxz += p.x * z; syz += p.y * z;
                }
                double A = nExp * sxx - sx * sx;
                double B = nExp * sxy - sx * sy;
                double C = nExp * syy - sy * sy;
                double D = 0.5 * (nExp * sxz - sx * sz);
                double E = 0.5 * (nExp * syz - sy * sz);
                double det = A * C - B * B;
                if (std::abs(det) > 1e-4) {
                    double cx = (D * C - B * E) / det;
                    double cy = (A * E - B * D) / det;
                    double rSum = 0.0;
                    for (Pt p : exposed) rSum += std::hypot(p.x - cx, p.y - cy);
                    double r = rSum / nExp;
                    if (r >= 2.5 && r <= 300.0) {
                        double maxArcErr = 0.0;
                        for (Pt p : exposed) {
                            maxArcErr = std::max(maxArcErr, std::abs(std::hypot(p.x - cx, p.y - cy) - r));
                        }
                        bool coversOuter = true;
                        for (Pt p : outer) {
                            if (std::hypot(p.x - cx, p.y - cy) > r + 0.65) {
                                coversOuter = false;
                                break;
                            }
                        }
                        if (maxArcErr <= std::max<double>(options.ellipseTolerance, 0.05 * r) &&
                            coversOuter &&
                            coverGrid->circleExcessSafe({cx, cy}, r, outer, coverZ, coverId)) {
                            out.push_back({
                                static_cast<float>(cx * toGrid),
                                static_cast<float>(cy * toGrid),
                                static_cast<float>(r * 2.0 * toGrid),
                                static_cast<float>(r * 2.0 * toGrid),
                                0.f, color, PrimitiveKind::Circle, layer, 0
                            });
                            ++stats.ellipsesFitted;
                            return;
                        }
                    }
                }
            }
        }
    }

    // 3. Merge any remaining holes & triangulate.
    auto merged = mergeHolesIntoOuter(std::move(outer), std::move(cleanHoles));
    auto tris = triangulatePolygon(merged);
    if (tris.empty()) return;

    // 3b. Iterative lossless 2 Tri -> 1 Tri mesh decimation under higher-Z cover!
    //     By updating the mesh directly, Pass 2 can collapse newly merged triangles again (4->2->1)!
    auto edgeKey = [](int u, int v) -> std::uint64_t {
        if (u > v) std::swap(u, v);
        return (static_cast<std::uint64_t>(u) << 32) | static_cast<std::uint32_t>(v);
    };
    auto oppVert = [](Tri const& t, int u, int v) -> int {
        if (t.a != u && t.a != v) return t.a;
        if (t.b != u && t.b != v) return t.b;
        return t.c;
    };

    if (coverGrid && coverGrid->valid() && tris.size() >= 2) {
        for (int pass = 0; pass < 4 && tris.size() >= 2; ++pass) {
            std::unordered_map<std::uint64_t, std::pair<int, int>> mAdj;
            mAdj.reserve(tris.size() * 3);
            for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti) {
                int v[3] = {tris[static_cast<std::size_t>(ti)].a,
                            tris[static_cast<std::size_t>(ti)].b,
                            tris[static_cast<std::size_t>(ti)].c};
                for (int e = 0; e < 3; ++e) {
                    auto k = edgeKey(v[e], v[(e + 1) % 3]);
                    auto it = mAdj.find(k);
                    if (it == mAdj.end()) mAdj[k] = {ti, -1};
                    else it->second.second = ti;
                }
            }
            struct TriMerge {
                int t1 = 0, t2 = 0;
                int ia = 0, id = 0;
                Pt X{};
                double excess = 1e30;
            };
            std::vector<TriMerge> merges;
            for (auto const& [k, pair] : mAdj) {
                if (pair.second < 0) continue;
                int u = static_cast<int>(k >> 32);
                int v = static_cast<int>(k & 0xffffffffu);
                int p = oppVert(tris[static_cast<std::size_t>(pair.first)], u, v);
                int q = oppVert(tris[static_cast<std::size_t>(pair.second)], u, v);
                if (p == q) continue;
                int qIdx[4] = {u, p, v, q};
                if (ptCross(merged[static_cast<std::size_t>(p)] - merged[static_cast<std::size_t>(u)],
                            merged[static_cast<std::size_t>(v)] - merged[static_cast<std::size_t>(u)]) < 0.0) {
                    std::swap(qIdx[1], qIdx[3]);
                }
                Pt qp[4] = {merged[static_cast<std::size_t>(qIdx[0])],
                            merged[static_cast<std::size_t>(qIdx[1])],
                            merged[static_cast<std::size_t>(qIdx[2])],
                            merged[static_cast<std::size_t>(qIdx[3])]};
                if (ptCross(qp[1] - qp[0], qp[2] - qp[1]) <= 1e-5 ||
                    ptCross(qp[2] - qp[1], qp[3] - qp[2]) <= 1e-5 ||
                    ptCross(qp[3] - qp[2], qp[0] - qp[3]) <= 1e-5 ||
                    ptCross(qp[0] - qp[3], qp[1] - qp[0]) <= 1e-5) {
                    continue;
                }
                TriMerge best;
                bool ok = false;
                for (int side = 0; side < 4; ++side) {
                    int ia = qIdx[side];
                    int ib = qIdx[(side + 1) % 4];
                    int ic = qIdx[(side + 2) % 4];
                    int id = qIdx[(side + 3) % 4];
                    // Only collapse across an outer boundary edge (ib -> ic) so we don't break mesh connectivity!
                    auto itBC = mAdj.find(edgeKey(ib, ic));
                    if (itBC != mAdj.end() && itBC->second.second >= 0) continue;
                    Pt a = qp[side], b = qp[(side + 1) % 4], cc = qp[(side + 2) % 4], d = qp[(side + 3) % 4];
                    Pt d1 = b - a, d2 = cc - d;
                    double den = ptCross(d1, d2);
                    if (std::abs(den) < 1e-5) continue;
                    double t = ptCross(cc - b, d2) / den;
                    double uPar = ptCross(cc - b, d1) / den;
                    if (t <= 1e-4 || uPar <= 1e-4) continue;
                    Pt X = b + d1 * t;
                    if (ptDist(X, b) > 85.0 || ptDist(X, cc) > 85.0) continue;
                    double excess = 0.5 * ptCross(X - b, cc - b);
                    if (excess <= 1e-4 || excess >= best.excess) continue;
                    if (triangleBestSkewAbs(a, X, d) > 73.5) continue;
                    if (!coverGrid->triangleSafe(b, X, cc, coverZ, coverId, b, cc)) continue;
                    best = {pair.first, pair.second, ia, id, X, excess};
                    ok = true;
                }
                if (ok) merges.push_back(best);
            }
            if (merges.empty()) break;
            std::sort(merges.begin(), merges.end(), [](auto const& a, auto const& b) {
                return a.excess < b.excess;
            });
            std::vector<std::uint8_t> dead(tris.size(), 0);
            std::vector<Tri> nextTris;
            nextTris.reserve(tris.size());
            bool anyMerged = false;
            for (auto const& m : merges) {
                if (dead[static_cast<std::size_t>(m.t1)] || dead[static_cast<std::size_t>(m.t2)]) continue;
                int xIdx = static_cast<int>(merged.size());
                merged.push_back(m.X);
                nextTris.push_back({m.ia, xIdx, m.id});
                dead[static_cast<std::size_t>(m.t1)] = 1;
                dead[static_cast<std::size_t>(m.t2)] = 1;
                ++stats.triPairsCollapsed;
                anyMerged = true;
            }
            if (!anyMerged) break;
            for (std::size_t ti = 0; ti < tris.size(); ++ti) {
                if (!dead[ti]) nextTris.push_back(tris[ti]);
            }
            tris = std::move(nextTris);
        }
    }

    // Build internal-diagonal adjacency for parallelogram / ribbon merging + seam bleed.
    std::unordered_map<std::uint64_t, std::pair<int, int>> adj;
    adj.reserve(tris.size() * 3);
    for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti) {
        int v[3] = {tris[static_cast<std::size_t>(ti)].a,
                    tris[static_cast<std::size_t>(ti)].b,
                    tris[static_cast<std::size_t>(ti)].c};
        for (int e = 0; e < 3; ++e) {
            auto k = edgeKey(v[e], v[(e + 1) % 3]);
            auto it = adj.find(k);
            if (it == adj.end()) adj[k] = {ti, -1};
            else it->second.second = ti;
        }
    }

    auto isInternal = [&](int u, int v) {
        auto it = adj.find(edgeKey(u, v));
        return it != adj.end() && it->second.second >= 0;
    };

    // 4. Greedy multi-triangle -> Block merging:
    //    4a0) Chordal-Axis Ribbon-Chain Skewed Block Fitter (collapses 2..16 consecutive thin
    //         lineart/ribbon triangles into 1 skewed Block along the ribbon's exact centerline!)
    //    4a)  3-Triangle & 4-Triangle ribbon strip -> 1 Oriented Block
    //    4b)  Tight parallelograms (exact/near-exact Block)
    //    4c)  Outer-edge-aligned 2-Tri Ribbon Trapezoid -> 1 Block
    //    4d)  Lossless cover-verified enclosing Parallelograms!
    std::vector<std::uint8_t> consumed(tris.size(), 0);
    if (options.mergeParallelograms && tris.size() >= 2) {
        std::vector<std::vector<std::pair<int, std::uint64_t>>> triNbrs(tris.size());
        for (auto const& [k, pair] : adj) {
            if (pair.second < 0) continue;
            triNbrs[static_cast<std::size_t>(pair.first)].push_back({pair.second, k});
            triNbrs[static_cast<std::size_t>(pair.second)].push_back({pair.first, k});
        }

        // 4a0) Chordal-Axis Ribbon-Chain Skewed Block Fitter!
        //      Walks maximal degree-2 sleeve chains in the triangulation dual graph (thin strokes
        //      whose perpendicular ribbon thickness <= 16u, regardless of diagonal length!)
        //      and fits minimal skewed/rect Blocks along the ribbon's exact outer axis.
        if (tris.size() >= 3) {
            auto triRibbonWidth = [&](int ti) -> double {
                auto const& nb = triNbrs[static_cast<std::size_t>(ti)];
                if (nb.size() != 2) return 1e30;
                auto const& tr = tris[static_cast<std::size_t>(ti)];
                int tv[3] = {tr.a, tr.b, tr.c};
                double area2 = std::abs(ptCross(
                    merged[static_cast<std::size_t>(tr.b)] - merged[static_cast<std::size_t>(tr.a)],
                    merged[static_cast<std::size_t>(tr.c)] - merged[static_cast<std::size_t>(tr.a)]));
                for (int e = 0; e < 3; ++e) {
                    std::uint64_t ek = edgeKey(tv[e], tv[(e + 1) % 3]);
                    if (ek != nb[0].second && ek != nb[1].second) {
                        double outLen = ptDist(merged[static_cast<std::size_t>(tv[e])],
                                               merged[static_cast<std::size_t>(tv[(e + 1) % 3])]);
                        if (outLen > 1e-4) return area2 / outLen;
                    }
                }
                return 1e30;
            };
            auto isSleeveTri = [&](int ti) -> bool {
                auto const& nb = triNbrs[static_cast<std::size_t>(ti)];
                if (nb.size() != 2) return false;
                return triRibbonWidth(ti) <= 18.0;
            };

            std::vector<std::uint8_t> visitedSleeve(tris.size(), 0);
            double const maxRibbonTol = std::min<double>(options.quadTolerance * 0.95, 0.72);

            for (int seed = 0; seed < static_cast<int>(tris.size()); ++seed) {
                if (visitedSleeve[static_cast<std::size_t>(seed)] || !isSleeveTri(seed)) continue;

                // Walk backward to start of this degree-2 chain (or detect closed loop).
                int cur = seed;
                int prev = -1;
                bool isLoop = false;
                while (true) {
                    auto const& nb = triNbrs[static_cast<std::size_t>(cur)];
                    int next = (nb[0].first == prev) ? nb[1].first : nb[0].first;
                    if (next == seed) { isLoop = true; break; }
                    if (!isSleeveTri(next) || visitedSleeve[static_cast<std::size_t>(next)]) break;
                    prev = cur;
                    cur = next;
                }
                int startTri = cur;
                int entryNbr = -1;
                if (!isLoop) {
                    auto const& nb = triNbrs[static_cast<std::size_t>(startTri)];
                    if (!isSleeveTri(nb[0].first) || visitedSleeve[static_cast<std::size_t>(nb[0].first)]) {
                        entryNbr = nb[0].first;
                    } else {
                        entryNbr = nb[1].first;
                    }
                } else {
                    entryNbr = triNbrs[static_cast<std::size_t>(startTri)][0].first;
                }

                // Walk forward collecting ordered sleeve triangles and their m+1 cross-section edges!
                std::vector<int> chainTris;
                std::vector<std::pair<int, int>> crossEdges; // (L_k, R_k) consistently oriented!
                auto edgeVertsOf = [&](int tA, int tB) -> std::pair<int, int> {
                    for (auto const& [n, k] : triNbrs[static_cast<std::size_t>(tA)]) {
                        if (n == tB) {
                            return {static_cast<int>(k >> 32), static_cast<int>(k & 0xffffffffu)};
                        }
                    }
                    return {-1, -1};
                };

                auto e0 = edgeVertsOf(startTri, entryNbr);
                if (e0.first < 0) { visitedSleeve[static_cast<std::size_t>(startTri)] = 1; continue; }
                int third0 = oppVert(tris[static_cast<std::size_t>(startTri)], e0.first, e0.second);
                if (ptCross(merged[static_cast<std::size_t>(e0.second)] - merged[static_cast<std::size_t>(e0.first)],
                            merged[static_cast<std::size_t>(third0)] - merged[static_cast<std::size_t>(e0.first)]) < 0.0) {
                    std::swap(e0.first, e0.second);
                }
                crossEdges.push_back(e0);

                cur = startTri;
                prev = entryNbr;
                while (true) {
                    if (visitedSleeve[static_cast<std::size_t>(cur)]) break;
                    visitedSleeve[static_cast<std::size_t>(cur)] = 1;
                    chainTris.push_back(cur);
                    auto const& nb = triNbrs[static_cast<std::size_t>(cur)];
                    int next = (nb[0].first == prev) ? nb[1].first : nb[0].first;
                    auto ek = edgeVertsOf(cur, next);
                    if (ek.first < 0) break;
                    auto prevE = crossEdges.back();
                    if (ek.first == prevE.first || ek.second == prevE.second) {
                        crossEdges.push_back(ek);
                    } else {
                        crossEdges.push_back({ek.second, ek.first});
                    }
                    if (!isSleeveTri(next) || visitedSleeve[static_cast<std::size_t>(next)]) break;
                    prev = cur;
                    cur = next;
                }

                std::size_t const m = chainTris.size();
                if (m < 2 || crossEdges.size() != m + 1) continue;

                // Try fitting a single skewed or rectangular Block to sub-chain [lo .. hi) (hi - lo >= 2).
                auto fitSubChain = [&](std::size_t lo, std::size_t hi, Primitive& outPrim) -> bool {
                    if (hi - lo < 2) return false;
                    Pt L0 = merged[static_cast<std::size_t>(crossEdges[lo].first)];
                    Pt R0 = merged[static_cast<std::size_t>(crossEdges[lo].second)];
                    Pt L1 = merged[static_cast<std::size_t>(crossEdges[hi].first)];
                    Pt R1 = merged[static_cast<std::size_t>(crossEdges[hi].second)];

                    // Collect unique boundary vertices of this sub-chain on Left and Right sides.
                    std::vector<Pt> leftPts, rightPts, allPts;
                    leftPts.reserve(hi - lo + 1);
                    rightPts.reserve(hi - lo + 1);
                    allPts.reserve((hi - lo + 1) * 2);
                    int lastL = -1, lastR = -1;
                    for (std::size_t k = lo; k <= hi; ++k) {
                        int li = crossEdges[k].first, ri = crossEdges[k].second;
                        if (li != lastL) {
                            leftPts.push_back(merged[static_cast<std::size_t>(li)]);
                            allPts.push_back(leftPts.back());
                            lastL = li;
                        }
                        if (ri != lastR) {
                            rightPts.push_back(merged[static_cast<std::size_t>(ri)]);
                            allPts.push_back(rightPts.back());
                            lastR = ri;
                        }
                    }
                    if (leftPts.empty() || rightPts.empty() || allPts.size() < 4) return false;

                    // Compute true ribbon axis from the actual outer boundary vectors (immune to slanted diagonals!).
                    Pt dL = leftPts.back() - leftPts.front();
                    Pt dR = rightPts.back() - rightPts.front();
                    Pt axis = dL + dR;
                    double axisLen = std::hypot(axis.x, axis.y);
                    if (axisLen < 1.5) {
                        axis = (L1 + R1) * 0.5 - (L0 + R0) * 0.5;
                        axisLen = std::hypot(axis.x, axis.y);
                    }
                    if (axisLen < 1.5) return false;
                    Pt uBase = axis * (1.0 / axisLen);
                    Pt nBase{-uBase.y, uBase.x};

                    // Verify both outer boundaries stay flat within maxRibbonTol along uBase!
                    double lMin = 1e30, lMax = -1e30, rMin = 1e30, rMax = -1e30;
                    for (Pt p : leftPts) {
                        double v = ptDot(p, nBase);
                        lMin = std::min(lMin, v); lMax = std::max(lMax, v);
                    }
                    for (Pt p : rightPts) {
                        double v = ptDot(p, nBase);
                        rMin = std::min(rMin, v); rMax = std::max(rMax, v);
                    }
                    double devL = 0.5 * (lMax - lMin);
                    double devR = 0.5 * (rMax - rMin);
                    if (std::max(devL, devR) > maxRibbonTol) return false;

                    double lSide = 0.5 * (lMin + lMax);
                    double rSide = 0.5 * (rMin + rMax);
                    double bh = std::abs(rSide - lSide);
                    if (bh < 0.7 || bh > 18.0) return false;

                    auto stripHull = convexHullCCW(allPts);
                    if (stripHull.size() < 3) return false;

                    // Evaluate candidate v-axes: (1) slanted average end-cap, (2) perpendicular rectangle!
                    double signN = (rSide >= lSide) ? 1.0 : -1.0;
                    Pt vNorm = nBase * signN;
                    std::vector<Pt> vCands;
                    Pt eStart = R0 - L0;
                    Pt eEnd = R1 - L1;
                    double lStart = std::hypot(eStart.x, eStart.y);
                    double lEnd = std::hypot(eEnd.x, eEnd.y);
                    if (lStart > 0.5 && lEnd > 0.5) {
                        Pt cv = eStart * (1.0 / lStart) + eEnd * (1.0 / lEnd);
                        double cl = std::hypot(cv.x, cv.y);
                        if (cl > 0.25) {
                            cv = cv * (1.0 / cl);
                            if (ptDot(cv, vNorm) > 0.40) vCands.push_back(cv);
                        }
                    }
                    vCands.push_back(vNorm);

                    double bestArea = 1e30;
                    bool found = false;
                    for (Pt vAxis : vCands) {
                        Pt uAxis = uBase;
                        if (ptCross(uAxis, vAxis) < 0.0) uAxis = uAxis * -1.0;
                        double xa = std::atan2(uAxis.y, uAxis.x);
                        double ya = std::atan2(vAxis.y, vAxis.x);
                        double delta = normalizePositiveAngle(ya - xa);
                        double skewDeg = delta * 180.0 / kPiD - 90.0;
                        if (std::abs(skewDeg) > 68.0) continue;
                        double bDet = ptCross(uAxis, vAxis);
                        if (std::abs(bDet) < 0.30) continue;

                        double minCu = 1e30, maxCu = -1e30, minCv = 1e30, maxCv = -1e30;
                        for (Pt p : allPts) {
                            double cu = ptCross(p, vAxis) / bDet;
                            double cv = ptCross(uAxis, p) / bDet;
                            minCu = std::min(minCu, cu); maxCu = std::max(maxCu, cu);
                            minCv = std::min(minCv, cv); maxCv = std::max(maxCv, cv);
                        }
                        double bw = maxCu - minCu;
                        double bv = maxCv - minCv;
                        if (bw < 1.2 || bv < 0.7) continue;
                        double area = bw * bv * std::abs(bDet);
                        if (area >= bestArea) continue;

                        double midCu = 0.5 * (minCu + maxCu);
                        double midCv = 0.5 * (minCv + maxCv);
                        Pt ctr = uAxis * midCu + vAxis * midCv;
                        Pt hu = uAxis * (bw * 0.5);
                        Pt hv = vAxis * (bv * 0.5);
                        std::vector<Pt> box{ctr - hu - hv, ctr + hu - hv, ctr + hu + hv, ctr - hu + hv};
                        if (polygonSignedArea(box) < 0.0) std::swap(box[1], box[3]);

                        if (coverGrid && !coverGrid->ribbonBoxFits(box, stripHull, maxRibbonTol * 1.05, coverZ, coverId)) {
                            continue;
                        }
                        bestArea = area;
                        outPrim = Primitive{
                            static_cast<float>(ctr.x * toGrid),
                            static_cast<float>(ctr.y * toGrid),
                            static_cast<float>((bw + options.internalSeamBleed * 0.45) * toGrid),
                            static_cast<float>((bv + options.internalSeamBleed * 0.20) * toGrid),
                            static_cast<float>(xa * 180.0 / kPiD),
                            color,
                            PrimitiveKind::Block,
                            layer,
                            0
                        };
                        outPrim.skew = static_cast<float>(skewDeg);
                        found = true;
                    }
                    return found;
                };

                // Greedily advance along the chain taking the LONGEST valid Block run each step!
                std::size_t pos = 0;
                while (pos + 2 <= m) {
                    std::size_t bestHi = 0;
                    Primitive bestPrim{};
                    std::size_t const maxSpan = std::min<std::size_t>(m - pos, 20);
                    for (std::size_t span = maxSpan; span >= 2; --span) {
                        Primitive cand{};
                        if (fitSubChain(pos, pos + span, cand)) {
                            bestHi = pos + span;
                            bestPrim = cand;
                            break;
                        }
                    }
                    if (bestHi > pos) {
                        out.push_back(bestPrim);
                        for (std::size_t k = pos; k < bestHi; ++k) {
                            consumed[static_cast<std::size_t>(chainTris[k])] = 1;
                        }
                        stats.parallelogramsMerged += static_cast<int>(bestHi - pos) - 1;
                        pos = bestHi;
                    } else {
                        ++pos;
                    }
                }
            }
        }

        // 4a) 3-Triangle & 4-Triangle ribbon strip -> 1 Oriented Block (for remaining triangles near junctions)!
        if (tris.size() >= 3) {
            struct StripCand {
                std::vector<int> tids;
                double cx = 0, cy = 0, w = 0, h = 0, angle = 0;
                double err = 0.0;
            };
            std::vector<StripCand> strips;

            auto tryAddStrip = [&](std::vector<int> const& tids, std::vector<std::uint64_t> const& sharedKeys) {
                std::vector<int> uniq;
                std::vector<Pt> pts;
                double stripArea = 0.0;
                for (int tid : tids) {
                    auto const& tr = tris[static_cast<std::size_t>(tid)];
                    int rv[3] = {tr.a, tr.b, tr.c};
                    stripArea += 0.5 * std::abs(ptCross(
                        merged[static_cast<std::size_t>(tr.b)] - merged[static_cast<std::size_t>(tr.a)],
                        merged[static_cast<std::size_t>(tr.c)] - merged[static_cast<std::size_t>(tr.a)]));
                    for (int vi : rv) {
                        if (std::find(uniq.begin(), uniq.end(), vi) == uniq.end()) {
                            uniq.push_back(vi);
                            pts.push_back(merged[static_cast<std::size_t>(vi)]);
                        }
                    }
                }
                if (pts.size() < 4 || stripArea < 1.0) return;
                auto stripHull = convexHullCCW(pts);
                if (stripHull.size() < 4) return;

                // Evaluate OBB along the 2 longest edges of the strip's convex hull.
                std::vector<std::pair<double, Pt>> axes;
                for (std::size_t ei = 0; ei < stripHull.size(); ++ei) {
                    Pt d = stripHull[(ei + 1) % stripHull.size()] - stripHull[ei];
                    double l = std::hypot(d.x, d.y);
                    if (l > 1.5) axes.push_back({l, d * (1.0 / l)});
                }
                if (axes.empty()) return;
                std::sort(axes.begin(), axes.end(), [](auto const& a, auto const& b) { return a.first > b.first; });

                double const maxSideTol = std::min<double>(options.quadTolerance * 0.88, 0.72);
                std::size_t const tryAxes = std::min<std::size_t>(axes.size(), 2);
                for (std::size_t ai = 0; ai < tryAxes; ++ai) {
                    Pt uHat = axes[ai].second;
                    Pt nHat{-uHat.y, uHat.x};
                    double minU = 1e30, maxU = -1e30, minV = 1e30, maxV = -1e30;
                    for (Pt p : pts) {
                        double pu = ptDot(p, uHat), pv = ptDot(p, nHat);
                        minU = std::min(minU, pu); maxU = std::max(maxU, pu);
                        minV = std::min(minV, pv); maxV = std::max(maxV, pv);
                    }
                    double bw = maxU - minU, bh = maxV - minV;
                    if (bw < 2.0 || bh < 0.7 || bh > bw * 0.72) continue;
                    double obbArea = bw * bh;
                    if (obbArea > stripArea * 1.50) continue;

                    double maxSideDev = 0.0;
                    bool outerFits = true;
                    for (int tid : tids) {
                        auto const& tr = tris[static_cast<std::size_t>(tid)];
                        int tv[3] = {tr.a, tr.b, tr.c};
                        for (int e = 0; e < 3; ++e) {
                            std::uint64_t ek = edgeKey(tv[e], tv[(e + 1) % 3]);
                            if (std::find(sharedKeys.begin(), sharedKeys.end(), ek) != sharedKeys.end()) continue;
                            if (isInternal(tv[e], tv[(e + 1) % 3])) continue;
                            Pt pa = merged[static_cast<std::size_t>(tv[e])];
                            Pt pb = merged[static_cast<std::size_t>(tv[(e + 1) % 3])];
                            double va = ptDot(pa, nHat), vb = ptDot(pb, nHat);
                            double dMin = std::max(std::abs(va - minV), std::abs(vb - minV));
                            double dMax = std::max(std::abs(va - maxV), std::abs(vb - maxV));
                            double dEnd = std::min(
                                std::max(std::abs(ptDot(pa, uHat) - minU), std::abs(ptDot(pb, uHat) - minU)),
                                std::max(std::abs(ptDot(pa, uHat) - maxU), std::abs(ptDot(pb, uHat) - maxU)));
                            double bestD = std::min({dMin, dMax, dEnd});
                            maxSideDev = std::max(maxSideDev, bestD);
                            if (bestD > maxSideTol) { outerFits = false; break; }
                        }
                        if (!outerFits) break;
                    }
                    if (!outerFits) continue;

                    double cu = 0.5 * (minU + maxU), cv = 0.5 * (minV + maxV);
                    Pt c{cu * uHat.x + cv * nHat.x, cu * uHat.y + cv * nHat.y};
                    Pt hu = uHat * (bw * 0.5), hv = nHat * (bh * 0.5);
                    std::vector<Pt> box{c - hu - hv, c + hu - hv, c + hu + hv, c - hu + hv};
                    if (polygonSignedArea(box) < 0.0) std::swap(box[1], box[3]);
                    if (coverGrid && !coverGrid->ribbonBoxFits(box, stripHull, maxSideTol, coverZ, coverId)) {
                        continue;
                    }
                    strips.push_back({tids, c.x, c.y, bw, bh, std::atan2(uHat.y, uHat.x), maxSideDev});
                    break;
                }
            };

            // Collect 4-triangle strips first (bigger savings!), then 3-triangle strips.
            for (int t1 = 0; t1 < static_cast<int>(tris.size()); ++t1) {
                if (consumed[static_cast<std::size_t>(t1)]) continue;
                for (auto const& [t2, k12] : triNbrs[static_cast<std::size_t>(t1)]) {
                    if (t2 <= t1 || consumed[static_cast<std::size_t>(t2)]) continue;
                    for (auto const& [t3, k23] : triNbrs[static_cast<std::size_t>(t2)]) {
                        if (t3 == t1 || consumed[static_cast<std::size_t>(t3)]) continue;
                        tryAddStrip({t1, t2, t3}, {k12, k23});
                        for (auto const& [t4, k34] : triNbrs[static_cast<std::size_t>(t3)]) {
                            if (t4 == t1 || t4 == t2 || consumed[static_cast<std::size_t>(t4)]) continue;
                            tryAddStrip({t1, t2, t3, t4}, {k12, k23, k34});
                        }
                    }
                }
            }
            // Prefer larger strips (4 before 3), then lowest error!
            std::sort(strips.begin(), strips.end(), [](auto const& a, auto const& b) {
                if (a.tids.size() != b.tids.size()) return a.tids.size() > b.tids.size();
                return a.err < b.err;
            });
            for (auto const& s : strips) {
                bool clash = false;
                for (int tid : s.tids) {
                    if (consumed[static_cast<std::size_t>(tid)]) { clash = true; break; }
                }
                if (clash) continue;
                out.push_back({
                    static_cast<float>(s.cx * toGrid),
                    static_cast<float>(s.cy * toGrid),
                    static_cast<float>((s.w + options.internalSeamBleed * 0.4) * toGrid),
                    static_cast<float>((s.h + options.internalSeamBleed * 0.2) * toGrid),
                    static_cast<float>(s.angle * 180.0 / kPiD),
                    color, PrimitiveKind::Block, layer, 0
                });
                for (int tid : s.tids) consumed[static_cast<std::size_t>(tid)] = 1;
                stats.parallelogramsMerged += static_cast<int>(s.tids.size()) - 1;
            }
        }

        struct QuadCand {
            int t1 = 0, t2 = 0;
            Pt p0, p1, p2, p3;
            bool int01 = false, int12 = false, int23 = false, int30 = false;
            double err = 0.0;
            double tol = 0.0;
        };
        std::vector<QuadCand> tightCands;
        std::vector<QuadCand> ribbonCands;
        std::vector<QuadCand> coverCands;
        for (auto const& [k, pair] : adj) {
            if (pair.second < 0) continue;
            if (consumed[static_cast<std::size_t>(pair.first)] || consumed[static_cast<std::size_t>(pair.second)]) continue;
            int u = static_cast<int>(k >> 32);
            int v = static_cast<int>(k & 0xffffffffu);
            int p = oppVert(tris[static_cast<std::size_t>(pair.first)], u, v);
            int q = oppVert(tris[static_cast<std::size_t>(pair.second)], u, v);
            if (p == q) continue;
            Pt pu = merged[static_cast<std::size_t>(u)];
            Pt pp = merged[static_cast<std::size_t>(p)];
            Pt pv = merged[static_cast<std::size_t>(v)];
            Pt pq = merged[static_cast<std::size_t>(q)];
            if (ptCross(pp - pu, pv - pu) < 0.0) {
                std::swap(pp, pq);
                std::swap(p, q);
            }
            if (ptCross(pp - pu, pv - pp) <= 1e-5 || ptCross(pv - pp, pq - pv) <= 1e-5 ||
                ptCross(pq - pv, pu - pq) <= 1e-5 || ptCross(pu - pq, pp - pu) <= 1e-5) {
                continue;
            }
            bool i01 = isInternal(u, p);
            bool i12 = isInternal(p, v);
            bool i23 = isInternal(v, q);
            bool i30 = isInternal(q, u);
            int intSides = (i01 ? 1 : 0) + (i12 ? 1 : 0) + (i23 ? 1 : 0) + (i30 ? 1 : 0);
            double tol = options.quadTolerance * (intSides >= 2 ? 1.45 : (intSides == 1 ? 1.20 : 1.0));
            Pt errVec = (pu + pv) - (pp + pq);
            double err = 0.25 * std::hypot(errVec.x, errVec.y);
            if (err <= tol) {
                tightCands.push_back({pair.first, pair.second, pu, pp, pv, pq, i01, i12, i23, i30, err, tol});
            } else {
                if (coverGrid && coverGrid->valid() && intSides >= 1) {
                    ribbonCands.push_back({pair.first, pair.second, pu, pp, pv, pq, i01, i12, i23, i30, err, tol});
                }
                if (coverGrid && coverGrid->valid() && err <= 22.0) {
                    coverCands.push_back({pair.first, pair.second, pu, pp, pv, pq, i01, i12, i23, i30, err, tol});
                }
            }
        }
        std::sort(tightCands.begin(), tightCands.end(), [](auto const& a, auto const& b) {
            return a.err < b.err;
        });
        for (auto const& c : tightCands) {
            if (consumed[static_cast<std::size_t>(c.t1)] || consumed[static_cast<std::size_t>(c.t2)]) continue;
            if (tryEmitParallelogram(out, c.p0, c.p1, c.p2, c.p3,
                                     c.tol, toGrid, color, layer,
                                     options.internalSeamBleed * 0.4)) {
                consumed[static_cast<std::size_t>(c.t1)] = 1;
                consumed[static_cast<std::size_t>(c.t2)] = 1;
                ++stats.parallelogramsMerged;
            }
        }

        // 4c) Outer-edge-aligned Ribbon Trapezoid -> 1 Block (tucks slanted internal ends into same ribbon!)
        if (coverGrid && coverGrid->valid() && !ribbonCands.empty()) {
            std::sort(ribbonCands.begin(), ribbonCands.end(), [](auto const& a, auto const& b) {
                return a.err < b.err;
            });
            for (auto const& c : ribbonCands) {
                if (consumed[static_cast<std::size_t>(c.t1)] || consumed[static_cast<std::size_t>(c.t2)]) continue;
                // Identify the two opposite outer edges (either 0-1 & 2-3, or 1-2 & 3-0).
                bool pairAOuter = !c.int01 && !c.int23 && (c.int12 || c.int30);
                bool pairBOuter = !c.int12 && !c.int30 && (c.int01 || c.int23);
                if (!pairAOuter && !pairBOuter) continue;
                Pt e1 = pairAOuter ? (c.p1 - c.p0) : (c.p2 - c.p1);
                Pt e2 = pairAOuter ? (c.p2 - c.p3) : (c.p3 - c.p0);
                Pt axis = e1 + e2;
                double aLen = std::hypot(axis.x, axis.y);
                if (aLen < 1.0) continue;
                Pt uHat = axis * (1.0 / aLen);
                Pt nHat{-uHat.y, uHat.x};
                Pt qPts[4] = {c.p0, c.p1, c.p2, c.p3};
                double minU = 1e30, maxU = -1e30, minV = 1e30, maxV = -1e30;
                for (Pt p : qPts) {
                    double pu = ptDot(p, uHat), pv = ptDot(p, nHat);
                    minU = std::min(minU, pu); maxU = std::max(maxU, pu);
                    minV = std::min(minV, pv); maxV = std::max(maxV, pv);
                }
                // Verify both outer edges hug minV / maxV tightly (zero outward bloat across ribbon width!).
                Pt oA0 = pairAOuter ? c.p0 : c.p1;
                Pt oA1 = pairAOuter ? c.p1 : c.p2;
                Pt oB0 = pairAOuter ? c.p3 : c.p0;
                Pt oB1 = pairAOuter ? c.p2 : c.p3;
                double vA0 = ptDot(oA0, nHat), vA1 = ptDot(oA1, nHat);
                double vB0 = ptDot(oB0, nHat), vB1 = ptDot(oB1, nHat);
                double devA = std::min(std::max(std::abs(vA0 - minV), std::abs(vA1 - minV)),
                                       std::max(std::abs(vA0 - maxV), std::abs(vA1 - maxV)));
                double devB = std::min(std::max(std::abs(vB0 - minV), std::abs(vB1 - minV)),
                                       std::max(std::abs(vB0 - maxV), std::abs(vB1 - maxV)));
                if (std::max(devA, devB) > std::min<double>(options.quadTolerance * 0.90, 0.72)) continue;
                double bw = maxU - minU, bh = maxV - minV;
                if (bw < 1.0 || bh < 0.6) continue;
                double cu = 0.5 * (minU + maxU), cv = 0.5 * (minV + maxV);
                Pt ctr{cu * uHat.x + cv * nHat.x, cu * uHat.y + cv * nHat.y};
                Pt hu = uHat * (bw * 0.5), hv = nHat * (bh * 0.5);
                std::vector<Pt> box{ctr - hu - hv, ctr + hu - hv, ctr + hu + hv, ctr - hu + hv};
                if (polygonSignedArea(box) < 0.0) std::swap(box[1], box[3]);
                if (!coverGrid->ribbonBoxFits(box, {c.p0, c.p1, c.p2, c.p3},
                                              std::min<double>(options.quadTolerance * 0.85, 0.68),
                                              coverZ, coverId)) {
                    continue;
                }
                out.push_back({
                    static_cast<float>(ctr.x * toGrid),
                    static_cast<float>(ctr.y * toGrid),
                    static_cast<float>((bw + options.internalSeamBleed * 0.4) * toGrid),
                    static_cast<float>((bh + options.internalSeamBleed * 0.2) * toGrid),
                    static_cast<float>(std::atan2(uHat.y, uHat.x) * 180.0 / kPiD),
                    color, PrimitiveKind::Block, layer, 0
                });
                consumed[static_cast<std::size_t>(c.t1)] = 1;
                consumed[static_cast<std::size_t>(c.t2)] = 1;
                ++stats.parallelogramsMerged;
            }
        }

        // 4d) Lossless cover-verified enclosing Blocks on remaining covered quads!
        if (coverGrid && coverGrid->valid() && !coverCands.empty()) {
            std::sort(coverCands.begin(), coverCands.end(), [](auto const& a, auto const& b) {
                return a.err < b.err;
            });
            for (auto const& c : coverCands) {
                if (consumed[static_cast<std::size_t>(c.t1)] || consumed[static_cast<std::size_t>(c.t2)]) continue;
                if (tryEmitParallelogram(out, c.p0, c.p1, c.p2, c.p3,
                                         c.tol, toGrid, color, layer,
                                         options.internalSeamBleed * 0.4,
                                         coverGrid, coverZ, coverId)) {
                    consumed[static_cast<std::size_t>(c.t1)] = 1;
                    consumed[static_cast<std::size_t>(c.t2)] = 1;
                    ++stats.parallelogramsMerged;
                }
            }
        }
    }

    // 5. Emit remaining triangles with safe internal-diagonal bleed (0 seams, 0 outer bloat).
    for (std::size_t ti = 0; ti < tris.size(); ++ti) {
        if (consumed[ti]) continue;
        auto const& t = tris[ti];
        Pt a = merged[static_cast<std::size_t>(t.a)];
        Pt b = merged[static_cast<std::size_t>(t.b)];
        Pt c = merged[static_cast<std::size_t>(t.c)];
        bleedTriangleInternalEdges(a, b, c,
            isInternal(t.a, t.b), isInternal(t.b, t.c), isInternal(t.c, t.a),
            options.internalSeamBleed);
        emitSingleTriangle(out, a, b, c, toGrid, color, layer);
    }
}

// -------------------- Visibility & Perceptual Impact Pruning --------------------

void pruneLowImpactPrimitives(
    ImportPlan& plan,
    SvgOptions const& options,
    SvgStats& stats,
    bool hasBackdrop
) {
    if (!options.pruneMicroObjects || plan.staticObjects.size() <= 1) return;

    // Sort by layer first (matching GD & renderPlanFrame stable_sort order).
    std::stable_sort(plan.staticObjects.begin(), plan.staticObjects.end(),
        [](Primitive const& a, Primitive const& b) { return a.layer < b.layer; });

    int const scale = std::clamp(
        static_cast<int>(std::ceil(1024.0 / std::max(plan.width, plan.height))), 2, 4);
    int const outW = plan.width * scale;
    int const outH = plan.height * scale;
    double const pxToArea1000 =
        (1000.0 / static_cast<double>(std::max(outW, outH))) *
        (1000.0 / static_cast<double>(std::max(outW, outH)));

    std::size_t const palN = plan.palette.size();
    std::vector<OkLab> paletteLab(palN);
    for (std::size_t i = 0; i < palN; ++i) {
        paletteLab[i] = rgbToOkLab(plan.palette[i]);
    }
    std::vector<float> labDist(palN * palN, 0.f);
    for (std::size_t a = 0; a < palN; ++a) {
        for (std::size_t b = a + 1; b < palN; ++b) {
            float d = oklabDistance(paletteLab[a], paletteLab[b]);
            labDist[a * palN + b] = d;
            labDist[b * palN + a] = d;
        }
    }

    struct StripeEntry {
        std::uint32_t objIdx = 0;
        std::uint32_t visPx = 0;
        double colorImpact = 0.0;
        float maxDelta = 0.f;
        std::uint8_t hasEmptyUnder = 0;
    };

    int const stripeH = 32;
    int const numStripes = (outH + stripeH - 1) / stripeH;

    for (int pass = 0; pass < 3; ++pass) {
        std::size_t const n = plan.staticObjects.size();
        std::vector<ShapeXform> xforms(n);
        std::vector<int> yMin(n, 1);
        std::vector<int> yMax(n, 0);
        std::vector<std::int16_t> colors(n, 0);

        auto buildXform = [&](std::size_t i) {
            auto const& obj = plan.staticObjects[i];
            xforms[i] = xformOf(obj);
            colors[i] = static_cast<std::int16_t>(obj.color);
            auto const box = xformBox(xforms[i], plan.width, plan.height);
            if (box[2] >= box[0] && box[3] >= box[1]) {
                yMin[i] = box[1] * scale;
                yMax[i] = (box[3] + 1) * scale - 1;
            }
        };
        if (n >= 128) {
            parallelFor(n, buildXform);
        } else {
            for (std::size_t i = 0; i < n; ++i) buildXform(i);
        }

        std::vector<std::vector<StripeEntry>> stripeResults(static_cast<std::size_t>(numStripes));
        parallelFor(static_cast<std::size_t>(numStripes), [&](std::size_t s) {
            int const rowBegin = static_cast<int>(s) * stripeH;
            int const rowEnd = std::min(outH, rowBegin + stripeH);
            int const rows = rowEnd - rowBegin;
            std::size_t const stripePixels = static_cast<std::size_t>(rows) * outW;

            std::vector<std::int32_t> owner(stripePixels, -1);
            std::vector<std::int16_t> underColor(stripePixels, -1);
            std::vector<std::int16_t> topColor(stripePixels, -1);

            for (std::size_t i = 0; i < n; ++i) {
                if (yMax[i] < rowBegin || yMin[i] >= rowEnd) continue;
                std::int16_t const col = colors[i];
                std::int32_t const id = static_cast<std::int32_t>(i);
                forEachSampleInRows(
                    xforms[i], plan.width, plan.height, scale, rowBegin, rowEnd,
                    [&](int x, int y) {
                        std::size_t const idx = static_cast<std::size_t>(y - rowBegin) * outW + x;
                        if (owner[idx] != id) {
                            underColor[idx] = topColor[idx];
                            owner[idx] = id;
                            topColor[idx] = col;
                        }
                        return false;
                    });
            }

            std::vector<int> slot(n, -1);
            auto& out = stripeResults[s];
            out.reserve(std::min<std::size_t>(n, 512));
            for (std::size_t idx = 0; idx < stripePixels; ++idx) {
                int const oi = owner[idx];
                if (oi < 0) continue;
                int sl = slot[static_cast<std::size_t>(oi)];
                if (sl < 0) {
                    sl = static_cast<int>(out.size());
                    slot[static_cast<std::size_t>(oi)] = sl;
                    out.push_back({static_cast<std::uint32_t>(oi), 0, 0.0, 0.f, 0});
                }
                auto& e = out[static_cast<std::size_t>(sl)];
                ++e.visPx;
                int const tc = topColor[idx];
                int const uc = underColor[idx];
                float delta = 1.f;
                if (uc < 0) {
                    e.hasEmptyUnder = 1;
                } else if (uc < static_cast<int>(palN) && tc >= 0 && tc < static_cast<int>(palN)) {
                    delta = labDist[static_cast<std::size_t>(tc) * palN + static_cast<std::size_t>(uc)];
                }
                e.colorImpact += delta;
                if (delta > e.maxDelta) e.maxDelta = delta;
            }
        });

        std::vector<std::size_t> visPx(n, 0);
        std::vector<double> colorImpact(n, 0.0);
        std::vector<float> maxDelta(n, 0.f);
        std::vector<std::uint8_t> hasEmptyUnder(n, 0);
        for (auto const& entries : stripeResults) {
            for (auto const& e : entries) {
                std::size_t const oi = e.objIdx;
                visPx[oi] += e.visPx;
                colorImpact[oi] += e.colorImpact;
                if (e.maxDelta > maxDelta[oi]) maxDelta[oi] = e.maxDelta;
                if (e.hasEmptyUnder) hasEmptyUnder[oi] = 1;
            }
        }

        std::vector<Primitive> kept;
        kept.reserve(n);
        int droppedThisPass = 0;
        for (std::size_t i = 0; i < n; ++i) {
            // Never drop the canvas backdrop block at index 0.
            if (hasBackdrop && i == 0 && plan.staticObjects[i].layer == 0) {
                kept.push_back(plan.staticObjects[i]);
                continue;
            }
            if (visPx[i] == 0) {
                ++stats.occludedPruned;
                ++droppedThisPass;
                continue;
            }
            // Same color as everything underneath -> 100% redundant!
            if (maxDelta[i] < 0.015f) {
                ++stats.occludedPruned;
                ++droppedThisPass;
                continue;
            }
            double area1000 = static_cast<double>(visPx[i]) * pxToArea1000;
            double impact1000 = colorImpact[i] * pxToArea1000;
            // Prune microscopic specks/slivers that add negligible visual signal,
            // while NEVER opening transparent holes (!hasEmptyUnder) and preserving sharp highlights!
            bool subPixelSpeck = (area1000 < 2.5 && impact1000 < 1.1);
            bool lowImpactMicro = (area1000 < options.minVisibleArea &&
                                   impact1000 < options.minVisualImpact &&
                                   !(area1000 >= 3.8 && maxDelta[i] > 0.35f));
            bool coveredSubPixelSliver = (!hasEmptyUnder[i] &&
                                          area1000 < std::max(11.0, static_cast<double>(options.minVisibleArea) * 1.7) &&
                                          impact1000 < std::max(2.6, static_cast<double>(options.minVisualImpact) * 1.3) &&
                                          !(area1000 >= 4.2 && maxDelta[i] > 0.42f));
            if (subPixelSpeck || lowImpactMicro || coveredSubPixelSliver) {
                ++stats.microPruned;
                ++droppedThisPass;
                continue;
            }
            kept.push_back(plan.staticObjects[i]);
        }
        plan.staticObjects = std::move(kept);
        if (droppedThisPass == 0) break;
    }
}

void finalizePlanCounts(ImportPlan& plan) {
    plan.blockObjects = 0;
    plan.strokeObjects = 0;
    plan.circleObjects = 0;
    plan.triangleObjects = 0;
    plan.glowObjects = 0;
    plan.stampObjects = 0;
    for (auto const& o : plan.staticObjects) {
        switch (o.kind) {
            case PrimitiveKind::Block: ++plan.blockObjects; break;
            case PrimitiveKind::Stroke: ++plan.strokeObjects; break;
            case PrimitiveKind::Circle: ++plan.circleObjects; break;
            case PrimitiveKind::Triangle:
            case PrimitiveKind::WideTriangle: ++plan.triangleObjects; break;
            case PrimitiveKind::Glow: ++plan.glowObjects; break;
            case PrimitiveKind::Stamp: ++plan.stampObjects; break;
        }
    }
    plan.visualObjects = plan.staticObjects.size();
    plan.triggerObjects = 0;
    plan.totalObjects = plan.visualObjects;
}

void mergeEmitStats(SvgStats& dst, SvgStats const& src) {
    dst.ellipsesFitted += src.ellipsesFitted;
    dst.parallelogramsMerged += src.parallelogramsMerged;
    dst.coverVerticesCollapsed += src.coverVerticesCollapsed;
    dst.triPairsCollapsed += src.triPairsCollapsed;
}

// -------------------- Prepared Document (Invariant Across Budget Retries) --------------------

struct PreparedSvgDoc {
    double normW = 0.0;
    double normH = 0.0;
    std::vector<ParsedShape> shapes;
    std::vector<double> shapeAreas;
    QuantizedPalette quant;
    std::vector<OkLab> paletteLab;
    bool isPlanar = false;
    std::vector<std::unordered_map<int, double>> adjLen;
    LayerCoverGrid layeredCoverGrid;
};

PreparedSvgDoc prepareSvgDoc(ParsedSvgDoc const& rawDoc, SvgOptions const& options) {
    PreparedSvgDoc prep;
    if (rawDoc.shapes.empty() || rawDoc.vbW <= 0.0 || rawDoc.vbH <= 0.0) return prep;

    // Normalize viewBox into [0, normW] x [0, normH] with max(normW, normH) == 1000.0.
    double const maxVb = std::max(rawDoc.vbW, rawDoc.vbH);
    double const toNorm = 1000.0 / maxVb;
    prep.normW = rawDoc.vbW * toNorm;
    prep.normH = rawDoc.vbH * toNorm;

    prep.shapes = rawDoc.shapes;
    prep.shapeAreas.assign(prep.shapes.size(), 0.0);

    auto normalizeOne = [&](std::size_t i) {
        auto& sh = prep.shapes[i];
        for (auto& r : sh.rings) {
            for (auto& s : r) {
                s.p0 = {(s.p0.x - rawDoc.vbX) * toNorm, (s.p0.y - rawDoc.vbY) * toNorm};
                s.p1 = {(s.p1.x - rawDoc.vbX) * toNorm, (s.p1.y - rawDoc.vbY) * toNorm};
                if (s.cubic) {
                    s.c1 = {(s.c1.x - rawDoc.vbX) * toNorm, (s.c1.y - rawDoc.vbY) * toNorm};
                    s.c2 = {(s.c2.x - rawDoc.vbX) * toNorm, (s.c2.y - rawDoc.vbY) * toNorm};
                }
            }
        }
        if (sh.isDirectEllipse) {
            sh.ellipseCenter = {(sh.ellipseCenter.x - rawDoc.vbX) * toNorm,
                                (sh.ellipseCenter.y - rawDoc.vbY) * toNorm};
            sh.ellipseRx *= toNorm;
            sh.ellipseRy *= toNorm;
        }
        if (sh.isDirectRect) {
            for (auto& c : sh.rectCorners) {
                c = {(c.x - rawDoc.vbX) * toNorm, (c.y - rawDoc.vbY) * toNorm};
            }
        }
        prep.shapeAreas[i] = normalizeShapeWinding(sh);
    };
    if (prep.shapes.size() >= 16) {
        parallelFor(prep.shapes.size(), normalizeOne);
    } else {
        for (std::size_t i = 0; i < prep.shapes.size(); ++i) normalizeOne(i);
    }

    // Quantize palette in OkLab.
    prep.quant = quantizeShapeColors(prep.shapes, prep.shapeAreas, options);
    prep.paletteLab.resize(prep.quant.palette.size());
    for (std::size_t i = 0; i < prep.quant.palette.size(); ++i) {
        prep.paletteLab[i] = rgbToOkLab(prep.quant.palette[i]);
    }

    // Detect whether the SVG is a planar partition (shared reversed half-edges).
    std::unordered_map<UndirSegKey, std::vector<int>, UndirSegHash> segOwners;
    std::size_t totalSegs = 0;
    for (int si = 0; si < static_cast<int>(prep.shapes.size()); ++si) {
        for (auto const& r : prep.shapes[static_cast<std::size_t>(si)].rings) {
            for (auto const& s : r) {
                if (segChordLen(s) < 1e-4 && !s.cubic) continue;
                segOwners[undirKeyOf(s)].push_back(si);
                ++totalSegs;
            }
        }
    }
    std::size_t matchedHalfEdges = 0;
    std::size_t tripleHalfEdges = 0;
    for (auto const& [k, owners] : segOwners) {
        if (owners.size() == 2 && owners[0] != owners[1]) matchedHalfEdges += 2;
        else if (owners.size() > 2) tripleHalfEdges += owners.size();
    }
    prep.isPlanar = totalSegs > 0 &&
        (matchedHalfEdges * 2 >= totalSegs) &&
        (tripleHalfEdges * 20 <= totalSegs);

    if (prep.isPlanar) {
        prep.adjLen.resize(prep.shapes.size());
        for (auto const& [k, owners] : segOwners) {
            if (owners.size() == 2 && owners[0] != owners[1]) {
                double dx = static_cast<double>(k.a.p1.x - k.a.p0.x) * 0.01;
                double dy = static_cast<double>(k.a.p1.y - k.a.p0.y) * 0.01;
                double len = std::max(std::hypot(dx, dy), 0.1);
                prep.adjLen[static_cast<std::size_t>(owners[0])][owners[1]] += len;
                prep.adjLen[static_cast<std::size_t>(owners[1])][owners[0]] += len;
            }
        }
    } else if (prep.shapes.size() >= 2) {
        // Pre-build Z-layer cover grid from explicit SVG paint order once (reused across budget retries).
        prep.layeredCoverGrid.init(prep.normW, prep.normH, 1024, -1);
        struct LayeredPolys {
            std::vector<std::vector<Pt>> outers;
            std::vector<std::vector<Pt>> holes;
        };
        std::vector<LayeredPolys> shapePolys(prep.shapes.size());
        parallelFor(prep.shapes.size(), [&](std::size_t i) {
            auto const& sh = prep.shapes[i];
            auto& out = shapePolys[i];
            if (sh.isDirectEllipse) {
                std::vector<Pt> ring;
                ring.reserve(32);
                for (int k = 0; k < 32; ++k) {
                    double a = 2.0 * kPiD * k / 32.0;
                    ring.push_back({sh.ellipseCenter.x + sh.ellipseRx * std::cos(a),
                                    sh.ellipseCenter.y + sh.ellipseRy * std::sin(a)});
                }
                out.outers.push_back(std::move(ring));
            } else if (sh.isDirectRect) {
                out.outers.emplace_back(sh.rectCorners.begin(), sh.rectCorners.end());
            } else {
                for (auto const& r : sh.rings) {
                    auto flat = flattenRing(r, 1.5);
                    double a = polygonSignedArea(flat);
                    if (std::abs(a) < 0.5) continue;
                    if (a > 0.0) out.outers.push_back(std::move(flat));
                    else out.holes.push_back(std::move(flat));
                }
            }
        });
        std::vector<LayerCoverGrid::RasterItem> items;
        items.reserve(prep.shapes.size());
        for (std::size_t i = 0; i < prep.shapes.size(); ++i) {
            std::int16_t const z = static_cast<std::int16_t>(std::min<std::size_t>(i + 1, 950));
            std::int32_t const id = static_cast<std::int32_t>(i);
            auto const& lp = shapePolys[i];
            auto const* hPtr = lp.holes.empty() ? nullptr : &lp.holes;
            for (auto const& o : lp.outers) {
                auto item = prep.layeredCoverGrid.makeRasterItem(&o, hPtr, z, id);
                if (item.y0 <= item.y1) items.push_back(item);
            }
        }
        prep.layeredCoverGrid.rasterizeBatchParallel(items);
    }

    return prep;
}

// -------------------- Core Build (Single Pass) --------------------

SvgBuildResult buildSvgPlanOnce(PreparedSvgDoc const& prep, SvgOptions const& options) {
    auto const tStart = std::chrono::steady_clock::now();
    SvgBuildResult res;
    if (prep.shapes.empty() || prep.normW <= 0.0 || prep.normH <= 0.0) {
        res.error = "SVG vacio o sin geometria rellena";
        return res;
    }

    double const normW = prep.normW;
    double const normH = prep.normH;
    int const dim = std::clamp(options.dimension, 16, 1024);
    int const planW = std::max(1, static_cast<int>(std::lround(normW * dim / 1000.0)));
    int const planH = std::max(1, static_cast<int>(std::lround(normH * dim / 1000.0)));
    double const toGrid = static_cast<double>(planW) / normW;

    auto const& shapes = prep.shapes;
    auto const& shapeAreas = prep.shapeAreas;
    auto const& quant = prep.quant;
    auto const& paletteLab = prep.paletteLab;
    bool const isPlanar = prep.isPlanar;

    res.stats.parsedShapes = static_cast<int>(shapes.size());
    res.stats.uniqueInputColors = quant.uniqueInputColors;
    res.stats.planarPartition = isPlanar;

    ImportPlan& plan = res.plan;
    plan.width = planW;
    plan.height = planH;
    plan.sourceFrames = 1;
    plan.sampledFrames = 1;
    plan.requestedDimension = dim;
    plan.actualDimension = dim;
    plan.sourceColorCount = quant.uniqueInputColors;
    plan.mode = ImportMode::Paint;
    plan.strategy = isPlanar ? "svg-planar" : "svg-layered";
    plan.palette = quant.palette;
    plan.frames.resize(1);
    plan.frames.front().delayMs = 100;

    bool hasBackdrop = false;

    if (isPlanar) {
        // ---- Planar Partition Path: Absorb Micro-Regions -> Fuse Same Color -> Eliminate Covered Holes ----
        std::vector<int> regionColor = quant.shapeColor;
        auto const& adjLen = prep.adjLen;

        // Absorb microscopic/low-impact regions into their dominant neighbor before fusion.
        for (int pass = 0; pass < 3; ++pass) {
            bool any = false;
            for (std::size_t i = 0; i < shapes.size(); ++i) {
                double area = shapeAreas[i];
                if (area >= options.minRegionArea) continue;
                auto const& nbrs = adjLen[i];
                if (nbrs.empty()) continue;
                float maxDelta = 0.f;
                int bestNbrColor = -1;
                double bestScore = -1.0;
                for (auto const& [nj, len] : nbrs) {
                    int nc = regionColor[static_cast<std::size_t>(nj)];
                    float d = oklabDistance(paletteLab[static_cast<std::size_t>(regionColor[i])],
                                            paletteLab[static_cast<std::size_t>(nc)]);
                    maxDelta = std::max(maxDelta, d);
                    double score = len / (0.15 + static_cast<double>(d));
                    if (score > bestScore) {
                        bestScore = score;
                        bestNbrColor = nc;
                    }
                }
                bool absorb = (area < 3.0) ||
                    (area * static_cast<double>(maxDelta) < static_cast<double>(options.minVisualImpact) * 2.2);
                if (absorb && bestNbrColor >= 0 && bestNbrColor != regionColor[i]) {
                    regionColor[i] = bestNbrColor;
                    ++res.stats.absorbedMicroRegions;
                    any = true;
                }
            }
            if (!any) break;
        }

        // Connected components of same-color regions (or 1 per shape if mergeSameColorRegions is false).
        struct UnifiedRegion {
            int color = 0;
            Ring outer;
            std::vector<Ring> holes;
            std::vector<Pt> outerFlat;
            BBox outerBox;
            Pt sample{};
            double outerArea = 0.0;
            bool touchesCanvasBorder = false;
            int depth = 1;
            bool skipped = false;
        };

        std::vector<UnifiedRegion> regions;
        std::vector<std::uint8_t> visited(shapes.size(), 0);

        for (std::size_t seed = 0; seed < shapes.size(); ++seed) {
            if (visited[seed]) continue;
            std::vector<int> comp;
            if (options.mergeSameColorRegions) {
                std::queue<int> q;
                q.push(static_cast<int>(seed));
                visited[seed] = 1;
                int const c = regionColor[seed];
                while (!q.empty()) {
                    int u = q.front();
                    q.pop();
                    comp.push_back(u);
                    for (auto const& [v, len] : adjLen[static_cast<std::size_t>(u)]) {
                        if (!visited[static_cast<std::size_t>(v)] && regionColor[static_cast<std::size_t>(v)] == c) {
                            visited[static_cast<std::size_t>(v)] = 1;
                            q.push(v);
                        }
                    }
                }
            } else {
                visited[seed] = 1;
                comp.push_back(static_cast<int>(seed));
            }

            // Collect directed half-edges and cancel exact opposite pairs inside the component!
            std::unordered_map<DirSegKey, std::vector<Seg>, DirSegHash> pool;
            for (int si : comp) {
                for (auto const& r : shapes[static_cast<std::size_t>(si)].rings) {
                    for (auto const& s : r) {
                        DirSegKey dk = dirKeyOf(s);
                        DirSegKey rk = reverseDirKey(dk);
                        auto it = pool.find(rk);
                        if (it != pool.end() && !it->second.empty()) {
                            it->second.pop_back();
                            if (it->second.empty()) pool.erase(it);
                        } else {
                            pool[dk].push_back(s);
                        }
                    }
                }
            }
            std::vector<Seg> boundary;
            for (auto& [k, vec] : pool) {
                for (auto& s : vec) boundary.push_back(std::move(s));
            }
            auto loops = chainHalfEdges(boundary);

            // Classify chained loops into outer loops (signed area > 0) and hole loops (< 0).
            std::vector<UnifiedRegion> compOuters;
            std::vector<std::pair<Ring, std::vector<Pt>>> compHoles;
            for (auto& lp : loops) {
                auto flat = flattenRing(lp, 1.5);
                double area = polygonSignedArea(flat);
                if (std::abs(area) < 0.5) continue;
                if (area > 0.0) {
                    UnifiedRegion ur;
                    ur.color = regionColor[seed];
                    ur.outer = std::move(lp);
                    ur.outerFlat = std::move(flat);
                    ur.outerBox = polyBBox(ur.outerFlat);
                    ur.sample = interiorSamplePoint(ur.outerFlat);
                    ur.outerArea = area;
                    for (auto const& p : ur.outerFlat) {
                        if (p.x <= 1.0 || p.y <= 1.0 || p.x >= normW - 1.0 || p.y >= normH - 1.0) {
                            ur.touchesCanvasBorder = true;
                            break;
                        }
                    }
                    compOuters.push_back(std::move(ur));
                } else {
                    compHoles.push_back({std::move(lp), std::move(flat)});
                }
            }
            // Assign each hole to the smallest enclosing outer loop of the same component.
            for (auto& [hRing, hFlat] : compHoles) {
                Pt hSample = interiorSamplePoint(hFlat);
                int bestO = -1;
                double bestA = 1e30;
                for (int oi = 0; oi < static_cast<int>(compOuters.size()); ++oi) {
                    auto const& o = compOuters[static_cast<std::size_t>(oi)];
                    if (o.outerArea < bestA && o.outerBox.contains(hSample) &&
                        pointInPolygon(hSample, o.outerFlat)) {
                        bestA = o.outerArea;
                        bestO = oi;
                    }
                }
                if (bestO >= 0) {
                    compOuters[static_cast<std::size_t>(bestO)].holes.push_back(std::move(hRing));
                }
            }
            for (auto& ur : compOuters) regions.push_back(std::move(ur));
        }
        res.stats.mergedRegions = static_cast<int>(regions.size());

        // Find dominant canvas-border color (for Backdrop or AutoBorder removal).
        std::vector<double> borderColorLen(quant.palette.size(), 0.0);
        for (auto const& r : regions) {
            if (!r.touchesCanvasBorder) continue;
            for (auto const& s : r.outer) {
                bool onEdge = (s.p0.x <= 1.0 && s.p1.x <= 1.0) ||
                              (s.p0.y <= 1.0 && s.p1.y <= 1.0) ||
                              (s.p0.x >= normW - 1.0 && s.p1.x >= normW - 1.0) ||
                              (s.p0.y >= normH - 1.0 && s.p1.y >= normH - 1.0);
                if (onEdge) borderColorLen[static_cast<std::size_t>(r.color)] += segChordLen(s);
            }
        }
        int backdropColor = -1;
        double maxBorder = 0.0;
        for (int ci = 0; ci < static_cast<int>(borderColorLen.size()); ++ci) {
            if (borderColorLen[static_cast<std::size_t>(ci)] > maxBorder) {
                maxBorder = borderColorLen[static_cast<std::size_t>(ci)];
                backdropColor = ci;
            }
        }
        if (maxBorder < (normW + normH) * 0.25) backdropColor = -1;

        // If AutoBorder: mark border-touching backdropColor regions as removed.
        std::vector<std::uint8_t> removedByAutoBorder(regions.size(), 0);
        if (options.background == BackgroundMode::AutoBorder && backdropColor >= 0) {
            for (std::size_t i = 0; i < regions.size(); ++i) {
                if (regions[i].color == backdropColor && regions[i].touchesCanvasBorder) {
                    removedByAutoBorder[i] = 1;
                    regions[i].skipped = true;
                }
            }
        }

        // Containment DAG & Covered-Hole Elimination!
        std::vector<std::vector<int>> dag(regions.size());
        std::vector<int> indeg(regions.size(), 0);

        if (options.eliminateCoveredHoles) {
            struct HoleElimOut {
                std::vector<std::uint8_t> keepMask;
                std::vector<int> children;
                int eliminated = 0;
            };
            std::vector<HoleElimOut> elimOuts(regions.size());
            parallelFor(regions.size(), [&](std::size_t i) {
                if (regions[i].skipped || regions[i].holes.empty()) return;
                auto& eo = elimOuts[i];
                eo.keepMask.assign(regions[i].holes.size(), 0);
                for (std::size_t hi = 0; hi < regions[i].holes.size(); ++hi) {
                    auto const& h = regions[i].holes[hi];
                    auto hFlat = flattenRing(h, 1.5);
                    double hArea = std::abs(polygonSignedArea(hFlat));
                    BBox hBox = polyBBox(hFlat);
                    bool containsRemoved = false;
                    std::vector<int> children;
                    for (std::size_t j = 0; j < regions.size(); ++j) {
                        if (j == i || regions[j].outerArea > hArea + 25.0) continue;
                        if (!hBox.contains(regions[j].sample)) continue;
                        if (pointInPolygon(regions[j].sample, hFlat)) {
                            if (removedByAutoBorder[j]) containsRemoved = true;
                            else children.push_back(static_cast<int>(j));
                        }
                    }
                    if (!containsRemoved && !children.empty()) {
                        for (int ch : children) eo.children.push_back(ch);
                        ++eo.eliminated;
                    } else {
                        eo.keepMask[hi] = 1;
                    }
                }
            });
            for (std::size_t i = 0; i < regions.size(); ++i) {
                if (regions[i].skipped || regions[i].holes.empty()) continue;
                auto const& eo = elimOuts[i];
                for (int ch : eo.children) {
                    dag[i].push_back(ch);
                    ++indeg[static_cast<std::size_t>(ch)];
                }
                res.stats.eliminatedHoles += eo.eliminated;
                if (eo.eliminated > 0) {
                    std::vector<Ring> keptHoles;
                    keptHoles.reserve(regions[i].holes.size() - static_cast<std::size_t>(eo.eliminated));
                    for (std::size_t hi = 0; hi < regions[i].holes.size(); ++hi) {
                        if (eo.keepMask[hi]) keptHoles.push_back(std::move(regions[i].holes[hi]));
                    }
                    regions[i].holes = std::move(keptHoles);
                }
            }
        }

        // Also detect stacked-over regions (e.g. OpenVector --layout stacked, where a smaller
        // region like an eye highlight or constellation sits directly inside a larger solid
        // region without a cutout hole): add DAG edge parent -> child so Z-order is exact!
        struct FlatHoleCache {
            std::vector<Pt> pts;
            BBox box;
        };
        std::vector<std::vector<FlatHoleCache>> keptHoleFlats(regions.size());
        parallelFor(regions.size(), [&](std::size_t i) {
            if (regions[i].skipped || regions[i].holes.empty()) return;
            keptHoleFlats[i].reserve(regions[i].holes.size());
            for (auto const& kh : regions[i].holes) {
                auto flat = flattenRing(kh, 1.5);
                BBox box = polyBBox(flat);
                keptHoleFlats[i].push_back({std::move(flat), box});
            }
        });

        std::vector<int> bestParents(regions.size(), -1);
        parallelFor(regions.size(), [&](std::size_t j) {
            if (regions[j].skipped || regions[j].touchesCanvasBorder) return;
            int bestParent = -1;
            double bestArea = 1e30;
            Pt const sample = regions[j].sample;
            double const jArea = regions[j].outerArea;
            for (std::size_t i = 0; i < regions.size(); ++i) {
                if (i == j || regions[i].skipped) continue;
                if (regions[i].outerArea <= jArea + 25.0) continue;
                if (regions[i].outerArea >= bestArea) continue;
                if (!regions[i].outerBox.contains(sample)) continue;
                if (!pointInPolygon(sample, regions[i].outerFlat)) continue;
                bool inKeptHole = false;
                for (auto const& kh : keptHoleFlats[i]) {
                    if (kh.box.contains(sample) && pointInPolygon(sample, kh.pts)) {
                        inKeptHole = true;
                        break;
                    }
                }
                if (inKeptHole) continue;
                bestArea = regions[i].outerArea;
                bestParent = static_cast<int>(i);
            }
            bestParents[j] = bestParent;
        });
        for (std::size_t j = 0; j < regions.size(); ++j) {
            int const bestParent = bestParents[j];
            if (bestParent >= 0) {
                auto& kids = dag[static_cast<std::size_t>(bestParent)];
                if (std::find(kids.begin(), kids.end(), static_cast<int>(j)) == kids.end()) {
                    kids.push_back(static_cast<int>(j));
                    ++indeg[j];
                }
            }
        }

        // Topological sort to compute Z-order depths.
        std::queue<int> topo;
        for (int i = 0; i < static_cast<int>(regions.size()); ++i) {
            if (indeg[static_cast<std::size_t>(i)] == 0) topo.push(i);
        }
        while (!topo.empty()) {
            int u = topo.front();
            topo.pop();
            for (int v : dag[static_cast<std::size_t>(u)]) {
                regions[static_cast<std::size_t>(v)].depth =
                    std::max(regions[static_cast<std::size_t>(v)].depth,
                             regions[static_cast<std::size_t>(u)].depth + 1);
                if (--indeg[static_cast<std::size_t>(v)] == 0) topo.push(v);
            }
        }

        // Emit single full-canvas backdrop Block at layer 0 when Keep + backdropColor found!
        // Only skip depth-1 backdrop-colored regions that actually touch the canvas border
        // (never skip interior white eye highlights / constellations / details!).
        if (options.background == BackgroundMode::Keep && options.useCanvasBackdrop && backdropColor >= 0) {
            hasBackdrop = true;
            plan.staticObjects.push_back({
                static_cast<float>(normW * 0.5 * toGrid),
                static_cast<float>(normH * 0.5 * toGrid),
                static_cast<float>(normW * toGrid),
                static_cast<float>(normH * toGrid),
                0.f,
                static_cast<std::uint16_t>(backdropColor),
                PrimitiveKind::Block,
                0,
                0
            });
            for (auto& r : regions) {
                if (!r.skipped && r.depth == 1 && r.touchesCanvasBorder && r.color == backdropColor) {
                    r.skipped = true;
                    ++res.stats.skippedBackdropRegions;
                }
            }
        }

        // Canonical Shared-Chain Flattening & Simplification across all surviving loops!
        // 1) Collect all unique undirected segments from surviving loops to find junction vertices.
        std::unordered_map<PtKey, std::unordered_set<UndirSegKey, UndirSegHash>, PtKeyHash> vertEdges;
        auto registerRing = [&](Ring const& r) {
            for (auto const& s : r) {
                UndirSegKey uk = undirKeyOf(s);
                vertEdges[quantPt(s.p0)].insert(uk);
                vertEdges[quantPt(s.p1)].insert(uk);
            }
        };
        for (auto const& r : regions) {
            if (r.skipped) continue;
            registerRing(r.outer);
            for (auto const& h : r.holes) registerRing(h);
        }

        auto isJunction = [&](PtKey pk) -> bool {
            auto it = vertEdges.find(pk);
            return it != vertEdges.end() && it->second.size() != 2;
        };
        auto isSharpCorner = [](Seg const& prev, Seg const& next) -> bool {
            double a1 = segEndAngle(prev);
            double a2 = segStartAngle(next);
            double diff = std::abs(std::fmod(a2 - a1 + 3.0 * kPiD, 2.0 * kPiD) - kPiD);
            return diff > (62.0 * kPiD / 180.0);
        };

        // Map each undirected segment to the two region colors on either side so we can
        // apply CONTRAST-ADAPTIVE simplification: crisp tight curves on high-contrast
        // edges (lineart/eyes/silhouette), aggressive savings on soft low-contrast shading!
        std::unordered_map<UndirSegKey, std::vector<int>, UndirSegHash> segColors;
        auto registerSegColors = [&](Ring const& r, int col) {
            for (auto const& s : r) {
                auto& vec = segColors[undirKeyOf(s)];
                if (std::find(vec.begin(), vec.end(), col) == vec.end()) vec.push_back(col);
            }
        };
        for (auto const& r : regions) {
            registerSegColors(r.outer, r.color);
            for (auto const& h : r.holes) registerSegColors(h, r.color);
        }

        auto chainContrast = [&](std::vector<Seg> const& chain, int selfColor) -> float {
            float maxD = 0.f;
            for (auto const& s : chain) {
                auto it = segColors.find(undirKeyOf(s));
                if (it == segColors.end()) continue;
                for (int c : it->second) {
                    if (c == selfColor) continue;
                    maxD = std::max(maxD, oklabDistance(
                        paletteLab[static_cast<std::size_t>(selfColor)],
                        paletteLab[static_cast<std::size_t>(c)]));
                }
            }
            // Border against backdrop or single-sided boundary -> treat as medium-high contrast.
            if (maxD <= 0.f && backdropColor >= 0 && selfColor != backdropColor) {
                maxD = oklabDistance(
                    paletteLab[static_cast<std::size_t>(selfColor)],
                    paletteLab[static_cast<std::size_t>(backdropColor)]);
            }
            return maxD > 0.f ? maxD : 0.35f;
        };

        auto contrastTols = [&](float delta) -> std::pair<double, double> {
            // High contrast (delta >= 0.32): crisp tight tolerance (0.80x).
            // Low contrast shading (delta <= 0.10): 2.1x tolerance & 3.2x area threshold!
            double t = std::clamp((0.32 - static_cast<double>(delta)) / 0.22, 0.0, 1.0);
            double devScale = 0.82 + 1.35 * t;
            double areaScale = 0.75 + 2.45 * t;
            return {options.simplifyTolerance * devScale, options.minTriangleArea * areaScale};
        };

        // Chain cache keyed by canonical sequence of DirSegKeys.
        struct ChainKey {
            std::vector<DirSegKey> segs;
            bool operator==(ChainKey const&) const = default;
        };
        struct ChainKeyHash {
            std::size_t operator()(ChainKey const& ck) const noexcept {
                DirSegHash sh;
                std::size_t h = ck.segs.size();
                for (auto const& s : ck.segs) {
                    h ^= sh(s) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
                }
                return h;
            }
        };

        // Assign real stacked Z-layers across regions so broad base fills sit below
        // medium shadows and thin lineart/highlights sit on top! This unlocks lossless
        // under-layer polygon simplification & 2->1 primitive collapsing at 0.00 dB loss.
        std::vector<double> regionThickness(regions.size(), 0.0);
        for (std::size_t i = 0; i < regions.size(); ++i) {
            double perim = 0.0;
            auto const& flat = regions[i].outerFlat;
            for (std::size_t k = 0; k < flat.size(); ++k) {
                perim += ptDist(flat[k], flat[(k + 1) % flat.size()]);
            }
            regionThickness[i] = regions[i].outerArea / std::max(perim, 1.0);
        }

        // Build region-level adjacency from surviving boundary segments.
        std::unordered_map<UndirSegKey, std::vector<int>, UndirSegHash> regSegOwners;
        auto addRegSegs = [&](Ring const& ring, int regIdx) {
            for (auto const& s : ring) {
                auto& vec = regSegOwners[undirKeyOf(s)];
                if (std::find(vec.begin(), vec.end(), regIdx) == vec.end()) vec.push_back(regIdx);
            }
        };
        for (int i = 0; i < static_cast<int>(regions.size()); ++i) {
            if (regions[static_cast<std::size_t>(i)].skipped) continue;
            addRegSegs(regions[static_cast<std::size_t>(i)].outer, i);
            for (auto const& h : regions[static_cast<std::size_t>(i)].holes) addRegSegs(h, i);
        }
        std::vector<std::unordered_set<int>> regAdj(regions.size());
        for (auto const& [k, owners] : regSegOwners) {
            for (std::size_t a = 0; a < owners.size(); ++a) {
                for (std::size_t b = a + 1; b < owners.size(); ++b) {
                    regAdj[static_cast<std::size_t>(owners[a])].insert(owners[b]);
                    regAdj[static_cast<std::size_t>(owners[b])].insert(owners[a]);
                }
            }
        }
        // Reverse DAG (parents of each region) so hole containment is always strictly honored.
        std::vector<std::vector<int>> dagParents(regions.size());
        for (int u = 0; u < static_cast<int>(dag.size()); ++u) {
            for (int v : dag[static_cast<std::size_t>(u)]) {
                dagParents[static_cast<std::size_t>(v)].push_back(u);
            }
        }

        // Emit order: hole-containment depth ASC, then effective thickness DESC (broad fills bottom, thin lineart top).
        std::vector<std::size_t> emitOrder(regions.size());
        std::iota(emitOrder.begin(), emitOrder.end(), 0);
        std::stable_sort(emitOrder.begin(), emitOrder.end(), [&](std::size_t a, std::size_t b) {
            if (regions[a].depth != regions[b].depth) return regions[a].depth < regions[b].depth;
            if (std::abs(regionThickness[a] - regionThickness[b]) > 0.25) {
                return regionThickness[a] > regionThickness[b];
            }
            return regions[a].outerArea > regions[b].outerArea;
        });

        // Pack compact Z-layers (1..900) where every touching/contained pair gets strictly ordered Z.
        std::vector<std::int16_t> zRank(regions.size(), 0);
        for (std::size_t idx : emitOrder) {
            if (regions[idx].skipped) continue;
            int z = 1;
            for (int p : dagParents[idx]) {
                if (zRank[static_cast<std::size_t>(p)] > 0) {
                    z = std::max(z, static_cast<int>(zRank[static_cast<std::size_t>(p)]) + 1);
                }
            }
            for (int nb : regAdj[idx]) {
                if (zRank[static_cast<std::size_t>(nb)] > 0) {
                    z = std::max(z, static_cast<int>(zRank[static_cast<std::size_t>(nb)]) + 1);
                }
            }
            zRank[idx] = static_cast<std::int16_t>(std::min(z, 900));
        }

        // Pre-simplify every surviving region's boundary FIRST (flattening & VW simplification
        // of all unique canonical chains and closed loops runs in parallel across all CPU cores!).
        struct UniqueChainJob {
            std::vector<Seg> chain;
            bool reversed = false;
            double devTol = 0.0;
            double areaTol = 0.0;
            double gentle = 0.65;
        };
        struct ClosedLoopJob {
            Ring const* ring = nullptr;
            double devTol = 0.0;
            double areaTol = 0.0;
            std::size_t regIdx = 0;
            int holeIdx = -1;
        };
        struct SplitRingPlan {
            std::size_t regIdx = 0;
            int holeIdx = -1;
            std::vector<std::pair<std::size_t, bool>> parts;
        };

        std::unordered_map<ChainKey, std::size_t, ChainKeyHash> chainIndex;
        std::vector<UniqueChainJob> uniqueChains;
        std::vector<ClosedLoopJob> closedJobs;
        std::vector<SplitRingPlan> ringPlans;

        auto planRing = [&](Ring const& ring, int selfColor, std::size_t regIdx, int holeIdx) {
            std::size_t const m = ring.size();
            if (m == 0) return;
            std::vector<std::size_t> splits;
            for (std::size_t i = 0; i < m; ++i) {
                PtKey pk = quantPt(ring[i].p0);
                if (isJunction(pk) || isSharpCorner(ring[(i + m - 1) % m], ring[i])) {
                    splits.push_back(i);
                }
            }
            if (splits.empty()) {
                float delta = chainContrast(ring, selfColor);
                auto [devTol, areaTol] = contrastTols(delta);
                closedJobs.push_back({&ring, devTol, areaTol, regIdx, holeIdx});
                return;
            }
            SplitRingPlan rp;
            rp.regIdx = regIdx;
            rp.holeIdx = holeIdx;
            std::size_t const sc = splits.size();
            rp.parts.reserve(sc);
            for (std::size_t si = 0; si < sc; ++si) {
                std::size_t from = splits[si];
                std::size_t to = splits[(si + 1) % sc];
                std::vector<Seg> chain;
                std::size_t cur = from;
                do {
                    chain.push_back(ring[cur]);
                    cur = (cur + 1) % m;
                } while (cur != to);

                ChainKey fwd, rev;
                fwd.segs.reserve(chain.size());
                rev.segs.reserve(chain.size());
                for (auto const& s : chain) fwd.segs.push_back(dirKeyOf(s));
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    rev.segs.push_back(reverseDirKey(dirKeyOf(*it)));
                }
                bool const reversed = (rev.segs < fwd.segs);
                ChainKey const& canon = reversed ? rev : fwd;

                auto it = chainIndex.find(canon);
                std::size_t ci = 0;
                if (it == chainIndex.end()) {
                    float delta = chainContrast(chain, selfColor);
                    auto [devTol, areaTol] = contrastTols(delta);
                    double gentle = delta >= 0.30f ? 0.58 : (delta >= 0.16f ? 0.68 : 0.78);
                    ci = uniqueChains.size();
                    chainIndex.emplace(canon, ci);
                    uniqueChains.push_back({std::move(chain), reversed, devTol, areaTol, gentle});
                } else {
                    ci = it->second;
                }
                rp.parts.push_back({ci, reversed});
            }
            ringPlans.push_back(std::move(rp));
        };

        std::vector<std::vector<Pt>> simplifiedOuters(regions.size());
        std::vector<std::vector<std::vector<Pt>>> simplifiedHoles(regions.size());
        for (std::size_t idx : emitOrder) {
            auto const& r = regions[idx];
            if (r.skipped || zRank[idx] <= 0) continue;
            planRing(r.outer, r.color, idx, -1);
            simplifiedHoles[idx].resize(r.holes.size());
            for (std::size_t hi = 0; hi < r.holes.size(); ++hi) {
                planRing(r.holes[hi], r.color, idx, static_cast<int>(hi));
            }
        }

        std::vector<std::vector<Pt>> chainOutputs(uniqueChains.size());
        double const curveTolSq = options.curveTolerance * options.curveTolerance;
        parallelFor(uniqueChains.size(), [&](std::size_t ci) {
            auto const& job = uniqueChains[ci];
            std::vector<Pt> flat;
            flat.reserve(job.chain.size() * 2 + 1);
            if (!job.reversed) {
                flat.push_back(job.chain.front().p0);
                for (auto const& s : job.chain) {
                    if (!s.cubic) flat.push_back(s.p1);
                    else flattenCubicRec(s.p0, s.c1, s.c2, s.p1, curveTolSq, 0, flat);
                }
            } else {
                flat.push_back(job.chain.back().p1);
                for (auto it = job.chain.rbegin(); it != job.chain.rend(); ++it) {
                    Seg rs = reverseSeg(*it);
                    if (!rs.cubic) flat.push_back(rs.p1);
                    else flattenCubicRec(rs.p0, rs.c1, rs.c2, rs.p1, curveTolSq, 0, flat);
                }
            }
            chainOutputs[ci] = simplifyOpenChainVW(flat, job.devTol, job.areaTol, job.gentle);
        });

        std::vector<std::vector<Pt>> closedOutputs(closedJobs.size());
        parallelFor(closedJobs.size(), [&](std::size_t ji) {
            auto const& job = closedJobs[ji];
            auto flat = flattenRing(*job.ring, options.curveTolerance);
            closedOutputs[ji] = simplifyClosedLoopVW(flat, job.devTol, job.areaTol);
        });

        for (std::size_t ji = 0; ji < closedJobs.size(); ++ji) {
            auto const& job = closedJobs[ji];
            if (job.holeIdx < 0) simplifiedOuters[job.regIdx] = std::move(closedOutputs[ji]);
            else simplifiedHoles[job.regIdx][static_cast<std::size_t>(job.holeIdx)] = std::move(closedOutputs[ji]);
        }
        for (auto const& rp : ringPlans) {
            std::vector<Pt> result;
            for (auto const& [ci, reversed] : rp.parts) {
                auto const& pts = chainOutputs[ci];
                if (!reversed) {
                    for (std::size_t k = 0; k + 1 < pts.size(); ++k) result.push_back(pts[k]);
                } else {
                    for (std::size_t k = pts.size(); k-- > 1;) result.push_back(pts[k]);
                }
            }
            if (rp.holeIdx < 0) simplifiedOuters[rp.regIdx] = std::move(result);
            else simplifiedHoles[rp.regIdx][static_cast<std::size_t>(rp.holeIdx)] = std::move(result);
        }

        LayerCoverGrid coverGrid;
        coverGrid.init(normW, normH, 1024, hasBackdrop ? 0 : -1);
        std::vector<LayerCoverGrid::RasterItem> rasterItems;
        rasterItems.reserve(emitOrder.size());
        for (std::size_t idx : emitOrder) {
            auto const& r = regions[idx];
            if (r.skipped || zRank[idx] <= 0) continue;
            auto const* hPtr = simplifiedHoles[idx].empty() ? nullptr : &simplifiedHoles[idx];
            auto item = coverGrid.makeRasterItem(
                &simplifiedOuters[idx], hPtr, zRank[idx], static_cast<std::int32_t>(idx));
            if (item.y0 <= item.y1) rasterItems.push_back(item);
        }
        coverGrid.rasterizeBatchParallel(rasterItems);

        std::vector<std::vector<Primitive>> perRegionPrims(emitOrder.size());
        std::vector<SvgStats> perRegionStats(emitOrder.size());
        parallelFor(emitOrder.size(), [&](std::size_t k) {
            std::size_t const idx = emitOrder[k];
            auto const& r = regions[idx];
            if (r.skipped) return;
            auto outerHighRes = r.holes.empty() && options.detectEllipses
                ? flattenRing(r.outer, 0.45) : std::vector<Pt>{};
            emitPolygonPrimitives(
                perRegionPrims[k], simplifiedOuters[idx], outerHighRes, simplifiedHoles[idx], toGrid,
                static_cast<std::uint16_t>(r.color),
                zRank[idx],
                options, perRegionStats[k],
                &coverGrid, zRank[idx], static_cast<std::int32_t>(idx)
            );
        });
        for (std::size_t k = 0; k < emitOrder.size(); ++k) {
            auto& vec = perRegionPrims[k];
            plan.staticObjects.insert(
                plan.staticObjects.end(),
                std::make_move_iterator(vec.begin()),
                std::make_move_iterator(vec.end()));
            mergeEmitStats(res.stats, perRegionStats[k]);
        }
    } else {
        // ---- General Layered SVG Path (Figma / Illustrator / Icons / OpenVector Stacked) ----
        res.stats.mergedRegions = static_cast<int>(shapes.size());
        LayerCoverGrid const* cgPtr = prep.layeredCoverGrid.valid() ? &prep.layeredCoverGrid : nullptr;

        std::vector<std::vector<Primitive>> perShapePrims(shapes.size());
        std::vector<SvgStats> perShapeStats(shapes.size());
        parallelFor(shapes.size(), [&](std::size_t i) {
            auto const& sh = shapes[i];
            auto& out = perShapePrims[i];
            auto& st = perShapeStats[i];
            std::uint16_t const col = static_cast<std::uint16_t>(quant.shapeColor[i]);
            std::int16_t const layer = static_cast<std::int16_t>(std::min<std::size_t>(i, 950));
            std::int16_t const coverZ = static_cast<std::int16_t>(std::min<std::size_t>(i + 1, 950));
            std::int32_t const coverId = static_cast<std::int32_t>(i);
            if (sh.isDirectEllipse) {
                out.push_back({
                    static_cast<float>(sh.ellipseCenter.x * toGrid),
                    static_cast<float>(sh.ellipseCenter.y * toGrid),
                    static_cast<float>(sh.ellipseRx * 2.0 * toGrid),
                    static_cast<float>(sh.ellipseRy * 2.0 * toGrid),
                    static_cast<float>(sh.ellipseAngleRad * 180.0 / kPiD),
                    col, PrimitiveKind::Circle, layer, 0
                });
                ++st.ellipsesFitted;
                return;
            }
            if (sh.isDirectRect && tryEmitParallelogram(
                    out, sh.rectCorners[0], sh.rectCorners[1],
                    sh.rectCorners[2], sh.rectCorners[3], options.quadTolerance,
                    toGrid, col, layer)) {
                ++st.parallelogramsMerged;
                return;
            }

            // Classify subpaths into outer rings & holes.
            std::vector<std::vector<Pt>> outers, outersHi, holes;
            for (auto const& r : sh.rings) {
                auto flat = flattenRing(r, options.curveTolerance);
                auto simp = simplifyClosedLoopVW(flat, options.simplifyTolerance, options.minTriangleArea);
                double a = polygonSignedArea(simp);
                if (std::abs(a) < 0.5) continue;
                if (a > 0.0) {
                    outers.push_back(std::move(simp));
                    outersHi.push_back(std::move(flat));
                } else {
                    holes.push_back(std::move(simp));
                }
            }
            if (outers.empty()) return;
            if (outers.size() == 1) {
                emitPolygonPrimitives(out, outers[0], outersHi[0], holes,
                                      toGrid, col, layer, options, st, cgPtr, coverZ, coverId);
            } else {
                std::vector<BBox> boxes;
                std::vector<double> oAreas;
                boxes.reserve(outers.size());
                oAreas.reserve(outers.size());
                for (auto const& o : outers) {
                    boxes.push_back(polyBBox(o));
                    oAreas.push_back(std::abs(polygonSignedArea(o)));
                }
                std::vector<std::vector<std::vector<Pt>>> groupedHoles(outers.size());
                for (auto& h : holes) {
                    Pt hs = interiorSamplePoint(h);
                    int bestO = -1;
                    double bestA = 1e30;
                    for (int oi = 0; oi < static_cast<int>(outers.size()); ++oi) {
                        if (oAreas[static_cast<std::size_t>(oi)] < bestA &&
                            boxes[static_cast<std::size_t>(oi)].contains(hs) &&
                            pointInPolygon(hs, outers[static_cast<std::size_t>(oi)])) {
                            bestA = oAreas[static_cast<std::size_t>(oi)];
                            bestO = oi;
                        }
                    }
                    if (bestO >= 0) groupedHoles[static_cast<std::size_t>(bestO)].push_back(std::move(h));
                }
                for (std::size_t oi = 0; oi < outers.size(); ++oi) {
                    emitPolygonPrimitives(out, outers[oi], outersHi[oi], groupedHoles[oi],
                                          toGrid, col, layer, options, st, cgPtr, coverZ, coverId);
                }
            }
        });
        std::size_t totalOut = plan.staticObjects.size();
        for (auto const& vec : perShapePrims) totalOut += vec.size();
        plan.staticObjects.reserve(totalOut);
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            auto& vec = perShapePrims[i];
            plan.staticObjects.insert(
                plan.staticObjects.end(),
                std::make_move_iterator(vec.begin()),
                std::make_move_iterator(vec.end()));
            mergeEmitStats(res.stats, perShapeStats[i]);
        }
    }

    // Post-conversion visibility & perceptual impact pruning (0 occluded, 0 useless micro-objects!).
    pruneLowImpactPrimitives(plan, options, res.stats, hasBackdrop);
    finalizePlanCounts(plan);

    auto const tEnd = std::chrono::steady_clock::now();
    res.stats.elapsedMs =
        std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    return res;
}

} // namespace

bool isSvgFile(std::filesystem::path const& path) {
    auto ext = path.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".svg";
}

bool isSvgBytes(std::uint8_t const* data, std::size_t size) {
    if (!data || size < 10) return false;
    std::size_t const probeLen = std::min<std::size_t>(size, 2048);
    std::string_view sv(reinterpret_cast<char const*>(data), probeLen);
    if (sv.find('\0') != std::string_view::npos) return false;
    if (sv.starts_with("\xEF\xBB\xBF")) sv.remove_prefix(3);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    if (sv.empty() || sv.front() != '<') return false;
    std::string lower = toLower(sv);
    return lower.find("<svg") != std::string::npos;
}

bool probeSvgDimensions(std::string_view svgContent, int& width, int& height) {
    width = 0;
    height = 0;

    // Fast path: read viewBox / width / height from the <svg> start tag without parsing every path.
    std::size_t pos = 0;
    double vbW = 0.0, vbH = 0.0;
    bool foundSvgTag = false;
    while (pos < svgContent.size()) {
        std::size_t lt = svgContent.find('<', pos);
        if (lt == std::string_view::npos) break;
        pos = lt + 1;
        if (pos >= svgContent.size()) break;
        if (svgContent.substr(lt, 4) == "<!--") {
            std::size_t endC = svgContent.find("-->", lt + 4);
            pos = (endC == std::string_view::npos) ? svgContent.size() : endC + 3;
            continue;
        }
        if (svgContent[pos] == '?' || svgContent[pos] == '!') {
            std::size_t gt = svgContent.find('>', pos);
            pos = (gt == std::string_view::npos) ? svgContent.size() : gt + 1;
            continue;
        }
        if (svgContent[pos] == '/') {
            std::size_t gt = svgContent.find('>', pos);
            pos = (gt == std::string_view::npos) ? svgContent.size() : gt + 1;
            continue;
        }
        std::size_t nameStart = pos;
        while (pos < svgContent.size() &&
               !std::isspace(static_cast<unsigned char>(svgContent[pos])) &&
               svgContent[pos] != '>' && svgContent[pos] != '/') {
            ++pos;
        }
        std::string tag = toLower(svgContent.substr(nameStart, pos - nameStart));
        if (auto colon = tag.rfind(':'); colon != std::string::npos) {
            tag = tag.substr(colon + 1);
        }
        if (tag != "svg") {
            std::size_t gt = svgContent.find('>', pos);
            pos = (gt == std::string_view::npos) ? svgContent.size() : gt + 1;
            continue;
        }
        foundSvgTag = true;
        std::unordered_map<std::string, std::string> attrs;
        while (pos < svgContent.size()) {
            while (pos < svgContent.size() && std::isspace(static_cast<unsigned char>(svgContent[pos]))) ++pos;
            if (pos >= svgContent.size() || svgContent[pos] == '>') {
                if (pos < svgContent.size()) ++pos;
                break;
            }
            if (svgContent[pos] == '/' && pos + 1 < svgContent.size() && svgContent[pos + 1] == '>') {
                pos += 2;
                break;
            }
            std::size_t kStart = pos;
            while (pos < svgContent.size() &&
                   !std::isspace(static_cast<unsigned char>(svgContent[pos])) &&
                   svgContent[pos] != '=' && svgContent[pos] != '>' && svgContent[pos] != '/') {
                ++pos;
            }
            std::string key = toLower(svgContent.substr(kStart, pos - kStart));
            while (pos < svgContent.size() && std::isspace(static_cast<unsigned char>(svgContent[pos]))) ++pos;
            std::string val;
            if (pos < svgContent.size() && svgContent[pos] == '=') {
                ++pos;
                while (pos < svgContent.size() && std::isspace(static_cast<unsigned char>(svgContent[pos]))) ++pos;
                if (pos < svgContent.size() && (svgContent[pos] == '"' || svgContent[pos] == '\'')) {
                    char q = svgContent[pos++];
                    std::size_t vStart = pos;
                    while (pos < svgContent.size() && svgContent[pos] != q) ++pos;
                    val = std::string(svgContent.substr(vStart, pos - vStart));
                    if (pos < svgContent.size()) ++pos;
                } else {
                    std::size_t vStart = pos;
                    while (pos < svgContent.size() &&
                           !std::isspace(static_cast<unsigned char>(svgContent[pos])) &&
                           svgContent[pos] != '>') ++pos;
                    val = std::string(svgContent.substr(vStart, pos - vStart));
                }
            }
            if (!key.empty()) attrs[key] = std::move(val);
        }
        if (auto vb = attrs.find("viewbox"); vb != attrs.end()) {
            auto nums = parseNumbers(vb->second);
            if (nums.size() >= 4) {
                vbW = nums[2];
                vbH = nums[3];
            }
        }
        if (vbW <= 0.0 && attrs.count("width")) vbW = parseLength(attrs["width"]);
        if (vbH <= 0.0 && attrs.count("height")) vbH = parseLength(attrs["height"]);
        break;
    }

    if (foundSvgTag && vbW > 0.0 && vbH > 0.0) {
        // Confirm at least one drawable shape tag exists without parsing path coordinates.
        bool hasShapeTag = false;
        while (pos < svgContent.size()) {
            std::size_t lt = svgContent.find('<', pos);
            if (lt == std::string_view::npos || lt + 2 >= svgContent.size()) break;
            pos = lt + 1;
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(svgContent[pos])));
            if (c == 'p' || c == 'r' || c == 'c' || c == 'e') {
                std::size_t end = pos;
                while (end < svgContent.size() && end - pos < 16 &&
                       !std::isspace(static_cast<unsigned char>(svgContent[end])) &&
                       svgContent[end] != '>' && svgContent[end] != '/') {
                    ++end;
                }
                std::string t = toLower(svgContent.substr(pos, end - pos));
                if (t == "path" || t == "rect" || t == "circle" || t == "ellipse" || t == "polygon") {
                    hasShapeTag = true;
                    break;
                }
            }
        }
        if (hasShapeTag) {
            width = std::clamp(static_cast<int>(std::lround(vbW)), 1, 8192);
            height = std::clamp(static_cast<int>(std::lround(vbH)), 1, 8192);
            return true;
        }
    }

    auto doc = parseSvgDocument(svgContent);
    if (doc.shapes.empty() || doc.vbW <= 0.0 || doc.vbH <= 0.0) return false;
    width = std::clamp(static_cast<int>(std::lround(doc.vbW)), 1, 8192);
    height = std::clamp(static_cast<int>(std::lround(doc.vbH)), 1, 8192);
    return true;
}

SvgOptions svgOptionsFromImportOptions(Options const& options) {
    SvgOptions svg;
    svg.dimension = options.autoResolution
        ? 256
        : std::clamp(options.maxDimension, 16, kMaxImportDimension);
    svg.maxColors = std::clamp(options.maxColors, 1, 64);
    svg.objectBudget = std::clamp(options.objectBudget, 100, 50000);
    svg.background = options.background;
    if (options.optimize) {
        svg.minColorDistance = 0.024f;
        svg.curveTolerance = 0.85f;
        svg.simplifyTolerance = 1.35f;
        svg.minTriangleArea = 14.0f;
        svg.quadTolerance = 0.65f;
        svg.ellipseTolerance = 1.40f;
        svg.minRegionArea = 24.0f;
        svg.minVisibleArea = 6.0f;
        svg.minVisualImpact = 2.0f;
    } else {
        svg.minColorDistance = 0.018f;
        svg.curveTolerance = 0.45f;
        svg.simplifyTolerance = 0.70f;
        svg.minTriangleArea = 6.0f;
        svg.quadTolerance = 0.45f;
        svg.ellipseTolerance = 1.15f;
        svg.minRegionArea = 14.0f;
        svg.minVisibleArea = 4.0f;
        svg.minVisualImpact = 1.3f;
    }
    return svg;
}

SvgBuildResult buildSvgPlan(std::string_view svgContent, SvgOptions const& options) {
    auto const tTotalStart = std::chrono::steady_clock::now();
    auto doc = parseSvgDocument(svgContent);
    if (doc.shapes.empty() || doc.vbW <= 0.0 || doc.vbH <= 0.0) {
        SvgBuildResult err;
        err.error = "No se encontro geometria rellena valida en el SVG";
        return err;
    }

    SvgOptions cur = options;
    auto prep = prepareSvgDoc(doc, cur);
    auto best = buildSvgPlanOnce(prep, cur);
    if (!best || cur.objectBudget <= 0 ||
        static_cast<int>(best.plan.totalObjects) <= cur.objectBudget) {
        best.stats.elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tTotalStart).count();
        return best;
    }

    // Automatically relax tolerances cleanly to fit objectBudget without collapsing thin lineart!
    for (int attempt = 0; attempt < 8; ++attempt) {
        double ratio = static_cast<double>(best.plan.totalObjects) /
                       static_cast<double>(std::max(cur.objectBudget, 100));
        float factor = static_cast<float>(std::clamp(std::sqrt(ratio) * 1.08, 1.12, 1.65));
        // Cap simplifyTolerance so thin 4px outlines never collapse on themselves;
        // absorb more low-contrast micro-regions & increase area threshold instead!
        cur.curveTolerance = std::min(cur.curveTolerance * factor, 2.2f);
        cur.simplifyTolerance = std::min(cur.simplifyTolerance * factor, 2.4f);
        cur.minTriangleArea *= factor * 1.45f;
        cur.quadTolerance = std::min(cur.quadTolerance * std::min(factor, 1.2f), 0.95f);
        cur.minRegionArea *= factor * 1.45f;
        cur.minVisibleArea *= std::min(factor, 1.35f);
        cur.minVisualImpact *= factor * 1.25f;
        best = buildSvgPlanOnce(prep, cur);
        if (!best || static_cast<int>(best.plan.totalObjects) <= cur.objectBudget) break;
    }
    best.stats.elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tTotalStart).count();
    return best;
}

SvgBuildResult buildAnimatedSvgPlan(
    std::vector<SvgFrame> const& svgFrames,
    SvgOptions const& options,
    bool loop,
    int sourceFrameCount,
    SvgAnimProgressCallback progress,
    std::atomic<bool> const* cancelled
) {
    auto const tTotalStart = std::chrono::steady_clock::now();
    SvgBuildResult res;
    if (svgFrames.empty()) {
        res.error = "No hay frames SVG para convertir.";
        return res;
    }

    if (svgFrames.size() == 1) {
        res = buildSvgPlan(svgFrames.front().svgContent, options);
        if (res) {
            if (res.plan.frames.empty()) res.plan.frames.resize(1);
            res.plan.frames.front().delayMs = std::max(svgFrames.front().delayMs, 10);
            res.plan.sourceFrames = std::max(sourceFrameCount, 1);
            res.plan.sampledFrames = 1;
        }
        if (progress) progress(1, 1);
        return res;
    }

    std::size_t const frameCount = svgFrames.size();
    std::vector<std::string const*> uniqueSvgs;
    std::vector<int> uniqueRefCount;
    std::vector<std::size_t> frameToUnique(frameCount, 0);
    std::unordered_map<std::string_view, std::size_t> dedupMap;
    dedupMap.reserve(frameCount);

    for (std::size_t f = 0; f < frameCount; ++f) {
        std::string_view sv(svgFrames[f].svgContent);
        auto it = dedupMap.find(sv);
        if (it != dedupMap.end()) {
            frameToUnique[f] = it->second;
            ++uniqueRefCount[it->second];
        } else {
            std::size_t const u = uniqueSvgs.size();
            dedupMap.emplace(sv, u);
            uniqueSvgs.push_back(&svgFrames[f].svgContent);
            uniqueRefCount.push_back(1);
            frameToUnique[f] = u;
        }
    }

    std::size_t const uniqueCount = uniqueSvgs.size();
    SvgOptions frameOpts = options;
    if (options.objectBudget > 0) {
        int const estTriggers = static_cast<int>(frameCount * 3 + uniqueCount + 2);
        int const visualBudget = std::max(
            static_cast<int>(uniqueCount) * 16,
            options.objectBudget - estTriggers);
        frameOpts.objectBudget = std::max(16, visualBudget / static_cast<int>(uniqueCount));
    }

    std::vector<SvgBuildResult> perUnique(uniqueCount);
    std::atomic<int> doneFrames{0};
    std::atomic<bool> aborted{false};
    std::mutex progressMutex;
    if (progress) progress(0, static_cast<int>(frameCount));

    parallelFor(uniqueCount, [&](std::size_t u) {
        if (aborted.load(std::memory_order_relaxed)) return;
        if (cancelled && cancelled->load(std::memory_order_relaxed)) {
            aborted.store(true, std::memory_order_relaxed);
            return;
        }
        perUnique[u] = buildSvgPlan(*uniqueSvgs[u], frameOpts);
        if (cancelled && cancelled->load(std::memory_order_relaxed)) {
            aborted.store(true, std::memory_order_relaxed);
            return;
        }
        int const done = doneFrames.fetch_add(
            uniqueRefCount[u], std::memory_order_relaxed) + uniqueRefCount[u];
        if (progress) {
            std::lock_guard lock(progressMutex);
            progress(std::min(done, static_cast<int>(frameCount)), static_cast<int>(frameCount));
        }
    });

    if (aborted.load(std::memory_order_relaxed) ||
        (cancelled && cancelled->load(std::memory_order_relaxed))) {
        res.error = "Conversion SVG cancelada.";
        return res;
    }

    for (std::size_t u = 0; u < uniqueCount; ++u) {
        if (!perUnique[u]) {
            res.error = perUnique[u].error.empty()
                ? "Un frame SVG no contiene geometria valida."
                : perUnique[u].error;
            return res;
        }
    }

    int const planW = perUnique.front().plan.width;
    int const planH = perUnique.front().plan.height;
    int const dim = std::clamp(options.dimension, 16, 1024);

    // Normalize any frame whose viewBox rounded to a slightly different grid size.
    for (std::size_t u = 1; u < uniqueCount; ++u) {
        auto& fp = perUnique[u].plan;
        if (fp.width == planW && fp.height == planH) continue;
        if (fp.width <= 0 || fp.height <= 0) continue;
        float const sx = static_cast<float>(planW) / static_cast<float>(fp.width);
        float const sy = static_cast<float>(planH) / static_cast<float>(fp.height);
        for (auto& obj : fp.staticObjects) {
            obj.x *= sx;
            obj.y *= sy;
            obj.width *= sx;
            obj.height *= sy;
        }
    }

    // Unify palettes across all unique frames in perceptual OkLab space (<= maxColors).
    struct PaletteCluster {
        OkLab lab{};
        double weight = 0.0;
    };
    std::unordered_map<std::uint32_t, double> rgbWeights;
    for (std::size_t u = 0; u < uniqueCount; ++u) {
        auto const& fp = perUnique[u].plan;
        std::vector<double> localWeights(fp.palette.size(), 0.0);
        for (auto const& obj : fp.staticObjects) {
            if (obj.color < localWeights.size()) {
                localWeights[obj.color] += std::max(
                    0.25, static_cast<double>(obj.width) * static_cast<double>(obj.height));
            }
        }
        double const refMul = static_cast<double>(uniqueRefCount[u]);
        for (std::size_t ci = 0; ci < fp.palette.size(); ++ci) {
            if (localWeights[ci] <= 0.0) continue;
            auto const c = fp.palette[ci];
            std::uint32_t const key =
                (static_cast<std::uint32_t>(c.r) << 16) |
                (static_cast<std::uint32_t>(c.g) << 8) |
                static_cast<std::uint32_t>(c.b);
            rgbWeights[key] += localWeights[ci] * refMul;
        }
    }

    int const uniqueInputColors = static_cast<int>(rgbWeights.size());
    std::vector<PaletteCluster> clusters;
    clusters.reserve(rgbWeights.size());
    for (auto const& [key, w] : rgbWeights) {
        Color const c{
            static_cast<std::uint8_t>((key >> 16) & 0xFF),
            static_cast<std::uint8_t>((key >> 8) & 0xFF),
            static_cast<std::uint8_t>(key & 0xFF)
        };
        clusters.push_back({rgbToOkLab(c), w});
    }
    if (clusters.empty()) {
        res.error = "Los frames SVG no generaron objetos visibles.";
        return res;
    }

    // Sort by weight descending so dominant colors act as stable cluster seeds.
    std::sort(clusters.begin(), clusters.end(), [](auto const& a, auto const& b) {
        return a.weight > b.weight;
    });

    auto mergeClusterPair = [](std::vector<PaletteCluster>& list, std::size_t a, std::size_t b) {
        double const total = list[a].weight + list[b].weight;
        double const ratio = total > 0.0 ? (list[b].weight / total) : 0.5;
        list[a].lab = {
            static_cast<float>(list[a].lab.L * (1.0 - ratio) + list[b].lab.L * ratio),
            static_cast<float>(list[a].lab.a * (1.0 - ratio) + list[b].lab.a * ratio),
            static_cast<float>(list[a].lab.b * (1.0 - ratio) + list[b].lab.b * ratio)
        };
        list[a].weight = total;
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(b));
    };

    // Fast greedy merge for near-identical colors below minColorDistance.
    std::vector<PaletteCluster> compact;
    compact.reserve(clusters.size());
    float const mergeThresh = std::max(options.minColorDistance, 0.015f);
    for (auto const& cl : clusters) {
        int bestIdx = -1;
        float bestDist = mergeThresh;
        for (std::size_t j = 0; j < compact.size(); ++j) {
            float const d = oklabDistance(cl.lab, compact[j].lab);
            if (d < bestDist) {
                bestDist = d;
                bestIdx = static_cast<int>(j);
            }
        }
        if (bestIdx >= 0) {
            auto& dst = compact[static_cast<std::size_t>(bestIdx)];
            double const total = dst.weight + cl.weight;
            double const ratio = total > 0.0 ? (cl.weight / total) : 0.5;
            dst.lab = {
                static_cast<float>(dst.lab.L * (1.0 - ratio) + cl.lab.L * ratio),
                static_cast<float>(dst.lab.a * (1.0 - ratio) + cl.lab.a * ratio),
                static_cast<float>(dst.lab.b * (1.0 - ratio) + cl.lab.b * ratio)
            };
            dst.weight = total;
        } else {
            compact.push_back(cl);
        }
    }
    clusters = std::move(compact);

    std::size_t const maxColors = static_cast<std::size_t>(std::clamp(options.maxColors, 1, 64));
    while (clusters.size() > maxColors) {
        std::size_t bestA = 0;
        std::size_t bestB = 1;
        float bestDist = std::numeric_limits<float>::max();
        for (std::size_t a = 0; a < clusters.size(); ++a) {
            for (std::size_t b = a + 1; b < clusters.size(); ++b) {
                float const d = oklabDistance(clusters[a].lab, clusters[b].lab);
                if (d < bestDist) {
                    bestDist = d;
                    bestA = a;
                    bestB = b;
                }
            }
        }
        mergeClusterPair(clusters, bestA, bestB);
    }

    ImportPlan& plan = res.plan;
    plan.width = planW;
    plan.height = planH;
    plan.sourceFrames = std::max(sourceFrameCount, static_cast<int>(frameCount));
    plan.sampledFrames = static_cast<int>(frameCount);
    plan.requestedDimension = dim;
    plan.actualDimension = dim;
    plan.sourceColorCount = uniqueInputColors;
    plan.mode = ImportMode::Paint;
    plan.strategy = "svg-anim";
    plan.palette.reserve(clusters.size());
    std::vector<OkLab> globalLabs;
    globalLabs.reserve(clusters.size());
    for (auto const& cl : clusters) {
        plan.palette.push_back(oklabToRgb(cl.lab));
        globalLabs.push_back(rgbToOkLab(plan.palette.back()));
    }

    // Remap every unique frame's primitives to the unified global palette.
    for (std::size_t u = 0; u < uniqueCount; ++u) {
        auto& fp = perUnique[u].plan;
        std::vector<std::uint16_t> remap(fp.palette.size(), 0);
        for (std::size_t ci = 0; ci < fp.palette.size(); ++ci) {
            OkLab const lab = rgbToOkLab(fp.palette[ci]);
            std::size_t best = 0;
            float bestD = std::numeric_limits<float>::max();
            for (std::size_t gi = 0; gi < globalLabs.size(); ++gi) {
                float const d = oklabDistance(lab, globalLabs[gi]);
                if (d < bestD) {
                    bestD = d;
                    best = gi;
                }
            }
            remap[ci] = static_cast<std::uint16_t>(best);
        }
        for (auto& obj : fp.staticObjects) {
            if (obj.color < remap.size()) obj.color = remap[obj.color];
        }
        res.stats.parsedShapes += perUnique[u].stats.parsedShapes;
        res.stats.mergedRegions += perUnique[u].stats.mergedRegions;
        res.stats.occludedPruned += perUnique[u].stats.occludedPruned;
        res.stats.microPruned += perUnique[u].stats.microPruned;
        mergeEmitStats(res.stats, perUnique[u].stats);
    }
    res.stats.uniqueInputColors = uniqueInputColors;

    plan.frames.resize(frameCount);
    for (std::size_t f = 0; f < frameCount; ++f) {
        plan.frames[f].delayMs = std::max(svgFrames[f].delayMs, 10);
    }

    if (uniqueCount == 1) {
        // Every sampled frame is identical: emit all objects statically with 0 animation triggers.
        plan.staticObjects = std::move(perUnique.front().plan.staticObjects);
    } else {
        // Lift primitives identical across ALL unique frames into plan.staticObjects.
        struct PrimKey {
            int kind = 0;
            std::uint16_t color = 0;
            std::int16_t layer = 0;
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            int rot = 0;
            int skew = 0;
            bool operator==(PrimKey const&) const = default;
        };
        struct PrimKeyHash {
            std::size_t operator()(PrimKey const& k) const noexcept {
                std::size_t h = static_cast<std::size_t>(k.kind) * 2654435761u;
                auto mix = [&](std::size_t v) {
                    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
                };
                mix(k.color);
                mix(static_cast<std::size_t>(k.layer));
                mix(static_cast<std::size_t>(k.x));
                mix(static_cast<std::size_t>(k.y));
                mix(static_cast<std::size_t>(k.w));
                mix(static_cast<std::size_t>(k.h));
                mix(static_cast<std::size_t>(k.rot));
                mix(static_cast<std::size_t>(k.skew));
                return h;
            }
        };
        auto keyOf = [](Primitive const& o) -> PrimKey {
            return {
                static_cast<int>(o.kind),
                o.color,
                o.layer,
                static_cast<int>(std::lround(o.x * 4.f)),
                static_cast<int>(std::lround(o.y * 4.f)),
                static_cast<int>(std::lround(o.width * 4.f)),
                static_cast<int>(std::lround(o.height * 4.f)),
                static_cast<int>(std::lround(o.rotation * 2.f)),
                static_cast<int>(std::lround(o.skew * 2.f))
            };
        };

        std::unordered_map<PrimKey, int, PrimKeyHash> sharedMinCount;
        for (auto const& obj : perUnique[0].plan.staticObjects) {
            ++sharedMinCount[keyOf(obj)];
        }
        for (std::size_t u = 1; u < uniqueCount && !sharedMinCount.empty(); ++u) {
            std::unordered_map<PrimKey, int, PrimKeyHash> curCounts;
            for (auto const& obj : perUnique[u].plan.staticObjects) {
                auto const k = keyOf(obj);
                if (sharedMinCount.find(k) != sharedMinCount.end()) ++curCounts[k];
            }
            for (auto it = sharedMinCount.begin(); it != sharedMinCount.end();) {
                auto cit = curCounts.find(it->first);
                if (cit == curCounts.end()) {
                    it = sharedMinCount.erase(it);
                } else {
                    it->second = std::min(it->second, cit->second);
                    ++it;
                }
            }
        }

        std::size_t const words = (frameCount + 63) / 64;
        std::unordered_map<PrimKey, int, PrimKeyHash> emittedStatic = sharedMinCount;
        for (auto const& obj : perUnique[0].plan.staticObjects) {
            auto it = emittedStatic.find(keyOf(obj));
            if (it != emittedStatic.end() && it->second > 0) {
                plan.staticObjects.push_back(obj);
                --it->second;
            }
        }

        plan.tracks.reserve(uniqueCount);
        for (std::size_t u = 0; u < uniqueCount; ++u) {
            auto remainingStatic = sharedMinCount;
            VisibilityTrack track;
            track.objects.reserve(perUnique[u].plan.staticObjects.size());
            for (auto const& obj : perUnique[u].plan.staticObjects) {
                auto it = remainingStatic.find(keyOf(obj));
                if (it != remainingStatic.end() && it->second > 0) {
                    --it->second;
                    continue;
                }
                track.objects.push_back(obj);
            }
            if (track.objects.empty()) continue;
            track.mask.assign(words, 0);
            for (std::size_t f = 0; f < frameCount; ++f) {
                if (frameToUnique[f] == u) {
                    track.mask[f / 64] |= (std::uint64_t{1} << (f % 64));
                }
            }
            plan.tracks.push_back(std::move(track));
        }
    }

    // Count all visual objects across staticObjects + tracks.
    auto tally = [&](std::vector<Primitive> const& objs) {
        for (auto const& o : objs) {
            switch (o.kind) {
                case PrimitiveKind::Block: ++plan.blockObjects; break;
                case PrimitiveKind::Stroke: ++plan.strokeObjects; break;
                case PrimitiveKind::Circle: ++plan.circleObjects; break;
                case PrimitiveKind::Triangle:
                case PrimitiveKind::WideTriangle: ++plan.triangleObjects; break;
                case PrimitiveKind::Glow: ++plan.glowObjects; break;
                case PrimitiveKind::Stamp: ++plan.stampObjects; break;
            }
        }
        plan.visualObjects += objs.size();
    };
    plan.blockObjects = 0;
    plan.strokeObjects = 0;
    plan.circleObjects = 0;
    plan.triangleObjects = 0;
    plan.glowObjects = 0;
    plan.stampObjects = 0;
    plan.visualObjects = 0;
    tally(plan.staticObjects);
    for (auto const& track : plan.tracks) tally(track.objects);

    // Exact trigger count matching GifObjectEmitter::prepareImport.
    std::size_t triggers = 0;
    if (plan.frames.size() > 1 && !plan.tracks.empty()) {
        for (auto const& track : plan.tracks) {
            if (!visibleAt(track.mask, 0)) ++triggers;
        }
        ++triggers;
        std::size_t const firstTransition = loop ? 0 : 1;
        for (std::size_t f = firstTransition; f < plan.frames.size(); ++f) {
            std::size_t const prev = f == 0 ? plan.frames.size() - 1 : f - 1;
            for (auto const& track : plan.tracks) {
                if (visibleAt(track.mask, static_cast<int>(f)) !=
                    visibleAt(track.mask, static_cast<int>(prev))) {
                    ++triggers;
                }
            }
            bool const hasNext = f + 1 < plan.frames.size();
            if (hasNext || loop) ++triggers;
        }
    }
    plan.triggerObjects = triggers;
    plan.totalObjects = plan.visualObjects + plan.triggerObjects;

    res.stats.elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tTotalStart).count();
    return res;
}

SvgBuildResult buildSvgPlanFromFile(
    std::filesystem::path const& path,
    SvgOptions const& options
) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        SvgBuildResult err;
        err.error = "No se pudo abrir el archivo SVG: " + path.string();
        return err;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return buildSvgPlan(ss.str(), options);
}

} // namespace paimon::gifimport
