/**
 * Plugin manifest (`plugin.json`, spec §7.2). The runtime/sandbox lands in
 * M6; the manifest format and its validation are defined now so the command
 * registry's pattern matching and permission names stay consistent.
 */

export type PluginType = 'code' | 'prompt' | 'macro';

export const PERMISSIONS = {
  'document.read': 'Read your document (layers, pixels, selection)',
  'document.write': 'Change your document',
  network: 'Access the network (limited to listed domains)',
  'ai.chat': 'Use your configured AI chat model',
  'ai.vision': 'Send images to your configured vision model',
  'ai.generate': 'Generate images with your configured provider',
  'ai.inpaint': 'Use AI inpainting',
  'ai.outpaint': 'Use AI outpainting',
  'ai.segment': 'Use AI segmentation',
  'ai.upscale': 'Use AI upscaling',
  'ai.bg_remove': 'Use AI background removal',
  'ui.panel': 'Add a panel to the workspace',
} as const;

export type Permission = keyof typeof PERMISSIONS;

export type UIPlacement = 'filters-menu' | 'agent-panel' | 'plugins-panel' | 'toolbar';

export interface PluginParameter {
  id: string;
  label?: string;
  type: 'number' | 'boolean' | 'string' | 'color' | 'enum';
  min?: number;
  max?: number;
  step?: number;
  options?: string[];
  default: unknown;
}

export interface PluginManifest {
  id: string;
  name: string;
  version: string;
  type: PluginType;
  /** Code entry (JS module) or instructions file (prompt plugins) or macro JSON. */
  entry: string;
  description?: string;
  ui?: { placement: UIPlacement[]; icon?: string };
  parameters?: PluginParameter[];
  permissions: Permission[];
  /** Command patterns the plugin may dispatch, e.g. ["crop.*", "adjust.*", "layer.create"]. */
  allowedCommands: string[];
  /** Required when `network` permission is requested. */
  networkAllowlist?: string[];
  models?: { preferred?: Record<string, string> };
  limits?: { cpuMs?: number; memoryMB?: number; maxAiSpendUsd?: number };
}

const ID_RE = /^[a-z0-9]+(\.[a-z0-9-]+){2,}$/;
const SEMVER_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$/;
const COMMAND_PATTERN_RE = /^(\*|[a-zA-Z][\w]*(\.[a-zA-Z][\w]*)*(\.\*)?)$/;

/** Validates an untrusted manifest. Returns human-readable problems (empty = valid). */
export function validateManifest(value: unknown): string[] {
  const errors: string[] = [];
  if (typeof value !== 'object' || value === null) return ['Manifest must be a JSON object'];
  const m = value as Record<string, unknown>;
  const str = (k: string) => typeof m[k] === 'string' && (m[k] as string).length > 0;
  if (!str('id') || !ID_RE.test(m.id as string)) errors.push('id must be reverse-DNS, e.g. "com.example.my-plugin"');
  if (!str('name')) errors.push('name is required');
  if (!str('version') || !SEMVER_RE.test(m.version as string)) errors.push('version must be semver, e.g. "1.0.0"');
  if (!['code', 'prompt', 'macro'].includes(m.type as string)) errors.push('type must be "code", "prompt" or "macro"');
  if (!str('entry') || (m.entry as string).includes('..') || (m.entry as string).startsWith('/')) errors.push('entry must be a relative path inside the package');
  if (!Array.isArray(m.permissions)) errors.push('permissions must be an array');
  else for (const p of m.permissions) if (!(p in PERMISSIONS)) errors.push(`Unknown permission: ${String(p)}`);
  if (!Array.isArray(m.allowedCommands)) errors.push('allowedCommands must be an array');
  else for (const p of m.allowedCommands) if (typeof p !== 'string' || !COMMAND_PATTERN_RE.test(p)) errors.push(`Invalid command pattern: ${String(p)}`);
  const perms = Array.isArray(m.permissions) ? (m.permissions as string[]) : [];
  if (perms.includes('network')) {
    const list = m.networkAllowlist;
    if (!Array.isArray(list) || list.length === 0) errors.push('network permission requires a non-empty networkAllowlist');
    else for (const d of list) if (typeof d !== 'string' || !/^([a-z0-9-]+\.)+[a-z]{2,}$/i.test(d)) errors.push(`Invalid domain in networkAllowlist: ${String(d)}`);
  }
  if (Array.isArray(m.allowedCommands) && m.allowedCommands.length > 0 && !perms.includes('document.write')) {
    errors.push('allowedCommands requires the document.write permission');
  }
  return errors;
}

/** Plain-language permission summary shown in the install dialog. */
export function describePermissions(manifest: Pick<PluginManifest, 'permissions' | 'networkAllowlist'>): string[] {
  return manifest.permissions.map((p) =>
    p === 'network' ? `${PERMISSIONS.network}: ${(manifest.networkAllowlist ?? []).join(', ')}` : PERMISSIONS[p],
  );
}
