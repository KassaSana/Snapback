/// <reference types="vite/client" />

// Nothing here declares an `ImportMetaEnv`. The dashboard reads no build-time env
// var at all -- it talks to the native app over `window.__snapback` (see bridge.ts),
// never over HTTP. A `VITE_API_BASE` used to be declared here alongside a
// `frontend/.env.example` pointing at `http://localhost:8080`; no code ever read it,
// and it described a backend this project does not have. `vite/client` supplies the
// standard `import.meta.env` typings if one is ever genuinely needed.
