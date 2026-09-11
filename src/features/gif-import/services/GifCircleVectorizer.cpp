#include "GifCircleVectorizer.hpp"

#include "GifPaintVectorizer.hpp"
#include "GifShapeRaster.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace paimon::gifimport {

namespace {

// Giros que se prueban al estirar la elipse. Dieciseis reparten media vuelta con
// mayor precision angular, alineandose mucho mejor con trazos en diagonal.
constexpr int kAngles = 16;
// Cinturas probadas: desde circulo entero hasta huso fino de 0.22 para lineas delgadas.
constexpr std::array<float, 6> kWaists{1.f, 0.82f, 0.65f, 0.48f, 0.35f, 0.22f};
// Con que paso se camina el eje al medir hasta donde llega la elipse.
constexpr float kWalk = 0.5f;
// Hasta donde se camina.
constexpr float kReach = 64.f;
// Radio menor de la elipse mas pequena.
constexpr float kMinRadius = 0.5f;
// Aire que se le da a la elipse ya elegida, de mas a menos.
constexpr std::array<float, 4> kBleeds{0.5f, 0.35f, 0.2f, 0.1f};
// Cuanto puede asomar una elipse sobre un color que se ve.
constexpr float kSpill = 0.14f;

struct Field {
    int width = 0;
    int height = 0;
    // Cuanto se puede alejar del centro de cada celda sin salirse de donde este
    // color puede pintar sin que se note.
    std::vector<float> clearance;

    float at(float x, float y) const {
        int const cellX = static_cast<int>(std::floor(x));
        int const cellY = static_cast<int>(std::floor(y));
        if (cellX < 0 || cellY < 0 || cellX >= width || cellY >= height) return 0.f;
        return clearance[static_cast<std::size_t>(cellY) * width + cellX];
    }
};

Primitive ellipse(
    Point const& center,
    float major,
    float minor,
    float angle,
    int color,
    int layer
) {
    return {
        center.x,
        center.y,
        major * 2.f,
        minor * 2.f,
        angle * 180.f / kPi,
        static_cast<std::uint16_t>(color),
        PrimitiveKind::Circle,
        static_cast<std::int16_t>(layer)
    };
}

// Cuantas celdas de las que faltan se lleva la elipse, y cuantas de las que ya
// estaban pintadas repite. Lo segundo no estorba —son del mismo color— pero entre
// dos que se llevan lo mismo gana la que menos se solape.
struct Gain {
    int fresh = 0;
    int repeated = 0;
};

Gain measure(
    Primitive const& shape,
    std::vector<std::uint8_t> const& remaining,
    std::vector<std::uint8_t> const& target,
    int width,
    int height
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    Gain gain;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            if (!target[index] || !placed.contains(x + 0.5f, y + 0.5f)) continue;
            if (remaining[index]) {
                ++gain.fresh;
            } else {
                ++gain.repeated;
            }
        }
    }
    return gain;
}

void consume(
    Primitive const& shape,
    std::vector<std::uint8_t>& remaining,
    int width,
    int height
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            if (remaining[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                remaining[index] = 0;
            }
        }
    }
}

// La elipse mas grande que cabe centrada sobre `seed` con este giro y esta
// cintura. El eje se camina hacia los dos lados mientras el hueco que queda
// alrededor siga dando para la cintura, y la elipse entra entera en ese pasillo:
// una elipse es la union de los circulos de radio decreciente que se apoyan en su
// eje, asi que si el circulo mas gordo cabe en todo el recorrido, ella tambien.
Primitive stretch(
    Field const& field,
    Point const& seed,
    float angle,
    float minor,
    int color,
    int layer
) {
    float const dirX = std::cos(angle);
    float const dirY = std::sin(angle);
    std::array<float, 2> reach{0.f, 0.f};
    for (int side = 0; side < 2; ++side) {
        float const sign = side ? 1.f : -1.f;
        float walked = 0.f;
        while (walked + kWalk <= kReach &&
               field.at(seed.x + dirX * sign * (walked + kWalk),
                        seed.y + dirY * sign * (walked + kWalk)) >= minor) {
            walked += kWalk;
        }
        reach[static_cast<std::size_t>(side)] = walked;
    }
    float const shift = (reach[1] - reach[0]) * 0.5f;
    float const major = std::max((reach[0] + reach[1]) * 0.5f + minor, minor);
    return ellipse(
        {seed.x + dirX * shift, seed.y + dirY * shift},
        major, minor, angle, color, layer);
}

void pruneCircles(
    std::vector<Primitive>& objects,
    std::vector<std::uint8_t> const& target,
    int width,
    int height
) {
    if (objects.size() <= 1) return;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    std::vector<int> count(cells, 0);

    for (auto const& object : objects) {
        auto const placed = xformOf(object);
        auto const box = xformBox(placed, width, height);
        for (int y = box[1]; y <= box[3]; ++y) {
            for (int x = box[0]; x <= box[2]; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * width + x;
                if (target[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                    ++count[index];
                }
            }
        }
    }

    std::vector<std::size_t> order(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return (objects[left].width * objects[left].height) <
               (objects[right].width * objects[right].height);
    });

    std::vector<std::uint8_t> removed(objects.size(), 0);
    for (std::size_t idx : order) {
        auto const placed = xformOf(objects[idx]);
        auto const box = xformBox(placed, width, height);
        bool canDrop = true;
        for (int y = box[1]; y <= box[3]; ++y) {
            for (int x = box[0]; x <= box[2]; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * width + x;
                if (target[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                    if (count[index] <= 1) {
                        canDrop = false;
                        break;
                    }
                }
            }
            if (!canDrop) break;
        }
        if (canDrop) {
            removed[idx] = 1;
            for (int y = box[1]; y <= box[3]; ++y) {
                for (int x = box[0]; x <= box[2]; ++x) {
                    std::size_t const index = static_cast<std::size_t>(y) * width + x;
                    if (target[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                        --count[index];
                    }
                }
            }
        }
    }

    std::vector<Primitive> kept;
    kept.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (!removed[i]) kept.push_back(objects[i]);
    }
    objects = std::move(kept);
}

} // namespace

std::vector<Primitive> vectorizeCircles(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& empty
) {
    std::vector<Primitive> output;
    if (positions.empty()) return output;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    int const layer = rank * kPaintSublayers;

    std::vector<std::uint8_t> target(cells, 0);
    for (int position : positions) {
        if (position >= 0 && static_cast<std::size_t>(position) < cells) {
            target[static_cast<std::size_t>(position)] = 1;
        }
    }
    // Por donde la elipse puede crecer sin que se vea: sus propias celdas y las
    // que una capa de mas arriba tapa despues. El hueco no entra —ahi crecer solo
    // engorda la silueta— pero se deja pasar el pico de las esquinas.
    std::vector<std::uint8_t> room = target;
    if (blocked.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            room[position] |= blocked[position];
        }
    }
    std::vector<std::uint8_t> permitted = room;
    if (empty.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            permitted[position] |= empty[position];
        }
    }

    Field field;
    field.width = width;
    field.height = height;
    field.clearance = distanceField(room, width, height);
    // La distancia es de centro a centro de celda, y la celda de al lado empieza
    // media antes; sin descontarla, la elipse se sale siempre esa media celda.
    for (auto& value : field.clearance) value = std::max(value - 0.5f, 0.f);

    std::vector<std::uint8_t> remaining = target;
    for (auto const& component : connectedComponents(positions, width, height)) {
        if (component.size() >= 4) {
            auto const box = bounds(component, width);
            float const boxW = static_cast<float>(box[2] - box[0] + 1);
            float const boxH = static_cast<float>(box[3] - box[1] + 1);
            float const aspect = std::max(boxW, boxH) / std::min(boxW, boxH);
            if (boxW >= 3.f && boxH >= 3.f && aspect <= 1.6f) {
                Point const center{
                    (box[0] + box[2] + 1) * 0.5f,
                    (box[1] + box[3] + 1) * 0.5f
                };
                Primitive const wholeCircle = ellipse(
                    center, boxW * 0.5f, boxH * 0.5f, 0.f, color, layer);
                Gain const gain = measure(wholeCircle, remaining, target, width, height);
                if (gain.fresh >= static_cast<int>(component.size() * 0.90f) &&
                    shapeSpill(wholeCircle, permitted, width, height) <= kSpill) {
                    consume(wholeCircle, remaining, width, height);
                    output.push_back(wholeCircle);
                    bool allDone = true;
                    for (int pos : component) {
                        if (remaining[static_cast<std::size_t>(pos)]) {
                            allDone = false;
                            break;
                        }
                    }
                    if (allDone) continue;
                }
            }
        }
        std::vector<int> pending = component;
        while (!pending.empty()) {
            // Se empieza siempre por donde la mancha es mas gorda: ahi cabe la
            // elipse mas grande, y las de despues se van repartiendo lo que ella
            // deja, que es como se pinta a manchas y no celda a celda.
            int seedCell = -1;
            float widest = -1.f;
            std::size_t alive = 0;
            for (int position : pending) {
                if (!remaining[static_cast<std::size_t>(position)]) continue;
                pending[alive++] = position;
                float const space = field.clearance[static_cast<std::size_t>(position)];
                if (space > widest) {
                    widest = space;
                    seedCell = position;
                }
            }
            pending.resize(alive);
            if (seedCell < 0) break;

            Point const seed{
                static_cast<float>(seedCell % width) + 0.5f,
                static_cast<float>(seedCell / width) + 0.5f
            };
            Primitive best;
            Gain bestGain;
            for (int step = 0; step < kAngles; ++step) {
                float const angle = step * kPi / kAngles;
                for (float waist : kWaists) {
                    float const minor = std::max(widest * waist, kMinRadius);
                    auto const candidate = stretch(
                        field, seed, angle, minor, color, layer);
                    auto const gain = measure(
                        candidate, remaining, target, width, height);
                    if (gain.fresh < bestGain.fresh ||
                        (gain.fresh == bestGain.fresh &&
                         gain.repeated >= bestGain.repeated)) {
                        continue;
                    }
                    if (shapeSpill(candidate, permitted, width, height) > kSpill) continue;
                    best = candidate;
                    bestGain = gain;
                }
            }
            if (bestGain.fresh <= 0) {
                // Una celda a la que ninguna elipse llega sin taparle algo a otro
                // color se queda con la suya, del tamano justo para pintarse el
                // centro. Sin esto el bucle no terminaria.
                best = ellipse(seed, kMinRadius, kMinRadius, 0.f, color, layer);
                remaining[static_cast<std::size_t>(seedCell)] = 0;
            }
            // Con el aire justo las de al lado se tocan y el trazo sale continuo,
            // pero solo hasta donde no tape a otro color.
            for (float bleed : kBleeds) {
                Primitive fatter = best;
                fatter.width += bleed * 2.f;
                fatter.height += bleed * 2.f;
                if (shapeSpill(fatter, permitted, width, height) > kSpill) continue;
                best = fatter;
                break;
            }
            consume(best, remaining, width, height);
            output.push_back(best);
        }
    }
    pruneCircles(output, target, width, height);
    return output;
}

} // namespace paimon::gifimport
