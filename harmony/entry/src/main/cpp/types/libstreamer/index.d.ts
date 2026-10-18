// source: 0 screen, 1 back camera, 2 front camera. Returns "" on success, otherwise the reason.
export const start: (source: number, width: number, height: number, fps: number, bitrate: number, port: number) => string;
export const stop: () => void;
export const isRunning: () => boolean;
export const lastError: () => string;
