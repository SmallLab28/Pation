const fs = require('fs');
const factory = require('./wrapper-wasm/pation.js');

factory().then(module => {
    const input = fs.readFileSync('test-copy.pdf');
    module.FS.writeFile('input.pdf', input);
    const success = module.compress_pdf('input.pdf', 'output.pdf', 150, 20);
    if (success) {
        const output = module.FS.readFile('output.pdf');
        fs.writeFileSync('output_wasm.pdf', output);
        console.log('Success! Saved to output_wasm.pdf');
    } else {
        console.log('Failed to compress');
    }
});
