(function () {
  const vscode = acquireVsCodeApi();
  // @ts-ignore
  const go = new Go();

  let isWasmInitialized = false;
  let pendingContent = null;

  /**
   * @param {string | URL} uri
   */
  async function initWasm(uri) {
    try {
      const result = await WebAssembly.instantiateStreaming(fetch(uri), go.importObject);
      go.run(result.instance);
      isWasmInitialized = true;
      console.log("MinML WASM initialized");
      if (pendingContent) {
        updateContent(pendingContent);
        pendingContent = null;
      }
    } catch (err) {
      const errorMessage = err instanceof Error ? err.message : String(err);
      vscode.postMessage({ command: "alert", message: errorMessage });
      console.error("Failed to initialize WASM", err);
    }
  }

  /**
   * @param {string} content
   */
  async function updateContent(content) {
    try {
      if (!isWasmInitialized) {
        pendingContent = content;
        return;
      }
      // @ts-ignore
      const html = minmlConvert(content);
      const contentElement = document.getElementById("content");
      if (contentElement) {
        contentElement.innerHTML = html;
      }
    } catch (err) {
      const errorMessage = err instanceof Error ? err.message : String(err);
      vscode.postMessage({ command: "alert", message: errorMessage });
      const contentElement = document.getElementById("content");
      if (contentElement) {
        contentElement.innerHTML = `<pre style="color: var(--vscode-errorForeground)">Error: ${errorMessage}</pre>`;
      }
    }
  }

  // Handle messages from the extension
  window.addEventListener("message", (event) => {
    const message = event.data;
    switch (message.command) {
      case "init":
        initWasm(message.wasmUri);
        break;
      case "update":
        updateContent(message.content);
        break;
    }
  });

  vscode.postMessage({ command: "ready" });
})();
