# PS2 Switch — Emulador de PS2 para Nintendo Switch (Tegra X1)

Fork do **Play! port Switch (`xerpi/play-switch`)** com foco em estabilidade
e desempenho no Tegra X1, com rebase periódico do núcleo
**`jpd002/Play-`** e técnicas avaliadas do **ARMSX2/PCSX2-ARM64** onde
forem portáveis. Base jogável, não milagre.

## Estado real (18/09/2026)

- O port Switch do Play! (ref. `xerpi/play-switch`) é **prova de conceito**:
  sem texturing, só polígonos coloridos básicos.
- Upstream PCSX2-ARM64 = só interpretador (+VIF dynarec). JIT ARM64 de
  verdade hoje está no fork **ARMSX2** (EE/IOP/VU + fastmem) — mas ele é
  amarrado à arquitetura do PCSX2 e **não é transplantável** para o Play!.
- **Sem Vulkan no Horizon** para homebrew (NVK upstream exclui Tegra).
  Caminho GPU: OpenGL via `switch-mesa` agora, `deko3d` depois.
- Ver `docs/ARQUITETURA.md` para o plano completo e `docs/BUILD.md` para compilar.

## Requisito legal

Você precisa dumpar a **BIOS do seu próprio PS2**. Este projeto não inclui,
não baixa e não distribui BIOS nem jogos. Sem BIOS, nada boota — em qualquer
emulador.

## Estrutura

```
ps2-switch/
  README.md                  este arquivo
  docs/
    ARQUITETURA.md           decisões técnicas (base, CPU, GPU, áudio)
    BUILD.md                 como compilar o NRO
  gamesdb/
    compat.yaml              base de compatibilidade + perfis por jogo
    perfil-exemplo.yaml      template de perfil
  scripts/
    windows-setup.bat        prepara devkitPro + clona o upstream
  .github/workflows/
    build-switch.yaml        CI: compila o NRO a cada commit
```

## Roadmap

1. **Fase 0 — fundação (este scaffold):** docs, CI, gamesdb, toolchain.
2. **Fase 1 — boot estável:** fork Play!, NRO compilando no CI, BIOS boota,
   `ps2sdk samples` rodando.
3. **Fase 2 — texturing:** trazer o renderer OpenGL do Play! para o target
   Switch (switch-mesa), corrigir texturing.
4. **Fase 3 — desempenho Tegra X1:** afinidade de threads no A57,
   EE underclock/frameskip por perfil, limite de VRAM (4 GB), medição
   com overlay de FPS.
5. **Fase 4 — deko3d:** backend de baixo nível (offline DKSH via `uam`),
   só depois do GL estável.
6. **Fase 5 — técnicas ARMSX2 avaliadas:** portar ideias (não código):
   fastmem com fallback, fusão de acesso desalinhado, idle-skip,
   clamp FPU — ver tabela em `docs/ARQUITETURA.md`.

## Créditos

- `jpd002` (Play!), `xerpi` (port Switch PoC), time PCSX2, time ARMSX2,
  devkitPro (`libnx`, `deko3d`), `CostelaBR` (comunidade CNX).
- Continuidade por **RoxasBR90**.
