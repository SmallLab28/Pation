const fs = require('fs');
const factory = require('./wrapper-wasm/pation.js');

factory().then((instance) => {
    console.log("Loaded!");
    const inputData = fs.readFileSync('test-copy.pdf');
    instance.FS.writeFile('input.pdf', inputData);
    
    console.log("Compressing...");
    const success = instance.compress_pdf('input.pdf', 'output.pdf', 72, 25);
    console.log("Success:", success);
    
    if (success) {
        const outData = instance.FS.readFile('output.pdf');
        fs.writeFileSync('output_node_wasm.pdf', outData);
        console.log("Output size:", outData.length);
    }
});
