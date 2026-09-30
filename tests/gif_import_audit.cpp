// auditoria objeto por objeto del plan que sale del importador. el banco de al
// lado puntua el dibujo entero; este mira cada objeto y dice que aporta: cuanto
// se ve de el, si lo que pinta ya estaba pintado del mismo color debajo, si con
// el de al lado sale un solo rectangulo, y si su capa lo deja donde toca.
// la marca de agua parte objetos en dos a proposito, asi que aqui se deshace
// antes de medir: contarla como desperdicio tapaba el desperdicio de verdad.
//   g++ -std=c++23 -o2 -o audit tests/gif_import_audit.cpp
//   ./audit <carpeta-o-imagen> [--mode paint|render|art|blocks] [--dim 64]
//           [--colors 16] [--budget 12000] [--top 8] [--dump /tmp/previews]

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "../src/utils/stb_image.h"

#include "../src/features/gif-import/services/GifImportPipeline.hpp"
#include "../src/features/gif-import/services/ColorSpace.cpp"
#include "../src/features/gif-import/services/GifParallel.cpp"
#include "../src/features/gif-import/services/GifShapeRaster.cpp"
#include "../src/features/gif-import/services/GifVectorMath.cpp"
#include "../src/features/gif-import/services/GifArtVectorizer.cpp"
#include "../src/features/gif-import/services/GifCircleVectorizer.cpp"
#include "../src/features/gif-import/services/GifFreeVectorizer.cpp"
#include "../src/features/gif-import/services/GifStampCatalog.cpp"
#include "../src/features/gif-import/services/GifGlowPass.cpp"
#include "../src/features/gif-import/services/GifMotionPlanner.cpp"
#include "../src/features/gif-import/services/GifPaintVectorizer.cpp"
#include "../src/features/gif-import/services/ImageWatermark.cpp"
#include "../src/features/gif-import/services/GifImportPipeline.cpp"

using namespace paimon::gifimport;
namespace fs = std::filesystem;

namespace {

// la rejilla de muestreo con la que se juzga si un objeto aporta. es la misma que
// usa la criba del vectorizador, para que lo que aqui salga sobrando sea de
// verdad algo que la criba dejo pasar y no un desacuerdo de resolucion.
constexpr int kAuditScale = 8;

void writePreview(
    fs::path const& path,
    std::vector<std::uint8_t> const& rgba,
    int width,
    int height
) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return;
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t index = 0; index + 3 < rgba.size(); index += 4) {
        // transparent pixels are exported as black so imagemagick and simple
        // viewers can inspect the geometry without needing an alpha-aware ui.
        auto const alpha = rgba[index + 3] / 255.f;
        file.put(static_cast<char>(rgba[index] * alpha));
        file.put(static_cast<char>(rgba[index + 1] * alpha));
        file.put(static_cast<char>(rgba[index + 2] * alpha));
    }
}

struct Loaded {
    SourceAnimation source;
    bool ok = false;
};

Loaded loadAnimation(fs::path const& path) {
    Loaded loaded;
    if (path.extension() == ".gif") {
        auto* file = std::fopen(path.string().c_str(), "rb");
        if (!file) return loaded;
        std::fseek(file, 0, SEEK_END);
        auto const size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(size));
        std::fread(raw.data(), 1, raw.size(), file);
        std::fclose(file);

        int* delays = nullptr;
        int frames = 0;
        int channels = 0;
        std::uint8_t* pixels = stbi_load_gif_from_memory(
            raw.data(), static_cast<int>(raw.size()), &delays,
            &loaded.source.width, &loaded.source.height, &frames, &channels, 4);
        if (!pixels) return loaded;
        std::size_t const stride =
            static_cast<std::size_t>(loaded.source.width) * loaded.source.height * 4;
        for (int frame = 0; frame < frames; ++frame) {
            SourceFrame entry;
            entry.delayMs = delays && delays[frame] > 0 ? delays[frame] : 100;
            entry.rgba.assign(pixels + frame * stride, pixels + (frame + 1) * stride);
            loaded.source.frames.push_back(std::move(entry));
        }
        stbi_image_free(pixels);
        std::free(delays);
        loaded.ok = frames > 0;
        return loaded;
    }

    int channels = 0;
    std::uint8_t* pixels = stbi_load(
        path.string().c_str(), &loaded.source.width, &loaded.source.height, &channels, 4);
    if (!pixels) return loaded;
    loaded.source.frames.resize(1);
    loaded.source.frames.front().rgba.assign(
        pixels,
        pixels + static_cast<std::size_t>(loaded.source.width) * loaded.source.height * 4);
    stbi_image_free(pixels);
    loaded.ok = true;
    return loaded;
}

bool sameFloat(float left, float right, float tolerance = 0.002f) {
    return std::abs(left - right) <= tolerance;
}

// deshace el marcado: las vueltas de mas del giro y los pares partidos por
// `splitprimitive`. lo que queda es el plan que la geometria produjo de verdad,
// que es lo unico que tiene sentido auditar.
std::size_t undoWatermark(std::vector<Primitive>& objects) {
    for (auto& object : objects) {
        object.rotation = std::fmod(object.rotation, 360.f);
        if (object.rotation < 0.f) object.rotation += 360.f;
        if (object.rotation >= 359.999f) object.rotation = 0.f;
    }

    std::size_t merged = 0;
    std::vector<Primitive> joined;
    joined.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        if (index + 1 < objects.size()) {
            auto const& first = objects[index];
            auto const& second = objects[index + 1];
            bool const pairable = first.color == second.color &&
                first.kind == second.kind && first.layer == second.layer &&
                sameFloat(first.rotation, second.rotation, 0.01f);
            if (pairable) {
                float const radians = first.rotation * paimon::gifimport::kPi / 180.f;
                float const cosine = std::cos(radians);
                float const sine = std::sin(radians);
                float const localX =
                    (second.x - first.x) * cosine + (second.y - first.y) * sine;
                float const localY =
                    -(second.x - first.x) * sine + (second.y - first.y) * cosine;
                bool const alongWidth = sameFloat(first.height, second.height) &&
                    sameFloat(std::abs(localX), (first.width + second.width) * 0.5f) &&
                    sameFloat(localY, 0.f);
                bool const alongHeight = sameFloat(first.width, second.width) &&
                    sameFloat(std::abs(localY), (first.height + second.height) * 0.5f) &&
                    sameFloat(localX, 0.f);
                if (alongWidth || alongHeight) {
                    auto whole = first;
                    if (alongWidth) {
                        whole.width = first.width + second.width;
                        whole.x = first.x + localX * 0.5f * cosine;
                        whole.y = first.y + localX * 0.5f * sine;
                    } else {
                        whole.height = first.height + second.height;
                        whole.x = first.x - localY * 0.5f * sine;
                        whole.y = first.y + localY * 0.5f * cosine;
                    }
                    joined.push_back(whole);
                    ++merged;
                    ++index;
                    continue;
                }
            }
        }
        joined.push_back(objects[index]);
    }
    objects = std::move(joined);
    return merged;
}

struct Composite {
    std::vector<std::int32_t> top;
    std::vector<std::int32_t> under;
    int width = 0;
    int height = 0;
};

// pinta de abajo arriba guardando quien manda en cada muestra y quien mandaba
// justo antes. con esas dos capas se responde lo unico que importa de un objeto:
// que se veria si no estuviera.
Composite composite(std::vector<Primitive> const& objects, int width, int height) {
    Composite result;
    result.width = width * kAuditScale;
    result.height = height * kAuditScale;
    std::size_t const samples =
        static_cast<std::size_t>(result.width) * result.height;
    result.top.assign(samples, -1);
    result.under.assign(samples, -1);

    // la lista del plan deja de ir ordenada por capa en cuanto absorbpaintrects
    // saca las fusiones al principio; el juego dibuja por capa, asi que aqui hay
    // que ordenar igual o el de abajo acaba contado como el de arriba.
    std::vector<std::size_t> order(objects.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return objects[left].layer < objects[right].layer;
    });

    for (std::size_t index : order) {
        auto const& object = objects[index];
        auto const placed = xformOf(object);
        auto const box = xformBox(placed, width, height);
        for (int y = box[1] * kAuditScale; y < (box[3] + 1) * kAuditScale; ++y) {
            for (int x = box[0] * kAuditScale; x < (box[2] + 1) * kAuditScale; ++x) {
                if (!placed.contains(
                        (x + 0.5f) / kAuditScale, (y + 0.5f) / kAuditScale)) {
                    continue;
                }
                auto const sample = static_cast<std::size_t>(y) * result.width + x;
                result.under[sample] = result.top[sample];
                result.top[sample] = static_cast<std::int32_t>(index);
            }
        }
    }
    return result;
}

struct ObjectAudit {
    std::size_t visible = 0;
    bool dead = false;
    bool repeats = false;
    bool sliver = false;
};

struct PlanAudit {
    std::size_t objects = 0;
    std::size_t watermark = 0;
    std::size_t dead = 0;
    std::size_t repeats = 0;
    std::size_t slivers = 0;
    std::size_t rotatedSpikes = 0;
    std::size_t mergeable = 0;
    std::size_t absorbable = 0;
    std::size_t layers = 0;
    std::size_t ties = 0;
    std::size_t inversions = 0;
    std::size_t painted = 0;
    std::size_t bySublayer[kPaintSublayers]{};
    std::size_t byKind[kPrimitiveKinds]{};
    std::vector<ObjectAudit> perObject;
};

// dos rectangulos rectos del mismo color pegados por un lado entero son uno
// solo. no mira la capa: entre objetos del mismo color el orden no cambia nada.
bool joinable(Primitive const& first, Primitive const& second) {
    if (first.color != second.color) return false;
    if (first.kind == PrimitiveKind::Circle || second.kind == PrimitiveKind::Circle) {
        return false;
    }
    if (first.kind == PrimitiveKind::Triangle || first.kind == PrimitiveKind::WideTriangle ||
        second.kind == PrimitiveKind::Triangle ||
        second.kind == PrimitiveKind::WideTriangle) {
        return false;
    }
    float difference = std::fmod(std::abs(first.rotation - second.rotation), 180.f);
    difference = std::min(difference, 180.f - difference);
    if (difference > 0.05f) return false;

    float const radians = first.rotation * paimon::gifimport::kPi / 180.f;
    float const cosine = std::cos(radians);
    float const sine = std::sin(radians);
    float const localX = (second.x - first.x) * cosine + (second.y - first.y) * sine;
    float const localY = -(second.x - first.x) * sine + (second.y - first.y) * cosine;
    bool const alongWidth = sameFloat(first.height, second.height, 0.01f) &&
        sameFloat(std::abs(localX), (first.width + second.width) * 0.5f, 0.01f) &&
        sameFloat(localY, 0.f, 0.01f);
    bool const alongHeight = sameFloat(first.width, second.width, 0.01f) &&
        sameFloat(std::abs(localY), (first.height + second.height) * 0.5f, 0.01f) &&
        sameFloat(localX, 0.f, 0.01f);
    return alongWidth || alongHeight;
}

// solape de verdad, no de cajas: dos rectangulos pegados comparten la celda del
// borde y por caja parecen pisarse, pero ahi no hay nada que ordenar.
bool shapesOverlap(Primitive const& first, Primitive const& second, int width, int height) {
    auto const a = shapeBox(first, width, height);
    auto const b = shapeBox(second, width, height);
    int const minX = std::max(a[0], b[0]);
    int const minY = std::max(a[1], b[1]);
    int const maxX = std::min(a[2], b[2]);
    int const maxY = std::min(a[3], b[3]);
    if (minX > maxX || minY > maxY) return false;

    for (int y = minY * kAuditScale; y < (maxY + 1) * kAuditScale; ++y) {
        for (int x = minX * kAuditScale; x < (maxX + 1) * kAuditScale; ++x) {
            float const sampleX = (x + 0.5f) / kAuditScale;
            float const sampleY = (y + 0.5f) / kAuditScale;
            if (xformOf(first).contains(sampleX, sampleY) &&
                xformOf(second).contains(sampleX, sampleY)) {
                return true;
            }
        }
    }
    return false;
}

// prueba de fusion fuerte: `joinable` solo ve dos rectangulos calcados pegados
// por un lado, y eso casi nunca pasa. lo que de verdad sobra es la pareja cuya
// caja comun se puede pintar entera sin ensuciar nada, porque ahi los dos son un
// rectangulo mas grande aunque no midan lo mismo ni se toquen.
bool absorbable(
    Primitive const& first,
    Primitive const& second,
    std::vector<std::uint8_t> const& spare,
    int width,
    int height
) {
    if (first.color != second.color) return false;
    if (first.kind != PrimitiveKind::Block && first.kind != PrimitiveKind::Stroke) {
        return false;
    }
    if (second.kind != PrimitiveKind::Block && second.kind != PrimitiveKind::Stroke) {
        return false;
    }
    if (std::abs(first.rotation) > 0.01f || std::abs(second.rotation) > 0.01f) return false;

    float const minX = std::min(first.x - first.width * 0.5f, second.x - second.width * 0.5f);
    float const maxX = std::max(first.x + first.width * 0.5f, second.x + second.width * 0.5f);
    float const minY = std::min(first.y - first.height * 0.5f, second.y - second.height * 0.5f);
    float const maxY = std::max(first.y + first.height * 0.5f, second.y + second.height * 0.5f);
    // una caja mucho mas grande que lo que los dos ocupan no es una fusion, es
    // pintar de mas: solo cuenta si el hueco que se traga es pequeno.
    float const united = (maxX - minX) * (maxY - minY);
    float const owned = first.width * first.height + second.width * second.height;
    if (united > owned * 1.35f) return false;

    for (int y = static_cast<int>(std::floor(minY)); y < static_cast<int>(std::ceil(maxY)); ++y) {
        for (int x = static_cast<int>(std::floor(minX)); x < static_cast<int>(std::ceil(maxX)); ++x) {
            if (x < 0 || y < 0 || x >= width || y >= height) return false;
            if (static_cast<float>(x) + 0.5f < minX || static_cast<float>(x) + 0.5f > maxX) continue;
            if (static_cast<float>(y) + 0.5f < minY || static_cast<float>(y) + 0.5f > maxY) continue;
            if (!spare[static_cast<std::size_t>(y) * width + x]) return false;
        }
    }
    return true;
}

PlanAudit audit(
    ImportPlan const& plan,
    std::vector<Primitive> const& objects,
    std::vector<std::vector<std::uint8_t>> const& spare
) {
    PlanAudit report;
    report.objects = objects.size();

    auto const view = composite(objects, plan.width, plan.height);
    report.perObject.assign(objects.size(), {});
    for (std::size_t sample = 0; sample < view.top.size(); ++sample) {
        auto const owner = view.top[sample];
        if (owner < 0) continue;
        ++report.painted;
        ++report.perObject[static_cast<std::size_t>(owner)].visible;
    }

    // un objeto repite cuando en todo lo que se ve de el ya habia debajo su mismo
    // color: quitarlo deja el dibujo identico y se lleva un objeto del nivel.
    std::vector<std::uint8_t> onlyRepeat(objects.size(), 1);
    for (std::size_t sample = 0; sample < view.top.size(); ++sample) {
        auto const owner = view.top[sample];
        if (owner < 0) continue;
        auto const below = view.under[sample];
        if (below < 0 ||
            objects[static_cast<std::size_t>(below)].color !=
                objects[static_cast<std::size_t>(owner)].color) {
            onlyRepeat[static_cast<std::size_t>(owner)] = 0;
        }
    }

    float const cell = static_cast<float>(kAuditScale) * kAuditScale;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto& entry = report.perObject[index];
        entry.dead = entry.visible == 0;
        entry.repeats = !entry.dead && onlyRepeat[index] != 0;
        entry.sliver = !entry.dead && static_cast<float>(entry.visible) < cell * 0.5f;
        report.dead += entry.dead;
        report.repeats += entry.repeats;
        report.slivers += entry.sliver;
        auto const& object = objects[index];
        float folded = std::fmod(std::abs(object.rotation), 90.f);
        folded = std::min(folded, 90.f - folded);
        report.rotatedSpikes += folded > 5.f && folded < 85.f &&
            object.width <= 1.6f && object.height <= 1.6f;
    }

    for (std::size_t i = 0; i < objects.size(); ++i) {
        for (std::size_t j = i + 1; j < objects.size(); ++j) {
            if (joinable(objects[i], objects[j])) ++report.mergeable;
        }
    }

    // cada objeto solo se puede fundir una vez, asi que se cuentan fusiones y no
    // parejas: un rectangulo que casa con cuatro vecinos ahorra uno, no cuatro.
    std::vector<std::uint8_t> fused(objects.size(), 0);
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (fused[i]) continue;
        auto const color = static_cast<std::size_t>(objects[i].color);
        if (color >= spare.size()) continue;
        for (std::size_t j = i + 1; j < objects.size(); ++j) {
            if (fused[j]) continue;
            if (!absorbable(objects[i], objects[j], spare[color], plan.width, plan.height)) {
                continue;
            }
            fused[i] = 1;
            fused[j] = 1;
            ++report.absorbable;
            break;
        }
    }

    for (auto const& object : objects) {
        ++report.bySublayer[
            static_cast<std::size_t>(((object.layer % kPaintSublayers) + kPaintSublayers) %
                                     kPaintSublayers)];
        ++report.byKind[static_cast<std::size_t>(object.kind)];
    }

    std::map<int, int> layerUse;
    for (auto const& object : objects) ++layerUse[object.layer];
    report.layers = layerUse.size();

    // empate de capa entre colores distintos que se pisan: el orden 25 no los
    // separa, asi que el juego elige y la previsualizacion no manda.
    for (std::size_t i = 0; i < objects.size(); ++i) {
        for (std::size_t j = i + 1; j < objects.size(); ++j) {
            if (objects[i].layer != objects[j].layer) continue;
            if (objects[i].color == objects[j].color) continue;
            if (!shapesOverlap(objects[i], objects[j], plan.width, plan.height)) continue;
            ++report.ties;
        }
    }

    // inversion: donde manda un color que no es el de la rejilla habiendo debajo
    // un objeto con el color bueno. ahi el dibujo esta, pero la capa lo entierra.
    auto const& cells = plan.frames.front().cells;
    for (std::size_t sample = 0; sample < view.top.size(); ++sample) {
        auto const owner = view.top[sample];
        if (owner < 0) continue;
        int const x = static_cast<int>(sample % view.width) / kAuditScale;
        int const y = static_cast<int>(sample / view.width) / kAuditScale;
        int const wanted = cells[static_cast<std::size_t>(y) * plan.width + x];
        if (wanted < 0) continue;
        if (objects[static_cast<std::size_t>(owner)].color == wanted) continue;
        auto const below = view.under[sample];
        if (below >= 0 && objects[static_cast<std::size_t>(below)].color == wanted) {
            ++report.inversions;
        }
    }
    return report;
}

char const* kindName(PrimitiveKind kind) {
    switch (kind) {
        case PrimitiveKind::Block: return "bloque";
        case PrimitiveKind::Stroke: return "tira";
        case PrimitiveKind::Circle: return "circulo";
        case PrimitiveKind::Triangle: return "triangulo";
        case PrimitiveKind::WideTriangle: return "triangulo-ancho";
        case PrimitiveKind::Glow: return "glow";
        case PrimitiveKind::Stamp: return "molde";
    }
    return "?";
}

ImportMode parseMode(std::string const& name) {
    if (name == "art") return ImportMode::Art;
    if (name == "blocks") return ImportMode::Blocks;
    if (name == "render") return ImportMode::Render;
    if (name == "free") return ImportMode::Free;
    if (name == "circles") return ImportMode::Circles;
    return ImportMode::Paint;
}

char const* modeName(ImportMode mode) {
    switch (mode) {
        case ImportMode::Blocks: return "blocks";
        case ImportMode::Art: return "art";
        case ImportMode::Paint: return "paint";
        case ImportMode::Render: return "render";
        case ImportMode::Free: return "free";
        case ImportMode::Circles: return "circles";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: audit <carpeta-o-imagen> [--mode paint] [--dim 64]"
                     " [--colors 16] [--budget 12000] [--top 8]\n";
        return 2;
    }

    fs::path const target = argv[1];
    Options options;
    options.mode = ImportMode::Paint;
    options.maxDimension = 64;
    options.maxColors = 16;
    options.objectBudget = 12000;
    options.background = BackgroundMode::Keep;
    options.sampling = SamplingMode::Smooth;
    int top = 0;
    fs::path dumpDir;

    for (int i = 2; i + 1 < argc; i += 2) {
        std::string const key = argv[i];
        std::string const value = argv[i + 1];
        if (key == "--mode") options.mode = parseMode(value);
        else if (key == "--dim") options.maxDimension = std::stoi(value);
        else if (key == "--colors") options.maxColors = std::stoi(value);
        else if (key == "--budget") options.objectBudget = std::stoi(value);
        else if (key == "--top") top = std::stoi(value);
        else if (key == "--dump") dumpDir = value;
    }

    std::vector<fs::path> inputs;
    if (fs::is_directory(target)) {
        for (auto const& entry : fs::directory_iterator(target)) {
            auto const extension = entry.path().extension().string();
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                extension == ".bmp" || extension == ".tga" || extension == ".gif") {
                inputs.push_back(entry.path());
            }
        }
        std::sort(inputs.begin(), inputs.end());
    } else {
        inputs.push_back(target);
    }
    if (inputs.empty()) {
        std::cerr << "no hay imagenes en " << target << '\n';
        return 2;
    }

    std::cout << "modo=" << modeName(options.mode) << " dim=" << options.maxDimension
              << " colores=" << options.maxColors << "\n\n";
    std::cout << std::left << std::setw(24) << "imagen"
              << std::right << std::setw(9) << "objetos"
              << std::setw(9) << "calidad"
              << std::setw(9) << "geometr."
              << std::setw(8) << "marca"
              << std::setw(9) << "muertos"
              << std::setw(9) << "repite"
              << std::setw(9) << "astilla"
              << std::setw(8) << "picos"
              << std::setw(9) << "fusion"
              << std::setw(10) << "absorbe"
              << std::setw(10) << "relleno"
              << std::setw(8) << "trazo"
              << std::setw(9) << "parche"
              << std::setw(8) << "capas"
              << std::setw(9) << "empate"
              << std::setw(10) << "invers." << '\n';

    std::size_t totals[12]{};
    std::size_t totalSpikes = 0;
    std::size_t kinds[kPrimitiveKinds]{};
    double totalDensity = 0.0;
    int counted = 0;

    for (auto const& input : inputs) {
        auto const loaded = loadAnimation(input);
        if (!loaded.ok) {
            std::cout << std::left << std::setw(24) << input.filename().string()
                      << "  no se pudo leer\n";
            continue;
        }
        auto const result = buildPlan(loaded.source, options);
        if (!result) {
            std::cout << std::left << std::setw(24) << input.filename().string()
                      << "  " << result.error << '\n';
            continue;
        }
        auto const& plan = result.plan;
        if (!dumpDir.empty()) {
            std::error_code error;
            fs::create_directories(dumpDir, error);
            auto const preview = renderPlanFrame(plan, 0, 8);
            writePreview(
                dumpDir / (input.stem().string() + "_preview.ppm"),
                preview, plan.width * 8, plan.height * 8);
        }

        std::vector<Primitive> objects = plan.staticObjects;
        for (auto const& track : plan.tracks) {
            if (track.mask.empty() || (track.mask.front() & 1u) != 0) {
                objects.insert(objects.end(), track.objects.begin(), track.objects.end());
            }
        }
        std::stable_sort(objects.begin(), objects.end(),
                         [](Primitive const& left, Primitive const& right) {
                             return left.layer < right.layer;
                         });
        std::size_t const before = objects.size();
        std::size_t const rejoined = undoWatermark(objects);

        // por donde un rectangulo de cada color puede crecer sin cambiar el
        // dibujo: sus propias celdas y las que otro color tapa despues. el hueco
        // vacio queda fuera a proposito, que ahi crecer engorda la silueta.
        auto const ranks = paintOrder(
            plan.frames, static_cast<int>(plan.palette.size()), plan.width, plan.height);
        auto spare = paintObstacles(
            plan.frames, ranks, static_cast<int>(plan.palette.size()),
            plan.width * plan.height);
        for (std::size_t color = 0; color < spare.size(); ++color) {
            for (std::size_t cell = 0; cell < spare[color].size(); ++cell) {
                if (plan.frames.front().cells[cell] == static_cast<std::int32_t>(color)) {
                    spare[color][cell] = 1;
                }
            }
        }

        auto const report = audit(plan, objects, spare);
        double const density = report.painted > 0
            ? static_cast<double>(report.objects) /
                  (static_cast<double>(report.painted) / (kAuditScale * kAuditScale))
            : 0.0;

        std::cout << std::left << std::setw(24) << input.filename().string()
                  << std::right << std::setw(9) << before
                  << std::setw(9) << std::fixed << std::setprecision(1)
                  << plan.similarity
                  << std::setw(9) << plan.geometrySimilarity
                  << std::setw(8) << rejoined
                  << std::setw(9) << report.dead
                  << std::setw(9) << report.repeats
                  << std::setw(9) << report.slivers
                  << std::setw(8) << report.rotatedSpikes
                  << std::setw(9) << report.mergeable
                  << std::setw(10) << report.absorbable
                  << std::setw(10) << report.bySublayer[0]
                  << std::setw(8) << report.bySublayer[1]
                  << std::setw(9) << report.bySublayer[2]
                  << std::setw(8) << report.layers
                  << std::setw(9) << report.ties
                  << std::setw(10) << report.inversions << '\n';

        totals[0] += before;
        totals[1] += rejoined;
        totals[2] += report.dead;
        totals[3] += report.repeats;
        totals[4] += report.slivers;
        totals[5] += report.mergeable;
        totals[6] += report.ties;
        totals[7] += report.inversions;
        totals[8] += report.absorbable;
        totals[9] += report.bySublayer[0];
        totals[10] += report.bySublayer[1];
        totals[11] += report.bySublayer[2];
        totalSpikes += report.rotatedSpikes;
        for (std::size_t kind = 0; kind < kPrimitiveKinds; ++kind) {
            kinds[kind] += report.byKind[kind];
        }
        totalDensity += density;
        ++counted;

        if (top > 0) {
            std::vector<std::size_t> order(objects.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
                return report.perObject[left].visible < report.perObject[right].visible;
            });
            int shown = 0;
            for (std::size_t index : order) {
                if (shown >= top) break;
                auto const& entry = report.perObject[index];
                if (!entry.dead && !entry.repeats && !entry.sliver) break;
                auto const& object = objects[index];
                std::cout << "    " << std::left << std::setw(16) << kindName(object.kind)
                          << " capa " << std::right << std::setw(3) << object.layer
                          << "  color " << std::setw(2) << object.color
                          << "  " << std::setprecision(2) << object.width << "x"
                          << object.height
                          << "  se ve " << std::setprecision(2)
                          << static_cast<float>(entry.visible) / (kAuditScale * kAuditScale)
                          << " celdas"
                          << (entry.dead ? "  [muerto]" : "")
                          << (entry.repeats ? "  [repite]" : "")
                          << (entry.sliver ? "  [astilla]" : "") << '\n';
                ++shown;
            }
        }
    }

    if (counted > 0) {
        std::cout << "\ntotales: objetos=" << totals[0]
                  << " marca=" << totals[1]
                  << " muertos=" << totals[2]
                  << " repite=" << totals[3]
                  << " astillas=" << totals[4]
                  << " picos=" << totalSpikes
                  << " fusionables=" << totals[5]
                  << " absorbibles=" << totals[8]
                  << " empates=" << totals[6]
                  << " inversiones=" << totals[7]
                  << " obj/celda=" << std::setprecision(3) << totalDensity / counted << '\n';
        std::cout << "reparto: relleno=" << totals[9] << " trazo=" << totals[10]
                  << " parche=" << totals[11]
                  << "   bloques=" << kinds[0] << " tiras=" << kinds[1]
                  << " circulos=" << kinds[2]
                  << " triangulos=" << kinds[3] + kinds[4]
                  << " moldes=" << kinds[6] << '\n';
    }
    return 0;
}
