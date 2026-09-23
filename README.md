# Canvas AI

A professional, layer-based raster image editor with an AI layer on top. The
editor is fully usable without AI; agents and plugins (later milestones) drive
it through the same command API as the UI, so everything they do is visible,
undoable and editable.

> This repository was previously an empty "Looper" placeholder; it now hosts the Canvas AI monorepo.

**Status:** Milestone 1 (foundation) — see [docs/milestones/M1.md](docs/milestones/M1.md).

## Quick start

```bash
pnpm install
pnpm dev              # web app at http://localhost:5173
pnpm test             # unit + CPU golden-image tests (Vitest)
pnpm test:e2e         # Playwright: app flows + GPU-vs-CPU golden images
pnpm bench            # 6000×4000 × 20-layer pan/zoom benchmark
pnpm typecheck
cargo test --manifest-path packages/pixel-wasm/Cargo.toml
```

Desktop (needs the [Tauri 2 prerequisites](https://v2.tauri.app/start/prerequisites/), e.g. WebKitGTK on Linux):

```bash
pnpm desktop:dev
pnpm desktop:build
```

Debug switches for the web app: `?renderer=webgpu|webgl2|cpu` forces a backend,
`?debug` exposes `window.canvasAI.store` in production builds, `?nostrict`
disables React StrictMode.

## Repository layout

```
apps/
  web/                 Vite + React entry for the browser (and harness.html for GPU tests/benchmarks)
  desktop/             Tauri 2 shell (src-tauri/) reusing the same UI with a native file platform
packages/
  core/                Document model, tile store, commands, history, command bus, .cnva/PNG I/O
  render/              Tile compositor: WebGPU → WebGL2 → CPU backends, dirty tracking, view math
  pixel-wasm/          Rust pixel kernels (wasm32 + native); JS paths are used until M2 filters
  ai/                  AI provider + agent contracts (implemented in M4/M5)
  plugins/             Plugin manifest format + validation (sandbox/runtime in M6)
  ui/                  React workspace: canvas view, Layers/History panels, menus, shortcuts
e2e/                   Playwright specs (*.spec.ts) and benchmarks (*.bench.ts)
docs/                  Architecture notes and milestone reports
```

## Architecture in one page

**Document model** (`packages/core/src/document.ts`). An immutable tree:
`Document { layers: LayerNode[] … }` where `LayerNode` is a discriminated union of
pixel, adjustment, text, shape, smart, group and AI-generated layers. Updates
use structural sharing, so an undo snapshot costs a few object references.

**Tile store** (`tiles.ts`). Pixels live in sparse, copy-on-write 256×256 tile
grids (`TileGrid`). Writing a pixel copies only the touched tile; untouched
tiles are shared between history states, duplicate layers and saved files. A
tile changed iff its object identity changed, which is what dirty tracking uses.

**Commands are the only way to mutate a document** (`command.ts`, `bus.ts`):

```ts
interface Command<P> {
  id: string;                 // "layer.setBlendMode"
  title: string;              // History panel
  category: string;
  agentDescription: string;   // the AI agent's documentation for this tool
  schema: ObjectSchema;       // validates params for UI, plugins and agents alike
  destructive?: boolean;      // needs approval in "Ask for destructive" mode
  describe?(params, doc): string;
  coalesceKey?(params): string | null;   // merge slider drags into one undo step
  execute(doc, params, ctx): Patch;      // Patch = invertible ops (+ optional result)
  invert?(patch): Patch;
}

bus.dispatch('layer.create', { name: 'Sky', above: layerId }, { actor });
bus.beginGroup('Warm the tones', { kind: 'agent', name: 'Color Agent', runId }); … bus.endGroup();
registry.toAgentTools(['layer.*']);      // tool list for a tool-calling LLM
```

The bus validates params against the schema, applies the patch, and records a
`HistoryEntry` with its actor (user / agent / plugin / macro). Groups (agent runs,
macros) undo in one step and can be expanded in the History panel;
`cancelGroup()` reverts a stopped run without leaving history behind.

**Rendering** (`packages/render`). For each 256×256 canvas tile, `buildTilePlan`
produces the minimal list of blend operations (layers, isolated groups,
pass-through groups). The same plan is executed by three backends:

| Backend | Where | Notes |
| --- | --- | --- |
| WebGPU | default | WGSL blend shaders, explicit mip generation |
| WebGL2 | fallback | GLSL port, `generateMipmap` |
| CPU | fallback + reference | float64 reference used by golden tests and export fallback |

Composited tiles are cached as premultiplied, mipmapped textures. Each frame
recomposites only dirty tiles (visible first, within a time budget) and draws
cached tiles, so pan/zoom cost doesn't depend on layer count. GPU results are
checked against the CPU reference within 1 LSB for all 26 blend modes and for
nested groups.

**Files.** `.cnva` is a zip: `document.json` (tile grids replaced by
references), `tiles/<grid>/<tx>_<ty>.png`, `thumbnail.png`, `history.json`.
Tiles shared between layers are stored once. PNG/JPEG/WebP/AVIF import and
export go through the platform codecs.

## Engineering rules

- The command API is the single path for mutations; no component mutates the document.
- Every command has `title`, `schema` and `agentDescription`.
- Pixel code is pure and deterministic; golden tests pin it.
- Tests ship with features; a milestone isn't done with failing tests.
