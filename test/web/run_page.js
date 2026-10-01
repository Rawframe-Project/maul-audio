// Runs a test page in headless Chrome: serves its directory, with the
// cross-origin isolation headers or without, clicks once the page logs
// that it waits for a gesture, and exits with the page's result.
// Usage: node run_page.js <directory> <page> <isolated: 1|0>
// Puppeteer comes from MAUD_PUPPETEER, a path to its module.
const http = require("http");
const fs = require("fs");
const path = require("path");
const puppeteer = require(process.env.MAUD_PUPPETEER || "puppeteer");

const [directory, page, isolatedArgument] = process.argv.slice(2);
const isolated = isolatedArgument === "1";
const types = {".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm"};

const server = http.createServer((request, response) => {
  const file = path.join(directory, decodeURIComponent(request.url.split("?")[0]));
  fs.readFile(file, (error, data) => {
    if (error) {
      response.writeHead(404);
      response.end();
      return;
    }
    const headers = {"Content-Type": types[path.extname(file)] || "application/octet-stream"};
    if (isolated) {
      headers["Cross-Origin-Opener-Policy"] = "same-origin";
      headers["Cross-Origin-Embedder-Policy"] = "require-corp";
    }
    response.writeHead(200, headers);
    response.end(data);
  });
});

server.listen(0, "127.0.0.1", async () => {
  // A headless page counts as backgrounded, and Chrome slows its timers
  // to about one a second unless told not to.
  const browser = await puppeteer.launch({
    headless: true,
    args: [
      "--no-sandbox",
      "--disable-background-timer-throttling",
      "--disable-renderer-backgrounding",
      "--disable-backgrounding-occluded-windows",
    ],
  });
  let result = "timeout";
  try {
    const tab = await browser.newPage();
    tab.on("pageerror", (error) => console.log(`page error: ${error.message}`));
    const done = new Promise((resolve) => {
      tab.on("console", async (message) => {
        const text = message.text();
        if (!text.includes("favicon") && !text.includes("Failed to load resource")) {
          console.log(text);
        }
        if (text.includes("MAUD_TEST_WAITING_FOR_GESTURE")) {
          await tab.mouse.click(10, 10);
        }
        const match = text.match(/MAUD_TEST_RESULT (\w+)/);
        if (match) {
          resolve(match[1]);
        }
      });
    });
    await tab.goto(`http://127.0.0.1:${server.address().port}/${page}`);
    result = await Promise.race([done, new Promise((resolve) => setTimeout(() => resolve("timeout"), 60000))]);
  } finally {
    await browser.close();
    server.close();
  }
  console.log(`${page} (${isolated ? "isolated" : "not isolated"}): ${result}`);
  process.exit(result === "pass" ? 0 : 1);
});
