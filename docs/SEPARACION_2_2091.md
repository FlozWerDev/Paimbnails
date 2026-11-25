# Separación de la variante GD 2.2091

Fecha: 2026-10-20.

> Documento histórico. El 2026-10-31 este árbol se migró al SDK público nightly
> para GD 2.2091. El estado actual se describe en la
> [revisión del SDK](REVISION_SDK_NIGHTLY_2026-10-31.md).

## Resultado

Este árbol (`Paimbnails-main`) vuelve a ser el proyecto **Geometry Dash
2.2081**. La variante nativa de Windows **2.209 / 2.2091** se trasladó a un
proyecto independiente:

```
F:\code\Paimbnails-2.2091
```

Los dos árboles comparten el código del mod, pero ya no comparten manifiesto,
SDK, bindings, layouts, scripts de compilación, workflow ni artefactos.

| Elemento | Paimbnails-main (2.2081) | Paimbnails-2.2091 |
| --- | --- | --- |
| `mod.json` → `gd.win` | `2.2081` | `2.2091` |
| SDK de Geode | público 5.11.2 | privado `compat/gd-2.2091` |
| Bindings | revisión `1012fcb3` | revisión `5b5f1062` + parche |
| Build Windows | `build/`, `build-clang/` | `build-gd-2.2091/` |
| Piloto standalone | no incluido | incluido |

## Qué se movió a `Paimbnails-2.2091`

- Los 105 archivos de la migración nativa, incluido
  `src/core/GD2091LayoutChecks.cpp`.
- `compat/gd-2.2091/`: parches de bindings, SDK, node-ids y simdutf, revisiones
  fijadas y bindings nativos verificados.
- `cmake/PaimGeodeDependencyCompat.cmake` y las comprobaciones de CMake que
  exigían el SDK de 2.2091.
- `tools/build-gd-2.2091.ps1` y `tools/prepare-gd-2.2091-toolchain.ps1`.
- El piloto standalone: `src/platform/standalone/`, `src/platform/PaimHook.hpp`,
  `cmake/PaimStandalone.cmake`, `tools/inject/`, `tools/hook_parity_check.py` y
  sus tests (`test_settings_store`, `test_minhook_backend`,
  `test_paimhook_macro`, `test_gamebuild`, `test_patternscan`).
- La documentación específica: `ARRANQUE_GD_2_209.md`,
  `COMPILACION_GD_2_2091.md`, `CRASHES_GD_2_2091.md` y
  `REVISION_DLL_GD_2_2091.md`.
- Los artefactos `build-gd-2.2091`, `build-hybrid-audit`,
  `build-clang-std` y `build-clang-std-sanitizers`.
- El workflow de CI con SDK privado y bindings parcheados.

La raíz se reconstruyó desde
`build-gd-2.2091/project-before-gd-2.2091` (copia conservada dentro del
proyecto 2.2091). Se verificaron los 97 archivos restaurados contra sus hashes
SHA-256 previos a la migración. Después se quitó de `CMakeLists.txt` el bloque
del piloto standalone; esa es la única diferencia intencional respecto a la
copia de referencia.

## Cómo compilar cada árbol

### 2.2081 (este árbol)

Requisitos: Geode SDK 5.11.2 público, CMake, Ninja, clang-cl y lld-link.

```powershell
# GEODE_SDK debe apuntar al SDK público 5.11.2
.\compilar-desde-cero.cmd
```

También se puede configurar manualmente:

```powershell
cmake -S . -B build-clang -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_LINKER=lld-link -DGEODE_DONT_INSTALL_MODS=ON
cmake --build build-clang --target Paimbnails_PACKAGE --parallel
```

`CMakeLists.txt` pide Geode 5.11.2 y fija los bindings de 2.2081. No hay
comprobaciones de 2.2091 ni dependencias del SDK privado.

### 2.2091 (`Paimbnails-2.2091`)

```powershell
cd F:\code\Paimbnails-2.2091
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build-gd-2.2091.ps1
```

Ese script prepara la toolchain con `compat/gd-2.2091/toolchain.json`, aplica
los parches y compila con el SDK privado. Para comprobar el estado sin compilar:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\prepare-gd-2.2091-toolchain.ps1 -VerifyOnly
```

## Regla para no volver a mezclarlos

- `Paimbnails-main` no debe contener `compat/gd-2.2091`, el SDK privado ni
  direcciones nativas de 2.2091.
- `Paimbnails-2.2091` no debe declarar `"win": "2.2081"` ni usar el SDK
  público para Windows.
- Un cambio compartido se aplica primero en un árbol y se traslada al otro con
  un diff explícito; no se copia el árbol completo, porque los manifiestos y
  los bindings son incompatibles.
- Los artefactos de compilación no se copian entre árboles: cada uno tiene su
  propia caché y sus propias rutas absolutas.
