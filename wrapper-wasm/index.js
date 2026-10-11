/**
 * Pation PDF Compressor - WebAssembly Wrapper
 * Optimized for Browser (ESM/Vanilla/Workers) & Node.js
 */

let cachedModule = null;

/**
 * 
 */
async function getModule() {
    if (cachedModule) return cachedModule;

    const moduleOptions = {
        locateFile: (path, scriptDirectory) => {
            if (typeof window !== 'undefined' || typeof document !== 'undefined' || typeof importScripts !== 'undefined' || typeof WorkerGlobalScope !== 'undefined') {
                return '/pation/' + path;
            }
            return (scriptDirectory || '') + path;
        }
    };

    let factory = typeof createPationModule === 'function' ? createPationModule : null;
    if (!factory && typeof globalThis !== 'undefined' && typeof globalThis.createPationModule === 'function') {
        factory = globalThis.createPationModule;
    }
    if (!factory && typeof window !== 'undefined' && typeof window.createPationModule === 'function') {
        factory = window.createPationModule;
    }

    if (!factory) {
        if (typeof importScripts === 'function') {
            importScripts('/pation/pation.js');
            factory = typeof createPationModule === 'function' ? createPationModule : (globalThis.createPationModule || self.createPationModule);
        } else if (typeof document !== 'undefined') {
            await new Promise((resolve, reject) => {
                const script = document.createElement('script');
                script.src = '/pation/pation.js';
                script.onload = resolve;
                script.onerror = () => reject(new Error('Pation WASM: Không thể tải /pation/pation.js'));
                document.head.appendChild(script);
            });
            factory = typeof createPationModule === 'function' ? createPationModule : (window.createPationModule || globalThis.createPationModule);
        } else if (typeof process !== 'undefined' && process.versions?.node) {
            try {
                const fs = await import('fs');
                const vm = await import('vm');
                const path = await import('path');
                const { fileURLToPath } = await import('url');
                const { createRequire } = await import('module');
                const dir = path.dirname(fileURLToPath(import.meta.url));
                globalThis.require = createRequire(import.meta.url);
                globalThis.__dirname = dir;
                const code = fs.readFileSync(path.join(dir, 'pation.js'), 'utf8');
                vm.runInThisContext(code);
                factory = globalThis.createPationModule;
            } catch (e) {
                console.error("Lỗi nạp pation.js trong Node:", e);
            }
        }
    }

    if (typeof factory !== 'function') {
        throw new Error("Pation WASM: createPationModule is not available. Please include or import pation.js first.");
    }

    cachedModule = await factory(moduleOptions);
    return cachedModule;
}

/**
 * Nén file PDF bằng WebAssembly Pation
 * 
 * @param {Uint8Array|ArrayBuffer|File|Blob} fileData Dữ liệu PDF đầu vào
 * @param {number|Object} [optionsOrDpi=150] DPI (number) hoặc options { dpi, quality }
 * @returns {Promise<Uint8Array>} Dữ liệu PDF đã nén
 */
async function compressPdf(fileData, optionsOrDpi = 150) {
    let dpi = 150;
    let quality = 25;

    if (typeof optionsOrDpi === 'number') {
        dpi = optionsOrDpi;
    } else if (typeof optionsOrDpi === 'object' && optionsOrDpi !== null) {
        if (optionsOrDpi.dpi !== undefined) dpi = optionsOrDpi.dpi;
        if (optionsOrDpi.quality !== undefined) quality = optionsOrDpi.quality;
    }
    let uint8Data;
    if (fileData instanceof Uint8Array) {
        uint8Data = fileData;
    } else if (fileData instanceof ArrayBuffer) {
        uint8Data = new Uint8Array(fileData);
    } else if (typeof Blob !== 'undefined' && fileData instanceof Blob) {
        uint8Data = new Uint8Array(await fileData.arrayBuffer());
    } else {
        throw new Error("Lỗi: Đầu vào phải là Uint8Array, ArrayBuffer hoặc File/Blob.");
    }

    const Module = await getModule();
    const randomId = Math.random().toString(36).substring(2, 9);
    const inputName = `input_${Date.now()}_${randomId}.pdf`;
    const outputName = `output_${Date.now()}_${randomId}.pdf`;

    try {
        Module.FS.writeFile(inputName, uint8Data);
        const success = Module.compress_pdf(inputName, outputName, dpi, quality);
        if (!success) {
            throw new Error("Pation WASM: Quá trình nén PDF thất bại.");
        }
        const outData = Module.FS.readFile(outputName);
        // Cơ chế bảo vệ: Nếu dung lượng sau khi nén không giảm (hoặc tăng lên), trả về file gốc
        if (outData.length >= uint8Data.length) {
            return uint8Data;
        }
        return new Uint8Array(outData);
    } finally {
        try { Module.FS.unlink(inputName); } catch (e) { }
        try { Module.FS.unlink(outputName); } catch (e) { }
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { compressPdf, getModule };
}
if (typeof window !== 'undefined') {
    window.compressPdf = compressPdf;
    window.PationCompressor = { compressPdf, getModule };
}
if (typeof globalThis !== 'undefined') {
    globalThis.compressPdf = compressPdf;
    globalThis.PationCompressor = { compressPdf, getModule };
}

export { compressPdf, getModule };
export default compressPdf;
