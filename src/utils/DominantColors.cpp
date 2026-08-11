#include "DominantColors.hpp"
#include "../utils/Constants.hpp"
#include <algorithm>
#include <vector>
#include <cmath>
#include <map>
#include <unordered_map>
#include <random>
#include <limits>

namespace {
    struct LABColor {
        double L;
        double a;
        double b;
    };
    
    // RGB [0,255] -> XYZ (D65).
    static void rgbToXYZ(uint8_t r, uint8_t g, uint8_t b, double& X, double& Y, double& Z) {
        double R = r / 255.0;
        double G = g / 255.0;
        double B = b / 255.0;
        
        auto gammaCorrect = [](double v) -> double {
            return (v <= 0.04045) ? (v / 12.92) : std::pow((v + 0.055) / 1.055, 2.4);
        };
        
        R = gammaCorrect(R);
        G = gammaCorrect(G);
        B = gammaCorrect(B);
        
        X = R * 0.4124564 + G * 0.3575761 + B * 0.1804375;
        Y = R * 0.2126729 + G * 0.7151522 + B * 0.0721750;
        Z = R * 0.0193339 + G * 0.1191920 + B * 0.9503041;
        
        X *= 100.0;
        Y *= 100.0;
        Z *= 100.0;
    }
    
    // XYZ -> LAB (CIE L*a*b*).
    static LABColor xyzToLAB(double X, double Y, double Z) {
        const double Xn = 95.047;
        const double Yn = 100.000;
        const double Zn = 108.883;
        
        double x = X / Xn;
        double y = Y / Yn;
        double z = Z / Zn;
        
        auto f = [](double t) -> double {
            const double delta = 6.0 / 29.0;
            return (t > delta * delta * delta) ? std::cbrt(t) : (t / (3.0 * delta * delta) + 4.0 / 29.0);
        };
        
        double fx = f(x);
        double fy = f(y);
        double fz = f(z);
        
        LABColor lab;
        lab.L = 116.0 * fy - 16.0;
        lab.a = 500.0 * (fx - fy);
        lab.b = 200.0 * (fy - fz);
        
        return lab;
    }
    
    static LABColor rgbToLAB(uint8_t r, uint8_t g, uint8_t b) {
        double X, Y, Z;
        rgbToXYZ(r, g, b, X, Y, Z);
        return xyzToLAB(X, Y, Z);
    }
    
    static void labToXYZ(LABColor const& lab, double& X, double& Y, double& Z) {
        const double Xn = 95.047;
        const double Yn = 100.000;
        const double Zn = 108.883;
        
        double fy = (lab.L + 16.0) / 116.0;
        double fx = lab.a / 500.0 + fy;
        double fz = fy - lab.b / 200.0;
        
        auto finv = [](double t) -> double {
            const double delta = 6.0 / 29.0;
            return (t > delta) ? (t * t * t) : (3.0 * delta * delta * (t - 4.0 / 29.0));
        };
        
        X = Xn * finv(fx);
        Y = Yn * finv(fy);
        Z = Zn * finv(fz);
    }
    
    // XYZ -> RGB [0,255].
    static DCColor xyzToRGB(double X, double Y, double Z) {
        X /= 100.0;
        Y /= 100.0;
        Z /= 100.0;
        
        double R = X *  3.2404542 + Y * -1.5371385 + Z * -0.4985314;
        double G = X * -0.9692660 + Y *  1.8760108 + Z *  0.0415560;
        double B = X *  0.0556434 + Y * -0.2040259 + Z *  1.0572252;
        
        auto gammaInv = [](double v) -> double {
            return (v <= 0.0031308) ? (12.92 * v) : (1.055 * std::pow(v, 1.0/2.4) - 0.055);
        };
        
        R = gammaInv(R);
        G = gammaInv(G);
        B = gammaInv(B);
        
        auto clamp = [](double v) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(v * 255.0, 0.0, 255.0));
        };
        
        return DCColor{ clamp(R), clamp(G), clamp(B) };
    }
    
    static DCColor labToRGB(LABColor const& lab) {
        double X, Y, Z;
        labToXYZ(lab, X, Y, Z);
        return xyzToRGB(X, Y, Z);
    }
    
    static double deltaESimple(LABColor const& lab1, LABColor const& lab2) {
        double dL = lab1.L - lab2.L;
        double da = lab1.a - lab2.a;
        double db = lab1.b - lab2.b;
        return std::sqrt(dL * dL + da * da + db * db);
    }
    
    static double deltaE2000(LABColor const& lab1, LABColor const& lab2) {
        
        const double kL = 1.0, kC = 1.0, kH = 1.0;
        
        double L1 = lab1.L, a1 = lab1.a, b1 = lab1.b;
        double L2 = lab2.L, a2 = lab2.a, b2 = lab2.b;
        
        double C1 = std::sqrt(a1 * a1 + b1 * b1);
        double C2 = std::sqrt(a2 * a2 + b2 * b2);
        double Cbar = (C1 + C2) / 2.0;
        
        double G = 0.5 * (1.0 - std::sqrt(std::pow(Cbar, 7) / (std::pow(Cbar, 7) + std::pow(25.0, 7))));
        
        double a1p = a1 * (1.0 + G);
        double a2p = a2 * (1.0 + G);
        
        double C1p = std::sqrt(a1p * a1p + b1 * b1);
        double C2p = std::sqrt(a2p * a2p + b2 * b2);
        
        auto computeHp = [](double ap, double bp) -> double {
            if (ap == 0.0 && bp == 0.0) return 0.0;
            double h = std::atan2(bp, ap) * 180.0 / M_PI;
            return (h >= 0.0) ? h : (h + 360.0);
        };
        
        double h1p = computeHp(a1p, b1);
        double h2p = computeHp(a2p, b2);
        
        double dL = L2 - L1;
        double dCp = C2p - C1p;
        
        double dhp = 0.0;
        if (C1p * C2p != 0.0) {
            double diff = h2p - h1p;
            if (std::abs(diff) <= 180.0) dhp = diff;
            else if (diff > 180.0) dhp = diff - 360.0;
            else dhp = diff + 360.0;
        }
        
        double dHp = 2.0 * std::sqrt(C1p * C2p) * std::sin(dhp * M_PI / 360.0);
        
        double Lbarp = (L1 + L2) / 2.0;
        double Cbarp = (C1p + C2p) / 2.0;
        
        double hbarp = 0.0;
        if (C1p * C2p != 0.0) {
            double sum = h1p + h2p;
            if (std::abs(h1p - h2p) <= 180.0) hbarp = sum / 2.0;
            else if (sum < 360.0) hbarp = (sum + 360.0) / 2.0;
            else hbarp = (sum - 360.0) / 2.0;
        }
        
        double T = 1.0 - 0.17 * std::cos((hbarp - 30.0) * M_PI / 180.0)
                      + 0.24 * std::cos(2.0 * hbarp * M_PI / 180.0)
                      + 0.32 * std::cos((3.0 * hbarp + 6.0) * M_PI / 180.0)
                      - 0.20 * std::cos((4.0 * hbarp - 63.0) * M_PI / 180.0);
        
        double dTheta = 30.0 * std::exp(-std::pow((hbarp - 275.0) / 25.0, 2));
        double RC = 2.0 * std::sqrt(std::pow(Cbarp, 7) / (std::pow(Cbarp, 7) + std::pow(25.0, 7)));
        
        double SL = 1.0 + (0.015 * std::pow(Lbarp - 50.0, 2)) / std::sqrt(20.0 + std::pow(Lbarp - 50.0, 2));
        double SC = 1.0 + 0.045 * Cbarp;
        double SH = 1.0 + 0.015 * Cbarp * T;
        double RT = -std::sin(2.0 * dTheta * M_PI / 180.0) * RC;
        
        double dE = std::sqrt(
            std::pow(dL / (kL * SL), 2) +
            std::pow(dCp / (kC * SC), 2) +
            std::pow(dHp / (kH * SH), 2) +
            RT * (dCp / (kC * SC)) * (dHp / (kH * SH))
        );
        
        return dE;
    }
    
    struct Cluster {
        LABColor centroid;
        std::vector<size_t> members;
        uint32_t pixelCount = 0;
    };
    
    static std::vector<LABColor> initializeCentroids(
        std::vector<LABColor> const& pixels, int K, std::mt19937& rng
    ) {
        std::vector<LABColor> centroids;
        if (pixels.empty()) return centroids;
        
        std::uniform_int_distribution<size_t> dist(0, pixels.size() - 1);
        centroids.push_back(pixels[dist(rng)]);
        
        for (int k = 1; k < K; k++) {
            std::vector<double> distances(pixels.size());
            double totalDist = 0.0;
            
            for (size_t i = 0; i < pixels.size(); i++) {
                double minDist = std::numeric_limits<double>::max();
                for (auto const& c : centroids) {
                    double d = deltaESimple(pixels[i], c);
                    minDist = std::min(minDist, d);
                }
                distances[i] = minDist * minDist;
                totalDist += distances[i];
            }
            
            if (totalDist == 0.0) break;
            
            std::uniform_real_distribution<double> prob(0.0, totalDist);
            double target = prob(rng);
            double cumulative = 0.0;
            
            for (size_t i = 0; i < pixels.size(); i++) {
                cumulative += distances[i];
                if (cumulative >= target) {
                    centroids.push_back(pixels[i]);
                    break;
                }
            }
            
            if (centroids.size() <= static_cast<size_t>(k)) {
                centroids.push_back(pixels[dist(rng)]);
            }
        }
        
        return centroids;
    }
    
    static std::vector<Cluster> kMeansClustering(
        std::vector<LABColor> const& pixels, int K, int maxIterations = 10
    ) {
        if (pixels.empty() || K <= 0) return {};
        
        std::mt19937 rng(42);  // deterministic palette selection
        K = std::min(K, static_cast<int>(pixels.size()));
        
        std::vector<LABColor> centroids = initializeCentroids(pixels, K, rng);
        if (centroids.size() < static_cast<size_t>(K)) {
            K = static_cast<int>(centroids.size());
        }
        
        std::vector<Cluster> clusters(K);
        
        for (int iter = 0; iter < maxIterations; iter++) {
            for (auto& cluster : clusters) {
                cluster.members.clear();
                cluster.pixelCount = 0;
            }
            
            for (size_t i = 0; i < pixels.size(); i++) {
                double minDist = std::numeric_limits<double>::max();
                int bestCluster = 0;
                
                for (int k = 0; k < K; k++) {
                    double dist = deltaESimple(pixels[i], centroids[k]);
                    if (dist < minDist) {
                        minDist = dist;
                        bestCluster = k;
                    }
                }
                
                clusters[bestCluster].members.push_back(i);
                clusters[bestCluster].pixelCount++;
            }
            
            bool converged = true;
            for (int k = 0; k < K; k++) {
                if (clusters[k].members.empty()) continue;
                
                double sumL = 0.0, sumA = 0.0, sumB = 0.0;
                for (size_t idx : clusters[k].members) {
                    sumL += pixels[idx].L;
                    sumA += pixels[idx].a;
                    sumB += pixels[idx].b;
                }
                
                LABColor newCentroid;
                newCentroid.L = sumL / clusters[k].members.size();
                newCentroid.a = sumA / clusters[k].members.size();
                newCentroid.b = sumB / clusters[k].members.size();
                
                if (deltaESimple(centroids[k], newCentroid) > 1.0) {
                    converged = false;
                }
                
                centroids[k] = newCentroid;
                clusters[k].centroid = newCentroid;
            }
            
            if (converged) break;
        }
        
        std::sort(clusters.begin(), clusters.end(),
            [](Cluster const& a, Cluster const& b) {
                return a.pixelCount > b.pixelCount;
            });
        
        return clusters;
    }
    
    // Exclude common UI extremes from the palette.
    static bool isLikelyUIOrObject(uint8_t r, uint8_t g, uint8_t b) {
        if (r < PaimonConstants::UI_BLACK_THRESHOLD && 
            g < PaimonConstants::UI_BLACK_THRESHOLD && 
            b < PaimonConstants::UI_BLACK_THRESHOLD) return true;
        if (r > PaimonConstants::UI_WHITE_THRESHOLD && 
            g > PaimonConstants::UI_WHITE_THRESHOLD && 
            b > PaimonConstants::UI_WHITE_THRESHOLD) return true;
        return false;
    }

    // RGB [0..255] -> HSV.
    static void rgb2hsv(uint8_t r, uint8_t g, uint8_t b, float& h, float& s, float& v) {
        float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
        float cmax = std::max(rf, std::max(gf, bf));
        float cmin = std::min(rf, std::min(gf, bf));
        float d = cmax - cmin;
        if (d == 0) h = 0;
        else if (cmax == rf) h = 60.f * std::fmod(((gf - bf) / d), 6.f);
        else if (cmax == gf) h = 60.f * (((bf - rf) / d) + 2.f);
        else h = 60.f * (((rf - gf) / d) + 4.f);
        if (h < 0) h += 360.f;
        s = (cmax == 0.f) ? 0.f : (d / cmax);
        v = cmax;
    }
}

std::pair<DCColor, DCColor> DominantColors::extract(const uint8_t* rgb, int width, int height) {
    if (!rgb || width <= 0 || height <= 0) return { DCColor{0,0,0}, DCColor{0,0,0} };

    // Sample in LAB, filtering UI colors and lightly favoring borders.
    std::vector<LABColor> labPixels;
    std::vector<DCColor> rgbPixels;
    
    const int borderTop = height * 15 / 100;
    const int borderBottom = height * 85 / 100;
    const int borderLeft = width * 15 / 100;
    const int borderRight = width * 85 / 100;
    
    const int totalPixels = width * height;
    const int maxSamples = 2000;
    int step = 1;
    
    if (totalPixels > maxSamples) {
        step = static_cast<int>(std::sqrt(static_cast<double>(totalPixels) / maxSamples)) + 1;
    }
    
    for (int y = 0; y < height; y += step) {
        bool inBorderY = (y < borderTop || y > borderBottom);
        const uint8_t* row = rgb + (y * width * 3);
        
        for (int x = 0; x < width; x += step) {
            bool inBorderX = (x < borderLeft || x > borderRight);
            const uint8_t* p = row + x * 3;
            uint8_t r = p[0], g = p[1], b = p[2];
            
            if (isLikelyUIOrObject(r, g, b)) continue;
            
            float h, s, v;
            rgb2hsv(r, g, b, h, s, v);
            
            if (s < 0.08f || v < 0.12f) continue;
            
            int weight = (inBorderY || inBorderX) ? 2 : 1;
            
            LABColor lab = rgbToLAB(r, g, b);
            for (int w = 0; w < weight; w++) {
                labPixels.push_back(lab);
                rgbPixels.push_back(DCColor{r, g, b});
                
                if (labPixels.size() >= maxSamples) break;
            }
            if (labPixels.size() >= maxSamples) break;
        }
        if (labPixels.size() >= maxSamples) break;
    }
    
    if (labPixels.size() < 100) {
        double sumL = 0, sumA = 0, sumB = 0;
        int count = 0;
        
        for (int y = 0; y < height; y++) {
            const uint8_t* row = rgb + (y * width * 3);
            for (int x = 0; x < width; x++) {
                const uint8_t* p = row + x * 3;
                if (!isLikelyUIOrObject(p[0], p[1], p[2])) {
                    LABColor lab = rgbToLAB(p[0], p[1], p[2]);
                    sumL += lab.L; sumA += lab.a; sumB += lab.b;
                    count++;
                }
            }
        }
        
        if (count == 0) return {DCColor{40,40,40}, DCColor{60,60,60}};
        
        LABColor avgLab;
        avgLab.L = sumL / count;
        avgLab.a = sumA / count;
        avgLab.b = sumB / count;
        DCColor avg = labToRGB(avgLab);
        return {avg, avg};
    }
    
    const int K = std::min(5, static_cast<int>(labPixels.size() / 50));
    if (K < 2) {
        double sumL = 0, sumA = 0, sumB = 0;
        for (auto const& lab : labPixels) {
            sumL += lab.L; sumA += lab.a; sumB += lab.b;
        }
        LABColor avgLab;
        avgLab.L = sumL / labPixels.size();
        avgLab.a = sumA / labPixels.size();
        avgLab.b = sumB / labPixels.size();
        DCColor avg = labToRGB(avgLab);
        return {avg, avg};
    }
    
    std::vector<Cluster> clusters = kMeansClustering(labPixels, K, 10);
    
    if (clusters.empty()) {
        return {DCColor{40,40,40}, DCColor{60,60,60}};
    }
    
    DCColor color1 = labToRGB(clusters[0].centroid);

    const double DELTA_E_MIN_THRESHOLD = 20.0;
    
    DCColor color2 = color1;
    int bestClusterIndex = -1;
    uint32_t bestClusterSize = 0;
    
    for (size_t i = 1; i < clusters.size(); i++) {
        if (clusters[i].pixelCount == 0) continue;
        
        double deltaE = deltaE2000(clusters[0].centroid, clusters[i].centroid);
        
        if (deltaE >= DELTA_E_MIN_THRESHOLD) {
            bestClusterIndex = static_cast<int>(i);
            bestClusterSize = clusters[i].pixelCount;
            color2 = labToRGB(clusters[i].centroid);
            break;
        }
    }
    
    if (bestClusterIndex == -1) {
        double maxDeltaE = 0.0;
        for (size_t i = 1; i < clusters.size(); i++) {
            if (clusters[i].pixelCount == 0) continue;
            
            double deltaE = deltaE2000(clusters[0].centroid, clusters[i].centroid);
            if (deltaE > maxDeltaE) {
                maxDeltaE = deltaE;
                bestClusterIndex = static_cast<int>(i);
                color2 = labToRGB(clusters[i].centroid);
            }
        }
        
        if (bestClusterIndex == -1 || maxDeltaE < 10.0) {
            LABColor lab1 = clusters[0].centroid;
            LABColor lab2 = lab1;
            
            if (std::abs(lab1.a) > std::abs(lab1.b)) {
                lab2.b += (lab2.b > 0) ? 25.0 : -25.0;
                lab2.a *= 0.6;
            } else {
                lab2.a += (lab2.a > 0) ? 25.0 : -25.0;
                lab2.b *= 0.6;
            }
            
            lab2.L = std::clamp(lab2.L + ((lab2.L < 50.0) ? 15.0 : -15.0), 0.0, 100.0);
            
            color2 = labToRGB(lab2);
        }
    }
    
    return {color1, color2};
}

std::pair<DCColor, DCColor> DominantColors::extractReviewed(const uint8_t* rgb, int width, int height) {
    auto primary = extract(rgb, width, height);
    if (!rgb || width <= 0 || height <= 0) return primary;

    int reviewWidth = std::min(width, 32);
    int reviewHeight = std::min(height, 18);
    std::vector<uint8_t> overview(static_cast<size_t>(reviewWidth) * reviewHeight * 3);

    for (int y = 0; y < reviewHeight; ++y) {
        int y0 = y * height / reviewHeight;
        int y1 = std::max(y0 + 1, (y + 1) * height / reviewHeight);
        for (int x = 0; x < reviewWidth; ++x) {
            int x0 = x * width / reviewWidth;
            int x1 = std::max(x0 + 1, (x + 1) * width / reviewWidth);
            uint64_t sums[3] = {};
            uint64_t count = 0;

            for (int sourceY = y0; sourceY < y1; ++sourceY) {
                auto row = rgb + static_cast<size_t>(sourceY) * width * 3;
                for (int sourceX = x0; sourceX < x1; ++sourceX) {
                    auto pixel = row + sourceX * 3;
                    sums[0] += pixel[0];
                    sums[1] += pixel[1];
                    sums[2] += pixel[2];
                    ++count;
                }
            }

            size_t output = (static_cast<size_t>(y) * reviewWidth + x) * 3;
            overview[output] = static_cast<uint8_t>(sums[0] / count);
            overview[output + 1] = static_cast<uint8_t>(sums[1] / count);
            overview[output + 2] = static_cast<uint8_t>(sums[2] / count);
        }
    }

    auto reviewed = extract(overview.data(), reviewWidth, reviewHeight);
    auto scorePair = [&](std::pair<DCColor, DCColor> const& pair) {
        auto first = rgbToLAB(pair.first.r, pair.first.g, pair.first.b);
        auto second = rgbToLAB(pair.second.r, pair.second.g, pair.second.b);
        double score = 0.0;

        for (size_t i = 0; i < overview.size(); i += 3) {
            auto pixel = rgbToLAB(overview[i], overview[i + 1], overview[i + 2]);
            score += std::min(deltaESimple(pixel, first), deltaESimple(pixel, second));
        }
        return score / (overview.size() / 3);
    };

    auto result = scorePair(reviewed) < scorePair(primary) ? reviewed : primary;
    if (reviewWidth < 2) return result;

    auto averageSide = [&](int beginX, int endX) {
        LABColor average{};
        size_t count = 0;
        for (int y = 0; y < reviewHeight; ++y) {
            for (int x = beginX; x < endX; ++x) {
                size_t index = (static_cast<size_t>(y) * reviewWidth + x) * 3;
                auto pixel = rgbToLAB(overview[index], overview[index + 1], overview[index + 2]);
                average.L += pixel.L;
                average.a += pixel.a;
                average.b += pixel.b;
                ++count;
            }
        }
        average.L /= count;
        average.a /= count;
        average.b /= count;
        return average;
    };

    int middle = reviewWidth / 2;
    auto left = averageSide(0, middle);
    auto right = averageSide(middle, reviewWidth);
    auto first = rgbToLAB(result.first.r, result.first.g, result.first.b);
    auto second = rgbToLAB(result.second.r, result.second.g, result.second.b);
    double direct = deltaESimple(first, left) + deltaESimple(second, right);
    double swapped = deltaESimple(second, left) + deltaESimple(first, right);
    if (swapped < direct) std::swap(result.first, result.second);

    return result;
}
