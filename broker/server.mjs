import http from 'node:http';
import crypto from 'node:crypto';

const PORT = Number(process.env.PORT || 8787);
const PUBLIC_BASE_URL = String(process.env.PUBLIC_BASE_URL || '').replace(/\/+$/, '');
const META_APP_ID = String(process.env.META_APP_ID || '');
const META_APP_SECRET = String(process.env.META_APP_SECRET || '');
const META_GRAPH_VERSION = String(process.env.META_GRAPH_VERSION || 'v26.0');
const ALLOWED_RETURN_URIS = new Set(
  String(process.env.ALLOWED_RETURN_URIS || 'http://127.0.0.1:18765/oauth/callback/facebook')
    .split(',').map(v => v.trim()).filter(Boolean)
);

if (!PUBLIC_BASE_URL || !META_APP_ID || !META_APP_SECRET) {
  console.error('Missing PUBLIC_BASE_URL, META_APP_ID or META_APP_SECRET.');
  process.exit(2);
}

const transactions = new Map();
const exchangeCodes = new Map();
const rate = new Map();
const TX_TTL_MS = 5 * 60_000;
const CODE_TTL_MS = 2 * 60_000;

function now() { return Date.now(); }
function randomCode(bytes = 32) { return crypto.randomBytes(bytes).toString('base64url'); }
function prune() {
  const t = now();
  for (const [k, v] of transactions) if (v.expiresAt < t) transactions.delete(k);
  for (const [k, v] of exchangeCodes) if (v.expiresAt < t) exchangeCodes.delete(k);
  for (const [k, v] of rate) if (v.resetAt < t) rate.delete(k);
}
setInterval(prune, 30_000).unref();

function clientIp(req) {
  const forwarded = req.headers['x-forwarded-for'];
  return String(Array.isArray(forwarded) ? forwarded[0] : forwarded || req.socket.remoteAddress || '').split(',')[0].trim();
}

function allowRequest(req) {
  const ip = clientIp(req);
  const t = now();
  const bucket = rate.get(ip) || { count: 0, resetAt: t + 60_000 };
  if (bucket.resetAt < t) { bucket.count = 0; bucket.resetAt = t + 60_000; }
  bucket.count += 1;
  rate.set(ip, bucket);
  return bucket.count <= 60;
}

function baseHeaders() {
  return {
    'Cache-Control': 'no-store',
    'Pragma': 'no-cache',
    'X-Content-Type-Options': 'nosniff',
    'Referrer-Policy': 'no-referrer',
    'Content-Security-Policy': "default-src 'none'; frame-ancestors 'none'; base-uri 'none'"
  };
}

function json(res, status, body) {
  const bytes = Buffer.from(JSON.stringify(body));
  res.writeHead(status, { ...baseHeaders(), 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': bytes.length });
  res.end(bytes);
}

function redirect(res, location) {
  res.writeHead(302, { ...baseHeaders(), Location: location });
  res.end();
}

async function readJson(req, maxBytes = 8192) {
  let total = 0;
  const chunks = [];
  for await (const chunk of req) {
    total += chunk.length;
    if (total > maxBytes) throw new Error('body_too_large');
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
}

async function metaGet(path, params) {
  const u = new URL(`https://graph.facebook.com/${META_GRAPH_VERSION}${path}`);
  for (const [k, v] of Object.entries(params)) u.searchParams.set(k, v);
  const r = await fetch(u, { headers: { 'Accept': 'application/json' }, redirect: 'error' });
  const data = await r.json().catch(() => ({}));
  if (!r.ok || data.error) {
    const message = data?.error?.message || `Meta HTTP ${r.status}`;
    throw new Error(message);
  }
  return data;
}

function safeLocalReturnUri(value) {
  if (!ALLOWED_RETURN_URIS.has(value)) return false;
  try {
    const u = new URL(value);
    return u.protocol === 'http:' && u.hostname === '127.0.0.1' && u.port === '18765' && u.pathname === '/oauth/callback/facebook';
  } catch { return false; }
}

const server = http.createServer(async (req, res) => {
  try {
    if (!allowRequest(req)) return json(res, 429, { error: 'rate_limited' });
    const u = new URL(req.url, PUBLIC_BASE_URL);

    if (req.method === 'GET' && u.pathname === '/health') {
      return json(res, 200, { ok: true, service: 'social-comments-oauth-broker', version: '0.4.0' });
    }

    if (req.method === 'GET' && u.pathname === '/facebook/start') {
      const originalState = u.searchParams.get('state') || '';
      const returnUri = u.searchParams.get('return_uri') || '';
      if (originalState.length < 16 || !safeLocalReturnUri(returnUri))
        return json(res, 400, { error: 'invalid_request' });

      const tx = randomCode(24);
      transactions.set(tx, { originalState, returnUri, expiresAt: now() + TX_TTL_MS });
      const callback = `${PUBLIC_BASE_URL}/facebook/callback`;
      const auth = new URL(`https://www.facebook.com/${META_GRAPH_VERSION}/dialog/oauth`);
      auth.searchParams.set('client_id', META_APP_ID);
      auth.searchParams.set('redirect_uri', callback);
      auth.searchParams.set('response_type', 'code');
      auth.searchParams.set('state', tx);
      auth.searchParams.set('scope', 'pages_show_list,pages_read_engagement,pages_read_user_content');
      return redirect(res, auth.toString());
    }

    if (req.method === 'GET' && u.pathname === '/facebook/callback') {
      const tx = u.searchParams.get('state') || '';
      const transaction = transactions.get(tx);
      transactions.delete(tx);
      if (!transaction || transaction.expiresAt < now())
        return json(res, 400, { error: 'expired_or_invalid_state' });

      const providerError = u.searchParams.get('error');
      if (providerError) {
        const back = new URL(transaction.returnUri);
        back.searchParams.set('state', transaction.originalState);
        back.searchParams.set('error', providerError);
        back.searchParams.set('error_description', u.searchParams.get('error_description') || 'Autorización cancelada');
        return redirect(res, back.toString());
      }

      const code = u.searchParams.get('code') || '';
      if (!code) return json(res, 400, { error: 'missing_code' });

      const callback = `${PUBLIC_BASE_URL}/facebook/callback`;
      const short = await metaGet('/oauth/access_token', {
        client_id: META_APP_ID,
        client_secret: META_APP_SECRET,
        redirect_uri: callback,
        code
      });
      const shortToken = short.access_token;
      if (!shortToken) throw new Error('Meta did not return access_token');

      let userToken = shortToken;
      try {
        const long = await metaGet('/oauth/access_token', {
          grant_type: 'fb_exchange_token',
          client_id: META_APP_ID,
          client_secret: META_APP_SECRET,
          fb_exchange_token: shortToken
        });
        if (long.access_token) userToken = long.access_token;
      } catch {
        // Short-lived token is still usable; do not log token values.
      }

      const oneTime = randomCode(32);
      exchangeCodes.set(oneTime, {
        accessToken: userToken,
        originalState: transaction.originalState,
        expiresAt: now() + CODE_TTL_MS
      });

      const back = new URL(transaction.returnUri);
      back.searchParams.set('broker_code', oneTime);
      back.searchParams.set('state', transaction.originalState);
      return redirect(res, back.toString());
    }

    if (req.method === 'POST' && u.pathname === '/facebook/exchange') {
      const body = await readJson(req);
      const code = String(body.code || '');
      const state = String(body.state || '');
      const item = exchangeCodes.get(code);
      exchangeCodes.delete(code);
      if (!item || item.expiresAt < now() || item.originalState !== state)
        return json(res, 400, { error: 'invalid_or_expired_code' });
      return json(res, 200, { access_token: item.accessToken, token_type: 'bearer' });
    }

    return json(res, 404, { error: 'not_found' });
  } catch (error) {
    // Deliberately never log request bodies or tokens.
    console.error('Broker request failed:', error?.message || 'unknown_error');
    return json(res, 500, { error: 'broker_error', error_description: 'No se pudo completar la autenticación.' });
  }
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`Social Comments OAuth broker listening on :${PORT}`);
});
