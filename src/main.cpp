// main.cpp — Entrypoint minimo.
// La logica de arranque y cierre se ha movido a:
//   - src/core/Bootstrap.cpp      (PaimonOnModLoaded, inicializacion diferida)
//   - src/core/RuntimeLifecycle.cpp ($on_game(Exiting), cleanupDiskCache)
//
// Este archivo se conserva vacio para no romper commits historicos.
// CMakeLists usa GLOB_RECURSE src/*.cpp, asi que los nuevos archivos
// se compilan automaticamente.
