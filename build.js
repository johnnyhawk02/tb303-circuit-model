const fs=require('fs');
const model=fs.readFileSync(__dirname+'/tb303-model.js','utf8');
const page=fs.readFileSync(__dirname+'/page.html','utf8');
fs.writeFileSync(__dirname+'/index.html', page.replace('/*__MODEL__*/', ()=>model));
