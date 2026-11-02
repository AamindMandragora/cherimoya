// starts cheri's language server for every .moya and .toya file, which gives VS Code its diagnostics, highlighting, hovers, and definitions, and runs a test when its Run test lens is clicked
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

// runs one test as a task, cheri test on its file with its exact name, where a task passes each argument as it is, so a path with spaces needs no quoting for whatever shell the terminal has
function runTest(uri, name) {
    const command = vscode.workspace.getConfiguration('cherimoya').get('compilerPath');
    const path = vscode.Uri.parse(uri).fsPath;
    const execution = new vscode.ProcessExecution(command, ['test', path, name, '--exact']);
    const task = new vscode.Task({ type: 'cheri' }, vscode.TaskScope.Workspace, name, 'cheri test', execution);
    return vscode.tasks.executeTask(task);
}

function activate(context) {
    context.subscriptions.push(vscode.commands.registerCommand('cherimoya.runTest', runTest));
    const command = vscode.workspace.getConfiguration('cherimoya').get('compilerPath');
    const documentSelector = [{ language: 'cherimoya' }, { language: 'cherimoya-tests' }];
    client = new LanguageClient('cherimoya', 'Cherimoya', { command, args: ['lsp'] }, { documentSelector });
    return client.start();
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
