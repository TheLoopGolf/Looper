/** Original line icons for Canvas AI (24×24 grid, currentColor). */
import type { SVGProps } from 'react';

const base = (paths: React.ReactNode) =>
  function Icon(props: SVGProps<SVGSVGElement>) {
    return (
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" {...props}>
        {paths}
      </svg>
    );
  };

export const EyeIcon = base(<><path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12Z" /><circle cx="12" cy="12" r="3" /></>);
export const EyeOffIcon = base(<><path d="M3 3l18 18" /><path d="M10.6 5.1A9.8 9.8 0 0 1 12 5c6.4 0 10 7 10 7a17 17 0 0 1-3.2 4.1M6.2 6.3C3.6 8 2 12 2 12s3.6 7 10 7a9.6 9.6 0 0 0 5.8-2" /></>);
export const LockIcon = base(<><rect x="5" y="11" width="14" height="10" rx="2" /><path d="M8 11V8a4 4 0 0 1 8 0v3" /></>);
export const PlusIcon = base(<path d="M12 5v14M5 12h14" />);
export const FolderIcon = base(<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z" />);
export const CopyIcon = base(<><rect x="8" y="8" width="12" height="12" rx="2" /><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2" /></>);
export const TrashIcon = base(<><path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3" /></>);
export const ChevronIcon = base(<path d="m9 6 6 6-6 6" />);
export const LayerIcon = base(<><path d="m12 3 9 5-9 5-9-5Z" /><path d="m3 13 9 5 9-5" /></>);
export const MoveIcon = base(<><path d="M12 2v20M2 12h20" /><path d="m9 5 3-3 3 3M9 19l3 3 3-3M5 9l-3 3 3 3M19 9l3 3-3 3" /></>);
export const HandIcon = base(<path d="M8 13V5.5a1.5 1.5 0 0 1 3 0V12m0-6.5v-1a1.5 1.5 0 0 1 3 0V12m0-6a1.5 1.5 0 0 1 3 0v6m0-3.5a1.5 1.5 0 0 1 3 0V15a7 7 0 0 1-7 7h-1.3a7 7 0 0 1-5.4-2.6L3.5 16a1.6 1.6 0 0 1 2.4-2.1L8 16" />);
export const ZoomIcon = base(<><circle cx="11" cy="11" r="7" /><path d="m20 20-4-4M8 11h6M11 8v6" /></>);
export const UndoIcon = base(<><path d="M9 14 4 9l5-5" /><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11" /></>);
export const RedoIcon = base(<><path d="m15 14 5-5-5-5" /><path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13" /></>);
export const SparkIcon = base(<path d="M12 3v4M12 17v4M3 12h4M17 12h4M6.3 6.3l2.5 2.5M15.2 15.2l2.5 2.5M6.3 17.7l2.5-2.5M15.2 8.8l2.5-2.5" />);
export const CloseIcon = base(<path d="M6 6l12 12M18 6 6 18" />);
export const UserIcon = base(<><circle cx="12" cy="8" r="4" /><path d="M4 21a8 8 0 0 1 16 0" /></>);

/** Canvas AI mark: a tilted frame with a spark — original artwork. */
export function Logo({ size = 22 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" aria-hidden="true">
      <defs>
        <linearGradient id="cai-g" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#2dd4bf" />
          <stop offset="1" stopColor="#8b5cf6" />
        </linearGradient>
      </defs>
      <rect x="4" y="6" width="22" height="20" rx="5" fill="url(#cai-g)" transform="rotate(-8 15 16)" />
      <path d="M20 9.5l1.2 3 3 1.2-3 1.2-1.2 3-1.2-3-3-1.2 3-1.2z" fill="#fff" />
      <path d="M9 22l4-5 3 3 2-2 3 4z" fill="rgba(255,255,255,.85)" />
    </svg>
  );
}

export const MarqueeIcon = base(<rect x="4" y="5" width="16" height="14" strokeDasharray="3 2.5" />);
export const EllipseMarqueeIcon = base(<ellipse cx="12" cy="12" rx="8.5" ry="7" strokeDasharray="3 2.5" />);
export const LassoIcon = base(<><path d="M7 16c-3-2-3.5-6 0-8.5S17 5 19 8s-1 7-6 7.5c-2 .2-3.6-.2-4.4.9C7.8 17.5 9 19 7.5 20.5" /></>);
export const PolyLassoIcon = base(<path d="M5 9 11 4l8 5-3 8-8 1Z" strokeDasharray="3 2" />);
export const WandIcon = base(<><path d="m4 20 11-11M14 4v2M18 6l-1.5 1.5M20 10h-2M10 6 8.5 4.5" /><path d="m13 7 4 4" /></>);
export const CropIcon = base(<><path d="M6 2v14a2 2 0 0 0 2 2h14" /><path d="M2 6h14a2 2 0 0 1 2 2v14" /></>);
export const EyedropperIcon = base(<><path d="m14 7 3 3M5 19l1-4L15 6a2.1 2.1 0 0 1 3 3l-9 9Z" /><path d="m16 4 4 4" /></>);
export const BrushIcon = base(<><path d="M18.4 3.6a2 2 0 0 1 2.9 2.9L11 16.8 7.2 13Z" /><path d="M7 13c-2.5 0-4 1.6-4 4 0 1.5-.5 2.5-1 3 3 0 7-1 7-4.5" /></>);
export const EraserIcon = base(<><path d="m7 21-4-4a2 2 0 0 1 0-2.8L13.2 4a2 2 0 0 1 2.8 0l4.6 4.6a2 2 0 0 1 0 2.8L11 21Z" /><path d="M7 21h14M8.5 9.5l6 6" /></>);
export const BucketIcon = base(<><path d="m19 11-8-8-8.6 8.6a2 2 0 0 0 0 2.8l5.2 5.2a2 2 0 0 0 2.8 0Z" /><path d="M5 2l5 5M2 13h15M22 20a2 2 0 1 1-4 0c0-1.6 1.7-2.4 2-4 .3 1.6 2 2.4 2 4Z" /></>);
export const GradientIcon = base(<><rect x="3" y="3" width="18" height="18" rx="2" /><path d="M7 3v18M11 3v18" strokeOpacity=".6" /><path d="M15 3v18" strokeOpacity=".3" /></>);
export const TextIcon = base(<path d="M5 6V4h14v2M12 4v16M9 20h6" />);
export const ShapeIcon = base(<><rect x="3" y="3" width="10" height="10" rx="1" /><circle cx="15" cy="15" r="6" /></>);
export const AdjustIcon = base(<><circle cx="12" cy="12" r="9" /><path d="M12 3a9 9 0 0 0 0 18Z" fill="currentColor" /></>);
export const MaskIcon = base(<><rect x="3" y="5" width="18" height="14" rx="2" /><circle cx="12" cy="12" r="4" fill="currentColor" /></>);
export const SwapIcon = base(<path d="M7 4 4 7l3 3M4 7h11a4 4 0 0 1 4 4v1M17 20l3-3-3-3M20 17H9a4 4 0 0 1-4-4v-1" />);
export const QuickMaskIcon = base(<><rect x="3" y="4" width="18" height="16" rx="2" /><circle cx="12" cy="12" r="4.5" strokeDasharray="2.5 2" /></>);
export const CheckIcon = base(<path d="m5 12 5 5L20 7" />);
export const AlignLeftIcon = base(<path d="M4 6h16M4 10h10M4 14h16M4 18h10" />);
export const AlignCenterIcon = base(<path d="M4 6h16M7 10h10M4 14h16M7 18h10" />);
export const AlignRightIcon = base(<path d="M4 6h16M10 10h10M4 14h16M10 18h10" />);
