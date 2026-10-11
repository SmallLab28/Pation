/**
 * Pation PDF Compressor - WebAssembly Wrapper TypeScript Definitions
 */

export interface CompressOptions {
    /**
     * The target DPI for downsampling images.
     * @default 150
     */
    dpi?: number;

    /**
     * The target quality for re-encoding images.
     * @default 25
     */
    quality?: number;
}

/**
 * 
 */
export function getModule(): Promise<any>;

/**
 * 
 * 
 * @param fileData 
 * @param optionsOrDpi 
 * @returns 
 */
export function compressPdf(
    fileData: Uint8Array | ArrayBuffer | Blob | File,
    optionsOrDpi?: number | CompressOptions
): Promise<Uint8Array>;
