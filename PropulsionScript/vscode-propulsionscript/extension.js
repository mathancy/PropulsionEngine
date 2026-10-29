// Starts the PropulsionScript language server (propulsion-lsp) and connects VS Code to it.
// The grammar and colours work without this; the server adds live error reporting.

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

/** The server binary: the `propulsionscript.serverPath` setting, else a build inside an open workspace folder. */
function findServer() {
	const configured = vscode.workspace.getConfiguration("propulsionscript").get("serverPath");
	if (configured) {
		return fs.existsSync(configured) ? configured : undefined;
	}
	const exe = process.platform === "win32" ? "propulsion-lsp.exe" : "propulsion-lsp";
	for (const folder of vscode.workspace.workspaceFolders ?? []) {
		for (const profile of ["release", "debug"]) {
			const candidate = path.join(folder.uri.fsPath, "target", profile, exe);
			if (fs.existsSync(candidate)) {
				return candidate;
			}
		}
	}
	return undefined;
}

async function activate(context) {
	const server = findServer();
	if (!server) {
		const choice = await vscode.window.showWarningMessage(
			"PropulsionScript: can't find the language server (propulsion-lsp), so errors won't be shown. " +
				"Build it with `cargo build --release -p propulsion-lsp` in the PropulsionScript folder, or set propulsionscript.serverPath.",
			"Open Settings"
		);
		if (choice) {
			vscode.commands.executeCommand("workbench.action.openSettings", "propulsionscript.serverPath");
		}
		return;
	}

	client = new LanguageClient(
		"propulsionscript",
		"PropulsionScript",
		{ command: server, transport: TransportKind.stdio },
		{ documentSelector: [{ scheme: "file", language: "propulsionscript" }] }
	);
	context.subscriptions.push(
		vscode.commands.registerCommand("propulsionscript.restartServer", async () => {
			await client.stop();
			await client.start();
		})
	);
	await client.start();
}

function deactivate() {
	return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
