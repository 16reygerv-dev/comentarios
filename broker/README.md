# OAuth Broker — Social Comments v0.4

Broker mínimo sin dependencias externas para mantener el **Meta App Secret fuera del DLL**.

## Requisitos

- Node.js 20+.
- HTTPS público delante del proceso (reverse proxy, plataforma serverless/container, etc.).
- Variables de entorno de `.env.example`.

## Endpoints

- `GET /health`
- `GET /facebook/start?state=...&return_uri=...`
- `GET /facebook/callback`
- `POST /facebook/exchange`

El broker nunca devuelve el token de Meta en la URL. El navegador recibe un `broker_code` de un solo uso; el plugin lo intercambia por HTTPS.

## Meta

En Facebook Login registra como Redirect URI el callback HTTPS del broker:

`https://TU-DOMINIO/facebook/callback`

El callback local `http://127.0.0.1:18765/oauth/callback/facebook` se usa entre el broker y OBS, y debe figurar en `ALLOWED_RETURN_URIS` del broker.

## Producción

- Termina TLS con HTTPS real.
- Mantén `META_APP_SECRET` solo en secretos del servidor.
- No registres tokens ni cuerpos OAuth.
- Añade rate limiting persistente si ejecutas múltiples réplicas.
- Considera Redis/DB con TTL para transactions/codes si necesitas más de una instancia.
- Restringe `ALLOWED_RETURN_URIS` al callback exacto del plugin.
