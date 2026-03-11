const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const root = __dirname;
const buildDir = path.join(root, "build");
const srcDir = path.join(root, "src");
if (!fs.existsSync(buildDir)) {
  fs.mkdirSync(buildDir, { recursive: true });
}

const testMain = path.join("tests", "test_main.c");
if (!fs.existsSync(path.join(root, testMain))) {
  console.error("Missing tests/test_main.c");
  process.exit(1);
}

const isWin = process.platform === "win32";
const cc = isWin ? "gcc" : "cc";
const out = path.join("build", isWin ? "tests.exe" : "tests");

const args = [
  "-O2",
  "-Iinclude",
  testMain,
  path.join("src", "parse.c"),
  path.join("src", "history.c"),
  "-o",
  out,
];

console.log(`Building tests with ${cc}...`);
const compile = spawnSync(cc, args, { stdio: "inherit" });
if (compile.error) {
  console.error(`Failed to run ${cc}:`, compile.error.message);
  process.exit(1);
}
if (compile.status !== 0) {
  process.exit(compile.status ?? 1);
}

console.log("Running tests...");
const exePath = path.join(root, out);
const run = spawnSync(exePath, [], { stdio: "inherit" });
process.exit(run.status ?? 1);
