import { describe, expect, it } from 'vitest';
import { describePermissions, validateManifest } from '../src';

const polaroid = {
  id: 'com.example.vintage-polaroid',
  name: 'Vintage Polaroid',
  version: '1.0.0',
  type: 'prompt',
  entry: 'instructions.md',
  ui: { placement: ['filters-menu', 'agent-panel'], icon: 'icon.svg' },
  parameters: [{ id: 'grain', type: 'number', min: 0, max: 100, default: 30 }],
  permissions: ['document.read', 'document.write', 'ai.inpaint'],
  allowedCommands: ['crop.*', 'adjust.*', 'filter.noise', 'layer.*'],
};

describe('plugin manifest', () => {
  it('accepts the spec example', () => {
    expect(validateManifest(polaroid)).toEqual([]);
  });

  it('rejects path traversal, unknown permissions and network without allowlist', () => {
    const errors = validateManifest({ ...polaroid, entry: '../../etc/passwd', permissions: ['document.write', 'fs.read', 'network'] });
    expect(errors).toContain('entry must be a relative path inside the package');
    expect(errors).toContain('Unknown permission: fs.read');
    expect(errors).toContain('network permission requires a non-empty networkAllowlist');
  });

  it('requires document.write to dispatch commands', () => {
    expect(validateManifest({ ...polaroid, permissions: ['document.read'] })).toContain('allowedCommands requires the document.write permission');
  });

  it('describes permissions in plain language', () => {
    expect(describePermissions({ permissions: ['document.read', 'network'], networkAllowlist: ['api.example.com'] })).toEqual([
      'Read your document (layers, pixels, selection)',
      'Access the network (limited to listed domains): api.example.com',
    ]);
  });
});
