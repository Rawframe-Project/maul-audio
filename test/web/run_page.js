// Runs a test page in headless Chrome: serves its directory, with the
// cross-origin isolation headers or without, clicks once the page logs
// that it waits for a gesture, and exits with the page's result.
// Usage: node run_page.js <directory> <page> <isolated: 1|0> [deny|loopback]
// With deny, the browser refuses the microphone and the page is told so.
// With loopback, the browser picks the tab titled MaudLoopback when the
// page asks for display capture, and its audio is not muted, so that the
// tab's own sound reaches the capture.
// Puppeteer comes from MAUD_PUPPETEER, a path to its module.
// Chrome plays into the device the environment gives it: on Linux, the
// private sound server's null sink (CTest's sound-server fixture). A
// device error Chrome reports fails the page with Chrome's message, since
// a page that lost its device tests nothing.
const http = require("http");
const fs = require("fs");
const path = require("path");
// Without a browser driver the environment is absent and the page is
// skipped (77); one named but missing is a broken environment.
let puppeteer;
try {
  puppeteer = require(process.env.MAUD_PUPPETEER || "puppeteer");
} catch (error) {
  console.log(`no puppeteer: ${error.message}`);
  process.exit(process.env.MAUD_PUPPETEER ? 1 : 77);
}

// On Linux the pages need the private sound server; without its
// programs there is no device to play into (MAUD_NO_SOUND_SERVER).
if (process.env.MAUD_NO_SOUND_SERVER) {
  console.log("no sound server for the browser");
  process.exit(77);
}

const [directory, page, isolatedArgument, denyArgument] = process.argv.slice(2);
const isolated = isolatedArgument === "1";
const deny = denyArgument === "deny";
const loopback = denyArgument === "loopback";
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
    ignoreDefaultArgs: loopback ? ["--mute-audio"] : [],
    args: [
      ...(loopback ? ["--auto-select-tab-capture-source-by-title=MaudLoopback"] : []),
      "--no-sandbox",
      // A fake microphone, granted without asking unless denied. Not for
      // loopback: the fake UI would answer display capture with a fake
      // source, not the tab.
      ...(loopback ? [] : ["--use-fake-device-for-media-stream"]),
      ...(deny || loopback ? [] : ["--use-fake-ui-for-media-stream"]),
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
        // "[AudioContext] Fallback to the default device ...", "The
        // AudioContext encountered an error from the audio device ...".
        if (/^\[AudioContext\]|AudioContext encountered an error/.test(text)) {
          resolve(`device error: ${text}`);
        }
        const match = text.match(/MAUD_TEST_RESULT (\w+)/);
        if (match) {
          resolve(match[1]);
        }
      });
    });
    await tab.goto(`http://127.0.0.1:${server.address().port}/${page}${deny ? "?deny=1" : ""}`);
    result = await Promise.race([done, new Promise((resolve) => setTimeout(() => resolve("timeout"), 60000))]);
  } finally {
    await browser.close();
    server.close();
  }
  console.log(`${page} (${isolated ? "isolated" : "not isolated"}${deny ? ", denied" : ""}): ${result}`);
  process.exit(result === "pass" ? 0 : 1);
});
