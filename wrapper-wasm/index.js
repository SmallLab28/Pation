/**
 * Pation PDF Compressor - WebAssembly Wrapper
 */

// This expects the emscripten compiled module 'pation.js' to be available or imported.
// In a typical bundler environment, you might import it, but here we assume it's loaded.
// If you are using this in Node.js or a bundler, you may need to adjust the import of the Wasm module.

const createPationModule = require('./pation.js'); // Standard emscripten output

/**
 * Compresses a PDF file using Pation.
 * 
 * @param {Uint8Array} pdfData - The raw bytes of the input PDF file.
 * @param {Object} options - Configuration options for compression.
 * @param {number} [options.dpi=150] - The target DPI for downsampling images.
 * @param {number} [options.quality=20] - The target quality for re-encoding images.
 * @returns {Promise<Uint8Array>} - A promise that resolves to the compressed PDF bytes.
 */
async function compressPdf(pdfData, options = {}) {
    const dpi = options.dpi !== undefined ? options.dpi : 150;
    const quality = options.quality !== undefined ? options.quality : 20;

    // Initialize the WebAssembly module
    const Module = await createPationModule();

    const inputName = `input_${Date.now()}.pdf`;
    const outputName = `output_${Date.now()}.pdf`;

    try {
        // Write the input PDF to the Emscripten virtual file system
        Module.FS.writeFile(inputName, pdfData);

        // Call the C++ function exposed via Embind
        const success = Module.compress_pdf(inputName, outputName, dpi, quality);

        if (!success) {
            throw new Error("Pation WASM: Compression failed internally.");
        }

        // Read the output PDF from the virtual file system
        const outData = Module.FS.readFile(outputName);
        
        // Return a copy of the data (as VFS memory can be volatile)
        return new Uint8Array(outData);
    } finally {
        // Clean up virtual file system
        try {
            Module.FS.unlink(inputName);
            Module.FS.unlink(outputName);
        } catch (e) {
            // Ignore cleanup errors
        }
    }
}

module.exports = {
    compressPdf
};
