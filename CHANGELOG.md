# Changelog

## 0.4.0

- Añadido instalador Windows x64 con Inno Setup.
- Añadido pipeline CI para generar paquete portable + Setup EXE.
- Añadido broker OAuth de referencia para Meta sin App Secret en el DLL.
- Añadido soporte de broker en el cliente OAuth del plugin.
- Añadidas variables de build para Google Client ID y Meta Broker URL.
- Añadida sección `Cuenta y privacidad`.
- Añadido `Cerrar sesión` local para YouTube/Facebook.
- Añadida revocación programática de Google OAuth.
- Añadida revocación de permisos de Facebook.
- Restauración de Facebook ya no exige App ID local cuando existe una sesión/broker.
- Modo técnico directo de Meta preservado como fallback.

## 0.3.0

- OAuth de Google con loopback + PKCE.
- OAuth directo de Facebook.
- Detección automática de directos y Páginas.
- Refresh token de Google.
- Credential Manager en Windows.
