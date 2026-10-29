# Instalando el Black en el iPhone (iOS)

🇧🇷 [Português](IOS-INSTALL.md) · 🇺🇸 [English](IOS-INSTALL.en.md) · 🇪🇸 **Español**

Esta guía cubre las tres formas más usadas de instalar el `Black-iOS-0.2.4.ipa`:

- **A — Sideloadly** (PC con Windows o macOS, cable USB, firma con tu Apple ID)
- **B — 3uTools** (PC con Windows; instala la app y también copia los archivos del juego)
- **C — directo en el teléfono** (TrollStore, o AltStore/SideStore tras instalarlo una vez desde el PC)

En todos los casos la app es la misma: el port no incluye el juego. Necesitas tu propia copia de Black (la carpeta con `default.xbe` o un `.iso`).

---

## Antes de empezar

| Elemento | Detalle |
|---|---|
| iOS | 15 o superior |
| Archivo de la app | `Black-iOS-0.2.4.ipa` (en la página de Releases) |
| Juego | carpeta `game` con `default.xbe` + `chars`, `data`, `export`, `language`, `levels`, `sound`, `videos` — **o** un único `.iso` |
| Cuenta Apple | necesaria para firmar (Sideloadly/3uTools/AltStore). Una cuenta gratis dura **7 días** por instalación |
| TrollStore | solo en iOS 14.0–16.6.1 (y 17.0 en algunos equipos); ahí el `.ipa` **no** necesita firma |

> La app no descarga nada de internet. Los archivos del juego entran por cable (iTunes/3uTools) o por la app **Archivos** del iPhone.

---

## A — Sideloadly (PC + cable)

Sideloadly firma el `.ipa` con tu Apple ID y lo instala por cable. Funciona en Windows y macOS.

### 1. En el PC

1. Descarga **Sideloadly** de <https://sideloadly.io> e instálalo.
2. En Windows, instala también **iTunes** (el de Apple, no el de la Microsoft Store) — es el que trae los drivers del iPhone. En macOS no hace falta.
3. Conecta el iPhone con el cable y toca **Confiar** en la pantalla del teléfono. Escribe el código si lo pide.

### 2. Instalar

1. Abre **Sideloadly**.
2. En **iOS Device**, elige tu iPhone.
3. Arrastra `Black-iOS-0.2.4.ipa` a la ventana (o pulsa el icono del `.ipa` y elige el archivo).
4. En **Apple ID**, escribe tu correo de Apple. La contraseña va en una ventana del propio Sideloadly (no en la app).
   - Con verificación en dos pasos, escribe el código cuando lo pida.
5. Pulsa **Start**.
6. Espera a que termine la barra; la app aparece en la pantalla de inicio.

### 3. Confiar en la app (solo la primera vez)

En el iPhone: **Ajustes → General → VPN y gestión de dispositivos → [tu Apple ID] → Confiar**.

Después abre **Black**.

> La cuenta gratis firma por **7 días**. Cuando la app deje de abrirse, repite el paso 2 (Sideloadly reinstala encima y los datos en Documents se mantienen).

---

## B — 3uTools (PC + cable)

3uTools instala el `.ipa` y además es la forma más directa de copiar el juego dentro de la app.

### 1. En el PC

1. Descarga **3uTools** de <https://www.3u.com> e instálalo.
2. Conecta el iPhone por cable y toca **Confiar**.
3. Si 3uTools lo pide, deja que instale su driver/“3uTools mobile” y confía en él en el teléfono.

### 2. Instalar la app

3uTools no instala un `.ipa` **sin firma** en un equipo normal. Usa una de estas:

- **Ya firmado:** `Apps → IPA Install` (o arrastra el `.ipa` a la ventana) y elige el archivo.
- **Firmar antes:** `Toolbox → IPA Signature` (en algunas versiones `Apps → IPA Signature`), elige el `.ipa`, entra con el Apple ID y fírmalo; después instala el archivo firmado con `IPA Install`.

En el iPhone, confía en el certificado como en el método A: **Ajustes → General → VPN y gestión de dispositivos**.

### 3. Copiar el juego

1. En 3uTools: **Apps** → busca **Black** en la lista → **Documents** (o “Browse”).
2. Arrastra ahí:
   - la carpeta `game` (con `default.xbe` dentro), o
   - cualquier archivo `.iso`.
3. Listo: la app busca `Documents/game`, `Documents/black/game` y cualquier `.iso` en `Documents` o `Documents/black`.

---

## C — En el propio teléfono (sin PC al instalar)

### C1 — TrollStore (lo más simple, cuando el iOS lo permite)

TrollStore instala `.ipa` **sin firma** y sin el límite de 7 días.

1. Comprueba si tu iOS/equipo es compatible (iOS 14.0–16.6.1, y 17.0 en algunos casos) en <https://github.com/opa334/TrollStore>.
2. Instala TrollStore con el método de tu iOS (TrollInstallerX, TrollHelper, etc.).
3. En el iPhone, descarga `Black-iOS-0.2.4.ipa` (Safari) y guárdalo en **Archivos**.
4. Abre **Archivos**, toca el `.ipa` → **Compartir** → **TrollStore** (o abre TrollStore → pestaña **Apps** → `+` → elige el `.ipa`).
5. Confirma la instalación y abre la app.

### C2 — AltStore / SideStore (renovación sin cable)

Necesita un PC **una vez** para instalar AltServer; después la renovación es por Wi-Fi.

1. En el PC: instala **AltServer** (<https://altstore.io>) e iTunes.
2. Conecta el iPhone y confía en el ordenador.
3. En AltServer (bandeja de Windows): **Install AltStore → [tu iPhone]** y entra con el Apple ID.
4. En el iPhone: **Ajustes → General → VPN y gestión de dispositivos** → confía en el perfil de AltStore.
5. Abre **AltStore** → **My Apps** → **+** (esquina superior) → elige el `Black-iOS-0.2.4.ipa` que está en **Archivos**.
6. AltStore firma e instala. La renovación ocurre con AltStore abierto y el PC en la misma red (o usa **SideStore**, que renueva solo).

### C3 — Copiar el juego sin PC

1. Descarga la carpeta del juego (o el `.iso`) en el iPhone y guárdala en **Archivos**.
2. En **Archivos**, mantén pulsada la carpeta/archivo → **Mover** → **En mi iPhone** → **Black**.
   - La carpeta `game` puede ir en `Black/game`; el `.iso` puede ir en la raíz de `Black`.
3. Abre Black.

---

## Estructura esperada de los archivos

```
En mi iPhone/Black/             <- la carpeta Documents de la app, visible en Archivos
├── game/
│   ├── default.xbe
│   ├── chars/  data/  export/  language/  levels/  sound/  videos/
└── (opcional) algún .iso
```

- El `default.xbe` debe estar **directamente** dentro de `game` (si hay una subcarpeta como `Black PT-BR` en medio, no lo encuentra).
- El `.iso` puede estar en la raíz de `Black` o dentro de `game`.
- Los logs están en `Black/logs/` — útiles si algo falla.

---

## Problemas comunes

| Síntoma | Qué hacer |
|---|---|
| “No se puede verificar la app” / la app no abre | Confía en el certificado en **Ajustes → General → VPN y gestión de dispositivos** |
| Funcionaba y dejó de abrir a los ~7 días | Firma caducada: reinstala con Sideloadly/AltStore/3uTools (los datos se mantienen) |
| Pantalla negra y vuelve al inicio | Juego no encontrado: revisa la estructura y mira `Black/logs/black_*.log` |
| Pantalla negra pero la app sigue abierta | Mira el log: la última línea antes de parar indica el paso (Vulkan, boot, …) |
| El juego corre pero no hay toque | El overlay de controles aparece al tocar la pantalla; el editor está en el menú del port → **TOUCH CONTROLS → EDIT LAYOUT** |
| Lento / tirones | En el menú del port, baja el **RENDER SCALE** (p. ej. 50–67%) — el iPhone se calienta y baja el reloj |

---

## Dónde descargar

- App: la página de **Releases** del repositorio (`Black-iOS-0.2.4.ipa`).
- Logs y ajustes: dentro de la app, en **Archivos → En mi iPhone → Black** (`black.toml`, `logs/`, `hdd/`).

El port no distribuye archivos del juego: usa tu propia copia de Black.
