# Zaid Root Tools

![Zaid Root Tools logo](assets/logo.svg)

Geode mod para **Geometry Dash 2.208 / 2.2081 Android ARM64** con soporte de **root (`su`)** para leer `/dev/input/eventN` y traducir eventos Linux `EV_KEY` a acciones del juego.

## Características actuales

- Sesión restringida con `su` para abrir **solo** el `eventN` elegido.
- Decodificador **nativo** de evdev en C++ (sin depender de `getevent`).
- 8 slots de bindings configurables.
- Acciones soportadas:
  - `JUMP_P1`
  - `JUMP_P2`
  - `JUMP_BOTH`
  - `PAUSE`
  - `RESTART`
  - `FULL_RESTART`
  - `PRACTICE_TOGGLE`
  - `PLACE_CHECKPOINT`
  - `LOAD_LAST_CHECKPOINT`
  - `CLEAR_CHECKPOINTS`
- Indicador de estado en pantalla: `OFF`, `SU...`, `ON`, `ERROR`.

## Seguridad / compatibilidad

- Nunca se pasa una ruta arbitraria ni un comando del usuario a `su`.
- El único dato interpolado al shell es un entero `0..63` para `/dev/input/eventN`.
- El stream root solo vive mientras hay un nivel activo.
- No cambia SELinux, no hace `chmod`, no parchea políticas, y no toca otros nodos.

## Uso

1. Configura el número de `eventN` correcto.
2. Asigna la entrada física y la acción para cada slot.
3. Entra a un nivel.
4. Autoriza `su` cuando el gestor root lo solicite.

## Archivos importantes

- `src/main.cpp` — lógica de bindings y acciones
- `src/RootInputReader.*` — worker root, pipes y sesión `su`
- `src/RawEvdev.*` — parser binario de `struct input_event`
- `assets/logo.svg` / `assets/logo.png` — logo minimalista del proyecto
