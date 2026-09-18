# Build — PS2 Switch (NRO)

## 1. Pré-requisitos

- devkitPro + devkitA64 + libnx instalados.
- No shell do devkitPro (msys), instalar os pacotes Switch (ajuste conforme
  seu `dkp-pacman`):
  ```
  dkp-pacman -S switch-dev switch-mesa switch-glfw switch-sdl2
  ```
  (`switch-deko3d`, necessário só a partir da Fase 4.)
- Nesta máquina (18/09/2026, verificado): `C:\devkitPro` + `C:\devkitA64`
  (gcc 16.1 aarch64), `switch-dev` (libnx 4.12, deko3d 0.5, uam),
  `switch-mesa` 20.1.0, `switch-glfw`, `switch-sdl2`, `switch-zlib`
  instalados via pacman do msys2 (repositórios `[dkp-libs]`/`[dkp-windows]`
  já presentes no `pacman.conf`). `dkp-pacman` não existe aqui — usar o
  `pacman.exe` do msys2.

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

No bash do msys2 (`C:\devkitPro\msys2`), com caminhos relativos (o cmake
do msys2 embaralha caminho Windows absoluto com espaço):

```
cd ps2-switch
cmake -S upstream/play-switch -B build-xerpi -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
  -DBUILD_TESTS:BOOL=OFF -DENABLE_AMAZON_S3:BOOL=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_DEPENDS_USE_COMPILER=FALSE
make -C build-xerpi Play_Switch_nro -j$(nproc)
```

Flags obrigatórias (descobertas no build de 18/09/2026):
- `CMAKE_POLICY_VERSION_MINIMUM=3.5`: submódulo xxHash antigo seria
  rejeitado pelo CMake novo.
- `CMAKE_DEPENDS_USE_COMPILER=FALSE`: depfiles com `C:/...` quebram o
  make ("múltiplos padrões para o alvo").
- Build 100% verificado: `Play_Switch.nro` (3,2 MB). Só warnings
  pré-existentes do upstream.

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
