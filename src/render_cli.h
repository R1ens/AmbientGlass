#pragma once

// Headless rendering: `VlcAmbient.exe --render in.png out.png [options]` draws one glow frame from a
// picture file with the real pipeline on the WARP device, for golden-image tests and tuning.
// `argv[1]` is "--render". Returns the process exit code; prints to the parent console.
int RunRenderCli(int argc, wchar_t** argv);
