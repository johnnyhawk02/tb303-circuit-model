const fs=require('fs');
const model=fs.readFileSync(__dirname+'/tb303-model.js','utf8');
const page=fs.readFileSync(__dirname+'/page.html','utf8');
const patterns=fs.readFileSync(__dirname+'/patterns.json','utf8').trim();
const head='<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n<link rel="manifest" href="manifest.webmanifest">\n<meta name="theme-color" content="#262626">\n<link rel="icon" href="icons/icon-192.png">\n<link rel="apple-touch-icon" href="icons/apple-touch-icon.png">\n<meta name="apple-mobile-web-app-capable" content="yes">\n<meta name="mobile-web-app-capable" content="yes">\n<meta name="apple-mobile-web-app-status-bar-style" content="black">\n<meta name="apple-mobile-web-app-title" content="303">\n';
fs.writeFileSync(__dirname+'/index.html', head + page.replace('/*__MODEL__*/', ()=>model).replace('/*__PATTERNS__*/{}', ()=>patterns));
