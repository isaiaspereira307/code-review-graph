# PLANO: Portagem para C — Híbrida primeiro, depois Full-C

> **Modo Plano (somente leitura).** Nenhuma alteração será feita. Este documento descreve a estratégia, fases, arquitetura, testes e critérios de aceitação.

## 1. Objetivo

Portar o projeto `code-review-graph` para C em **duas etapas**:

1. **Abordagem híbrida** (recomendado): mover apenas o **núcleo de grafo/análise/fluxos** para uma biblioteca C (`c_core`), expondo API via bindings Python (ctypes). Manter CLI, MCP, resolvers, embeddings, eval, visualização em Python.
2. **Full-C** (segunda etapa): após a híbrida estar estável e com paridade validada, portar camadas restantes progressivamente até o projeto rodar majoritariamente em C.

**Requisitos:**
- Usar a **skill `transpile-to-c-safety`** obrigatoriamente em cada módulo convertido.
- **Primeiro híbrido, depois Full-C** (conforme solicitado).
- **Sem CI no GitHub Actions**. Não criar/alterar workflows em `.github/workflows/`.
- **Com testes** (testes C locais + testes de paridade + manter suíte pytest existente).

## 2. Princípios

- **Safety-first (C seguro)**: aplicar estritamente as diretrizes da skill `transpile-to-c-safety` (sem buffer overflows, null derefs, use-after-free, UB). Toda alocação deve ter ownership claro e paths de erro tratados.
- **Paridade obrigatória**: comportamento bit-a-bit/semântico idêntico entre implementação Python e C para os mesmos inputs. Regressão de comportamento é inaceitável.
- **Incremental por módulo (atômico)**: um módulo por vez. Cada conversão vira um commit atômico na branch correspondente.
- **Híbrido-first para mitigar risco**: isola o rewrite no core puro (algoritmos) e preserva camadas complexas (MCP, embeddings) sem bloquear a evolução.
- **Feature-flag por backend**: permitir alternar entre backend `python` e `c` (runtime) para validar paridade lado-a-lado sem quebrar o caminho padrão durante a fase híbrida.
- **Mínimo necessário**: não portar o que não traz valor ou é muito custoso (seguir escada "ponytail"). Preferir simplificar fronteiras ao invés de reescrever tudo de uma vez.
- **Testes antes/por validação**: testes C locais e de paridade são obrigatórios por fase. Não depende de GitHub Actions.

## 3. Arquitetura Alvo

### 3.1 Fase 1 — Híbrida (Python + Core C)

Estrutura proposta:

```text
c_core/
  include/code_review_graph/  # headers (.h) públicos (API C)
    graph.h
    neighbourhood.h
    graph_diff.h
    flows.h
    enrich.h
    postprocess.h
    c_core_export.h           # macros de visibilidade/export
  src/                        # implementação (.c)
    graph.c
    neighbourhood.c
    ...
  third_party/                # vendoring opcional (pequenos helpers)
  CMakeLists.txt              # build local (lib estática/compartilhada)
  README.md                   # build/test local

code_review_graph/
  _c_core/                    # bindings/wrappers C
    __init__.py
    ctypes_loader.py          # carrega .so/.dylib/.dll localmente
    bindings_graph.py         # mapeia tipos Python<->C (structs/pointers)
  graph.py                    # delega p/ backend C se habilitado (ou mantém API)
  neighbourhood.py            # idem
  ...
  config.py / backend.py      # seleção backend (python|c, env var/flag)
```

**Decisão de bindings:** **ctypes** (preferido nesta fase).

- Vantagens: sem dependência de build Python C extra (pybind11/cffi exigem build wheel), mais fácil de depurar ABI, reduz complexidade de empacotamento local, menos superfície de acoplamento. Ideal para API C estável e minimalista.
- Alternativa: `pybind11` (mais ergonômico com tipos complexos/containers). Recomendado **não** iniciar com pybind11 agora; pode avaliar só se ctypes ficar verboso em casos pontuais (containers/dicts aninhados). Manter decisão por simplicidade.

**Backend switch (feature flag):**

- Variável de ambiente: `CRG_BACKEND=python|c` (default: `python` durante migração, trocar para `c` por módulo conforme validado)
- Flag em runtime/config: `code_review_graph.backend.set_backend("c")`
- Wrappers mantêm **mesma assinatura pública** (API Python inalterada para consumidores CLI/MCP/tools).

### 3.2 Fase 2 — Transição Hybrid → Full-C

Após core híbrido estável com paridade completa:

- **Camada de fronteira**: isolar I/O, persistência, parsing, ferramentas (tools/*) com interfaces bem definidas (facilita extração para C).
- **Migração incremental por domínio** (não big-bang). Cada domínio migrado mantém testes de paridade com o estado anterior.
- Estrutura alvo full-C (diretório proposto): `crg/` (biblioteca + binários CLI) + `tests/c/`, mantendo compatibilidade de comportamento (sem mudar contratos lógicos).

> Detalhes Full-C na Seção 7.

## 4. Branch Strategy (separadas, sem CI)

Criar branches **separadas** por etapa. **Nunca** modificar `.github/workflows/`.

| Branch | Escopo | Finalidade |
|---|---|---|
| `c-port/hybrid-core` | Núcleo base: `neighbourhood`, `graph` | Base estável do grafo em C + bindings + testes |
| `c-port/hybrid-flows` | `graph_diff`, `flows` | Fluxos/impacto/diffs (depende de core) |
| `c-port/hybrid-enrich` | `enrich.py`, `postprocessing.py` | Pós-processamento/enriquecimento |
| `c-port/hybrid-persistence` | `registry.py`, `migrations.py`, `build_state.py`, `incremental.py` | Estado/persistência (avaliar port vs manter thin wrapper) |
| `c-port/hybrid-parsers` | Parsers/resolvers críticos (subset) | Opcional/avaliativo (tree-sitter C API) |
| `c-port/full-c` | Migração Full-C progressiva | Ramo de integração Full-C (feature branches filhas por domínio) |
| `c-port/full-c-tools` | `tools/*`, `cli.py`, `main.py` (thin) | Extração CLI/MCP para C (fase full-C) |
| `c-port/full-c-resolvers` | Resolvers + parser (tree-sitter C) | Portagem controlada de análise estática |

**Regra:** cada módulo convertido = **commit atômico** (headers + .c + bindings + testes C + testes de paridade + ajustes mínimos). Trabalhar por feature branch filha quando escopo maior, integrar na branch da fase após validação de paridade.

## 5. Fase 0 — Preparação (Hybrid)

### 5.1 Setup de repositório/branches
- [ ] Criar branch base: `git checkout -b c-port/hybrid-core`
- [ ] Documentar escopo híbrido em `docs/PORTING_C.md` (rascunho inicial, somente leitura/planejado — não editar agora, apenas planejar localização)
- [ ] Definir critério de **baseline**: rodar pytest existente e salvar lista de testes críticos/passando (referência de paridade)

### 5.2 Infraestrutura C (build local, sem CI)
- [ ] Criar `c_core/CMakeLists.txt` (build local). Suportar: `Debug/RelWithDebInfo`, build `shared` (`.so/.dylib/.dll`) e opcional `static`. **Sem** integração GA.
- [ ] Definir estrutura headers com `include/code_review_graph/` e include guards + `c_core_export.h` (visibilidade).
- [ ] Configurar tooling local: `clang-format` (estilo consistente), `clang-tidy` (safety checks) — arquivos de config locais (`.clang-format`, `.clang-tidy`) opcionais, não obrigatórios mas recomendados.
- [ ] Definir framework de testes C: **Unity** (leve, sem deps, fácil CMake, ótimo para unit puro) **OU** **Catch2** (mais expressivo). **Recomendação: Unity** (menor superfície, C estrito). Alternativa Catch2 (C++-friendly) — evitar complexidade desnecessária. Decisão: **Unity**.

### 5.3 Infra de testes (obrigatória, local)
- [ ] Criar `tests/c_core/` (testes unitários C por módulo: `test_graph.c`, `test_neighbourhood.c`, ...)
- [ ] Criar `tests/parity/` (testes de paridade Python×C: comparam saídas/estruturas para fixtures comuns)
- [ ] Criar `tests/fixtures/porting/` (casos mínimos + casos de borda para validação de paridade)
- [ ] Integrar testes C no CMake (`enable_testing()` + CTest) — **apenas execução local** (sem workflows).
- [ ] Manter suíte `pytest` intacta (referência de regressão).

### 5.4 Backend/feature flag
- [ ] Adicionar `code_review_graph/backend.py` (seleção `python|c` por módulo + env `CRG_BACKEND`)
- [ ] Adicionar `code_review_graph/_c_core/ctypes_loader.py` (descoberta de lib: `c_core/build/...`, `build/lib*/`, caminhos relativos ao repo)
- [ ] Definir contrato de wrapper: **mesma API pública Python**. Nenhuma alteração de assinatura visível a tools/CLI/MCP.

## 6. Fase 1–4 — Portagem Híbrida (módulo por módulo + transpile-to-c-safety)

**Regra de ouro:** Para **cada arquivo Python** a portar, **invocar a skill `transpile-to-c-safety`** na íntegra (seguir checklist da skill). Produzir `.h + .c`, revisar segurança, testar, validar paridade.

Ordem sugerida (baixo → médio risco, dependências crescentes):

| Ordem | Módulos Python | Destino C (`c_core/`) | Dependências | Risco |
|---|---|---|---|---|
| **1.** | `neighbourhood.py` | `neighbourhood.h/c` | Nenhuma (ou grafo mínimo) | **Baixo** |
| **2.** | `graph.py` | `graph.h/c` | Base de estruturas | **Baixo** |
| **3.** | `graph_diff.py` | `graph_diff.h/c` | `graph`, `neighbourhood` | **Baixo–Médio** |
| **4.** | `flows.py` | `flows.h/c` | `graph`, `neighbourhood` | **Médio** |
| **5.** | `enrich.py` | `enrich.h/c` | Grafo/fluxos | **Baixo–Médio** |
| **6.** | `postprocessing.py` | `postprocess.h/c` (ou `postprocessing.h/c`) | Depende de resultados anteriores | **Baixo** |
| **7.** | `registry.py`, `migrations.py` | `registry.h/c`, `migrations.h/c` | Persistência (SQLite C API). Avaliar **por partes**: lógica de migração vs I/O. Pode manter I/O fino em wrapper se verboso. | **Médio** |
| **8.** | `build_state.py`, `incremental.py` | `build_state.h/c`, `incremental.h/c` | Estado + diffs | **Médio** |
| **9.** | Parsers/resolvers (subset crítico) | *Opcional (híbrido)* | tree-sitter C API. **Só portar se necessário** p/ performance/isolamento. Caso contrário **manter em Python** nesta fase. | **Médio–Alto** |

### 6.1 Checklist por módulo (obrigatório)

Para cada módulo na ordem acima:

- [ ] **1. Analisar escopo**: extrair tipos (dataclasses/NamedTuple/Pydantic-lite), estruturas, funções puras vs com efeitos, I/O, contratos de retorno, casos de borda.
- [ ] **2. Invocar `transpile-to-c-safety`** (skill): aplicar no arquivo `code_review_graph/<modulo>.py`. Gerar `.h` + `.c` em `c_core/include/...` e `c_core/src/...`. **Seguir checklist completo da skill** (ownership, bounds, null checks, erros, alocações/ desalocações explícitas).
- [ ] **3. Modelar dados em C**: definir structs claros, evitar tipos "mágicos", usar arrays/ponteiros com tamanhos explícitos (ou estruturas auxiliares). Documentar ownership (quem aloca/libera).
- [ ] **4. API C minimalista e estável**: expor apenas funções necessárias, retornar códigos de erro (`int`/enum) quando aplicável, evitar vazamento de detalhes internos.
- [ ] **5. Testes unitários C**: criar `tests/c_core/test_<modulo>.c` (Unity). Cobrir casos felizes + borda + erros. Registrar no CMake/CTest.
- [ ] **6. Bindings ctypes**: criar `code_review_graph/_c_core/bindings_<modulo>.py` (mapear structs, ponteiros, conversões Python↔C). Lidar com strings/bytes, arrays, listas/dicts com conversão explícita (evitar cópias desnecessárias).
- [ ] **7. Wrapper Python com backend switch**: adaptar `code_review_graph/<modulo>.py` para delegar a backend C quando `CRG_BACKEND=c` (ou por módulo). **Garantir API pública idêntica**. Manter caminho Python como fallback/referência.
- [ ] **8. Testes de paridade**: criar/expandir `tests/parity/test_parity_<modulo>.py` com fixtures de `tests/fixtures/porting/`. Comparar resultados (estruturas, conjuntos, ordem quando relevante, valores numéricos). Falhar se houver qualquer divergência semântica.
- [ ] **9. Validação cruzada**: rodar `pytest -q` (módulo + dependentes) e testes C (`ctest --output-on-failure`) localmente. Exigir **paridade 100%** nos casos críticos; cobrir casos de borda relevantes.
- [ ] **10. Limpeza e commit atômico**: headers+src+bindings+tests+wrappers. Mensagem de commit clara (módulo, abordagem híbrida, usa transpile-to-c-safety).

### 6.2 Critérios de aceitação por módulo (Hybrid)

- [ ] Build C local (`cmake --build build`) sem warnings relevantes (clang).
- [ ] `ctest` passa 100% para o módulo.
- [ ] Testes de paridade passam 100% (Python==C) para fixtures cobertas.
- [ ] Pytest dos módulos afetados não regrediu vs baseline.
- [ ] Sem vazamentos óbvios em caminhos testados (smoke básico). Ownership de alocações documentado.
- [ ] Código C segue safety checks (sem casts arriscados desnecessários, checagens de NULL/bounds explícitas).
- [ ] API Python pública **inalterada** (consumidores CLI/MCP/tools não mudam).

## 7. Fase 2 — Full-C (após híbrida estável)

**Pré-requisito:** Fase híbrida completa (core + fluxos + enrich + persistência crítica) com paridade validada, backend `c` estável para core, testes C verdes e paridade sólida.

Objetivo: migrar camadas restantes para C, reduzindo ponte Python→C ao mínimo. **Incremental**, nunca big-bang.

### 7.1 Arquitetura Full-C (proposta)

```text
crg/                        # raiz C (biblioteca + binários)
  include/crg/
    graph.h, flows.h, ...
    parser.h, resolvers.h
    registry.h, store.h
    tools_api.h             # API tools
    cli.h
    mcp/                    # MCP protocol (subset)
  src/
    core/, parsing/, store/, tools/, cli/, mcp/
  CMakeLists.txt
  README.md

tests/c/                    # testes C (unificados)
  unit/, parity/, integration/, smoke/
```

Binários previstos (locais): `crg-cli` (CLI), opcional `crg-mcp` (servidor MCP standalone em C). Visualização/Eval podem **permanecer em Python/JS** inicialmente (não crítico) ou migrar por último.

### 7.2 Ordem de migração Full-C

| Ordem | Domínio | Arquivos/origem | Notas |
|---|---|---|---|
| **F1.** | **Core consolidation** | Consolidar `c_core` → `crg/core` (headers namespace/paths). Sem lógica nova. | Manter API estável, renomear paths com cuidado. |
| **F2.** | **Parsing/AST boundary** | `parser.py` + subset resolvers | Usar **tree-sitter C API** diretamente. Mapear AST→modelos internos C. Aplicar `transpile-to-c-safety` em lógica de resolução (não bindings). Complexidade média–alta. |
| **F3.** | **Resolvers (incremental)** | `python_resolver.py`, `tsconfig_resolver.py`, `spring_resolver.py`, `hcl_resolver.py`, `rescript_resolver.py`, `event_resolver.py`, `jedi_resolver.py`, `scoped_resolver.py`, `temporal_resolver.py` | **Um por vez**. Validar com fixtures de parsing. tree-sitter facilita. Considerar manter alguns resolvers complexos por último ou avaliar ROI. |
| **F4.** | **Persistência/Store** | `registry.py`, `migrations.py`, `memory.py`, `build_state.py`, `incremental.py` | SQLite C API direto (`sqlite3.h`). Reescrever migrations em lógica C (sem ORM). Atenção a schema/compatibilidade de arquivos existentes (não quebrar DBs locais de usuários). |
| **F5.** | **Tools layer (MCP)** | `tools/*.py` (`query.py, flows_tools.py, review.py, refactor_tools.py, context.py, community_tools.py, build.py, analysis_tools.py, docs.py, registry_tools.py`) | Maior bloco de trabalho. MCP protocol (JSON-RPC + stdio) requer implementação mínima em C (sem FastMCP). **Estratégia: subset mínimo viável** (tools essenciais p/ casos de uso) antes de migrar todos. Evitar reescrever 1:1 sem necessidade. |
| **F6.** | **CLI** | `cli.py`, `main.py` | CLI fino (typer/click→argp/getopt ou clopts). Perde rich/TUI, mas funcional. Migrar após tools estabilizarem contratos. |
| **F7.** | **Embeddings** | `embeddings.py` | **Alto custo/complexidade**. Opções: (a) manter como camada opcional externa (Python) com IPC mínimo, (b) integrar ONNX Runtime C (mais pesado), (c) **feature-flag + adiar/descartar por escopo** (não bloquear full-C). **Recomendação:** adiar para pós-full-C ou manter opcional (não obrigatório p/ full-C funcional). |
| **F8.** | **Eval/Benchmarks/Visualização** | `eval/*`, `visualization.py`, `wiki.py` | **Não crítico para runtime**. Melhor manter em Python/JS (HTML/D3). Não priorizar na migração Full-C. Pode permanecer híbrido indefinidamente com baixo acoplamento. |

### 7.3 Regras Full-C (aplicação)

- [ ] **Transpile-to-c-safety obrigatório** por arquivo/módulo migrado.
- [ ] **Paridade 100%** entre implementação anterior (Python ou híbrida) e nova C (testes de paridade com fixtures ampliados).
- [ ] **Migração por domínio completo + estável** (não deixar meio-migrado com contratos instáveis).
- [ ] **Thin boundaries**: isolar JSON (cJSON/json-c), SQLite, I/O, stdio (MCP). Facilita testes unitários (mockable por camadas).
- [ ] **Backward-compatibilidade lógica**: preservar formatos de dados/saídas (CLI, tools) para evitar quebras em consumidores.
- [ ] **Testes C unificados**: migrar/expandir para `tests/c/` (unit+parity+integration+smoke). Manter pytest apenas onde camadas ainda Python (transição) ou remover gradualmente quando domínio migrado estiver coberto 100% por C tests.

### 7.4 Critérios de aceitação Full-C (por domínio)

- [ ] Build C local limpo (CMake), sem warnings relevantes.
- [ ] `ctest` 100% (unit + parity + integration + smoke do domínio).
- [ ] Paridade validada contra baseline (fixtures abrangentes + casos reais).
- [ ] Smoke end-to-end local (CLI path crítico mínimo) passa.
- [ ] Sem regressões funcionais detectadas vs fase híbrida.
- [ ] Ownership/alocações claros, sem vazamentos em caminhos testados.
- [ ] Domínio migrado cobre seu escopo com testes C suficientes (não depender exclusivamente de Python).

## 8. Estratégia de Testes (obrigatória, local, sem CI)

**Sem GitHub Actions.** Toda execução é **local**.

| Tipo | Local | Ferramenta | Propósito |
|---|---|---|---|
| **Unit C** | `tests/c_core/`, `tests/c/unit/` | **Unity** (recomendado) + CTest | Cobrir lógica pura (grafo/fluxos). Rápido, isolado. |
| **Paridade** | `tests/parity/` | **pytest** | Compara Python×C (fixtures determinísticas). Garante equivalência semântica. **Obrigatório por módulo**. |
| **Integração** | `tests/c/integration/` (Full-C) | Unity/CTest | Fluxos multi-módulo (build/registry/flows). |
| **Smoke** | `tests/c/smoke/` (Full-C) | CTest | Caminhos críticos end-to-end mínimos (CLI/tools essenciais). |
| **Regressão** | Raiz (pytest) | **pytest** | Manter suíte existente como guarda de regressão durante híbrida. Gradualmente pode ser enxugada à medida que domínios Full-C cobrem com C tests. |
| **Fixtures** | `tests/fixtures/porting/` | JSON/py/structs | Casos mínimos, borda, determinísticos (reprodutíveis). |

### 8.1 Requisitos de testes
- [ ] **Determinísticos**: sem time/random não controlado em fixtures de paridade.
- [ ] **Cobertura por módulo**: unit C + parity obrigatórios antes de avançar próximo módulo.
- [ ] **Zero divergência** em parity tests para casos cobertos (falha bloqueia avanço).
- [ ] **Testes locais documentados** (comandos de execução por fase) — ver Seção 10.

## 9. Build & Execução Local (sem CI)

**Apenas local.** Sem criação de workflows.

### 9.1 CMake (c_core — Híbrida)

```bash
cmake -S c_core -B c_core/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build c_core/build -j
ctest --test-dir c_core/build --output-on-failure
```

### 9.2 CMake (Full-C)

```bash
cmake -S crg -B crg/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build crg/build -j
ctest --test-dir crg/build --output-on-failure
```

### 9.3 Python (híbrida, validação)

```bash
# Backend Python (referência)
CRG_BACKEND=python pytest -q

# Backend C (validado por módulo)
CRG_BACKEND=c pytest -q tests/parity tests/<módulos_afetados> -v

# Suite completa (regressão)
pytest -q
```

### 9.4 Descoberta da lib compartilhada (bindings)
`ctypes_loader.py` deve buscar em ordem: `c_core/build/`, `c_core/build/lib*`, `build/lib*/`, caminhos relativos ao repo (dev local). Sem hardcode de paths absolutos. Permite build local em diferentes layouts.

## 10. Ordem de Execução Recomendada (Plano de Trabalho)

> Sequência **faseada**. Avançar **só** quando critérios da fase/módulo atendidos.

| Fase | Ação | Branch | Gate (bloqueio p/ próxima) |
|---|---|---|---|
| **P0** | Criar `c-port/hybrid-core`. Estruturar `c_core/`, CMake, Unity, `tests/c_core`, `tests/parity`, `tests/fixtures/porting`, backend flag. | `c-port/hybrid-core` | Build C vazio OK + estrutura testes criada. |
| **P1** | **Módulo 1**: `neighbourhood.py` → C via `transpile-to-c-safety`. Headers+.c + bindings + unit C + parity + wrapper. Validar. | `c-port/hybrid-core` | Unit C + parity 100%. Pytest módulo OK. |
| **P2** | **Módulo 2**: `graph.py` → C. Depende P1. Validar paridade cruzada. | `c-port/hybrid-core` | 100% unit+parity. Sem regressões. |
| **P3** | Merge/estabilizar core (neigh+graph). Testar com `CRG_BACKEND=c` em caminhos básicos. | `c-port/hybrid-core` | Estável. Core híbrido funcional. |
| **P4** | Criar `c-port/hybrid-flows`. **Módulo 3**: `graph_diff.py` → C. | `c-port/hybrid-flows` | Unit+parity 100%. |
| **P5** | **Módulo 4**: `flows.py` → C (maior lógica). Paridade abrangente. | `c-port/hybrid-flows` | Unit+parity 100%. Cobrir casos de borda críticos. |
| **P6** | Estabilizar flows. Validar com pytest relevante + parity. | `c-port/hybrid-flows` | Core+flows híbridos estáveis com C backend. |
| **P7** | `c-port/hybrid-enrich`: **Módulo 5** `enrich.py`, **Módulo 6** `postprocessing.py` (podem ser encadeados). | `c-port/hybrid-enrich` | Unit+parity 100% por módulo. |
| **P8** | `c-port/hybrid-persistence`: **Módulo 7–8** (registry/migrations/build_state/incremental). **Avaliar por submódulos** (migrações com cautela). Só migrar lógica pura p/ C; I/O fino pode permanecer wrapper se complexidade alta. | `c-port/hybrid-persistence` | Paridade 100% + não quebra arquivos existentes (smoke). |
| **P9** | **Decisão de parada híbrida vs parsers**: `c-port/hybrid-parsers` **opcional** (subset). Não obrigatório p/ passar p/ Full-C. Recomendado **não forçar** agora. | (opcional) | Só se ROI claro. Caso contrário pular. |
| **P10** | **Gate Híbrido → Full-C**: validar backend `CRG_BACKEND=c` estável em core completo, testes C 100%, parity 100% em todos módulos híbridos, pytest regressão aceitável/sem blocantes críticos. | Base estável | **Pré-requisito atendido**. Criar `c-port/full-c`. |
| **P11** | **Full-C F1**: consolidar `c_core` → `crg/core` (estrutura + paths). Sem lógica. | `c-port/full-c` | Build+ctest OK, sem mudança semântica. |
| **P12** | **Full-C F2–F8**: migrar por domínios (parser→resolvers→store→tools→cli→embeddings→eval). **Um domínio por vez**. Aplicar `transpile-to-c-safety` + unit+parity+integration+smoke C. Embeddings/eval: **adiar** se alto esforço (ver 7.2). | `c-port/full-c` (+ filhas) | Domínio completo + estável antes próximo. |
| **P13** | **Hardening Full-C**: smoke end-to-end CLI mínimo, revisão safety (ownership, bounds), checagens de alocação. | `c-port/full-c` | Smoke C OK, testes C verdes, paridade global satisfatória. |

## 11. Riscos, Mitigações e Tradeoffs

| Risco | Impacto | Prob. | Mitigação |
|---|---|---|---|
| **Divergência semântica** (Python dinâmico vs C estrito) | Alto | Médio | **Paridade obrigatória** por módulo + fixtures de borda + comparação determinística. Manter backend Python como referência até validação completa. |
| **Vazamentos/UB** (ownership) | Alto | Baixo–Médio | Seguir **transpile-to-c-safety** rigorosamente, checagens NULL/bounds explícitas, documentar ownership, smoke com alocações. Revisar por módulo. |
| **tree-sitter C API** (parsers/resolvers Full-C) | Médio–Alto | Médio | Adiar parsers Full-C (fase opcional). Manter em Python na híbrida. Migrar subset pequeno primeiro, com fixtures estáveis. |
| **MCP protocol em C (tools Full-C)** | Alto | Médio | **Subset mínimo viável** primeiro (tools essenciais). Não reescrever todos de uma vez. Considerar manter tools/CLI/MCP em Python indefinidamente (híbrido pode ser estado final viável). |
| **Embeddings (Full-C)** | Alto | Médio | **Adiar/feature-flag**. Não bloqueante p/ full-C funcional. Manter opcional externo. |
| **SQLite/migrations compatibilidade** | Médio–Alto | Baixo | Testar com arquivos existentes (smoke). Lógica de migração portada com cautela, validar ordem/semântica vs Python. |
| **Verboso ctypes (containers)** | Médio | Baixo | Converter listas/dicts explicitamente (helpers em bindings). Se ficar excessivo em poucos pontos, avaliar tradeoff pontual (sem adotar pybind11 por default). |
| **"Big bang" Full-C** | Alto | Alto | **Proibido.** Incremental por domínio + gates obrigatórios. |

### 11.1 Tradeoffs importantes (clarificação)

- **Híbrido vs Full-C**: híbrido reduz drasticamente risco/tempo, mantém ecossistema (MCP/CLI/eval/visualização) intacto. Full-C dá portabilidade/footprint potencial, mas com **custo alto** em MCP+embeddings.
- **ctypes vs pybind11**: ctypes escolhido p/ simplicidade/estabilidade ABI e menor complexidade local (sem build deps extras). Tradeoff: mais código de binding manual (aceitável, isolado em `_c_core/`).
- **Sem CI GitHub Actions**: atende solicitação. Em compensação, **testes locais obrigatórios** por fase (ctest + pytest + parity). Documentação de comandos local é mandatória.
- **Unity vs Catch2**: Unity (C puro, leve) favorece objetivo "C puro/seguro". Recomendado.

## 12. Critérios Globais de Aceitação

### 12.1 Gate Híbrido (pronto p/ Full-C)
- [ ] Todos módulos 1–8 híbridos com **unit C + parity 100%**.
- [ ] `CRG_BACKEND=c` funciona estável para core+flows+enrich+persistência crítica (smoke básico).
- [ ] `ctest` (c_core) 100% em todos módulos migrados.
- [ ] `pytest` (regressão) sem regressões bloqueantes vs baseline documentado.
- [ ] Bindings cobrem todos casos usados, sem memory leaks óbvios em caminhos testados.
- [ ] Estrutura `c_core/` estável, API C pública coesa, ownership documentado.

### 12.2 Gate Full-C (conclusão por domínio/geral)
- [ ] Domínios migrados com **unit+parity+integration+smoke C 100%** (CTest).
- [ ] Smoke end-to-end CLI mínimo passa localmente (caminho crítico).
- [ ] Paridade global entre estado híbrido e Full-C validada para domínios migrados.
- [ ] Código C segue princípios `transpile-to-c-safety` (sem violações óbvias). Alocações/liberações corretas.
- [ ] Camadas não críticas (eval/visualização) podem permanecer Python/JS sem bloquear conclusão do escopo Full-C funcional.

## 13. Plano de Rollback

- **Por módulo**: reverter commit atômico (git revert) se paridade/unit falharem. Fácil por granularidade.
- **Por fase**: branches separadas permitem descartar branch inteira sem afetar `main`.
- **Fallback runtime**: feature flag `CRG_BACKEND=python` permanece **sempre disponível** durante fase híbrida (reduz risco de lock-in). Na transição Full-C, caminhos Python são removidos **apenas por domínio** após domínio equivalente C estar 100% validado e com smoke verde.
- **Preservar baseline**: guardar resultados pytest baseline (lista + estado) antes de P0 para comparação objetiva de regressões.

## 14. Documentação (planejada, sem editar agora)

Local sugerido para documentar à medida que avança (planejamento apenas):

- `docs/PORTING_C.md` — visão geral, arquitetura híbrida/full-c, build local, testes, troubleshooting.
- `c_core/README.md` — build/test C local, convenções, API pública.
- `crg/README.md` (Full-C) — build local, binários, testes C.
- Comentários em headers `.h` (API pública) + `ponytail:` apenas quando simplificações deliberadas (conforme regra ponytail).

## 15. Resumo Executivo

- **Viável:** Híbrido **SIM**. Full-C **SIM (incremental, com tradeoffs)**. Full-C "big bang" **NÃO**.
- **Abordagem:** **Híbrido primeiro → Full-C depois**, incremental por módulo, com **gates obrigatórios**.
- **Segurança:** **`transpile-to-c-safety` obrigatório** em toda conversão.
- **Qualidade:** **Paridade 100% + unit C + parity tests** obrigatórios. **Sem CI GitHub Actions**, **com testes locais completos** (CTest + pytest).
- **Risco controlado:** feature flag backend, branches separadas, rollback por commit, adiar domínios altos (embeddings/MCP completo/parsers) quando ROI não justificar agora.

**Recomendação de execução:** seguir estritamente P0→P13 com critérios de aceitação por fase. O estado final **híbrido** já é um ponto estável e viável; **Full-C** pode ser concluído incrementalmente conforme necessidade, sem pressionar big-bang.
