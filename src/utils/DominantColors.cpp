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
    // color-space conversion helpers
    
    struct LABColor {
        double L;  // lightness [0, 100]
        double a;  // green-red [-128, 127]
        double b;  // blue-yellow [-128, 127]
    };
    
    // RGB [0,255] -> XYZ (D65)
    static void rgbToXYZ(uint8_t r, uint8_t g, uint8_t b, double& X, double& Y, double& Z) {
        // normalize RGB to [0, 1]
        double R = r / 255.0;
        double G = g / 255.0;
        double B = b / 255.0;
        
        // apply sRGB gamma
        auto gammaCorrect = [](double v) -> double {
            return (v <= 0.04045) ? (v / 12.92) : std::pow((v + 0.055) / 1.055, 2.4);
        };
        
        R = gammaCorrect(R);
        G = gammaCorrect(G);
        B = gammaCorrect(B);
        
        // RGB->XYZ matrix (D65)
        X = R * 0.4124564 + G * 0.3575761 + B * 0.1804375;
        Y = R * 0.2126729 + G * 0.7151522 + B * 0.0721750;
        Z = R * 0.0193339 + G * 0.1191920 + B * 0.9503041;
        
        // scale to the D65 white point
        X *= 100.0;
        Y *= 100.0;
        Z *= 100.0;
    }
    
    // XYZ -> LAB (CIE L*a*b*)
    static LABColor xyzToLAB(double X, double Y, double Z) {
        // D65 white point
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
    
    // RGB -> LAB directo
    static LABColor rgbToLAB(uint8_t r, uint8_t g, uint8_t b) {
        double X, Y, Z;
        rgbToXYZ(r, g, b, X, Y, Z);
        return xyzToLAB(X, Y, Z);
    }
    
    // LAB -> XYZ
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
    
    // XYZ -> RGB [0,255]
    static DCColor xyzToRGB(double X, double Y, double Z) {
        // scale back
        X /= 100.0;
        Y /= 100.0;
        Z /= 100.0;
        
        // XYZ->RGB matrix (D65)
        double R = X *  3.2404542 + Y * -1.5371385 + Z * -0.4985314;
        double G = X * -0.9692660 + Y *  1.8760108 + Z *  0.0415560;
        double B = X *  0.0556434 + Y * -0.2040259 + Z *  1.0572252;
        
        // inverse sRGB gamma
        auto gammaInv = [](double v) -> double {
            return (v <= 0.0031308) ? (12.92 * v) : (1.055 * std::pow(v, 1.0/2.4) - 0.055);
        };
        
        R = gammaInv(R);
        G = gammaInv(G);
        B = gammaInv(B);
        
        // clamp back to [0, 255]
        auto clamp = [](double v) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(v * 255.0, 0.0, 255.0));
        };
        
        return DCColor{ clamp(R), clamp(G), clamp(B) };
    }
    
    // LAB -> RGB
    static DCColor labToRGB(LABColor const& lab) {
        double X, Y, Z;
        labToXYZ(lab, X, Y, Z);
        return xyzToRGB(X, Y, Z);
    }
    
    // fast LAB distance (for clustering)
    static double deltaESimple(LABColor const& lab1, LABColor const& lab2) {
        double dL = lab1.L - lab2.L;
        double da = lab1.a - lab2.a;
        double db = lab1.b - lab2.b;
        return std::sqrt(dL * dL + da * da + db * db);
    }
    
    // Delta E (CIEDE2000) for fine comparisons
    static double deltaE2000(LABColor const& lab1, LABColor const& lab2) {
        // simplified CIEDE2000
        
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
        std::vector<size_t> members;  // pixel indices
        uint32_t pixelCount = 0;
    };
    
    // initialize K centroids with a simplified K-Means++
    static std::vector<LABColor> initializeCentroids(
        std::vector<LABColor> const& pixels, int K, std::mt19937& rng
    ) {
        std::vector<LABColor> centroids;
        if (pixels.empty()) return centroids;
        
        // first centroid: random
        std::uniform_int_distribution<size_t> dist(0, pixels.size() - 1);
        centroids.push_back(pixels[dist(rng)]);
        
        // rest: K-Means++ using simple distance
        for (int k = 1; k < K; k++) {
            std::vector<double> distances(pixels.size());
            double totalDist = 0.0;
            
            for (size_t i = 0; i < pixels.size(); i++) {
                double minDist = std::numeric_limits<double>::max();
                for (auto const& c : centroids) {
                    double d = deltaESimple(pixels[i], c);  // fast distance
                    minDist = std::min(minDist, d);
                }
                distances[i] = minDist * minDist;
                totalDist += distances[i];
            }
            
            if (totalDist == 0.0) break;  // avoid division by zero
            
            // pick the next centroid with probability proportional to distance
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
            
            // if none chosen, add a random one and continue
            if (centroids.size() <= static_cast<size_t>(k)) {
                centroids.push_back(pixels[dist(rng)]);
            }
        }
        
        return centroids;
    }
    
    // K-means en LAB (un poco optimizado)
    static std::vector<Cluster> kMeansClustering(
        std::vector<LABColor> const& pixels, int K, int maxIterations = 10
    ) {
        if (pixels.empty() || K <= 0) return {};
        
        std::mt19937 rng(42);  // fixed seed for reproducible results
        K = std::min(K, static_cast<int>(pixels.size()));
        
        // initialize centroids
        std::vector<LABColor> centroids = initializeCentroids(pixels, K, rng);
        if (centroids.size() < static_cast<size_t>(K)) {
            K = static_cast<int>(centroids.size());
        }
        
        std::vector<Cluster> clusters(K);
        
        // k-means iteration (simple distance for speed)
        for (int iter = 0; iter < maxIterations; iter++) {
            // clear clusters
            for (auto& cluster : clusters) {
                cluster.members.clear();
                cluster.pixelCount = 0;
            }
            
            // assign each pixel to the nearest centroid
            for (size_t i = 0; i < pixels.size(); i++) {
                double minDist = std::numeric_limits<double>::max();
                int bestCluster = 0;
                
                for (int k = 0; k < K; k++) {
                    double dist = deltaESimple(pixels[i], centroids[k]);  // fast distance
                    if (dist < minDist) {
                        minDist = dist;
                        bestCluster = k;
                    }
                }
                
                clusters[bestCluster].members.push_back(i);
                clusters[bestCluster].pixelCount++;
            }
            
            // recompute centroids
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
                
                // rough convergence check
                if (deltaESimple(centroids[k], newCentroid) > 1.0) {
                    converged = false;
                }
                
                centroids[k] = newCentroid;
                clusters[k].centroid = newCentroid;
            }
            
            if (converged) break;
        }
        
        // sort clusters by size (desc)
        std::sort(clusters.begin(), clusters.end(),
            [](Cluster const& a, Cluster const& b) {
                return a.pixelCount > b.pixelCount;
            });
        
        return clusters;
    }
    
    // check if the pixel looks like UI/object (very dark or pure black/white)
    static bool isLikelyUIOrObject(uint8_t r, uint8_t g, uint8_t b) {
        // pure/near black (UI, text)
        if (r < PaimonConstants::UI_BLACK_THRESHOLD && 
            g < PaimonConstants::UI_BLACK_THRESHOLD && 
            b < PaimonConstants::UI_BLACK_THRESHOLD) return true;
        // pure/near white (UI borders)
        if (r > PaimonConstants::UI_WHITE_THRESHOLD && 
            g > PaimonConstants::UI_WHITE_THRESHOLD && 
            b > PaimonConstants::UI_WHITE_THRESHOLD) return true;
        return false;
    }

    // convert RGB [0..255] to HSV (h in [0..360), s,v in [0..1])
    static void rgb2hsv(uint8_t r, uint8_t g, uint8_t b, float& h, float& s, float& v) {
        float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
        float cmax = std::max(rf, std::max(gf, bf));
        float cmin = std::min(rf, std::min(gf, bf));
        float d = cmax - cmin;
        // Hue
        if (d == 0) h = 0;
        else if (cmax == rf) h = 60.f * std::fmod(((gf - bf) / d), 6.f);
        else if (cmax == gf) h = 60.f * (((bf - rf) / d) + 2.f);
        else h = 60.f * (((rf - gf) / d) + 4.f);
        if (h < 0) h += 360.f;
        // Saturation
        s = (cmax == 0.f) ? 0.f : (d / cmax);
        // Value
        v = cmax;
    }
}

std::pair<DCColor, DCColor> DominantColors::extract(const uint8_t* rgb, int width, int height) {
    if (!rgb || width <= 0 || height <= 0) return { DCColor{0,0,0}, DCColor{0,0,0} };

    // K-means in LAB (CIEDE2000).
    
    // collect samples and convert to LAB
    std::vector<LABColor> labPixels;
    std::vector<DCColor> rgbPixels;  // keep original RGB for reference
    
    const int borderTop = height * 15 / 100;
    const int borderBottom = height * 85 / 100;
    const int borderLeft = width * 15 / 100;
    const int borderRight = width * 85 / 100;
    
    // limit samples (k-means is costly)
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
            
            // filter likely UI/extremes
            if (isLikelyUIOrObject(r, g, b)) continue;
            
            float h, s, v;
            rgb2hsv(r, g, b, h, s, v);
            
            // filter low saturation and very dark
            if (s < 0.08f || v < 0.12f) continue;
            
            // slight bias toward border samples
            int weight = (inBorderY || inBorderX) ? 2 : 1;
            
            // Add weighted samples.
            LABColor lab = rgbToLAB(r, g, b);
            for (int w = 0; w < weight; w++) {
                labPixels.push_back(lab);
                rgbPixels.push_back(DCColor{r, g, b});
                
                // limit sample count
                if (labPixels.size() >= maxSamples) break;
            }
            if (labPixels.size() >= maxSamples) break;
        }
        if (labPixels.size() >= maxSamples) break;
    }
    
    // fallback if not enough samples
    if (labPixels.size() < 100) {
        // average with minimal filtering
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
    
    // K-means in LAB.
    const int K = std::min(5, static_cast<int>(labPixels.size() / 50));  // Minimum 50 pixels per cluster
    if (K < 2) {
        // too few samples
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
    
    // color1 = largest cluster
    DCColor color1 = labToRGB(clusters[0].centroid);
    
    // color2 = largest cluster sufficiently distinct from color1
    const double DELTA_E_MIN_THRESHOLD = 20.0;  // threshold for "sufficiently distinct"
    
    DCColor color2 = color1;  // defaults to color1
    int bestClusterIndex = -1;
    uint32_t bestClusterSize = 0;
    
    // pick the first (largest) cluster outside color1's range
    for (size_t i = 1; i < clusters.size(); i++) {
        if (clusters[i].pixelCount == 0) continue;
        
        double deltaE = deltaE2000(clusters[0].centroid, clusters[i].centroid);
        
        if (deltaE >= DELTA_E_MIN_THRESHOLD) {
            bestClusterIndex = static_cast<int>(i);
            bestClusterSize = clusters[i].pixelCount;
            color2 = labToRGB(clusters[i].centroid);
            break;  // first large cluster outside the range
        }
    }
    
    // If everything is too similar, use the most different cluster (or synthesize).
    if (bestClusterIndex == -1) {
        // pick the most different cluster
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
        
        // if still too similar, synthesize a second color
        if (bestClusterIndex == -1 || maxDeltaE < 10.0) {
            LABColor lab1 = clusters[0].centroid;
            LABColor lab2 = lab1;
            
            // adjust in LAB
            if (std::abs(lab1.a) > std::abs(lab1.b)) {
                lab2.b += (lab2.b > 0) ? 25.0 : -25.0;
                lab2.a *= 0.6;
            } else {
                lab2.a += (lab2.a > 0) ? 25.0 : -25.0;
                lab2.b *= 0.6;
            }
            
            // adjust lightness
            lab2.L = std::clamp(lab2.L + ((lab2.L < 50.0) ? 15.0 : -15.0), 0.0, 100.0);
            
            color2 = labToRGB(lab2);
        }
    }
    
    return {color1, color2};
}

