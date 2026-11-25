#include "MicroRanker.hpp"

// TU de registro para el glob de CMake (toda la logica es inline en el .hpp
// para que el harness host-side pueda usarla sin compilar nada extra).

namespace paimon::guide {
// ancla de version visible en el binario.
char const* microRankerVersionAnchor() {
    return MicroRanker::weightsVersion();
}
} // namespace paimon::guide
