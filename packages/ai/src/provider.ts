/**
 * Provider-agnostic AI interface (spec §6). M1 defines the contract only;
 * adapters (LLM agent brain, image generation/inpainting, local ONNX models)
 * are implemented in M4.
 */

export type AICapability =
  | 'chat'
  | 'vision'
  | 'tool_calling'
  | 'image_generate'
  | 'inpaint'
  | 'outpaint'
  | 'segment'
  | 'upscale'
  | 'bg_remove';

export interface ModelInfo {
  readonly id: string;
  readonly name: string;
  readonly capabilities: readonly AICapability[];
  /** Runs on-device (no data leaves the machine). */
  readonly local: boolean;
  readonly contextWindow?: number;
}

export interface CostEstimate {
  readonly credits?: number;
  readonly usd?: number;
  /** Human-readable, e.g. "≈ $0.04 (1 image, 1024×1024)". */
  readonly summary: string;
}

export interface InvokeOptions {
  readonly signal: AbortSignal;
  readonly onProgress?: (progress: { fraction?: number; message?: string }) => void;
  readonly model?: string;
}

/** Straight-alpha RGBA8 image passed to/from providers. */
export interface AIImage {
  readonly width: number;
  readonly height: number;
  readonly pixels: Uint8ClampedArray;
}

export interface CapabilityIO {
  chat: { input: { messages: unknown[]; tools?: unknown[]; system?: string }; output: { message: unknown; stopReason: string } };
  vision: CapabilityIO['chat'];
  tool_calling: CapabilityIO['chat'];
  image_generate: { input: { prompt: string; negativePrompt?: string; width: number; height: number; seed?: number; references?: AIImage[] }; output: { images: AIImage[]; seed: number } };
  inpaint: { input: { image: AIImage; mask: AIImage; prompt: string; strength?: number; seed?: number }; output: { images: AIImage[]; seed: number } };
  outpaint: { input: { image: AIImage; direction: 'left' | 'right' | 'top' | 'bottom' | 'all'; pixels: number; prompt?: string; seed?: number }; output: { images: AIImage[]; seed: number } };
  segment: { input: { image: AIImage; prompt?: string; point?: [number, number]; box?: [number, number, number, number] }; output: { masks: AIImage[]; scores: number[] } };
  upscale: { input: { image: AIImage; factor: 2 | 4 }; output: { image: AIImage } };
  bg_remove: { input: { image: AIImage }; output: { mask: AIImage } };
}

export interface AIProvider {
  readonly id: string;
  readonly name: string;
  readonly capabilities: readonly AICapability[];
  models(): Promise<ModelInfo[]>;
  invoke<C extends AICapability>(capability: C, input: CapabilityIO[C]['input'], opts: InvokeOptions): Promise<CapabilityIO[C]['output']>;
  estimateCost?<C extends AICapability>(capability: C, input: CapabilityIO[C]['input']): CostEstimate;
}

/** Thrown when a provider refuses a request (content policy etc.) — surfaced to the user verbatim, never swallowed. */
export class ProviderRefusal extends Error {
  override name = 'ProviderRefusal';
  constructor(
    message: string,
    readonly providerId: string,
  ) {
    super(message);
  }
}
