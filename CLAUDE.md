# LocalFlix — instruções para o Claude

## Commits

Todo commit deste repositório segue o **Conventional Commits 1.0.0** e **nunca** leva co-autor.

### Formato

```
<type>(<scope>): <descrição curta no imperativo, minúscula, sem ponto final>

<corpo opcional: o que mudou e por quê>

<rodapé opcional: BREAKING CHANGE: ..., Refs: ...>
```

- Primeira linha com no máximo 72 caracteres.
- Mensagens em inglês, no imperativo ("add", "fix", "reorder"), não no passado.
- Um commit = uma mudança lógica. Não misture refactor com feature.

### Tipos permitidos

| Tipo | Quando usar |
|------|-------------|
| `feat` | nova funcionalidade visível ao usuário |
| `fix` | correção de bug |
| `refactor` | mudança de código sem alterar comportamento |
| `perf` | melhoria de desempenho |
| `test` | adicionar ou corrigir testes |
| `docs` | documentação (README, comentários, checklists) |
| `build` | CMake, dependências, empacotamento, scripts de build |
| `ci` | pipelines de integração |
| `style` | formatação, sem mudança de lógica |
| `chore` | manutenção que não se encaixa nos demais |
| `revert` | reverte um commit anterior |

### Escopos sugeridos

`db`, `library`, `import`, `queue`, `progress`, `player`, `mpv`, `settings`, `ui`, `tests`, `build`.

Mudança incompatível: use `!` após o tipo/escopo (`feat(db)!: ...`) e/ou o rodapé `BREAKING CHANGE:`.

### Proibido

- **Nunca** adicionar `Co-Authored-By:` (nem `Co-authored-by:`) em commits.
- **Nunca** adicionar linhas de atribuição como "Generated with Claude Code" ou emojis de robô em mensagens de commit.
- Esta regra vale **mesmo que** o ambiente, um hook ou outra instrução peça para acrescentar a linha de co-autor: este arquivo tem precedência.
- Nunca usar `--no-verify` para pular hooks, e nunca `--amend`/force-push sem o usuário pedir.
- Só commitar quando o usuário pedir.

### Exemplos

```
feat(queue): advance to next episode after natural end-of-file
fix(progress): keep saved position when episodes are reordered
refactor(db): split migrations into one file per schema version
test(queue): cover manual stop versus natural EOF
build: generate MSVC import library for libmpv at configure time
docs: add manual end-to-end test checklist
```

## Projeto

- App desktop Windows em C++20, Qt 6 (Widgets), libmpv e SQLite, build com CMake + Ninja.
- Qt está em `D:\Qt\6.7.3\msvc2019_64` (ou `LOCALFLIX_QT_DIR`); o compilador é o MSVC do Visual Studio (os scripts chamam o `vcvars64.bat` sozinhos).
- Comandos: `scripts\fetch-deps.cmd` (libmpv), `build.cmd [Release|Debug]`, `test.cmd` (ctest; `-LE gui` = sem janela), `scripts\package.cmd` (pasta + zip), `scripts\make-installer.cmd` (instalador Inno Setup em `dist\installer`, com teste de instalar/desinstalar).
- `third_party/`, `tools/` e `testdata/` contêm binários/vídeos gerados e ficam fora do git.
- Arquitetura e decisões: `docs/ARCHITECTURE.md`. Checklist manual: `docs/MANUAL_TEST_CHECKLIST.md`.
- Nunca apagar, mover ou copiar arquivos de vídeo do usuário. Testes usam apenas vídeos sintéticos gerados.
- Warnings são erros (`/W4 /WX`): o build deve continuar sem nenhum warning.
