# Instalador Windows — v0.4

Archivo fuente:

`installer/social-comments.iss`

El instalador:

- requiere permisos de administrador;
- intenta usar `C:\Program Files\obs-studio` como destino;
- permite cambiar la ruta;
- verifica que exista `bin\64bit\obs64.exe`;
- instala únicamente el plugin dentro de `obs-plugins\64bit`;
- crea desinstalador propio.

## Crear el Setup

Con el DLL ya compilado:

```powershell
.\scripts\make-installer.ps1 -DllPath "C:\build\obs-social-comments.dll"
```

Salida esperada:

`release\Social-Comments-for-OBS-v0.4.0-Setup-x64.exe`

## Firma de código

v0.4 deja preparado el instalador pero **no incluye certificado de firma**. Para distribución pública, firma tanto el DLL como el Setup con tu certificado de code signing antes de publicar.
