# Build de Windows — v0.4

## Opción recomendada: GitHub Actions

El workflow `.github/workflows/build-windows.yml`:

1. Descarga el template oficial `obsproject/obs-plugintemplate`.
2. Inyecta el código de Social Comments.
3. Compila Windows x64 Release.
4. Ejecuta el packaging oficial de OBS.
5. Localiza `obs-social-comments.dll`.
6. Compila un instalador Inno Setup.
7. Publica dos artifacts:
   - paquete portable;
   - instalador Setup x64.

### Variables opcionales/recomendadas

En GitHub: `Settings > Secrets and variables > Actions > Variables`:

- `SOCIAL_COMMENTS_GOOGLE_CLIENT_ID`
- `SOCIAL_COMMENTS_FACEBOOK_BROKER_URL`

Si se dejan vacías, el plugin sigue permitiendo introducir la configuración en `Configuración avanzada`.

## Build local

Requisitos:

- Visual Studio 2022 + Desktop development with C++.
- CMake compatible con el template actual de OBS.
- PowerShell 7 cuando lo requieran los scripts oficiales.
- Inno Setup 6 para producir el instalador.

Puedes compilar con el template oficial y luego ejecutar:

```powershell
.\scripts\make-installer.ps1 -DllPath "C:\ruta\obs-social-comments.dll" -Version "0.4.0"
```

## Preconfiguración local por variables de entorno

Antes de configurar CMake:

```powershell
$env:SOCIAL_COMMENTS_GOOGLE_CLIENT_ID="tu-client-id.apps.googleusercontent.com"
$env:SOCIAL_COMMENTS_FACEBOOK_BROKER_URL="https://oauth.tudominio.com"
```

El CMake incluido lee estas variables y las incorpora como valores por defecto del plugin.
