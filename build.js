const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const root = __dirname;
const buildDir = path.join(root, "build");
const srcDir = path.join(root, "src");
const includeDir = path.join(root, "include");

if (!fs.existsSync(buildDir)) {
  fs.mkdirSync(buildDir, { recursive: true });
}

const sources = fs
  .readdirSync(srcDir)
  .filter((f) => f.endsWith(".c"))
  .map((f) => path.join("src", f));

if (sources.length === 0) {
  console.error("No C sources found in src/");
  process.exit(1);
}

const isWin = process.platform === "win32";
const cc = isWin ? "gcc" : "cc";
const out = path.join("build", isWin ? "shivishell.exe" : "shivishell");

const args = ["-O3", "-Iinclude", ...sources, "-o", out];

console.log(`Building shivishell with ${cc}...`);
const result = spawnSync(cc, args, { stdio: "inherit" });
if (result.error) {
  console.error(`Failed to run ${cc}:`, result.error.message);
  process.exit(1);
}
process.exit(result.status ?? 1);
