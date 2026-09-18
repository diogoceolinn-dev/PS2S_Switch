# Build — PS2 Switch (NRO)

## 1. Pré-requisitos

- devkitPro + devkitA64 + libnx instalados.
- No shell do devkitPro (msys), instalar os pacotes Switch (ajuste conforme
  seu `dkp-pacman`):
  ```
  dkp-pacman -S switch-dev switch-mesa switch-glfw switch-sdl2
  ```
  (`switch-deko3d`, necessário só a partir da Fase 4.)
- Nesta máquina já existe `C:\devkitPro` + `C:\devkitA64` (gcc 15.2
  aarch64). Falta configurar o `dkp-pacman`/repositórios devkitPro e
  instalar os pacotes acima — ver `scripts/windows-setup.bat`.

## 2. Clonar a base do fork (referência)

O fork nasce do port Switch (único com `ui_switch`):

```
git clone --recurse-submodules https://github.com/xerpi/play-switch.git
cd play-switch
```

Núcleo upstream (fonte de rebase, sem target Switch):

```
git clone --recurse-submodules https://github.com/jpd002/Play-.git
```

## 3. Configurar e compilar (target Switch)

```
mkdir build-switch && cd build-switch
cmake -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/Switch.cmake" \
      -DBUILD_TESTS:BOOL=OFF -DENABLE_AMAZON_S3:BOOL=OFF ..
make Play_Switch_nro -j$(nproc)
```

Saída: `Source/ui_switch/Play_Switch.nro` → copiar para `/switch/` do SD.
BIOS (dump próprio): pasta documentada na UI do fork.

## 4. Testes de fumaça (ordem)

1. `ps2sdk samples` (executáveis simples — ref. GBAtemp do port).
2. Boot da BIOS até o menu.
3. Jogo 2D simples com perfil `frameskip: 0`.
4. Só então 3D, com perfil do `gamesdb/`.

## 5. Troubleshooting

- NRO abre e fecha / tela preta: confira `switch-mesa` instalado e BIOS válida.
- Sem textura em 3D: esperado no estágio PoC (Fase 1) — é a tarefa da Fase 2.
- Lento no handheld: docked primeiro para isolar teto térmico; depois perfis.
- CI quebrando: ver `.github/workflows/build-switch.yaml`.
