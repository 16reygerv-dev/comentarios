# APIs / OAuth — v0.4

## YouTube

Recomendado para distribución:

- habilita YouTube Data API v3;
- crea OAuth Client de escritorio;
- incorpora el Client ID en el build con `SOCIAL_COMMENTS_GOOGLE_CLIENT_ID`.

Scope utilizado:

`https://www.googleapis.com/auth/youtube.readonly`

Fallback técnico: API key + URL/ID del directo.

## Facebook

Recomendado para distribución:

- crea la app Meta correspondiente;
- configura permisos de Páginas necesarios para tu caso y revisión de Meta;
- aloja `broker/` detrás de HTTPS;
- registra el callback HTTPS del broker en Facebook Login;
- incorpora `SOCIAL_COMMENTS_FACEBOOK_BROKER_URL` en el build.

El plugin consulta Páginas, Lives y comentarios con la Graph API configurada en el código.

Fallback técnico: Meta App ID + App Secret o Page Access Token manual.
