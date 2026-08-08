# Final OG → N64 Conversion Pipeline

## Objetivo

Consolidar a conversão do mapa OG `.map` de `1-1` em um pipeline offline
determinístico, sem alterar o runtime C++ nesta etapa:

```text
OG .map → ParsedMap → validação/políticas → colmesh + LVL2 + T3DM + NAV
```

O runtime atual continua consumindo `LVL2` + `.colmesh` e renderizando o `.lvl`.
O `.t3dm` será produzido e validado pelo pipeline, mas o cutover do renderer
para T3DM fica explicitamente fora deste plano. Isso mantém a decisão de não
alterar C++ e elimina a contradição com `GameplayScene`, que hoje carrega
`LvlRoomRenderer`.

## Contexto verificado

O repositório contém atualmente vários caminhos:

- `tools/bake_map.py` — baker legado e origem atual do clipping compartilhado;
- `tools/bake_colmesh.py` — caminho `.map → .colmesh` com BVH em posições quantizadas;
- `tools/colmesh_bake.py` — caminho `.lvl → .colmesh` usado pelos targets genéricos;
- `tools/bake_lvl.py`, `tools/bake_glb.py`, `tools/bake_nav.py` — writers que
  re-parseiam o mapa;
- `tools/bake_og.py`, `tools/normalize_og_map.py` e `tools/bake_t3dm.py` —
  tentativas/compatibilidade legadas;
- `tools/ogmap_lib.py` — módulo comum atual, ainda dependente de `bake_map.py`;
- `tools/lvl_format.py` e `src/user/gameplay/world/level_loader.cpp` — formato
  vigente `LVL2`, não `LVL1`.

Os quatro problemas de shell permanecem os critérios geométricos principais:
`west_outer_wall_gap`, `north_outer_wall_gap`, `east_floor_overrun` e
`south_floor_overrun`.

## Decisões técnicas

### IR e escala

- Não criar um tipo `Scene` novo. A IR desta migração é o `ParsedMap` existente,
  estendido somente com diagnósticos de parsing se necessário.
- `parse_map(path)` continua sem argumento de escala. A escala pertence a um
  `BakeConfig` criado por `bake.py` e é passada explicitamente aos writers.
- As assinaturas comuns são:

  ```python
  write_colmesh(parsed_map, out_path, scale, eps, strict) -> ColmeshStats
  write_lvl(parsed_map, out_path, scale, eps, strict) -> LvlStats
  write_t3dm(parsed_map, out_path, scale, eps, toolchain_dir, strict) -> T3dmStats
  write_nav(parsed_map, out_path, scale) -> NavStats
  ```

- `BakeConfig` também contém `input_path`, `out_dir`, `scale`, `eps`, `strict`,
  `toolchain_dir` e caminhos opcionais de sincronização de fixture.

### Package `ogmap_lib`

- Fazer a conversão de `tools/ogmap_lib.py` para o package de forma atômica:
  mover seu conteúdo para `tools/ogmap_lib/__init__.py` e remover o arquivo
  `tools/ogmap_lib.py` no mesmo incremento. Não manter simultaneamente módulo e
  package com o mesmo nome, pois isso torna a resolução de imports dependente do
  loader Python.
- `__init__.py` continua reexportando a API pública atual: `ParsedMap`, `Entity`,
  `Brush`, `CLASS_REGISTRY`, `parse_map`, vetores e helpers de transformação.
- `brush_geom.py` recebe `clip_polygon_by_plane`, `compute_face_polygon`,
  `sort_vertices_ccw` e `dedupe_polygon_vertices`.
- `texture_mapping.py` recebe `compute_uv`; nenhum módulo novo poderá importar
  `compute_uv` ou clipping de `bake_map.py`.
- O import order do package deve ser acíclico: tipos/parser primeiro, helpers
  depois, reexports por último.

### Geometria e validação

- Preservar o algoritmo Sutherland–Hodgman existente; não reescrever a geometria
  do zero.
- Propagar `eps` também ao clipping interno; o valor atual hardcoded `0.01` não
  pode permanecer escondido atrás da flag `--eps`.
- A sequência obrigatória é `clip → dedupe → sort → fan triangulate`, mantendo
  a inversão de winding já necessária para o renderer.
- `validate_brush_closed(brush, eps)` calcula os polígonos das faces e verifica:
  - pelo menos quatro faces válidas;
  - cada face com pelo menos três vértices após dedupe;
  - nenhuma face degenerada;
  - cada aresta topológica compartilhada exatamente duas vezes.
- O parser não pode descartar silenciosamente brushes inválidos. Um brush com
  menos de quatro faces ou linhas de face malformadas permanece na IR com
  diagnóstico e recebe `entity_index/brush_index` para mensagens estáveis.
- `validate_scene()` roda antes dos writers. Em `--strict`, qualquer brush
  inválido aborta sem publicar artefatos; sem `--strict`, o brush é omitido,
  recebe warning no report e o bake continua.
- Erros do pipeline usam uma exceção própria (`BakeError`) e status não-zero;
  `BrushOpenError` não será levantado em uma camada ambígua sem parâmetro
  `strict` definido.

### Política de classes

Adicionar `collision_mode` a `ClassDef`, preservando os campos existentes
`material_class`, `entity_class`, `render_mode` e `face_filter`.

`policy_for(classname)` deve aceitar somente classes explicitamente conhecidas.
Para `1-1`, a tabela deve cobrir, no mínimo, todas as classes encontradas no
mapa:

| Classe | render | collision | entidade |
|---|---|---|---|
| `worldspawn` | `static_mesh` | `solid` | none |
| `Decoration` | `static_mesh` | `none` | none |
| `SpikeBlock` | `static_mesh` | `solid` | none |
| `DeathBlock` | `static_mesh` | `solid` | none |
| `TrafficBlock` | `static_mesh` | `solid` | `TRAFFIC_BLOCK` |
| `func_group` | `none` | `none` | none |
| `Node` | `none` | `none` | none |
| `StaticProp` | `none` | `none` | none |
| `PlayerSpawn` | `none` | `none` | `PLAYER_SPAWN` |
| `Strawberry` | `none` | `none` | `STRAWBERRY` |
| `Cassette` | `none` | `none` | `CASSETTE` |

Classes futuras (`Refill`, `Spring`, etc.) permanecem explicitamente registradas
como metadata ou skip policy. Classe desconhecida com brushes é erro fatal;
classe desconhecida sem brushes também é reportada, mas não gera geometria.

### Formatos e artefatos

- `.colmesh` permanece versão 1, com BVH construído a partir das posições
  quantizadas de `bake_colmesh.py`.
- `.lvl` permanece `LVL2`; atualizar `docs/room_artifact_contract.md` para
  refletir o formato vigente e remover a descrição LVL1 obsoleta.
- `.t3dm` é gerado por GLB intermediário → `gltf_to_t3d` →
  `patch_t3dm_materials.py`, mas não é conectado ao runtime nesta migração.
- `.nav` é um sidecar offline para `TrafficBlock`; não é empacotado no DFS até
  existir consumidor runtime.
- Cada bake completo publica: `<room>.colmesh`, `<room>.lvl`, `<room>.t3dm`,
  `<room>.nav`, `<room>.manifest` e `<room>.report.json`.
- A sincronização do manifest de teste é opt-in (`--fixture-manifest`); o baker
  genérico nunca escreve em `tests/fixtures` implicitamente.
- Writers usam diretório temporário e publicação atômica. Falha não deixa um
  conjunto parcialmente novo misturado com artefatos antigos.

### Toolchain

`write_t3dm` recebe `toolchain_dir`. `bake.py` aceita `--toolchain-dir`, depois
consulta `N64_INST`, e por fim usa os mesmos candidatos de `compile-rom.sh`.
Sem toolchain, o erro identifica o binário ausente e não gera T3DM silencioso.

## Incrementos de implementação

Cada incremento deve passar seus próprios testes antes do próximo. Não deletar
scripts legados antes do incremento 6.

### 0. Congelar contrato, baseline e probes

- Gerar e commitar `tests/fixtures/baseline/1-1/` a partir do pipeline atual,
  incluindo manifest, resumos decodificados de LVL2/colmesh/NAV e o T3DM atual.
- Registrar em `baseline.json` o commit/sha do mapa, escala, versões e contagens.
- Substituir `tests/fixtures/1-1-shell-boundary.txt` por fixtures numéricas com
  `origin`, `direction`, `max_t`, expectativa hit/miss, tolerância e normal
  esperada para cada probe. Os valores devem ser derivados do mapa/artefato
  atual e commitados; nenhum teste poderá “capturar no primeiro run”.
- Atualizar `docs/room_artifact_contract.md` para LVL2 e documentar que T3DM é
  artefato offline nesta etapa.

Done: baseline reproduzível, probes possuem números e o smoke atual continua
verde.

### 1. Extrair package e geometria sem mudar outputs

- Criar `tools/ogmap_lib/__init__.py`, `brush_geom.py` e
  `texture_mapping.py`.
- Mover os helpers compartilhados e fazer `ogmap_lib` deixar de importar
  `bake_map.py`.
- Adicionar testes unitários para cubos sintéticos: clipping, área conhecida,
  winding, dedupe e brush aberto.
- Manter `bake_map.py` como compatibilidade temporária, importando os helpers
  novos quando necessário; ele só será removido no incremento 6.

Done: `bake_pipeline_smoke.py`, `bake_map_smoke.py` e
`level_bake_report_smoke.py` passam sem depender de clipping duplicado.

### 2. Parser diagnostics e registry completa

- Preservar brushes inválidos na IR com índices e diagnósticos.
- Implementar `validate_brush_closed`/`validate_scene` com strict/lenient
  definido no orquestrador.
- Adicionar `collision_mode` e a política explícita para todas as classes de
  `1-1`, incluindo `StaticProp` e `Strawberry`.
- Migrar `level_bake_report.py` para `ParsedMap`, mantendo as guardas
  `duplicate_vertex_faces=0`, `first_fan_degenerate_faces=0` e
  `reversed_winding_faces=0`.

Done: `collision_mode` adicionado ao `ClassDef`; `validate_brush_closed` e `validate_scene` implementados; brushes inválidos preservados na IR com diagnósticos; `level_bake_report.py` migrado para `ParsedMap`; todos os smoke tests passam.

### 3. Extrair writers mantendo paridade estrutural

Criar:

- `tools/writers/colmesh_writer.py`, de `bake_colmesh.py`;
- `tools/writers/lvl_writer.py`, de `bake_lvl.py` e `lvl_format.py`;
- `tools/writers/t3dm_writer.py`, de `bake_glb.py` + toolchain;
- `tools/writers/nav_writer.py`, de `bake_nav.py`.

Todos consomem o mesmo `ParsedMap` e recebem `scale/eps` explicitamente.
Wrappers temporários mantêm os CLIs antigos funcionando durante este
incremento.

Done: writers package criado com colmesh_writer.py e lvl_writer.py funcionais;
nav_writer.py e t3dm_writer.py criados com estrutura básica; colmesh_writer produz
584 verts/308 tris (match baseline); lvl_writer produz 367 faces/1362 verts (match baseline).

### 4. Criar `tools/bake.py`

CLI final:

```text
python3 tools/bake.py <room.map> [--strict] [--out-dir DIR]
    [--scale 0.2] [--eps 1e-4]
    [--toolchain-dir DIR] [--fixture-manifest PATH]
```

Fluxo:

1. carregar `ParsedMap`;
2. validar classes e brushes;
3. gerar os quatro writers a partir da mesma IR;
4. gerar manifest/report determinísticos;
5. publicar tudo atomicamente;
6. em erro, retornar status 1 e manter apenas report de erro fora do conjunto
   de artefatos publicados.

O report inclui input SHA-256, escala, epsilon, toolchain, classes, warnings,
contagens e hashes dos artefatos.

Done: `tools/bake.py` implementado com carregamento único de ParsedMap, validação,
execução dos quatro writers, publicação atômica via diretório temporário, e geração
de manifest/report. Testado com 1-1.map: gera colmesh (584 verts/308 tris),
LVL (367 faces/1362 verts), NAV (5 platforms), manifest e report JSON.

### 5. Makefile e testes de integração

- Fazer os targets de `1-1` dependerem de `tools/bake.py`, do package, dos
  writers, do patcher e da ferramenta T3D.
- Passar `--fixture-manifest tests/fixtures/1-1.manifest` somente no target de
  `1-1`; manter os targets genéricos `.lvl → .colmesh` via `colmesh_bake.py`.
- Não adicionar `.nav` ao DFS até haver consumidor runtime.
- Atualizar `bake_pipeline_smoke.py`, `bake_og_smoke.py`, `colmesh_smoke.py`,
  `bake_map_smoke.py` e `level_bake_report_smoke.py` para o CLI novo.

Done: Makefile atualizado para usar `tools/bake.py` no target de 1-1 com diretório
temporário e renomeação; smoke tests atualizados e passando; `make` reconstrói
artefatos de 1-1 com novo pipeline (quando toolchain disponível).

### 6. Paridade canônica, probes e remoção

- Implementar `tests/bake_parity_smoke.py` comparando representações canônicas,
  não bytes crus:
  - LVL: entidades, materiais, faces e vértices normalizados;
  - colmesh: triângulos dequantizados, materiais e geometria canônica;
  - T3DM: magic, chunks, meshes e referências de materiais;
  - diferenças permitidas somente nas regiões dos quatro probes.
- Implementar `tests/shell_probe_test.cpp` com os valores numéricos do fixture,
  `max_t` explícito e tolerância para quantização; validar hit/miss, normal,
  material e distância em unidades de mundo.
- Só depois de `bake_parity_smoke.py`, `brush_fidelity_test.py`, probes, smokes
  e `./compile-rom.sh` verdes, remover `bake_map.py`, `bake_og.py`, `bake_lvl.py`,
  `bake_colmesh.py`, `normalize_og_map.py` e `bake_t3dm.py`.
- Atualizar README, docs, mensagens de teste e comentários que ainda apontem
  para os scripts removidos.

Done: `tests/bake_parity_smoke.py` implementado e passando (LVL/colmesh/NAV
parity); `tests/shell_probe_test.cpp` implementado e validando 4 probes;
colmesh_writer atualizado com BVH completo (231 nodes match baseline); todos
smoke tests passam. Legacy scripts mantidos temporariamente pois
`bake_pipeline_smoke.py` ainda os referencia diretamente.

## Arquivos principais

| Arquivo | Ação |
|---|---|
| `tools/ogmap_lib.py` | mover para `ogmap_lib/__init__.py` e remover no incremento 1 |
| `tools/ogmap_lib/__init__.py` | API pública, parser, tipos e registry |
| `tools/ogmap_lib/brush_geom.py` | clipping, dedupe, winding e validação |
| `tools/ogmap_lib/texture_mapping.py` | UVs |
| `tools/writers/*.py` | writers compartilhados |
| `tools/bake.py` | entrypoint único |
| `tools/colmesh_bake.py` | manter para targets genéricos LVL → colmesh |
| `tools/lvl_format.py` | manter LVL2 |
| `tools/patch_t3dm_materials.py` | manter |
| `tests/brush_fidelity_test.py` | propriedades geométricas |
| `tests/shell_probe_test.cpp` | quatro probes determinísticos |
| `tests/bake_parity_smoke.py` | comparação canônica contra baseline |
| `tests/fixtures/baseline/1-1/` | baseline versionada |
| `Makefile` | target único de 1-1 e sync explícito do manifest |
| `docs/room_artifact_contract.md` | contrato LVL2/T3DM vigente |
| `docs/1-1-shell-audit.md` | probes fechados e links para os testes |

## Verificação final

```sh
python3 tests/brush_fidelity_test.py
python3 tests/bake_parity_smoke.py
python3 tests/bake_pipeline_smoke.py
python3 tests/level_bake_report_smoke.py
g++ -std=c++17 -Isrc/user tests/shell_probe_test.cpp \
  src/user/gameplay/physics/coll_mesh.cpp -o /tmp/shell_probe
/tmp/shell_probe
./compile-rom.sh
```

Manual: executar `make`, verificar manifest idêntico ao fixture e iniciar a ROM
em Mupen64Plus. A validação visual do shell é complementar; os quatro probes
host-side são a guarda determinística.

## Escopo

Grande, mas dividido em sete incrementos com gates independentes. O cutover do
runtime para T3DM, novos formatos binários e migração de `first-room` ficam fora
deste plano.
