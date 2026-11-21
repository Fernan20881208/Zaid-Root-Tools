# Zaid Root Tools · Root Input Mapper v0.1 (experimental)

Proyecto **de código fuente**, para Geometry Dash **2.2081** en Android **ARM64** con **Geode 5.11.2**. **No contiene un `.geode` compilado**: hace falta el SDK Geode y Android NDK. Se incluyen pruebas de host del parser binario y un workflow de GitHub Actions que genera el `.geode`.

## Funciones de esta versión

- Acceso root real a través de `su -c` (KernelSU, Magisk, APatch si exponen `su` al proceso de Geode).
- Lectura en tiempo real mediante **lector C++ integrado de eventos binarios de `/dev/input/eventN`** del **único dispositivo elegido**.
- Salto de jugador 1 y jugador 2 con eventos **pulsar/soltar**; repeticiones automáticas descartadas.
- El usuario elige el evento N y los códigos de los botones en **Geode → Mods → Zaid Root Tools → Settings**.
- Estado visible dentro del nivel: OFF, SU..., ON, ERROR.
- No se abre teclado completo ni se hace `EVIOCGRAB`; no se alteran permisos, SELinux, kernel, particiones ni otros procesos.
- Se cierran las tuberías hacia root al desactivar el mod o **salir del nivel**; el shell root ejecuta su propia limpieza del `getevent` (solo para identificar el eventN; **no se usa dentro del mod**).
- Lectura en hilo secundario; interacción con el juego **únicamente** mediante `Loader::queueInMainThread`.

## Compilar en una PC con Android NDK

1. Instala **Geode CLI**, **SDK v5.11.2**, **Android NDK**, Ninja y CMake.
2. Instala binarios de Geode Android64:
   ```bash
   geode sdk install-binaries -p android64
   ```
3. Dentro de la raíz del proyecto:
   ```bash
   geode build -p android64
   ```
4. Instala `zaid.root-input-mapper.geode` desde el juego o copia el `.geode` generado desde `build-android64` a:
   ```text
   /storage/emulated/0/Android/media/com.geode.launcher/game/geode/mods/
   ```

> Para compilar en GitHub, crea un repositorio y sube esta carpeta conservando `.github/workflows/build-android64.yml`. En **Actions → Build Zaid Root Tools Android64 → Run workflow**, descarga el archivo `ZaidRootTools-android64` que contendrá el `.geode`. El workflow utiliza el action oficial `geode-sdk/build-geode-mod` y SDK **5.11.2**, no la rama v6 alpha.

## Configurar el dispositivo de entrada

Desde **Termux** (con root configurado para su aplicación):

```sh
su -c '/system/bin/getevent -il'
```

Identifica el `eventN` correspondiente al botón físico (por ejemplo, `/dev/input/event4`). Puedes comprobar voluntariamente el dispositivo concreto con:

```sh
su -c '/system/bin/getevent -lt /dev/input/event4'
```

Detén con `Ctrl+C`. No utilices el dispositivo de pantalla táctil ni el teclado completo. Los números `eventN` **cambian entre reinicios, actualizaciones y periféricos**; vuelve a identificarlos al conectar un mando.

En Geode cambia `Numero de /dev/input/eventN` a N, configura los botones P1/P2 y finalmente activa **Activar mapper con root**. Si el gestor root pide autorización para Geode, apruébala conscientemente. El valor inicial es **-1** y el mapper viene apagado. **Abre un nivel para que se inicie la sesión root.**

### Códigos evdev soportados

| Opción | Código Linux | Hardware típico |
| --- | ---: | --- |
| `VOL_UP` | 115 | Volumen arriba |
| `VOL_DOWN` | 114 | Volumen abajo |
| `BTN_SOUTH` | 304 | Botón inferior de un mando |
| `BTN_EAST` | 305 | Botón derecho |
| `BTN_NORTH` | 307 | Botón superior |
| `BTN_WEST` | 308 | Botón izquierdo |
| `BTN_TL` | 310 | Botón superior izquierdo |
| `BTN_TR` | 311 | Botón superior derecho |
| `BTN_SELECT` | 314 | Select/Back |
| `BTN_START` | 315 | Start/Menu |

**Nota:** El botón de volumen seguirá realizando su función de Android si el sistema también procesa el evento: esta versión **duplica** el evento para el juego, sin capturarlo en exclusiva. Para dos botones en **distintos** nodos `eventN`, haría falta ampliar el lector a dos dispositivos en una versión posterior.

## Seguridad y límites

- El proceso **no solicita root** hasta que el usuario active el mapper, haya seleccionado un `eventN` válido y **entre a un nivel**. Fuera de niveles no existe una sesión root del mod.
- No acepta comandos shell ni rutas arbitrarias como ajustes: al shell se interpola exclusivamente un número `eventN` verificado (`0..63`).
- No mantiene un proceso servidor privilegiado, ni escucha por red, ni instala ningún binario descargable como root.
- No registra eventos de teclados ni pantalla; únicamente reenvía las dos teclas permitidas, y únicamente cuando hay un nivel abierto y no pausado.
- No manipula la política SELinux ni permisos de `/dev/input`.
- La aprobación root es necesaria **en el gestor root de la aplicación que ejecuta Geode**. Un `su` funcional en Termux no garantiza automáticamente uno funcional en Geode.
- `ROOT INPUT: ERROR` puede indicar denegación de root, nombre equivocado, versión incompatible, SELinux o falta de acceso al dispositivo. Apaga y enciende el mapper para reintentar.
- Por seguridad, el shell privilegiado solo ejecuta comandos de sistema que se incluyen como literales en el código (`test`, `cat`, `kill`, `wait`), y no evalúa entradas del usuario. El proceso hijo se cierra al recibir EOF en stdin.
- Compatibilidad únicamente **2.2081 / Geode 5.11.2 / arm64-v8a**. La versión 2.208 sin el último dígito **no está probada**. No utilizar con Geode v6 ni Geometry Dash 2.209.x sin portar y recompilar.
- Si se desconecta un dispositivo durante una sesión, algunos controladores cierran el descriptor de forma inmediata y otros no; desactiva y reactiva el mapper y verifica que no quede una tecla retenida.
- No se ha realizado prueba de ejecución con un teléfono real, por lo que la autorización de `su`, el hook y el orden de eventos deben validarse en tu hardware.

## Verificar parser sin Android / Geode

```bash
cmake -S tests -B build-tests -G Ninja
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Diseño

```text
mod.json (settings GUI, OFF por defecto)
    ↓
RootInputReader.cpp (worker C++23)
    ├─ fork + execvp("su", "-c", comando fijo)
    ├─ getevent -q /dev/input/eventN (Android system, root)
    ├─ stdout → parser EV_KEY en InputParser.cpp
    └─ queueInMainThread → main.cpp → PlayLayer::handleButton

Salir del nivel / cambiar ajustes → invalida generation → libera botones → cierra stdin
                                              ↓
                        sh root lee EOF y mata getevent
```

## Archivos

- `src/main.cpp`: integración con Geode, ajustes, hooks de PlayLayer y HUD.
- `src/RootInputReader.cpp`: ciclo root y tuberías IPC controladas.
- `src/InputParser.cpp`: parser de eventos Linux `EV_KEY`.
- `tests/`: pruebas automatizadas del parser sin root.
- `.github/workflows/`: compilación ARM64 en GitHub Actions.
- `scripts/find-input.sh`: enumeración informativa de dispositivos input.

## ¿Incluye getevent dentro del mod?

**Sí, funcionalmente:** el código `src/RawEvdev.*` implementa la lectura y decodificación de los registros de `struct input_event` de Android ARM64. **No se invoca `/system/bin/getevent` ni se necesita instalarlo**. `su` invoca `/system/bin/sh` y el `cat` estándar de Android exclusivamente para transportar bytes del único nodo autorizado al parser C++ dentro del `.geode`. Este diseño evita desplegar un ELF auxiliar con permisos root y no cambia permisos globales de `/dev/input`.
