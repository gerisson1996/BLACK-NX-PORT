# Migração do port do Switch para XboxXBE-Recomp + Black-Android

Base nova: [Black-Android](https://github.com/RanggaCasper/Black-Android) v1.5.0 (MIT) e o SDK [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) (MIT) fixado no commit `99078b1`. O projeto antigo (`black-nx`, xboxrecomp com emulação do NV2A) fica como referência para o que é específico do Switch.

## Por que

O Black-Android não emula a GPU do Xbox: o Direct3D 8 e o DirectSound do jogo foram reescritos (`src/d3d`, `src/sound`) e desenham direto em Vulkan. Já roda o jogo inteiro em ARM64, com menus, vídeos, áudio e ganchos de jogo prontos.

## Feito

- `tools/europe_to_us.py`: converte o `default.xbe` europeu (SHA-1 `796ad836…`, o da pasta Black PT-BR, intacto pela tradução) no americano (SHA-1 `c5bfed25…`) com o `europe.patch`, o mesmo algoritmo do `game_files.cpp`. Saída em `Black-Android/assets/default.xbe`.
- `tools/recompile.ps1`: compila o recompilador e gera `Black-Android/generated/game` (64 partes, 15.042 funções, 107 MB de C++).

- **Host do Switch** em `switch/` (no lugar de `native/` do Android), compilado e linkado: `black.nro` de 29,8 MB.
  - `main.cpp`: libnx, log em `sdmc:/switch/black/logs`, configurações em `black.toml`, 30 fps com vsync por padrão, 1280×720 portátil / 1920×1080 na base, erro pelo applet de erro.
  - `vulkan.cpp`: o backend do Android com `vkCreateViSurfaceNN` na janela do applet; `vk_shader.cpp` sem mudança, com o glslang 16.0.0 + HLSL compilado no build (`third_party/glslang`).
  - `input.cpp`: HID do libnx, 4 portas, botões por nome (A = A), R/L = black/white, ZL/ZR = gatilhos, vibração.
  - `audio.cpp`: audren do libnx, mix 5.1 dobrado para estéreo, blocos de 10 ms.
  - `game_files.cpp`: jogo em `sdmc:/switch/black/game`, `europe.patch` aplicado em tempo de execução.
  - `stubs.cpp`: toque, giroscópio, teclado/mouse neutros; `pread` (newlib não tem).
  - `compat/SDL3/SDL.h`: as duas chamadas de SDL que `src/` faz (taxa da tela, sair) em libnx, para o código do jogo ficar intacto.
- **Filmes (FMV)**: o decoder WMV é código da própria XMV no XBE, recompilado e preso às ops MMX/SSE de 4/8 lanes. Duas alavancas, medidas no console:
  - o código recompilado é compilado com `-O3 -fvect-cost-model=very-cheap` (`switch/CMakeLists.txt`): o GCC vetoriza os laços de lane em NEON (2,9× mais instruções NEON na seção `XMV`). Os 640×480 saíram de 8,7 para 30 fps só com isso.
  - `app::CpuBoost` (opção `cpu_boost`, desligada por padrão) pede `ApmCpuBoostMode_FastLoad` enquanto um filme de tela cheia toca (CPU 1785 MHz, GPU no mínimo), com watchdog de 1 s no `Pace()`; no teste no console não foi preciso — os 30 fps vêm só do `-O3`.
  - Resultado no console: `LO_N_UK`/`C_N_UK` (640×480) e `AK_N`/`02_N`/`05_N` (320×240) estáveis em 30 fps.
- **Mudanças no SDK** (`XboxXBE-Recomp`, commit `99078b1`):
  - `kernel/memory.cpp`: caminho `__SWITCH__` — reserva de 4 GB com `virtmemFindAslr`, memória por code memory em blocos de 1 MB; a RAM só em `0x80000000` (a janela `0xF0000000` fica sem mapear).
  - `cpu/x86_ops.h`: no GCC do Switch, tipos de memória sem `aligned(1)` (o GCC 16 travava com erro interno; o processador aceita acesso desalinhado).
  - `kernel/threads.cpp`, `kernel/boot.cpp`: sem `pthread_setname_np` e sem relançar o executável.
- `switch/build.sh` (bash do devkitPro) e `tools/package_switch.ps1` (copia o `.nro` para `switch_sd/switch/black`).

## Não testado ainda

O primeiro teste no console vai mostrar onde parar: a memória por code memory com o SDK novo, o NVK com o backend do Android, o tempo de compilar os shaders HLSL na primeira vez (há cache em `sdmc:/switch/black/shadercache`) e as pilhas de 64 MB por thread do jogo.
