# Fase 2 — Texturing no backend deko3d

## Diagnóstico (o que o port tinha)

- `GSH_Deko3d` renderizava vértice `{x, y, z, cor}` com fragment shader
  flat (`color_fsh.glsl`): UVs calculados no `VertexKick` e **descartados**
  no `Prim_Triangle`; `Prim_Point/Line/Sprite` com corpo em `#if 0`
  (mortos); transfers (`HostToLocal`, `CLUT`) como stubs vazios;
  `GSH_Deko3d_Texture.cpp` comentado no CMake (arquivo nem existia).

## O que a Fase 2 implementa (`fork/` aplicado via `scripts/apply-fork.*`)

1. **Vértice v2** `{x, y, z, cor, u, v, q, pad}` (32 B) + 3 atributos;
   `triangle_vsh.glsl` repassa cor e UVQ.
2. **Fragment texturizado** (`texture_fsh.glsl`, TFX modulate):
   `uv/q` com guarda `q≈0`, `texel × cor`. Pipeline de cor mantido p/
   primitivas sem textura.
3. **Draw runs**: primitivas agrupadas por (tipo, textura, wrap); um `Draw`
   por run no `EndFrame` (textura homogênea por draw — exigência do binding).
4. **Cache de texturas** (`GSH_Deko3d_Texture.cpp`, adaptado do
   `GSH_OpenGL_Texture.cpp` caminho não-SIMD): PSMCT32/24/16(S) diretos,
   PSMT8/4 resolvidos na CPU com CLUT linearizada (`MakeLinearCLUT`);
   chave TEX0 + hash CLUT; LRU 256; upload via `CopyBufferToImage` p/
   `DkImage` RGBA8; `Flush()` em `HostToLocal` e `CLUT`.
5. **Segurança de memória GPU**: blocos de textura evictados só morrem em
   `CollectGarbage` (após `WaitIdle`, teto de 256 no registro).
6. **Sprites/pontos/linhas** implementados (sprites = 2 tris; pontos/linhas
   nativos, sem textura na v1).
7. **ST normalizado** para 0..1 na CPU; shader divide por `q`.

## Limites honestos da v1 (viram issues)

- Sem alpha-test (ATE), sem TFX DECAL/highlight, sem mipmaps, sem
  PSMT8H/4HL/4HH (desenha sem textura + aviso, máx. 8 no log).
- Render-to-texture vira snapshot (GS RAM não recebe nosso framebuffer).
- `binding=0 ↔ imageId 0` e handle `(slot, sampler)` seguem a convenção
  deko3d/uam — **verificar no hardware** (tela preta com geometria
  colorida ok = binding errado é o 1º suspeito).
- Teto de 4096 slots de imagem/frame (degradação segura além disso).

## Verificar no hardware (checklist p/ "ok")

1. NRO abre, log mostra versão, sem crash no boot de ELF (`test.elf`).
2. `ps2sdk samples` com textura: triângulos **texturizados** (antes: cor chapada).
3. Menu 2D (sprites) visível.
4. Sem regressão: samples antigos continuam coloridos onde não há textura.
5. FPS/estabilidade por 5 min (overlay futuro).

## Arquivos do overlay

```
fork/Source/gs/GSH_Deko3d/
  GSH_Deko3d.cpp/.h        backend modificado (runs, pipelines, sprites)
  GSH_Deko3d_Texture.h/.cpp cache + upload (novos)
  shader/triangle_vsh.glsl v2 (uvq) + texture_fsh.glsl (novo)
  shader/CMakeLists.txt    + texture_fsh
  CMakeLists.txt           + arquivos de textura
```
