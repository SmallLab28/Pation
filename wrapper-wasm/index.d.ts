/**
 * Pation PDF Compressor - WebAssembly Wrapper TypeScript Definitions
 */

export interface CompressProgress {
    /** Trang hiện tại đang xử lý (hoặc đối tượng tương ứng) */
    current: number;
    /** Tổng số trang của tài liệu */
    total: number;
    /** Tiến độ phần trăm (0 - 100) */
    percent: number;
    /** Alias cho current */
    currentPage: number;
    /** Alias cho total */
    totalPages: number;
}

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

    /**
     * Callback nhận tiến trình xử lý realtime (số trang, phần trăm hoàn thành).
     */
    onProgress?: (progress: CompressProgress) => void;
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
