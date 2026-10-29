# Instalando o Black no iPhone (iOS)

Este guia cobre as três formas mais usadas de instalar o `Black-iOS-0.2.4.ipa`:

- **A — Sideloadly** (PC com Windows ou macOS, cabo USB, assinatura com seu Apple ID)
- **B — 3uTools** (PC com Windows; instala e também copia os arquivos do jogo)
- **C — direto no celular** (TrollStore, ou AltStore/SideStore depois de instalado uma vez pelo PC)

Em todas elas o app é o mesmo: o port não inclui o jogo. Você precisa ter a sua cópia do Black (a pasta com `default.xbe` ou uma `.iso`).

---

## Antes de começar

| Item | Detalhe |
|---|---|
| iOS | 15 ou mais novo |
| Arquivo do app | `Black-iOS-0.2.4.ipa` (na página de Releases) |
| Jogo | pasta `game` com `default.xbe` + `chars`, `data`, `export`, `language`, `levels`, `sound`, `videos` — **ou** um único `.iso` |
| Conta Apple | necessária para assinar (Sideloadly/3uTools/AltStore). Conta grátis: validade de **7 dias** por instalação |
| TrollStore | só em iOS 14.0–16.6.1 (e 17.0 em alguns aparelhos); nele o `.ipa` **não precisa** de assinatura |

> O app não baixa nada da internet. Os arquivos do jogo entram por cabo (iTunes/3uTools) ou pelo app **Arquivos** do iPhone.

---

## A — Sideloadly (PC + cabo)

O Sideloadly assina o `.ipa` com o seu Apple ID e instala pelo cabo. Funciona no Windows e no macOS.

### 1. No PC

1. Baixe o **Sideloadly** em <https://sideloadly.io> e instale.
2. No Windows, instale também o **iTunes** (da Apple, não o da Microsoft Store) — é o que fornece os drivers do iPhone. No macOS não precisa.
3. Conecte o iPhone pelo cabo e toque em **Confiar** na tela do aparelho. Se pedir a senha do iPhone, digite.

### 2. Instalar

1. Abra o **Sideloadly**.
2. Em **iOS Device**, escolha o seu iPhone.
3. Arraste o `Black-iOS-0.2.4.ipa` para a janela (ou clique no ícone do `.ipa` e escolha o arquivo).
4. Em **Apple ID**, digite o seu e-mail da Apple. A senha vai num pop-up do próprio Sideloadly (não é no app).
   - Se a conta tiver autenticação em dois fatores, digite o código quando pedir.
5. Clique em **Start**.
6. Espere a barra terminar. O app aparece na tela inicial.

### 3. Confiar no app (obrigatório na primeira vez)

No iPhone: **Ajustes → Geral → VPN e Gerenciamento de Dispositivos → [seu Apple ID] → Confiar**.

Depois abra o **Black**.

> A conta grátis assina por **7 dias**. Quando o app parar de abrir, repita o passo 2 (o Sideloadly reinstala por cima e os dados em Documents são mantidos).

---

## B — 3uTools (PC + cabo)

O 3uTools instala o `.ipa` e também é o jeito mais direto de copiar os arquivos do jogo para dentro do app.

### 1. No PC

1. Baixe o **3uTools** em <https://www.3u.com> e instale.
2. Conecte o iPhone pelo cabo e toque em **Confiar**.
3. Se o 3uTools pedir, deixe instalar o driver/“3uTools mobile” e confie nele no aparelho.

### 2. Instalar o app

O 3uTools não instala `.ipa` **sem assinatura** em aparelho normal. Use um destes:

- **Já assinado:** `Apps → IPA Install` (ou arraste o `.ipa` para a janela) e escolha o arquivo.
- **Assinar antes:** `Toolbox → IPA Signature` (em algumas versões: `Apps → IPA Signature`), escolha o `.ipa`, entre com o Apple ID e assine; depois instale o arquivo assinado com `IPA Install`.

No iPhone, confie no certificado como no método A: **Ajustes → Geral → VPN e Gerenciamento de Dispositivos**.

### 3. Copiar o jogo

1. No 3uTools: **Apps** → encontre **Black** na lista → **Documents** (ou “Browse”).
2. Arraste para lá:
   - a pasta `game` (com `default.xbe` dentro), ou
   - um arquivo `.iso` qualquer.
3. Pronto: o app procura `Documents/game`, `Documents/black/game` e qualquer `.iso` em `Documents` ou `Documents/black`.

---

## C — Direto no celular (sem PC na hora de instalar)

### C1 — TrollStore (o mais simples, quando o iOS permite)

O TrollStore instala `.ipa` **sem assinatura** e sem prazo de 7 dias.

1. Confira se o seu iOS/aparelho é compatível (iOS 14.0–16.6.1, e 17.0 em alguns casos) em <https://github.com/opa334/TrollStore>.
2. Instale o TrollStore pelo método indicado para o seu iOS (TrollInstallerX, TrollHelper etc.).
3. No iPhone, baixe o `Black-iOS-0.2.4.ipa` (Safari) e salve em **Arquivos**.
4. Abra o **Arquivos**, toque no `.ipa` → **Compartilhar** → **TrollStore** (ou abra o TrollStore → aba **Apps** → `+` → escolha o `.ipa`).
5. Confirme a instalação e abra o app.

### C2 — AltStore / SideStore (renovação sem cabo)

Precisa de um PC **uma vez** para instalar o AltServer; depois a renovação é por Wi-Fi.

1. No PC: instale o **AltServer** (<https://altstore.io>) e o iTunes.
2. Conecte o iPhone, confie no computador.
3. No AltServer (bandeja do Windows): **Install AltStore → [seu iPhone]** e entre com o Apple ID.
4. No iPhone: **Ajustes → Geral → VPN e Gerenciamento de Dispositivos** → confie no perfil do AltStore.
5. Abra o **AltStore** → aba **My Apps** → **+** (canto superior) → escolha o `Black-iOS-0.2.4.ipa` que está em **Arquivos**.
6. O AltStore assina e instala. A renovação acontece com o AltStore aberto e o PC na mesma rede (ou use o **SideStore**, que renova sozinho).

### C3 — Copiar o jogo sem PC

1. Baixe a pasta do jogo (ou a `.iso`) no iPhone e salve em **Arquivos**.
2. Em **Arquivos**, segure a pasta/arquivo → **Mover** → **No Meu iPhone** → **Black**.
   - A pasta `game` pode ficar em `Black/game`; a `.iso` pode ficar na raiz de `Black`.
3. Abra o Black.

---

## Estrutura esperada dos arquivos

```
No Meu iPhone/Black/            <- a pasta Documents do app, visível no app Arquivos
├── game/
│   ├── default.xbe
│   ├── chars/  data/  export/  language/  levels/  sound/  videos/
└── (opcional) algum .iso
```

- O `default.xbe` precisa estar **direto** dentro de `game` (se vier uma subpasta tipo `Black PT-BR` no meio, não acha).
- A `.iso` pode ficar na raiz de `Black` ou dentro de `game`.
- Logs ficam em `Black/logs/` — útil se algo falhar.

---

## Problemas comuns

| Sintoma | O que fazer |
|---|---|
| “Não é possível verificar o app” / app não abre | Confie no certificado em **Ajustes → Geral → VPN e Gerenciamento de Dispositivos** |
| Funcionava e parou depois de ~7 dias | Assinatura vencida: reinstale com Sideloadly/AltStore/3uTools (os dados são mantidos) |
| Tela preta e volta para a tela inicial | O jogo não foi encontrado: confira a estrutura acima e veja `Black/logs/black_*.log` |
| Tela preta mas o app fica aberto | Veja o log: a última linha antes de parar indica o passo (Vulkan, boot etc.) |
| Jogo abre mas sem toque | O overlay de controles aparece ao tocar na tela; o editor fica em **menu do port → TOUCH CONTROLS → EDIT LAYOUT** |
| Travando/lento | No menu do port, baixe o **RENDER SCALE** (ex.: 50–67%) — o iPhone esquenta e reduz o clock |

---

## Onde baixar

- App: página de **Releases** do repositório (`Black-iOS-0.2.4.ipa`).
- Logs e configurações: dentro do app, em **Arquivos → No Meu iPhone → Black** (`black.toml`, `logs/`, `hdd/`).

O port não distribui nenhum arquivo do jogo: use a sua própria cópia do Black.
