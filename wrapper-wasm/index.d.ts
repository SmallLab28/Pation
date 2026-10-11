/**
 * Pation PDF Compressor - WebAssembly Wrapper
 */

export interface CompressOptions {
    /**
     * The target DPI for downsampling images.
     * @default 150
     */
    dpi?: number;

    /**
     * The target quality for re-encoding images.
     * @default 20
     */
    quality?: number;
}

/**
 * Compresses a PDF file using Pation via WebAssembly.
 * 
 * @param pdfData - The raw bytes of the input PDF file (Uint8Array).
 * @param options - Configuration options for compression.
 * @returns A promise that resolves to the compressed PDF bytes.
 */
export function compressPdf(pdfData: Uint8Array, options?: CompressOptions): Promise<Uint8Array>;
