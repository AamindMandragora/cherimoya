// starts cheri's language server for every .moya file, which gives VS Code its diagnostics, highlighting, hovers, and definitions
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

function activate() {
    const command = vscode.workspace.getConfiguration('cherimoya').get('compilerPath');
    client = new LanguageClient('cherimoya', 'Cherimoya', { command, args: ['lsp'] }, { documentSelector: [{ language: 'cherimoya' }] });
    return client.start();
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
