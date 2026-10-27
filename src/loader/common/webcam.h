#ifndef WEBCAM_H
#define WEBCAM_H

// Webcams (/dev/videoN) of the Raw Thrills and Namco ES1 games: colour
// cameras only, held open (see webcam.c).
int webcamIsPath(const char *path);
int webcamOpen(const char *path, int flags, int mode, int (*realOpen)(const char *, int, ...));
// A colour camera is there (for the games that can do without one).
int webcamHasCamera(void);

#endif // WEBCAM_H
