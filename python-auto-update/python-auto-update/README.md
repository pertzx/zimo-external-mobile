# StormMemory — Auto-Update de Offsets (v7a + v8a)

Ferramentas que geraram o V8.4 (`FFTHV7A76()` / `FFTHV8A()` do `Offsets.cpp`)
a partir dos `dump.cs` do Il2CppDumper. Na próxima atualização do jogo, rode
de novo — não precisa adivinhar offset.

## Requisitos

- Python 3.8+ (só stdlib, sem pip)
- O repo `zimo-external-mobile` com os dumps novos na raiz:
  - `dump_v7a.cs` (32-bit)
  - `dump_v8a.cs` (64-bit)
- O dump ANTIGO da geração atual (o "ponte", commit `8313c28`), usado só para
  alinhar campos ofuscados por índice. Se não tiver o arquivo, o script
  extrai sozinho do git: `git show 8313c28:dump_v7a.cs`.

## Configuração (opcional)

| Variável        | Default                        | O que é |
|-----------------|--------------------------------|---------|
| `ZIMO_REPO`     | `/home/z/my-project/repo-zimo` | Pasta do repo com os dumps |
| `STORM_BRIDGE`  | auto-detect                    | Dump antigo p/ alinhamento (tenta `dump_old_v7a.cs` no repo → `/tmp` → git) |

## Fluxo (3 passos)

```bash
# 0. (opcional) restaurar o dump-ponte manualmente
bash restore_bridge.sh /caminho/do/repo-zimo

# 1. resolve TODOS os campos usados pelo Offsets.cpp nos dumps novos
#    -> imprime tabela code_v76 | novo v7a | novo v8a | status
#    -> grava offsets_result.json
python3 build_table.py

# 2. gera os blocos novos mantendo os manuais (typeinfo/AccessClass/ViewMatrix)
#    -> new_v76.cpp e new_v8a.cpp
python3 gen_offsets.py

# 3. no Offsets.cpp, substitua o corpo de FFTHV7A76() e FFTHV8A()
#    pelos blocos gerados (assinaturas e chaves iguais).
```

## Estratégias de resolução de campo (por que não quebra com ofuscação)

Os nomes de campo são re-ofuscados a cada versão, então cada campo do
`Offsets.cpp` é mapeado por uma de 4 estratégias (definidas em
`extract_fields.py`):

- `('nome', Classe, Nome)` — campo com nome estável entre versões;
- `('tipo', Classe, Tipo, n)` — n-ésimo campo daquele TIPO na classe (o tipo
  quase nunca muda de nome, ex: `AvatarManager`, `PlayerAttributes`);
- `('bridge', Classe, NomeBridge)` — nome no dump antigo + alinhamento de
  índice com assinatura (visibilidade + tipo normalizado + gap de offset);
- `('fixo', valor)` — manual (typeinfos, `AccessClass`, ViewMatrix,
  `GetPosWorld`) — copiado como está.

Se um campo vier `None` na tabela, o pipeline AVISA (não gera silenciosamente
errado): `N falhas -> offsets_result.json` no final.

## Arquivos

| Arquivo | Papel |
|---|---|
| `dumplib.py` | Parser do dump.cs (classes → campos → offsets) |
| `extract_fields.py` | Estratégias de identificação de cada campo + dump-ponte |
| `build_table.py` | Passo 1: tabela completa + `offsets_result.json` |
| `gen_offsets.py` | Passo 2: gera `new_v76.cpp` / `new_v8a.cpp` |
| `update_offsets.py` | Versão antiga one-shot (legado, mantida por referência) |
| `audit_v8a2.py` | Auditoria pontual do perfil v8a por ranges de linha |
| `restore_bridge.sh` | Extrai o dump-ponte do git history |

## Validado

- `build_table.py`: **103 campos, 0 falhas** nos dumps atuais;
- Blocos gerados = **byte-idênticos em valores e ordem** ao `Offsets.cpp`
  V8.4 commitado (`f107f93`) para v7a e v8a (119 atribuições cada).

## Regras que NÃO mudam

- Typeinfos (`*_TypeInfo`) são manuais — ignorem o que o dump disser;
- `AccessClass` é manual;
- `GetPosWorld` (v8a fixed) e ViewMatrix são manuais;
- Leitura/escrita continuam em syscall direto (`pread64`/`pwrite64`) — isso
  é camada de RW, não de offsets; os scripts só tocam offsets.
