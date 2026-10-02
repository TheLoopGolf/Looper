import { useEditor, type Tool } from '../store';
import { formatKeys, SHORTCUTS } from '../shortcuts';
import { cssColor, fromHex, toHex } from '../lib/color';
import {
  BrushIcon,
  BucketIcon,
  CropIcon,
  EllipseMarqueeIcon,
  EraserIcon,
  EyedropperIcon,
  GradientIcon,
  HandIcon,
  LassoIcon,
  MarqueeIcon,
  MoveIcon,
  PolyLassoIcon,
  QuickMaskIcon,
  ShapeIcon,
  SwapIcon,
  TextIcon,
  WandIcon,
  ZoomIcon,
} from './Icons';

type IconT = typeof MoveIcon;

export const TOOL_LIST: { id: Tool; label: string; shortcut?: keyof typeof SHORTCUTS; Icon: IconT; group: number }[] = [
  { id: 'move', label: 'Move', shortcut: 'toolMove', Icon: MoveIcon, group: 0 },
  { id: 'marquee', label: 'Rectangular Marquee', shortcut: 'toolMarquee', Icon: MarqueeIcon, group: 0 },
  { id: 'ellipseMarquee', label: 'Elliptical Marquee', Icon: EllipseMarqueeIcon, group: 0 },
  { id: 'lasso', label: 'Lasso', shortcut: 'toolLasso', Icon: LassoIcon, group: 0 },
  { id: 'polyLasso', label: 'Polygonal Lasso', Icon: PolyLassoIcon, group: 0 },
  { id: 'wand', label: 'Magic Wand', shortcut: 'toolWand', Icon: WandIcon, group: 0 },
  { id: 'crop', label: 'Crop', shortcut: 'toolCrop', Icon: CropIcon, group: 1 },
  { id: 'eyedropper', label: 'Eyedropper', shortcut: 'toolEyedropper', Icon: EyedropperIcon, group: 1 },
  { id: 'brush', label: 'Brush', shortcut: 'toolBrush', Icon: BrushIcon, group: 2 },
  { id: 'eraser', label: 'Eraser', shortcut: 'toolEraser', Icon: EraserIcon, group: 2 },
  { id: 'bucket', label: 'Paint Bucket', shortcut: 'toolBucket', Icon: BucketIcon, group: 2 },
  { id: 'gradient', label: 'Gradient', shortcut: 'toolGradient', Icon: GradientIcon, group: 2 },
  { id: 'text', label: 'Text', shortcut: 'toolText', Icon: TextIcon, group: 3 },
  { id: 'shape', label: 'Shape', shortcut: 'toolShape', Icon: ShapeIcon, group: 3 },
  { id: 'hand', label: 'Hand', shortcut: 'toolHand', Icon: HandIcon, group: 4 },
  { id: 'zoom', label: 'Zoom', shortcut: 'toolZoom', Icon: ZoomIcon, group: 4 },
];

function ColorWell({ which }: { which: 'fg' | 'bg' }) {
  const color = useEditor((s) => s.colors[which]);
  const setColor = useEditor((s) => s.setColor);
  const label = which === 'fg' ? 'Foreground color' : 'Background color';
  return (
    <label className={`color-well ${which}`} title={`${label} (${toHex(color)})`} style={{ background: cssColor(color) }}>
      <span className="sr-only">{label}</span>
      <input type="color" value={toHex(color)} onChange={(e) => setColor(which, fromHex(e.target.value))} data-testid={`color-${which}`} />
    </label>
  );
}

export function Toolbar() {
  const tool = useEditor((s) => s.tool);
  const setTool = useEditor((s) => s.setTool);
  const quickMask = useEditor((s) => s.quickMask);
  return (
    <div className="toolbar" role="toolbar" aria-label="Tools" aria-orientation="vertical">
      {TOOL_LIST.map(({ id, label, shortcut, Icon, group }, i) => {
        const keys = shortcut ? formatKeys(SHORTCUTS[shortcut].keys) : null;
        const name = keys ? `${label} (${keys})` : label;
        return (
          <div key={id} className={i > 0 && TOOL_LIST[i - 1].group !== group ? 'tool-sep' : undefined}>
            <button className={`tool-btn${tool === id ? ' active' : ''}`} aria-pressed={tool === id} aria-label={name} title={name} onClick={() => setTool(id)} data-testid={`tool-${id}`}>
              <Icon width={18} height={18} />
            </button>
          </div>
        );
      })}
      <div className="color-wells" aria-label="Colors">
        <ColorWell which="bg" />
        <ColorWell which="fg" />
        <button className="icon-btn swap" title={`Swap colors (${formatKeys(SHORTCUTS.swapColors.keys)})`} aria-label="Swap colors" onClick={() => useEditor.getState().swapColors()}>
          <SwapIcon width={12} height={12} />
        </button>
      </div>
      <button
        className={`tool-btn${quickMask ? ' active' : ''}`}
        aria-pressed={quickMask}
        aria-label={`Quick mask (${formatKeys(SHORTCUTS.quickMask.keys)})`}
        title={`Quick mask (${formatKeys(SHORTCUTS.quickMask.keys)})`}
        onClick={() => useEditor.getState().setQuickMask(!quickMask)}
      >
        <QuickMaskIcon width={18} height={18} />
      </button>
    </div>
  );
}
