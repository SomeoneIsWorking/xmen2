import {prepareApplication} from "./isolation.mjs";
import {claimCanvasGestures} from "./canvas.mjs";
import {FileStager, persistentStorage} from "./storage.mjs";

const archive = document.querySelector("#archive");
const play = document.querySelector("#play");
const testPlay = document.querySelector("#test-play");
const progress = document.querySelector("#progress");
const status = document.querySelector("#status");
const reload = document.querySelector("#reload");
const canvas = document.querySelector("#canvas");
let launched = false;
let storageRoot;

async function removeIfPresent(root, name, recursive = false) {
  try {
    await root.removeEntry(name, {recursive});
  } catch (error) {
    if (error.name !== "NotFoundError") throw error;
  }
}

function formatBytes(bytes) {
  const units = ["B", "KB", "MB", "GB"];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  return `${unit > 0 && value < 10 ? value.toFixed(2) : Math.round(value)} ${units[unit]}`;
}

function report(message, failed = false) {
  status.textContent = message;
  status.dataset.failed = String(failed);
  if (failed) {
    document.body.classList.remove("playing");
    reload.hidden = false;
  }
}

/*
 * What this page asks the port to do. The product's own argument is decided
 * here and is never replaced by a URL.
 *
 * The browser has no command line and no environment, so a repeated `?arg=`
 * is the only way a maintainer can ask the runtime for a diagnostic while it
 * runs in the browser: `?arg=--set&arg=jit.profile=24` reaches the same
 * option parser the native launcher uses, and nothing about the page has to
 * grow a second vocabulary for it.
 */
function launchArguments(importing, gameplayTest) {
  const product = importing
    ? ["--import"]
    : gameplayTest ? ["--test-deadzone"] : [];
  return product.concat(new URLSearchParams(location.search).getAll("arg"));
}

function launch(importing, gameplayTest = false) {
  if (launched) throw new Error("The game has already started in this page.");
  launched = true;
  archive.disabled = play.disabled = testPlay.disabled = true;
  progress.hidden = !importing;
  if (importing) {
    progress.max = 100;
    progress.value = 0;
  }
  globalThis.Module = {
    canvas,
    arguments: launchArguments(importing, gameplayTest),
    onSetupStatus: report,
    onUnpackProgress(percent, done, total) {
      progress.max = 100;
      progress.value = percent;
      report(total > 0
        ? `Checking and unpacking your game files… ${percent}% (${formatBytes(done)} of ${formatBytes(total)})`
        : `Checking and unpacking your game files… ${percent}%`);
    },
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

/* A browser owns scroll, pinch, long-press and overscroll on any element the
 * page has not claimed, and the virtual pad lives on this canvas. Claim it
 * before the game can start rather than when it becomes visible, so no early
 * contact is spent teaching the page whose gesture it is. */
claimCanvasGestures(canvas);

reload.addEventListener("click", () => location.reload());
play.addEventListener("click", () => launch(false));
testPlay.addEventListener("click", () => launch(false, true));
archive.addEventListener("change", async () => {
  const file = archive.files[0];
  if (!file) return;
  archive.disabled = play.disabled = testPlay.disabled = true;
  progress.hidden = false;
  try {
    report("Copying your ZIP into private browser storage…");
    await new FileStager().stage(file, {
      directory: "incoming", name: "input.zip", maxBytes: file.size,
      progress({bytes, total}) { progress.max = total; progress.value = bytes; }
    });
    report("Preparing the new installation…");
    // The previous bar measured copied bytes; deleting the old install is real
    // work with no known duration, so show it as indeterminate rather than
    // leaving a stale 100% claim on screen.
    progress.removeAttribute("value");
    // `install` is this build's own leaf. The `install.preparing*` leaves belong
    // to the retired atomic extraction, which renamed a staging directory into
    // place and could leave whole trees behind when that rename failed on OPFS.
    // They are dead bytes now, and dead bytes can exhaust the storage quota the
    // new import needs.
    for (const leaf of [
      "install", "install.ready", "install.ready.tmp",
      "install.preparing", "install.preparing.lucent-stage", "install.preparing.previous", "install.previous"
    ]) {
      await removeIfPresent(storageRoot, leaf, true);
    }
    launch(true);
  } catch (error) {
    report(error.message, true);
  }
});

/* The capability gate is the only thing a player on an unsupported browser ever
 * sees, so it must name the capability that is actually missing. */
function missingBrowserFeatures() {
  const missing = [];
  if (!navigator.gpu) missing.push("WebGPU");
  if (!globalThis.OffscreenCanvas) missing.push("OffscreenCanvas");
  return missing;
}

async function prepare() {
    const missing = missingBrowserFeatures();
    if (missing.length > 0) {
      throw new Error(
        `This browser does not provide ${missing.join(" or ")}. The port renders ` +
        "through WebGPU in a worker, so it cannot start without it."
      );
    }
    const {root, persistent, granted} = await persistentStorage();
    storageRoot = root;
    // The grant is a permission question the browser may answer late or never,
    // so say what is true now and improve the note if the answer arrives. The
    // page must never wait on it: the player is here to choose a game file.
    const note = document.querySelector("#storage-note");
    const describeStorage = state => {
      note.textContent = state
        ? "Game files and saves are kept in persistent storage on this device."
        : "The browser has not granted persistent storage. It may clear game files and saves under storage pressure.";
    };
    describeStorage(persistent);
    granted.then(state => { if (state) describeStorage(true); });
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
      await root.getFileHandle("install.ready");
      play.disabled = testPlay.disabled = false;
    } catch (error) {
      if (error.name !== "NotFoundError") throw error;
      report("Removing an interrupted installation…");
      await removeIfPresent(root, "install", true);
      await removeIfPresent(root, "install.ready.tmp");
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
