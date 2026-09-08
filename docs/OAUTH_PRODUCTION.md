# OAuth de producción — v0.4

## Objetivo

`Instalar -> Conectar YouTube -> Conectar Facebook -> transmitir`

sin pedir al usuario final API keys, Page Tokens ni Meta App Secret.

## Google / YouTube

La app de escritorio mantiene:

- navegador del sistema;
- callback loopback `127.0.0.1`;
- `state` aleatorio;
- PKCE S256;
- scope `youtube.readonly`;
- refresh token;
- renovación del access token;
- revocación programática.

Para una distribución controlada, incorpora el Client ID con `SOCIAL_COMMENTS_GOOGLE_CLIENT_ID`.

## Meta / Facebook

No conviene incrustar el Meta App Secret en un DLL público. v0.4 incorpora un broker de referencia:

`Plugin -> /facebook/start -> Meta -> /facebook/callback -> broker_code -> callback local -> /facebook/exchange`

### Propiedades del diseño

- App Secret solo en servidor.
- El token no viaja en la URL de retorno local.
- `state` original validado por el plugin y el broker.
- `broker_code` aleatorio, corto y de un solo uso.
- Transactions y exchange codes expiran.
- `Cache-Control: no-store`.
- Callback local restringido a `127.0.0.1:18765`.
- Rate limit básico de referencia.

## Desplegar el broker

1. Usa Node 20+.
2. Configura variables de `broker/.env.example` en tu plataforma.
3. Expón el servicio detrás de HTTPS.
4. En Meta configura:
   `https://TU-DOMINIO/facebook/callback`
5. Compila el plugin con:
   `SOCIAL_COMMENTS_FACEBOOK_BROKER_URL=https://TU-DOMINIO`

## Escalado

El broker incluido usa memoria local para transactions/codes. En múltiples réplicas sustituye esos mapas por Redis/DB con TTL.

## Modo directo

Se conserva para pruebas y uso propio. Si Broker URL está vacía, el plugin usa Meta App ID + App Secret locales.
