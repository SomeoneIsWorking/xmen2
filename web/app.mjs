import {prepareApplication} from "./isolation.mjs";
import {FileStager, persistentStorage} from "./storage.mjs";

const archive = document.querySelector("#archive");
const play = document.querySelector("#play");
const progress = document.querySelector("#progress");
const status = document.querySelector("#status");
const reload = document.querySelector("#reload");
const canvas = document.querySelector("#canvas");
let launched = false;

function report(message, failed = false) {
  status.textContent = message;
  status.dataset.failed = String(failed);
  if (failed) {
    document.body.classList.remove("playing");
    reload.hidden = false;
  }
}

function launch(importing) {
  if (launched) throw new Error("The game has already started in this page.");
  launched = true;
  archive.disabled = play.disabled = true;
  progress.hidden = true;
  globalThis.Module = {
    canvas,
    arguments: importing ? ["--import"] : [],
    onSetupStatus: report,
    onGameReady() {
      document.body.classList.add("playing");
      canvas.focus();
    },
    onAbort(reason) { report(`The game stopped: ${reason}`, true); },
    onExit(code) {
      document.body.classList.remove("playing");
      if (status.dataset.failed !== "true") report(`The game closed (status ${code}).`, code !== 0);
      reload.hidden = false;
    }
  };
  const script = document.createElement("script");
  script.src = "x2native.js";
  script.onerror = () => report("The game runtime could not be loaded. Reload to try again.", true);
  document.body.append(script);
}

reload.addEventListener("click", () => location.reload());
play.addEventListener("click", () => launch(false));
archive.addEventListener("change", async () => {
  const file = archive.files[0];
  if (!file) return;
  archive.disabled = play.disabled = true;
  progress.hidden = false;
  try {
    report("Copying your ZIP into private browser storage…");
    await new FileStager().stage(file, {
      directory: "incoming", name: "input.zip", maxBytes: file.size,
      progress({bytes, total}) { progress.max = total; progress.value = bytes; }
    });
    launch(true);
  } catch (error) {
    report(error.message, true);
  }
});

async function prepare() {
    if (!navigator.gpu || !globalThis.OffscreenCanvas) {
      throw new Error("This game requires a browser with WebGPU and OffscreenCanvas support.");
    }
    const {root, persistent} = await persistentStorage();
    document.querySelector("#storage-note").textContent = persistent
      ? "Game files and saves are kept in persistent storage on this device."
      : "The browser has not granted persistent storage. It may clear game files and saves under storage pressure.";
    // A prior closed page can leave its input file; release only this owned
    // staging leaf while holding the same cross-tab lock used by FileStager.
    await navigator.locks.request("lucent-import:incoming", {ifAvailable: true}, async lock => {
      if (!lock) throw new Error("Another tab is importing game files.");
      try {
        const incoming = await root.getDirectoryHandle("incoming");
        await incoming.removeEntry("input.zip");
      } catch (error) {
        if (error.name !== "NotFoundError") throw error;
      }
    });
    archive.disabled = false;
    try {
      await root.getDirectoryHandle("install");
      play.disabled = false;
    } catch (error) {
      if (error.name !== "NotFoundError") throw error;
    }
    report("Choose your game ZIP or play the installation saved on this device.");
}

try {
  if (await prepareApplication()) {
    if (!navigator.locks) throw new Error("This browser cannot protect private game storage.");
    await navigator.locks.request("xmen2-application", {ifAvailable: true}, async lock => {
      if (!lock) throw new Error("X-Men Legends II is already open in another tab. Close it first.");
      await prepare();
      // Own the installation and saves until this page closes, including while
      // native workers are unpacking or running the game.
      await new Promise(() => {});
    });
  }
} catch (error) {
  report(error.message, true);
}
