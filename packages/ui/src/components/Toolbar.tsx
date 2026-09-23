import { useEditor, type Tool } from '../store';
import { formatKeys, SHORTCUTS } from '../shortcuts';
import { HandIcon, MoveIcon, ZoomIcon } from './Icons';

const TOOLS: { id: Tool; label: string; keys: string; Icon: typeof MoveIcon }[] = [
  { id: 'move', label: 'Move', keys: SHORTCUTS.toolMove.keys, Icon: MoveIcon },
  { id: 'hand', label: 'Hand', keys: SHORTCUTS.toolHand.keys, Icon: HandIcon },
  { id: 'zoom', label: 'Zoom', keys: SHORTCUTS.toolZoom.keys, Icon: ZoomIcon },
];

export function Toolbar() {
  const tool = useEditor((s) => s.tool);
  const setTool = useEditor((s) => s.setTool);
  return (
    <div className="toolbar" role="toolbar" aria-label="Tools" aria-orientation="vertical">
      {TOOLS.map(({ id, label, keys, Icon }) => (
        <button
          key={id}
          className={`tool-btn${tool === id ? ' active' : ''}`}
          aria-pressed={tool === id}
          aria-label={`${label} tool (${formatKeys(keys)})`}
          title={`${label} (${formatKeys(keys)})`}
          onClick={() => setTool(id)}
        >
          <Icon width={18} height={18} />
        </button>
      ))}
    </div>
  );
}
