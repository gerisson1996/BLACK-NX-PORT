# BLACK NX PORT

🇧🇷 **Português** · 🇺🇸 [English](README.en.md) · 🇪🇸 [Español](README.es.md)

Port do **Black** (Xbox original, Criterion 2006) para o **Nintendo Switch**, como homebrew (`.nro`), por **recompilação estática**. O código x86 do jogo é traduzido para C++ no PC e compilado para ARM64. Não é emulação: o jogo roda nativo, com o Direct3D 8 e o DirectSound do Xbox reescritos sobre Vulkan (NVK) e o áudio do Switch.

Funciona com a tradução **PT-BR da Tokyo Games RGH**: o `default.xbe` europeu é convertido no americano no próprio console.

**Estado:** o jogo roda inteiro (menus, vídeos, som, fases) a até 60 fps.

**Download:** o `black.nro` está em [Releases](https://github.com/gerisson1996/BLACK-NX-PORT/releases).

**Mira por giroscópio (v0.2.0):** em *Opções → TOUCH CONTROLS → MOTION CONTROLS*, ligue **GYROSCOPE** (**ALWAYS** ou **WHILE FIRING**, só com ZR apertado). Funciona no modo portátil, com o Pro Controller ou com o Joy-Con direito. A sensibilidade e a inversão dos eixos ficam no mesmo menu e são salvas em `gyro.toml`. Enquanto o giroscópio mira, a assistência de mira do jogo fica desligada.

**Vibração (v0.2.0):** funciona no modo portátil, com o par de Joy-Cons, com um Joy-Con sozinho e com o Pro Controller. O motor pesado do Xbox vibra grave no lado esquerdo, e o leve vibra agudo no direito.

### ☕ Apoie o projeto

Se o port foi útil para você, considere apoiar:

- **Ko-fi:** [ko-fi.com/gerissoncassiano](https://ko-fi.com/gerissoncassiano)
- **Pix:** `pocketneogames@gmail.com`

> [!IMPORTANT]
> Este repositório **não contém nenhum arquivo do jogo** nem código derivado dele. Você precisa da sua própria cópia original do Black.

---

## Como funciona

| Pasta | O que é |
|---|---|
| `switch/` | A camada do Switch (libnx): loop do applet, logs e crash handler (`main.cpp`), renderizador Vulkan/NVK (`vulkan.cpp`), áudio (`audio.cpp`), Joy-Con (`input.cpp`), busca do jogo no SD (`game_files.cpp`), e o CMake. |
| `XboxXBE-Recomp/` | O SDK de recompilação ([TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp), commit `99078b1`), com as mudanças para o Switch: memória por code memory, arquivos no SD, threads. |
| `Black-Android/` | O port de Android ([RanggaCasper/Black-Android](https://github.com/RanggaCasper/Black-Android) v1.5.0). Daqui vêm `src/` (D3D8, DirectSound, ganchos do jogo) e `config/game.toml`, mais a correção do `Lock2DSurface` para o Switch. |
| `third_party/` | glslang (com HLSL), Dear ImGui, toml++. |
| `tools/` | `recompile.ps1` (recompila o xbe), `package_switch.ps1`, `europe_to_us.py`. |
| `MIGRACAO.md` | As mudanças feitas para o Switch. |

---

## Compilar

### Requisitos

- **Visual Studio 2022 Build Tools** com "Desenvolvimento para desktop com C++" e "Ferramentas do C++ Clang".
- **devkitPro** com `switch-dev` e, no bash dele: `pacman -S switch-cmake switch-libzstd switch-libexpat switch-zlib cmake ninja`.
- **NVK (mesa-switch)** compilado, em `nvk_sdk/` na raiz do repositório: `include/` e `lib/` com `libvulkan.a` e as `.rlib` que o `switch/CMakeLists.txt` lista. Não está no repositório porque o `libvulkan.a` tem mais de 350 MB.
- Python 3.
- O repositório num caminho **sem acento e sem espaço** (ex.: `C:\bnx2`): o recompilador não abre caminhos com acento.

### Passos

1. O xbe americano em `Black-Android/assets/default.xbe`. Para converter o europeu (o da tradução PT-BR):

   ```bash
   python tools/europe_to_us.py "<seu jogo>/default.xbe" Black-Android/src/common/europe.patch Black-Android/assets/default.xbe
   ```

2. Recompilar, o que gera `Black-Android/generated/game`:

   ```bash
   powershell -ExecutionPolicy Bypass -File tools\recompile.ps1
   ```

3. Compilar o `.nro`, no bash do devkitPro:

   ```bash
   JOBS=8 ./switch/build.sh
   ```

4. `tools\package_switch.ps1` copia o `.nro` para `switch_sd/switch/black/`.

---

## Instalar no Switch

```
sdmc:/switch/black/black.nro
sdmc:/switch/black/game/        <- os arquivos do seu disco (default.xbe + pastas) ou um .iso
```

- Precisa de Atmosphère e hbmenu.
- Abra em **modo título**: segure R ao abrir um jogo, depois escolha o Black no hbmenu.
- Os logs ficam em `sdmc:/switch/black/logs/`.

---

## Créditos

- **XboxXBE-Recomp**: TGP482 (MIT)
- **Black-Android**: RanggaCasper (MIT)
- **Tradução PT-BR**: Tokyo Games RGH ([tokyogamesrgh.com.br](https://www.tokyogamesrgh.com.br))
- **Port para Switch**: gerisson1996
- **Bibliotecas**: Mesa/NVK, glslang, Dear ImGui, toml++, libnx/devkitPro

Black é marca da Criterion Games / Electronic Arts. Este projeto não tem relação com elas.
