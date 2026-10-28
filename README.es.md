# BLACK NX PORT

🇧🇷 [Português](README.md) · 🇺🇸 [English](README.en.md) · 🇪🇸 **Español**

Port de **Black** (Xbox original, Criterion 2006) para **Nintendo Switch** como homebrew (`.nro`), hecho por **recompilación estática**. El código x86 del juego se traduce a C++ en el PC y se compila para ARM64. No es emulación: el juego corre de forma nativa, con el Direct3D 8 y el DirectSound de Xbox reescritos sobre Vulkan (NVK) y el audio de Switch.

El port admite la **traducción al portugués de Brasil de Tokyo Games RGH**. Su `default.xbe` europeo se convierte en el estadounidense en la propia consola.

**Estado:** el juego entero funciona, con menús, vídeos, sonido y niveles, a hasta 60 fps.

**Descarga:** el `black.nro` está en [Releases](https://github.com/gerisson1996/BLACK-NX-PORT/releases).

**Apuntado con giroscopio (v0.2.0):** en *Opciones → TOUCH CONTROLS → MOTION CONTROLS*, activa **GYROSCOPE** (**ALWAYS**, o **WHILE FIRING** solo con ZR pulsado). Funciona en modo portátil, con el Pro Controller o con el Joy-Con derecho. La sensibilidad y la inversión de ejes están en el mismo menú y se guardan en `gyro.toml`. Mientras el giroscopio apunta, la asistencia de apuntado del juego se desactiva.

**Vibración (v0.2.0):** funciona en modo portátil, con el par de Joy-Con, con un solo Joy-Con y con el Pro Controller. El motor pesado del Xbox vibra grave en el lado izquierdo y el ligero vibra agudo en el derecho.

### ☕ Apoya el proyecto

Si el port te resultó útil, considera apoyarlo:

- **Ko-fi:** [ko-fi.com/gerissoncassiano](https://ko-fi.com/gerissoncassiano)
- **Pix (Brasil):** `pocketneogames@gmail.com`

> [!IMPORTANT]
> Este repositorio **no contiene ningún archivo del juego** ni código derivado de él. Necesitas tu propia copia original de Black.

---

## Cómo funciona

| Carpeta | Qué es |
|---|---|
| `switch/` | La capa de Switch (libnx): el bucle del applet, los logs y el gestor de fallos (`main.cpp`), el renderizador Vulkan/NVK (`vulkan.cpp`), el audio (`audio.cpp`), los Joy-Con (`input.cpp`), la búsqueda del juego en la SD (`game_files.cpp`) y la compilación con CMake. |
| `XboxXBE-Recomp/` | El SDK de recompilación ([TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp), commit `99078b1`), con los cambios para Switch: memoria del Xbox en code memory, archivos en la SD, hilos. |
| `Black-Android/` | El port de Android ([RanggaCasper/Black-Android](https://github.com/RanggaCasper/Black-Android) v1.5.0). De aquí vienen `src/` (D3D8, DirectSound, ganchos del juego) y `config/game.toml`, además de la corrección de `Lock2DSurface` para Switch. |
| `third_party/` | glslang (con HLSL), Dear ImGui, toml++. |
| `tools/` | `recompile.ps1` (recompila el xbe), `package_switch.ps1`, `europe_to_us.py`. |
| `MIGRACAO.md` | Los cambios hechos para Switch (en portugués). |

---

## Compilar

### Requisitos

- **Visual Studio 2022 Build Tools** con "Desarrollo para el escritorio con C++" y "Herramientas de Clang para C++".
- **devkitPro** con `switch-dev`. En su bash, ejecuta: `pacman -S switch-cmake switch-libzstd switch-libexpat switch-zlib cmake ninja`.
- **NVK (mesa-switch)** compilado, en `nvk_sdk/` en la raíz del repositorio: `include/` y una carpeta `lib/` con `libvulkan.a` y los `.rlib` que lista `switch/CMakeLists.txt`. No está en el repositorio porque `libvulkan.a` pesa más de 350 MB.
- Python 3.
- El repositorio en una ruta **sin tildes ni espacios** (p. ej. `C:\bnx2`): el recompilador no abre rutas con caracteres especiales.

### Pasos

1. Coloca el xbe estadounidense en `Black-Android/assets/default.xbe`. Para convertir el europeo (el de la traducción al portugués de Brasil):

   ```bash
   python tools/europe_to_us.py "<tu juego>/default.xbe" Black-Android/src/common/europe.patch Black-Android/assets/default.xbe
   ```

2. Recompila. Esto genera `Black-Android/generated/game`:

   ```bash
   powershell -ExecutionPolicy Bypass -File tools\recompile.ps1
   ```

3. Compila el `.nro` en el bash de devkitPro:

   ```bash
   JOBS=8 ./switch/build.sh
   ```

4. Ejecuta `tools\package_switch.ps1`. Copia el `.nro` a `switch_sd/switch/black/`.

---

## Instalar en Switch

```
sdmc:/switch/black/black.nro
sdmc:/switch/black/game/        <- los archivos de tu disco (default.xbe + carpetas) o un .iso
```

- Necesitas Atmosphère y hbmenu.
- Ábrelo en **modo título**: mantén pulsado R al abrir cualquier juego instalado y elige Black en hbmenu.
- Los logs se guardan en `sdmc:/switch/black/logs/`.

---

## Créditos

- **XboxXBE-Recomp**: TGP482 (MIT)
- **Black-Android**: RanggaCasper (MIT)
- **Traducción al portugués de Brasil**: Tokyo Games RGH ([tokyogamesrgh.com.br](https://www.tokyogamesrgh.com.br))
- **Port para Switch**: gerisson1996
- **Bibliotecas**: Mesa/NVK, glslang, Dear ImGui, toml++, libnx/devkitPro

Black es una marca de Criterion Games / Electronic Arts. Este proyecto no tiene relación con ellas.
