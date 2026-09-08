# Social Comments for OBS — v0.4

Plugin nativo para OBS Studio que reúne comentarios de **YouTube Live** y **Facebook Live** en un dock y los muestra mediante un overlay moderno y limpio.

## Qué cambia en v0.4

La v0.4 convierte el proyecto en una base mucho más cercana a distribución real:

- **Instalador Windows x64** mediante Inno Setup.
- Workflow de GitHub Actions que genera:
  - paquete portable del plugin;
  - `Social-Comments-for-OBS-v0.4.0-Setup-x64.exe`.
- **OAuth broker opcional para Meta**, manteniendo el Meta App Secret fuera del DLL.
- Broker de referencia incluido en `broker/`, sin dependencias npm externas.
- Soporte para **Google Client ID preconfigurado en build**.
- Soporte para **Facebook Broker URL preconfigurada en build**.
- `Cerrar sesión` separado de `Revocar acceso`.
- Revocación programática de Google.
- Revocación de permisos de Facebook mediante Graph API.
- Sesiones/tokens locales siguen protegidos con Windows Credential Manager.
- El modo directo con Meta App ID + App Secret continúa disponible como respaldo técnico.

## Experiencia objetivo

En un build preconfigurado:

1. Instala el `.exe`.
2. Abre OBS.
3. `Docks > Social Comments`.
4. Pulsa **Conectar YouTube**.
5. Pulsa **Conectar Facebook**.
6. Autoriza en el navegador.
7. Pulsa **＋ Añadir overlay**.
8. Selecciona comentarios y usa **Mostrar** o **★ Destacar**.

No hace falta que el usuario final pegue tokens ni App Secrets.

## Funciones del plugin

- YouTube Live + Facebook Live simultáneos.
- Detección automática del directo activo de YouTube.
- Selector automático de Páginas y Lives de Facebook.
- Modo Manual y Automático.
- Mostrar / Destacar / Ocultar.
- Comentario destacado fijo hasta ocultarlo.
- Logos automáticos de plataforma.
- Avatar/iniciales.
- Presets Dark Clean / Minimal / Light Clean.
- Tipografía, tamaño, colores, fondo, opacidad, duración y animación.
- Browser Source creada desde el propio dock.
- Vista previa sin estar transmitiendo.

## Cuenta y privacidad

Dentro de `Cuenta y privacidad`:

- **Cerrar sesión**: detiene la conexión y borra el token guardado en este PC.
- **Revocar acceso**: además solicita al proveedor invalidar/retirar el permiso OAuth.

## Dos modos de OAuth para Facebook

### Producción recomendada — Broker

`OBS -> navegador -> broker HTTPS -> Meta -> broker -> callback local de OBS`

El broker intercambia el código con Meta sin exponer el App Secret en el plugin. Devuelve al navegador un código de un solo uso y el plugin lo canjea por HTTPS.

Configura en el build:

- `SOCIAL_COMMENTS_FACEBOOK_BROKER_URL=https://oauth.tudominio.com`

Consulta `broker/README.md` y `docs/OAUTH_PRODUCTION.md`.

### Modo técnico directo

Para pruebas/uso propio puedes seguir introduciendo:

- Meta App ID.
- Meta App Secret.

Este modo queda dentro de `Configuración avanzada`.

## YouTube en distribución

Puedes compilar el plugin con el Client ID ya incorporado:

- `SOCIAL_COMMENTS_GOOGLE_CLIENT_ID=...`

El flujo usa navegador del sistema, loopback local, `state`, PKCE S256, refresh token y renovación automática.

## Seguridad local

En Windows, cuando `Recordar sesión y credenciales` está activo, los tokens y secretos técnicos se guardan como credenciales genéricas en **Windows Credential Manager**, no en `QSettings`.

## Instalador

`installer/social-comments.iss` instala el DLL en:

`OBS_ROOT/obs-plugins/64bit/obs-social-comments.dll`

El instalador verifica que la carpeta seleccionada contenga `bin/64bit/obs64.exe`.

Consulta `docs/INSTALLER_WINDOWS.md`.

## Compilar

El workflow `.github/workflows/build-windows.yml` usa el template oficial de plugins OBS y Visual Studio 2022 en Windows.

Para un build de usuario final crea estas **Repository Variables** en GitHub:

- `SOCIAL_COMMENTS_GOOGLE_CLIENT_ID`
- `SOCIAL_COMMENTS_FACEBOOK_BROKER_URL`

Después ejecuta el workflow manualmente desde **Actions**.

## Compatibilidad objetivo

- Windows 10/11 x64.
- OBS Studio 30+.
- Qt 6.
- OBS Browser.

## Estado

**v0.4 — código fuente + OAuth broker de referencia + logout/revoke + pipeline de instalador Windows.**

El ZIP de este repositorio no contiene un DLL/EXE ya compilado porque la compilación Windows requiere el entorno y dependencias de OBS/Qt. El workflow incluido produce ambos artefactos en un runner Windows.
