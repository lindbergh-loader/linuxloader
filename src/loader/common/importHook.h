#ifndef IMPORT_HOOK_H
#define IMPORT_HOOK_H

// Point the game executable's own import of name (its PLT's GOT slot) at
// replacement; returns what the import resolves to otherwise, NULL if the
// game does not import it. Only the executable's calls are diverted: the
// libraries' own calls to name are not.
void *hookExecutableImport(const char *name, void *replacement);

#endif // IMPORT_HOOK_H
