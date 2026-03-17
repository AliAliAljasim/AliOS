#pragma once

/*
 * nano_run — launch the full-screen text editor.
 *
 *   filename  — file to open (may be NULL or empty for a new buffer)
 *   base_cwd  — current working directory for path resolution
 *
 * Takes over the entire 80×25 VGA display until Ctrl+X is pressed.
 * The caller is responsible for restoring the shell UI afterwards.
 */
void nano_run(const char *filename, const char *base_cwd);
