# Release v0.2.0-fase2 — Texturing no deko3d

**Versão atual do projeto: v0.2.0-fase2** (commit `6aa6e76` + docs de release).

## Instalar para testar

1. Copie `Play_Switch_v0.2.0.nro` para `/switch/` do SD.
2. Coloque a BIOS (dump do seu próprio PS2) onde a UI espera e um
   `test.elf` dos ps2sdk samples em `/switch/Play/` p/ fumaça.
3. Rode pelo hbmenu. Opcional: `nxlink` p/ ver o log em tempo real.

## O que mudou (desde v0.1.0-scaffold)

- Backend `GSH_Deko3d` com **texturing**: cache LRU (PSMCT32/24/16,
  PSMT8/4 + CLUT), upload `CopyBufferToImage` p/ `DkImage` RGBA8.
- Fragment shader `texture_fsh` (modulate) + vértice com UV/Q.
- Draw runs por textura; **sprites/pontos/linhas** implementados
  (sprites eram `#if 0` — menus 2D invisíveis antes).
- Invalidação em `HostToLocal`/`CLUT`; VRAM liberada com segurança.
- CI recompilado na nuvem a cada push (artefato `Play_Switch`).

## Limites conhecidos (viram Fase 3)

Sem alpha-test, sem TFX DECAL, sem mipmaps, sem PSMT8H/4HL/4HH
(desenha sem textura + aviso), render-to-texture = snapshot.

## Checklist de aceite (hardware)

- [ ] NRO abre sem crash; log mostra versão
- [ ] `ps2sdk samples` com textura (antes: cor chapada)
- [ ] Menu 2D (sprites) visível
- [ ] Sem regressão nos samples antigos
- [ ] 5 min estável

## Se der errado

Tela preta com geometria colorida ok → suspeito nº 1 é o binding do
sampler (`docs/FASE2.md`). Anexe o log do `nxlink` na issue.
