# Arquitetura — PS2 Switch (Tegra X1)

## 1. Base: fork do Play!, não merge com PCSX2

`jpd002/Play-` (C++, portátil: Windows/macOS/UNIX/Android/iOS/web) já tem
target Switch: `cmake -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/Switch.cmake"`
+ `make Play_Switch_nro` (`Source/ui_switch/`). Referência do port:
`xerpi/play-switch` (PoC, sem texturing).

Por que não "juntar as duas bases" literalmente: PCSX2/ARMSX2 e Play! têm
arquiteturas incompatíveis (PCSX2 = núcleo pluginado x86 com 20 anos de
acoplamento; Play! = núcleo portátil próprio). Merge de código é inviável;
o que se aproveita do ARMSX2 são **técnicas**, avaliadas uma a uma:

| Técnica ARMSX2            | Portável p/ Play! Switch? | Nota                                  |
|---------------------------|---------------------------|---------------------------------------|
| EE/IOP/VU recompilers     | Não (código)              | Amarrados ao núcleo PCSX2             |
| Fastmem + fallback (vmap) | Avaliar                   | Horizon não tem POSIX signals iguais; exige mecanismo próprio de fallback |
| Fusão LDL/LDR, SDL/SDR    | Avaliar                   | Ideia de codegen, reimplementar no dynarec do Play! |
| Idle-skip (BC0F/BC0T, Count poll) | Sim, como conceito | Maior ganho fácil em jogos com busy-poll |
| Clamp FPU igual x86       | Sim, como spec            | Evita divergência em gamefixes        |
| GameDB (fixes por jogo)   | Sim                       | Formato próprio em `gamesdb/`         |

## 2. CPU — Tegra X1 (4x Cortex-A57, 4 GB RAM)

- Relógios: ~1.02 GHz handheld / ~1.78 GHz docked (teto térmico manda).
- Afinidade: prender EMU num core, GS/render noutro, UI/áudio no resto;
  medir contenção com perfis por jogo.
- Alavancas de desempenho (por perfil em `gamesdb/`): EE underclock,
  frameskip, resolução interna, limite de cache de texturas (RAM total 4 GB).
- Meta de medição: overlay de FPS + tempo de frame; sem número, sem merge.

## 3. GPU — OpenGL agora, deko3d depois, sem Vulkan

- **Horizon homebrew não tem Vulkan** (NVK upstream exclui Tegra; o
  `switch-nvk` de terceiros é experimental e fora do escopo da Fase 1-3).
- **Fase 2 = `switch-mesa` (OpenGL):** reaproveita o renderer GL do Play!,
  API padrão, custo = overhead de CPU maior. É o caminho para texturing
  funcionar primeiro.
- **Fase 4 = `deko3d`:** baixo nível estilo Vulkan, usa Zcull/tiled cache/
  alvos comprimidos do Maxwell que o nouveau não expõe, overhead mínimo.
  Preço: **sem compilação de shader em runtime** — shaders via `uam`
  offline (DKSH). Emulador com shaders dinâmicos precisa de cache DKSH
  pré-aquecido + fallback. Por isso, só após o GL estável.
- Shaders do frontend/menu: GLSL → DKSH embutido no build (`uam` + objcopy),
  padrão já usado pelo driver deko3d do RetroArch.

## 4. Áudio, input, UI

- Áudio: backend `audren` (libnx), buffer dimensionado p/ 48 kHz, sem crackle
  como critério de aceite junto do FPS.
- Input: HID libnx (4+ controles, rumble onde o núcleo suportar).
- UI: manter `ui_switch` do Play!; tela de seleção de BIOS/jogo +
  overlay de perfil ativo do `gamesdb/`.

## 5. Compatibilidade

Fonte de verdade inicial: `jpd002/Play-Compatibility`
(`state-playable` etc.). Espelho local mínimo em `gamesdb/compat.yaml`
com estados próprios medidos no hardware (Switch Erista/Mariko, modo
docked/handheld). Estado sem evidência (foto/vídeo/log) não entra.
