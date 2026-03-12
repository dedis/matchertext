(function () {
  const vscode = acquireVsCodeApi();
  // @ts-ignore
  const go = new Go();

  /**
   * @param {string | URL} uri
   */
  async function initWasm(uri) {
    try {
      const result = await WebAssembly.instantiateStreaming(fetch(uri), go.importObject);
      go.run(result.instance);
      const minmlTestString = `
                div{style=[flex: 1 1 500px; background-color: #b3d0ff]}[
                    h1[This is just a test file to make sure the wasm is functioning correctly]
                    h2{style=[color:red]}[If this is red and looking like normal HTML then it's working pretty well]
                ]
            `;
      try {
        // @ts-ignore
        const html = minmlConvert(minmlTestString);
        const contentElement = document.getElementById("content");
        if (contentElement) {
          contentElement.innerHTML = html;
        }
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        console.error("Conversion error", err);
        vscode.postMessage({ command: "alert", message: errorMessage });
        const contentElement = document.getElementById("content");
        if (contentElement) {
          contentElement.innerHTML = `<pre style="color: var(--vscode-errorForeground)">Error: ${errorMessage}</pre>`;
        }
      }
      console.log("MinML WASM initialized");
    } catch (err) {
      const errorMessage = err instanceof Error ? err.message : String(err);
      vscode.postMessage({ command: "alert", message: errorMessage });
      console.error("Failed to initialize WASM", err);
    }
  }

  // Handle messages from the extension
  window.addEventListener("message", (event) => {
    const message = event.data;
    switch (message.command) {
      case "init":
        initWasm(message.wasmUri);
        break;
    }
  });

  vscode.postMessage({ command: "ready" });
})();
