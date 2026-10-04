// Starts the ForgeScript language server (forge-lsp) and connects VS Code to it.
// The grammar and colours work without this; the server adds live error reporting.

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

/** The server binary: the `forgescript.serverPath` setting, else a build inside an open workspace folder. */
function findServer() {
	const configured = vscode.workspace.getConfiguration("forgescript").get("serverPath");
	if (configured) {
		return fs.existsSync(configured) ? configured : undefined;
	}
	const exe = process.platform === "win32" ? "forge-lsp.exe" : "forge-lsp";
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
			"ForgeScript: can't find the language server (forge-lsp), so errors won't be shown. " +
				"Build it with `cargo build --release -p forge-lsp` in the ForgeScript folder, or set forgescript.serverPath.",
			"Open Settings"
		);
		if (choice) {
			vscode.commands.executeCommand("workbench.action.openSettings", "forgescript.serverPath");
		}
		return;
	}

	client = new LanguageClient(
		"forgescript",
		"ForgeScript",
		{ command: server, transport: TransportKind.stdio },
		{ documentSelector: [{ scheme: "file", language: "forgescript" }] }
	);
	context.subscriptions.push(
		vscode.commands.registerCommand("forgescript.restartServer", async () => {
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
